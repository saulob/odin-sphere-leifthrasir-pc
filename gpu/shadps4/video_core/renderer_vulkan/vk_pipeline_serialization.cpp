// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <map>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include "common/hash.h"
#include "common/serdes.h"
#include "common/scope_exit.h"
#include "video_core/renderer_vulkan/vk_pipeline_serialization.h"
#include "common/thread.h"
#include "osl_overlay.h"
#include "core/emulator_settings.h"
#include "shader_recompiler/frontend/fetch_shader.h"
#include "shader_recompiler/info.h"
#include "video_core/cache_storage.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_warmup_inbox.h"

namespace Serialization {
/* You should increment versions below once corresponding serialization scheme is changed. */
static constexpr u32 ShaderBinaryVersion = 8u; // osl: recompiler modernization (SPIR-V changes)
static constexpr u32 ShaderMetaVersion = 9u; // osl: RuntimeInfo, sharp and resource layouts; sealed blobs
static constexpr u32 PipelineKeyVersion = 6u; // osl: sealed keys with stage hashes and the discard FS
} // namespace Serialization

namespace Vulkan {

namespace PipelineBinaryCache {
bool ValidKey(const Key& key) {
    return !key.empty() && key.size() <= VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR;
}
std::vector<u8> Encode(const Key& global, const Key& pipeline, u64 graphics_hash,
                       const std::vector<Binary>& binaries) {
    if (!ValidKey(global) || !ValidKey(pipeline) || binaries.empty() ||
        binaries.size() > MaxBinaries) {
        return {};
    }
    std::vector<u8> bytes;
    const auto append = [&](const auto& value) {
        const auto* p = reinterpret_cast<const u8*>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(value));
    };
    const auto blob = [&](const auto& data) {
        append(u32(data.size()));
        bytes.insert(bytes.end(), data.begin(), data.end());
    };
    append(u32{0x31425047}); // GPB1, little-endian Windows/Linux cache format.
    append(u32{1});
    append(graphics_hash);
    blob(global);
    blob(pipeline);
    append(u32(binaries.size()));
    for (const auto& binary : binaries) {
        if (!ValidKey(binary.key) || binary.data.empty() || binary.data.size() > MaxFileSize ||
            bytes.size() + binary.key.size() + binary.data.size() + 16 > MaxFileSize) {
            return {};
        }
        blob(binary.key);
        blob(binary.data);
    }
    append(u64{XXH3_64bits(bytes.data(), bytes.size())});
    return bytes;
}
std::optional<std::vector<Binary>> Decode(std::span<const u8> bytes, const Key& global,
                                          const Key& pipeline, u64 graphics_hash) {
    if (bytes.size() < 8 || bytes.size() > MaxFileSize || !ValidKey(global) ||
        !ValidKey(pipeline)) {
        return std::nullopt;
    }
    u64 checksum;
    std::memcpy(&checksum, bytes.data() + bytes.size() - 8, 8);
    bytes = bytes.first(bytes.size() - 8);
    if (checksum != XXH3_64bits(bytes.data(), bytes.size())) {
        return std::nullopt;
    }
    size_t offset = 0;
    const auto read = [&](auto& value) {
        if (sizeof(value) > bytes.size() - offset) {
            return false;
        }
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        offset += sizeof(value);
        return true;
    };
    const auto blob = [&](auto& data, size_t limit) {
        u32 size;
        if (!read(size) || !size || size > limit || size > bytes.size() - offset) {
            return false;
        }
        data.assign(bytes.begin() + offset, bytes.begin() + offset + size);
        offset += size;
        return true;
    };
    u32 magic, version, count;
    u64 stored_hash;
    Key stored_global, stored_pipeline;
    if (!read(magic) || magic != 0x31425047 || !read(version) || version != 1 ||
        !read(stored_hash) || stored_hash != graphics_hash || !blob(stored_global, 32) ||
        stored_global != global || !blob(stored_pipeline, 32) || stored_pipeline != pipeline ||
        !read(count) || !count || count > MaxBinaries) {
        return std::nullopt;
    }
    std::vector<Binary> binaries(count);
    for (auto& binary : binaries) {
        if (!blob(binary.key, 32) || !blob(binary.data, MaxFileSize)) {
            return std::nullopt;
        }
    }
    return offset == bytes.size() ? std::optional{std::move(binaries)} : std::nullopt;
}
bool Save(const std::filesystem::path& file, std::span<const u8> bytes) {
    if (bytes.empty() || bytes.size() > MaxFileSize) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (error) {
        return false;
    }
    // Unique siblings avoid concurrent writers truncating one another's temporary data.
    static std::atomic<u64> sequence{0};
    auto temp = file;
    temp += "." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
            "." + std::to_string(sequence++) + ".tmp";
    bool written = false;
    {
        std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
        if (stream) {
            stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            stream.flush();
            written = bool(stream);
            stream.close();
            written = written && !stream.fail();
        }
    }
    bool renamed = false;
    if (written) {
#ifdef _WIN32
        renamed = MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
        std::filesystem::rename(temp, file, error);
        renamed = !error;
#endif
    }
    if (!renamed) {
        std::filesystem::remove(temp, error);
    }
    return renamed;
}
std::optional<std::vector<Binary>> Load(const std::filesystem::path& file, const Key& global,
                                        const Key& pipeline, u64 graphics_hash) {
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream) {
        return std::nullopt;
    }
    const auto size = stream.tellg();
    if (size <= 0 || size > std::streamoff(MaxFileSize)) {
        return std::nullopt;
    }
    std::vector<u8> bytes(static_cast<size_t>(size));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) {
        return std::nullopt;
    }
    return Decode(bytes, global, pipeline, graphics_hash);
}
} // namespace PipelineBinaryCache

namespace {
std::atomic<u64> binary_hits{}, binary_misses{}, binary_rejected{}, binary_saved{}, binary_errors{};
std::atomic<u64> binary_restore_ns{}, binary_compile_ns{};
std::atomic<u64> binary_restore_calls{}, binary_compile_calls{};
std::atomic<u64> binary_key_ns{}, binary_load_ns{}, binary_handles_ns{};
u64 ElapsedNs(std::chrono::steady_clock::time_point started) {
    return u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now() - started).count());
}
PipelineBinaryCache::Key BinaryKey(const vk::PipelineBinaryKeyKHR& key) {
    if (!key.keySize || key.keySize > VK_MAX_PIPELINE_BINARY_KEY_SIZE_KHR) {
        return {};
    }
    return {key.key.begin(), key.key.begin() + key.keySize};
}

std::optional<std::vector<PipelineBinaryCache::Binary>> LoadLegacyBinary(
    const std::filesystem::path& directory, const std::string& name,
    const PipelineBinaryCache::Key& global, const PipelineBinaryCache::Key& pipeline) {
    using Index = std::unordered_multimap<std::string, std::pair<std::filesystem::path, u64>>;
    static std::mutex mutex;
    static std::map<std::filesystem::path, Index> indexes;
    std::vector<std::pair<std::filesystem::path, u64>> candidates;
    {
        std::scoped_lock lock{mutex};
        auto [it, inserted] = indexes.try_emplace(directory);
        if (inserted) {
            std::error_code error;
            for (std::filesystem::directory_iterator files(directory, error), end;
                 !error && files != end; files.increment(error)) {
                const auto stem = files->path().stem().string();
                if (files->path().extension() != ".bin" || stem.size() < 18 || stem[16] != '_') {
                    continue;
                }
                try {
                    size_t used = 0;
                    const auto hash = std::stoull(stem.substr(0, 16), &used, 16);
                    if (used == 16) {
                        it->second.emplace(stem.substr(17), std::pair{files->path(), hash});
                    }
                } catch (const std::exception&) {
                }
            }
        }
        const auto [first, last] = it->second.equal_range(name);
        for (auto entry = first; entry != last; ++entry) {
            candidates.push_back(entry->second);
        }
    }
    for (const auto& [file, hash] : candidates) {
        if (auto binaries = PipelineBinaryCache::Load(file, global, pipeline, hash)) {
            return binaries;
        }
    }
    return std::nullopt;
}
} // namespace

void ReportPipelineBinaryCache() {
    const auto hits = binary_hits.load();
    const auto misses = binary_misses.load();
    std::printf("Pipeline binary cache: %llu hits, %llu compiled, %llu rejected, %llu saved, "
                "%llu cache errors (restore %.3f ms, compile %.3f ms each)\n",
                static_cast<unsigned long long>(hits), static_cast<unsigned long long>(misses),
                static_cast<unsigned long long>(binary_rejected.load()),
                static_cast<unsigned long long>(binary_saved.load()),
                static_cast<unsigned long long>(binary_errors.load()),
                binary_restore_ns.load() / 1e6 / std::max(u64{1}, binary_restore_calls.load()),
                binary_compile_ns.load() / 1e6 / std::max(u64{1}, binary_compile_calls.load()));
}

vk::ResultValue<vk::UniquePipeline> CreateCachedGraphicsPipeline(
    vk::Device device, vk::PipelineCache cache, const vk::GraphicsPipelineCreateInfo& info,
    const GraphicsPipelineKey& key, const vk::PipelineBinaryKeyKHR& global_key,
    const std::filesystem::path& directory) {
    using namespace PipelineBinaryCache;
    const auto& dispatch = VULKAN_HPP_DEFAULT_DISPATCHER;
    const bool enabled = global_key.keySize && EmulatorSettings.IsPipelineCacheEnabled();
    const auto create = [&](vk::PipelineCache used_cache, const auto& used_info, bool restored) {
        const auto started = std::chrono::steady_clock::now();
        auto result = device.createGraphicsPipelineUnique(used_cache, used_info);
        const auto ns = u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count());
        extern std::atomic<u64> g_osl_pipeline_driver_ns;
        g_osl_pipeline_driver_ns += ns;
        if (enabled) {
            (restored ? binary_restore_ns : binary_compile_ns) += ns;
            ++(restored ? binary_restore_calls : binary_compile_calls);
        }
        return result;
    };
    if (!enabled) {
        return create(cache, info, false);
    }
    SCOPE_EXIT {
        const auto count = binary_hits.load() + binary_misses.load();
        if (count && count % 256 == 0) {
            ReportPipelineBinaryCache();
        }
    };
    const auto global = BinaryKey(global_key);
    vk::PipelineCreateFlags2CreateInfoKHR flags{
        .pNext = info.pNext,
        .flags = vk::PipelineCreateFlags2{static_cast<VkPipelineCreateFlags>(info.flags)} |
                 vk::PipelineCreateFlagBits2::eCaptureDataKHR,
    };
    auto captured_info = info;
    captured_info.pNext = &flags;
    vk::PipelineCreateInfoKHR key_info{.pNext = &captured_info};
    vk::PipelineBinaryKeyKHR pipeline_key;
    const auto key_started = std::chrono::steady_clock::now();
    const auto key_result = device.getPipelineKeyKHR(&key_info, &pipeline_key);
    binary_key_ns += ElapsedNs(key_started);
    if (key_result != vk::Result::eSuccess ||
        !ValidKey(global) || !ValidKey(BinaryKey(pipeline_key))) {
        ++binary_errors;
        ++binary_misses;
        return create(cache, info, false);
    }
    const auto pipeline_id = BinaryKey(pipeline_key);
    std::string name;
    for (const auto byte : pipeline_id) {
        name += fmt::format("{:02x}", byte);
    }
    // The driver key identifies the pipeline independently of emulator draw-state keys.
    const auto file = directory / (name + ".bin");
    std::vector<VkPipelineBinaryKHR> handles;
    const auto destroy = [&] {
        for (auto handle : handles) {
            if (handle) {
                dispatch.vkDestroyPipelineBinaryKHR(device, handle, nullptr);
            }
        }
        handles.clear();
    };
    SCOPE_EXIT { destroy(); };
    try {
        const auto load_started = std::chrono::steady_clock::now();
        auto binaries = Load(file, global, pipeline_id, 0);
        if (!binaries) {
            binaries = LoadLegacyBinary(directory, name, global, pipeline_id);
            if (binaries) {
                Save(file, Encode(global, pipeline_id, 0, *binaries));
            }
        }
        binary_load_ns += ElapsedNs(load_started);
        if (binaries) {
            std::vector<VkPipelineBinaryKeyKHR> keys(binaries->size());
            std::vector<VkPipelineBinaryDataKHR> data(binaries->size());
            for (size_t i = 0; i < binaries->size(); ++i) {
                keys[i].sType = VK_STRUCTURE_TYPE_PIPELINE_BINARY_KEY_KHR;
                keys[i].keySize = u32((*binaries)[i].key.size());
                std::ranges::copy((*binaries)[i].key, keys[i].key);
                data[i] = {(*binaries)[i].data.size(), (*binaries)[i].data.data()};
            }
            VkPipelineBinaryKeysAndDataKHR stored{u32(keys.size()), keys.data(), data.data()};
            VkPipelineBinaryCreateInfoKHR binary_info{
                VK_STRUCTURE_TYPE_PIPELINE_BINARY_CREATE_INFO_KHR};
            binary_info.pKeysAndDataInfo = &stored;
            handles.resize(keys.size());
            VkPipelineBinaryHandlesInfoKHR output{
                VK_STRUCTURE_TYPE_PIPELINE_BINARY_HANDLES_INFO_KHR};
            output.pipelineBinaryCount = u32(handles.size());
            output.pPipelineBinaries = handles.data();
            const auto handles_started = std::chrono::steady_clock::now();
            const auto handles_result =
                dispatch.vkCreatePipelineBinariesKHR(device, &binary_info, nullptr, &output);
            binary_handles_ns += ElapsedNs(handles_started);
            if (handles_result == VK_SUCCESS) {
                VkPipelineBinaryInfoKHR restored{VK_STRUCTURE_TYPE_PIPELINE_BINARY_INFO_KHR};
                restored.pNext = &flags;
                restored.binaryCount = u32(handles.size());
                restored.pPipelineBinaries = handles.data();
                // CAPTURE_DATA is only for compilation, never for binary-backed creation.
                flags.flags =
                    vk::PipelineCreateFlags2{static_cast<VkPipelineCreateFlags>(info.flags)};
                auto restored_info = info;
                restored_info.pNext = &restored;
                auto result = create(nullptr, restored_info, true);
                if (result.result == vk::Result::eSuccess) {
                    ++binary_hits;
                    return result;
                }
            }
            ++binary_rejected;
            destroy();
        }
    } catch (const std::exception&) {
        ++binary_errors;
        destroy();
    }
    ++binary_misses;
    flags.flags = vk::PipelineCreateFlags2{static_cast<VkPipelineCreateFlags>(info.flags)} |
                  vk::PipelineCreateFlagBits2::eCaptureDataKHR;
    auto result = create(nullptr, captured_info, false);
    if (result.result != vk::Result::eSuccess) {
        ++binary_errors;
        return create(cache, info, false);
    }
    VkReleaseCapturedPipelineDataInfoKHR release{
        VK_STRUCTURE_TYPE_RELEASE_CAPTURED_PIPELINE_DATA_INFO_KHR};
    release.pipeline = *result.value;
    SCOPE_EXIT {
        if (dispatch.vkReleaseCapturedPipelineDataKHR(device, &release, nullptr) != VK_SUCCESS) {
            ++binary_errors;
        }
    };
    try {
        VkPipelineBinaryCreateInfoKHR binary_info{
            VK_STRUCTURE_TYPE_PIPELINE_BINARY_CREATE_INFO_KHR};
        binary_info.pipeline = *result.value;
        VkPipelineBinaryHandlesInfoKHR output{VK_STRUCTURE_TYPE_PIPELINE_BINARY_HANDLES_INFO_KHR};
        if (dispatch.vkCreatePipelineBinariesKHR(device, &binary_info, nullptr, &output) !=
                VK_SUCCESS ||
            !output.pipelineBinaryCount || output.pipelineBinaryCount > MaxBinaries) {
            ++binary_errors;
            return result;
        }
        handles.resize(output.pipelineBinaryCount);
        output.pPipelineBinaries = handles.data();
        if (dispatch.vkCreatePipelineBinariesKHR(device, &binary_info, nullptr, &output) !=
            VK_SUCCESS) {
            ++binary_errors;
            return result;
        }
        std::vector<Binary> binaries;
        size_t total_size = 0;
        for (const auto handle : handles) {
            VkPipelineBinaryDataInfoKHR data_info{VK_STRUCTURE_TYPE_PIPELINE_BINARY_DATA_INFO_KHR};
            data_info.pipelineBinary = handle;
            vk::PipelineBinaryKeyKHR binary_key;
            size_t size = 0;
            auto* raw_key = reinterpret_cast<VkPipelineBinaryKeyKHR*>(&binary_key);
            if (dispatch.vkGetPipelineBinaryDataKHR(device, &data_info, raw_key, &size, nullptr) !=
                    VK_SUCCESS ||
                !size || size > MaxFileSize || total_size + size > MaxFileSize ||
                !ValidKey(BinaryKey(binary_key))) {
                ++binary_errors;
                return result;
            }
            total_size += size;
            Binary binary{BinaryKey(binary_key), std::vector<u8>(size)};
            if (dispatch.vkGetPipelineBinaryDataKHR(device, &data_info, raw_key, &size,
                                                    binary.data.data()) != VK_SUCCESS ||
                size > binary.data.size()) {
                ++binary_errors;
                return result;
            }
            binary.key = BinaryKey(binary_key);
            binary.data.resize(size);
            binaries.push_back(std::move(binary));
        }
        const auto encoded = Encode(global, pipeline_id, 0, binaries);
        if (Save(file, encoded)) {
            ++binary_saved;
        } else {
            ++binary_errors;
        }
    } catch (const std::exception&) {
        ++binary_errors;
    }
    return result;
}

namespace {
// osl: integrity of the shader cache. Meta and key blobs start with a tag of the layouts they copy
// as raw bytes and end with an XXH3 of their contents, so a damaged blob or one of a build with
// other layouts reads as stale instead of being parsed. A meta records the hash of the SPIR-V
// saved with it and a graphics key the hash of each stage it was built with (its "stage
// prints"): a cached pipeline is built only from the binaries it was made of. Stale entries are
// removed; the game compiles and saves them again when it uses them.

/// Sizes of the structures meta and key blobs copy as raw bytes: a build where one differs reads
/// the blobs of another as stale, also when a version constant was not raised.
constexpr u64 BlobLayoutTag() {
    constexpr std::array sizes = {
        sizeof(Shader::InfoPersistent),         sizeof(Shader::PersistentSrtInfo),
        sizeof(Shader::RuntimeInfo),            sizeof(Shader::Backend::Bindings),
        sizeof(Shader::VsAttribSpecialization), sizeof(Shader::BufferSpecialization),
        sizeof(Shader::ImageSpecialization),    sizeof(Shader::FMaskSpecialization),
        sizeof(Shader::SamplerSpecialization),  sizeof(Shader::Gcn::VertexAttribute),
        sizeof(GraphicsPipelineKey),            sizeof(GraphicsPipeline::SerializationSupport),
    };
    u64 tag = 0xcbf29ce484222325ULL;
    for (const size_t size : sizes) {
        tag = (tag ^ size) * 0x100000001b3ULL;
    }
    return tag;
}

std::string MetaName(u64 perm_hash) {
    return fmt::format("{:#018x}", perm_hash);
}

std::string BinaryName(u64 pgm_hash, size_t perm_idx) {
    return fmt::format("{:#018x}_{}", pgm_hash, perm_idx);
}

void SealBlob(std::vector<u8>& blob) {
    const u64 hash = XXH3_64bits(blob.data(), blob.size());
    const auto* bytes = reinterpret_cast<const u8*>(&hash);
    blob.insert(blob.end(), bytes, bytes + sizeof(hash));
}

/// Checks and strips the hash SealBlob appended.
bool UnsealBlob(std::vector<u8>& blob) {
    u64 hash{};
    if (blob.size() < sizeof(hash)) {
        return false;
    }
    std::memcpy(&hash, blob.data() + blob.size() - sizeof(hash), sizeof(hash));
    blob.resize(blob.size() - sizeof(hash));
    return hash == XXH3_64bits(blob.data(), blob.size());
}

std::atomic<u32> g_stale_entries{0};

void RemoveStale(Storage::BlobType type, const std::string& name, const char* why) {
    Storage::DataBase::Instance().Remove(type, name);
    if (g_stale_entries.fetch_add(1, std::memory_order_relaxed) < 16) {
        std::printf("Pipeline cache: removed %s (%s)\n", name.c_str(), why);
    }
}

std::mutex g_print_mutex;
std::unordered_map<u64, u64> g_saved_binaries; ///< perm hash -> hash of the SPIR-V just saved
std::unordered_map<u64, u64> g_stage_prints;   ///< perm hash -> hash of the stage in use

u64 TakeBinaryHash(u64 perm_hash) {
    std::scoped_lock lock{g_print_mutex};
    const auto it = g_saved_binaries.find(perm_hash);
    if (it == g_saved_binaries.end()) {
        return 0;
    }
    const u64 hash = it->second;
    g_saved_binaries.erase(it);
    return hash;
}

/// `replace`: a permutation compiled this session; a loaded one does not override it.
void NoteStagePrint(u64 perm_hash, u64 print, bool replace) {
    std::scoped_lock lock{g_print_mutex};
    if (replace) {
        g_stage_prints[perm_hash] = print;
    } else {
        g_stage_prints.try_emplace(perm_hash, print);
    }
}

u64 StagePrint(u64 perm_hash) {
    std::scoped_lock lock{g_print_mutex};
    const auto it = g_stage_prints.find(perm_hash);
    return it == g_stage_prints.end() ? 0 : it->second;
}

/// The background warm-up reads the store while the game saves: a permutation this session
/// compiled has new files on their way, which a removal queued after them would delete.
bool CompiledThisSession(u64 perm_hash) {
    return StagePrint(perm_hash) != 0;
}

std::unordered_set<std::string> g_saved_keys; ///< pipeline keys saved this session

// The check and the queued removal hold g_print_mutex, which the registrations hold while they
// queue a stage's first save or a key's save: a removal is never queued after such a save.
template <typename Remove>
void UnlessCompiledThisSession(u64 perm_hash, Remove&& remove) {
    std::scoped_lock lock{g_print_mutex};
    const auto it = g_stage_prints.find(perm_hash);
    if (it == g_stage_prints.end() || it->second == 0) {
        remove();
    }
}

void RemoveStaleKey(const std::string& name, const char* why) {
    std::scoped_lock lock{g_print_mutex};
    if (!g_saved_keys.contains(name)) {
        RemoveStale(Storage::BlobType::PipelineKey, name, why);
    }
}

void SaveKey(std::string name, std::vector<u8>&& blob) {
    std::scoped_lock lock{g_print_mutex};
    g_saved_keys.insert(name);
    Storage::DataBase::Instance().Save(Storage::BlobType::PipelineKey, name, std::move(blob));
}

// A compile storm (a shader specialized on garbage resource values) can leave a program with
// thousands of cached versions that every warm-up would rebuild. Programs with this many are
// neither loaded nor kept; the versions in use compile again.
constexpr size_t CrowdedShaderVersions = 100;
/// Perm hashes of those programs' versions. Filled and read by the thread that reads the store.
std::unordered_set<u64> g_crowded_stages;

void FindCrowdedShaders() {
    g_crowded_stages.clear();
    auto& db = Storage::DataBase::Instance();
    std::unordered_map<u64, std::vector<size_t>> versions;
    db.ForEachBlobName(Storage::BlobType::ShaderBinary, [&](const std::string& name) {
        unsigned long long pgm_hash{};
        size_t perm_idx{};
        if (std::sscanf(name.c_str(), "%llx_%zu", &pgm_hash, &perm_idx) == 2) {
            versions[pgm_hash].push_back(perm_idx);
        }
    });
    for (const auto& [pgm_hash, perm_indices] : versions) {
        if (perm_indices.size() < CrowdedShaderVersions) {
            continue;
        }
        for (const size_t perm_idx : perm_indices) {
            const u64 perm_hash = HashCombine(pgm_hash, u64(perm_idx));
            g_crowded_stages.insert(perm_hash);
            UnlessCompiledThisSession(perm_hash, [&] {
                db.Remove(Storage::BlobType::ShaderBinary, BinaryName(pgm_hash, perm_idx));
                db.Remove(Storage::BlobType::ShaderMeta, MetaName(perm_hash));
                db.Remove(Storage::BlobType::PipelineKey, fmt::format("c_{:#018x}", perm_hash));
            });
        }
        std::printf("Pipeline cache: shader %#llx has %zu cached versions: not loaded, removed\n",
                    static_cast<unsigned long long>(pgm_hash), perm_indices.size());
    }
}

// A fault on a warm-up thread (a driver crash while it builds a cached pipeline) leaves this
// mark in the cache folder (oslgpu_note_fatal_fault); the next start clears the cache instead of
// crashing on the same pipeline again.
thread_local bool t_warmup_thread = false;
std::filesystem::path::value_type g_warmup_mark[1024] = {};

/// Arms the mark for this start; true when the previous start crashed in its warm-up.
bool TakeWarmupMark() {
    auto& db = Storage::DataBase::Instance();
    if (!db.ConcurrentReads() || db.Folder().empty()) {
        return false;
    }
    const auto mark = db.Folder() / "warmup-crashed.txt";
    const auto& path = mark.native();
    if (path.size() >= std::size(g_warmup_mark)) {
        return false;
    }
    std::ranges::copy(path, g_warmup_mark);
    g_warmup_mark[path.size()] = 0;
    std::error_code error;
    if (!std::filesystem::exists(mark, error)) {
        return false;
    }
    std::filesystem::remove(mark, error);
    return true;
}
} // namespace

// osl: the background warm-up's worker threads. The cache store is read and the shader modules
// created in order on the calling thread (program_cache and its permutation checks are not
// thread-safe); only the pipeline objects, the slow part, are built here.
struct PipelineCache::WarmupPool {
    explicit WarmupPool(u32 count, bool low_priority = false) {
        for (u32 i = 0; i < count; ++i) {
            threads.emplace_back([this, low_priority] {
                if (low_priority) {
                    Common::SetCurrentThreadName("osl:ShaderWarmup");
                    Common::SetCurrentThreadPriority(Common::ThreadPriority::Low);
                }
                Run();
            });
        }
    }
    ~WarmupPool() {
        {
            std::scoped_lock lock{mutex};
            jobs.clear(); // only the running ones finish
            closing = true;
        }
        cv.notify_all();
        for (auto& thread : threads) {
            thread.join();
        }
    }
    void Push(std::function<bool()> job) {
        {
            std::scoped_lock lock{mutex};
            jobs.push_back(std::move(job));
        }
        cv.notify_one();
    }
    /// Waits for every job; `tick` runs on this thread meanwhile (the loading screen).
    void Wait(const std::function<void()>& tick) {
        std::unique_lock lock{mutex};
        while (!jobs.empty() || running) {
            idle.wait_for(lock, std::chrono::milliseconds(50));
            lock.unlock();
            tick();
            lock.lock();
        }
    }
    /// Drops the queued jobs and waits for the running ones; the threads stay parked.
    void Drain() {
        std::unique_lock lock{mutex};
        jobs.clear();
        idle.wait(lock, [this] { return running == 0; });
    }
    bool Idle() {
        std::scoped_lock lock{mutex};
        return jobs.empty() && !running;
    }
    std::atomic<u32> built{0};
    std::atomic<u32> failed{0};

private:
    void Run() {
        t_warmup_thread = true;
        std::unique_lock lock{mutex};
        while (true) {
            cv.wait(lock, [this] { return closing || !jobs.empty(); });
            if (jobs.empty()) {
                return;
            }
            auto job = std::move(jobs.front());
            jobs.pop_front();
            ++running;
            lock.unlock();
            bool ok = false;
            try {
                ok = job();
            } catch (const std::exception& e) {
                LOG_ERROR(Render_Vulkan, "osl: warm-up pipeline failed: {}", e.what());
            } catch (...) {
                LOG_ERROR(Render_Vulkan, "osl: warm-up pipeline failed");
            }
            (ok ? built : failed).fetch_add(1, std::memory_order_relaxed);
            lock.lock();
            --running;
            idle.notify_all();
        }
    }

    std::mutex mutex;
    std::condition_variable cv, idle;
    std::deque<std::function<bool()>> jobs;
    std::vector<std::thread> threads;
    u32 running = 0;
    bool closing = false;
};

void RegisterPipelineData(const ComputePipelineKey& key,
                          ComputePipeline::SerializationSupport& sdata) {
    if (!Storage::DataBase::Instance().IsOpened()) {
        return;
    }

    Serialization::Archive ar{};
    Serialization::Writer pldata{ar};

    pldata.Write(Serialization::PipelineKeyVersion);
    pldata.Write(BlobLayoutTag());
    pldata.Write(u32{1}); // compute

    key.Serialize(ar);
    sdata.Serialize(ar);

    auto blob = ar.TakeOff();
    SealBlob(blob);
    SaveKey(fmt::format("c_{:#018x}", key.value), std::move(blob));
}

void RegisterPipelineData(const GraphicsPipelineKey& key, u64 hash,
                          GraphicsPipeline::SerializationSupport& sdata) {
    if (!Storage::DataBase::Instance().IsOpened()) {
        return;
    }

    Serialization::Archive ar{};
    Serialization::Writer pldata{ar};

    pldata.Write(Serialization::PipelineKeyVersion);
    pldata.Write(BlobLayoutTag());
    pldata.Write(u32{0}); // graphics

    key.Serialize(ar);
    sdata.Serialize(ar);

    std::array<u64, MaxShaderStages> prints{};
    for (u32 i = 0; i < MaxShaderStages; ++i) {
        prints[i] = key.stage_hashes[i] ? StagePrint(key.stage_hashes[i]) : 0;
    }
    pldata.Write(prints.data(), sizeof(prints));

    auto blob = ar.TakeOff();
    SealBlob(blob);
    SaveKey(fmt::format("g_{:#018x}", hash), std::move(blob));
}

void RegisterShaderMeta(const Shader::Info& info,
                        const std::optional<Shader::Gcn::FetchShaderData>& fetch_shader_data,
                        const Shader::StageSpecialization& spec, size_t perm_hash,
                        size_t perm_idx) {
    if (!Storage::DataBase::Instance().IsOpened()) {
        return;
    }

    Serialization::Archive ar;
    Serialization::Writer meta{ar};

    meta.Write(Serialization::ShaderMetaVersion);
    meta.Write(BlobLayoutTag());
    meta.Write(Serialization::ShaderBinaryVersion);

    meta.Write(perm_hash);
    meta.Write(perm_idx);
    // RegisterShaderBinary ran just before for this permutation.
    const u64 binary_hash = TakeBinaryHash(perm_hash);
    meta.Write(binary_hash);
    NoteStagePrint(perm_hash, binary_hash, true);

    spec.Serialize(ar);
    info.Serialize(ar);

    auto blob = ar.TakeOff();
    SealBlob(blob);
    Storage::DataBase::Instance().Save(Storage::BlobType::ShaderMeta, MetaName(perm_hash),
                                       std::move(blob));
}

void RegisterShaderBinary(std::vector<u32>&& spv, u64 pgm_hash, size_t perm_idx) {
    if (!Storage::DataBase::Instance().IsOpened()) {
        return;
    }

    const u64 perm_hash = HashCombine(pgm_hash, u64(perm_idx));
    const u64 binary_hash = XXH3_64bits(spv.data(), spv.size() * sizeof(u32));
    std::scoped_lock lock{g_print_mutex};
    g_saved_binaries[perm_hash] = binary_hash;
    g_stage_prints[perm_hash] = binary_hash;
    Storage::DataBase::Instance().Save(Storage::BlobType::ShaderBinary,
                                       BinaryName(pgm_hash, perm_idx), std::move(spv));
}

/// Parses a meta blob. False for one of another version or layout, a damaged one, or one that
/// is not `perm_hash`'s; Serialization::CorruptData when it ends before its contents do.
static bool LoadShaderMeta(std::vector<u8>&& blob, u64 perm_hash, Shader::Info& info,
                           Shader::StageSpecialization& spec,
                           std::optional<Shader::Gcn::FetchShaderData>& fetch_shader_data,
                           size_t& perm_idx, u64& binary_hash) {
    if (!UnsealBlob(blob)) {
        return false;
    }
    Serialization::Archive ar{std::move(blob)};
    Serialization::Reader meta{ar};

    u32 meta_version{};
    meta.Read(meta_version);
    if (meta_version != Serialization::ShaderMetaVersion) {
        return false;
    }

    u64 layout{};
    meta.Read(layout);
    if (layout != BlobLayoutTag()) {
        return false;
    }

    u32 binary_version{};
    meta.Read(binary_version);
    if (binary_version != Serialization::ShaderBinaryVersion) {
        return false;
    }

    u64 perm_hash_ar{};
    meta.Read(perm_hash_ar);
    meta.Read(perm_idx);
    meta.Read(binary_hash);

    spec.Deserialize(ar);
    info.Deserialize(ar);

    fetch_shader_data = spec.fetch_shader_data;
    return perm_hash_ar == perm_hash && HashCombine(info.pgm_hash, u64(perm_idx)) == perm_hash;
}

/// Reads cached stage `perm_hash`: its meta into `info`, `spec`, `fetch`, `perm_idx` and
/// `binary_hash`, its SPIR-V into `spv`. A damaged or foreign meta, or a binary that is not the
/// one its meta was written with, is removed. False when the stage cannot be used; `gone` then
/// tells that the store no longer holds it, so keys naming it are stale.
static bool ReadCachedStage(u64 perm_hash, Shader::Info& info, Shader::StageSpecialization& spec,
                            std::optional<Shader::Gcn::FetchShaderData>& fetch, size_t& perm_idx,
                            u64& binary_hash, std::vector<u32>& spv, bool& gone) {
    gone = true;
    if (g_crowded_stages.contains(perm_hash)) {
        return false;
    }
    auto& db = Storage::DataBase::Instance();
    const auto meta_name = MetaName(perm_hash);
    std::vector<u8> blob;
    db.Load(Storage::BlobType::ShaderMeta, meta_name, blob);
    if (blob.empty()) {
        return false;
    }
    bool valid = false;
    try {
        valid = LoadShaderMeta(std::move(blob), perm_hash, info, spec, fetch, perm_idx,
                               binary_hash);
    } catch (const std::exception&) {
    }
    if (!valid) {
        UnlessCompiledThisSession(perm_hash, [&] {
            RemoveStale(Storage::BlobType::ShaderMeta, meta_name, "damaged or of another build");
        });
        return false;
    }
    // Motion vertex shaders embed session-local buffer device addresses. They must be
    // recompiled for the current allocation, never loaded from a previous process.
    if (info.hw_stage == Shader::HwStage::Vertex && spec.runtime_info.hw.vs.motion_vectors) {
        gone = false;
        return false;
    }
    const auto binary_name = BinaryName(info.pgm_hash, perm_idx);
    db.Load(Storage::BlobType::ShaderBinary, binary_name, spv);
    if (spv.empty()) {
        return false;
    }
    if (XXH3_64bits(spv.data(), spv.size() * sizeof(u32)) != binary_hash) {
        UnlessCompiledThisSession(perm_hash, [&] {
            RemoveStale(Storage::BlobType::ShaderMeta, meta_name, "its binary was replaced");
            db.Remove(Storage::BlobType::ShaderBinary, binary_name);
        });
        return false;
    }
    gone = false;
    return true;
}

void ComputePipelineKey::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer key{ar};
    key.Write(value);
}

bool ComputePipelineKey::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader key{ar};
    key.Read(value);
    return true;
}

void ComputePipeline::SerializationSupport::Serialize(Serialization::Archive& ar) const {
    // Nothing here yet
    return;
}

bool ComputePipeline::SerializationSupport::Deserialize(Serialization::Archive& ar) {
    // Nothing here yet
    return true;
}

/// osl: a pipeline key blob as RegisterPipelineData writes it.
struct PipelineCache::CachedPipelineKey {
    bool compute = false;
    ComputePipelineKey compute_key{};
    GraphicsPipelineKey graphics_key{};
    GraphicsPipeline::SerializationSupport sdata{};
    std::array<u64, MaxShaderStages> prints{}; ///< graphics: the binary hash of each stage
};

bool PipelineCache::ReadPipelineKey(std::vector<u8>&& blob, CachedPipelineKey& key) {
    try {
        if (!UnsealBlob(blob)) {
            return false;
        }
        Serialization::Archive ar{std::move(blob)};
        Serialization::Reader pldata{ar};
        u32 version{};
        pldata.Read(version);
        if (version != Serialization::PipelineKeyVersion) {
            return false;
        }
        u64 layout{};
        pldata.Read(layout);
        if (layout != BlobLayoutTag()) {
            return false;
        }
        u32 is_compute{};
        pldata.Read(is_compute);
        key.compute = is_compute != 0;
        if (key.compute) {
            ComputePipeline::SerializationSupport sdata{};
            return key.compute_key.Deserialize(ar) && sdata.Deserialize(ar);
        }
        if (!key.graphics_key.Deserialize(ar) || !key.sdata.Deserialize(ar)) {
            return false;
        }
        pldata.Read(key.prints.data(), sizeof(key.prints));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

PipelineCache::CacheLoad PipelineCache::LoadComputePipeline(const CachedPipelineKey& key) {
    if (compute_pipelines.contains(key.compute_key)) {
        return CacheLoad::Skipped;
    }
    SCOPE_EXIT {
        sel.infos.fill(nullptr);
        sel.modules.fill(nullptr);
        sel.fetch_shader.reset();
    };
    const auto stage = LoadPipelineStage(key.compute_key.value, 0, std::nullopt);
    if (stage != CacheLoad::Loaded) {
        return stage;
    }

    ComputePipeline::SerializationSupport sdata{};
    auto pipeline = std::make_unique<ComputePipeline>(instance, scheduler, desc_heap, profile,
                                                      *pipeline_cache, key.compute_key,
                                                      *sel.infos[0], sel.modules[0], sdata, true);
    if (pipeline->Failed()) {
        return CacheLoad::Skipped;
    }
    compute_pipelines.emplace(key.compute_key, std::move(pipeline));
    return CacheLoad::Loaded;
}

void GraphicsPipelineKey::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer key{ar};

    key.Write(this, sizeof(*this));
}

bool GraphicsPipelineKey::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader key{ar};

    key.Read(this, sizeof(*this));
    return true;
}

void GraphicsPipeline::SerializationSupport::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer sdata{ar};

    sdata.Write(&vertex_attributes, sizeof(vertex_attributes));
    sdata.Write(&vertex_bindings, sizeof(vertex_bindings));
    sdata.Write(&divisors, sizeof(divisors));
    sdata.Write(multisampling);
    sdata.Write(tcs);
    sdata.Write(tes);
    sdata.Write(fragment);
}

bool GraphicsPipeline::SerializationSupport::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader sdata{ar};

    sdata.Read(&vertex_attributes, sizeof(vertex_attributes));
    sdata.Read(&vertex_bindings, sizeof(vertex_bindings));
    sdata.Read(&divisors, sizeof(divisors));
    sdata.Read(multisampling);
    sdata.Read(tcs);
    sdata.Read(tes);
    sdata.Read(fragment);
    return true;
}

PipelineCache::CacheLoad PipelineCache::LoadGraphicsPipeline(const CachedPipelineKey& key) {
    if (graphics_pipelines.contains(key.graphics_key)) {
        return CacheLoad::Skipped;
    }
    SCOPE_EXIT {
        sel.infos.fill(nullptr);
        sel.modules.fill(nullptr);
        sel.fetch_shader.reset();
    };
    for (u32 stage_idx = 0; stage_idx < MaxShaderStages; ++stage_idx) {
        const u64 hash = key.graphics_key.stage_hashes[stage_idx];
        if (!hash) {
            continue;
        }
        const auto stage = LoadPipelineStage(hash, stage_idx, key.prints[stage_idx]);
        if (stage != CacheLoad::Loaded) {
            return stage;
        }
    }

    auto sdata = key.sdata;
    auto pipeline = std::make_unique<GraphicsPipeline>(
        instance, scheduler, desc_heap, profile, key.graphics_key, *pipeline_cache, sel.infos,
        sel.runtime_infos, sel.fetch_shader, sel.modules, sdata, true);
    if (pipeline->Failed()) {
        return CacheLoad::Skipped;
    }
    graphics_pipelines.emplace(key.graphics_key, std::move(pipeline));
    return CacheLoad::Loaded;
}

PipelineCache::CacheLoad PipelineCache::LoadPipelineStage(u64 perm_hash, size_t stage,
                                                          std::optional<u64> expected_print) {
    auto program = std::make_unique<Program>();
    Shader::StageSpecialization spec{};
    spec.info = &program->info;
    std::optional<Shader::Gcn::FetchShaderData> fetch;
    size_t perm_idx{};
    u64 binary_hash{};
    std::vector<u32> spv{};
    bool gone = false;
    if (!ReadCachedStage(perm_hash, program->info, spec, fetch, perm_idx, binary_hash, spv, gone)) {
        return gone && !CompiledThisSession(perm_hash) ? CacheLoad::Stale : CacheLoad::Skipped;
    }
    if (expected_print && *expected_print != binary_hash) {
        return CacheLoad::Stale; // the key names a stage that was compiled again since
    }

    // Permutation hash depends on shader variation index. To prevent collisions, we need insert it
    // at the exact position rather than append

    vk::ShaderModule module{};

    auto [it_pgm, new_program] = program_cache.try_emplace(program->info.pgm_hash);
    if (new_program) {
        module = CompileSPV(spv, instance.GetDevice());
        it_pgm.value() = std::move(program);
    } else {
        const auto& it = std::ranges::find(it_pgm.value()->modules, spec, &Program::Module::spec);
        if (it != it_pgm.value()->modules.end()) {
            // A matching permutation is valid only at its original index. A different index means
            // the store holds entries from more than one cache generation, so this pipeline is
            // left to compile at runtime.
            const auto idx = std::distance(it_pgm.value()->modules.begin(), it);
            if (perm_idx != idx) {
                LOG_WARNING(Render_Vulkan,
                            "Cached permutation {} of {}_{:x} conflicts with index {}, skipping "
                            "preload",
                            perm_idx, program->info.hw_stage, program->info.pgm_hash, idx);
                return CacheLoad::Skipped;
            }
            module = it->module;
        } else {
            module = CompileSPV(spv, instance.GetDevice());
        }
    }
    it_pgm.value()->InsertPermut(module, std::move(spec), perm_idx);
    NoteStagePrint(perm_hash, binary_hash, false);

    sel.infos[stage] = &it_pgm.value()->info;
    sel.modules[stage] = module;
    if (fetch) {
        sel.fetch_shader = std::move(fetch);
    }

    return CacheLoad::Loaded;
}

/// osl: true when the previous start crashed while it built cached pipelines: the cache (this
/// game's, this GPU's) is cleared and refilled as the game plays.
bool PipelineCache::DropCrashedCache() {
    if (!TakeWarmupMark()) {
        return false;
    }
    auto& db = Storage::DataBase::Instance();
    db.Clear();
    db.FinishPreload();
    std::vector<u8> profile_data(sizeof(profile));
    std::memcpy(profile_data.data(), &profile, sizeof(profile));
    db.Save(Storage::BlobType::ShaderProfile, "profile", std::move(profile_data));
    std::printf("Pipeline cache: the last start crashed while cached pipelines were built; the "
                "cache was cleared\n");
    return true;
}

bool PipelineCache::CheckCacheProfile() {
    if (!EmulatorSettings.IsPipelineCacheEnabled()) {
        return false;
    }

    auto& db = Storage::DataBase::Instance();
    db.Open();

    // Check if cache is compatible
    std::vector<u8> profile_data{};
    db.Load(Storage::BlobType::ShaderProfile, "profile", profile_data);
    const auto matches = [&](const std::vector<u8>& data) {
        Shader::Profile stored{};
        if (data.size() != sizeof(stored)) {
            return false;
        }
        std::memcpy(&stored, data.data(), sizeof(stored));
        return stored == profile;
    };
    if (matches(profile_data)) {
        return true;
    }
    if (profile_data.size() == sizeof(Shader::Profile)) {
        // osl: another GPU's cache (the same build) is put aside, and this GPU's own comes back
        // if one was kept.
        LOG_WARNING(Render, "Pipeline cache belongs to another GPU: putting it aside");
        if (db.SwapCache(matches)) {
            return true;
        }
    } else if (!profile_data.empty()) {
        // osl: upstream closed the cache for the session here, so it was never rewritten
        // and every later session compiled every shader again (stutters on each new area).
        // Start a fresh cache for this build instead.
        LOG_WARNING(Render, "Pipeline cache profile has unexpected size ({} != {}): rebuilding it",
                    profile_data.size(), sizeof(Shader::Profile));
        db.Clear();
    }
    db.FinishPreload();
    profile_data.resize(sizeof(profile));
    std::memcpy(profile_data.data(), &profile, sizeof(profile));
    db.Save(Storage::BlobType::ShaderProfile, "profile", std::move(profile_data));
    return false;
}

void PipelineCache::WarmUp(const std::function<void(u32, u32)>& progress) {
    if (!CheckCacheProfile() || DropCrashedCache()) {
        return;
    }
    t_warmup_thread = true;
    SCOPE_EXIT {
        t_warmup_thread = false;
    };
    FindCrowdedShaders();

    u32 num_pipelines{};
    u32 num_total_pipelines{};
    const u32 expected =
        u32(Storage::DataBase::Instance().CountBlobs(Storage::BlobType::PipelineKey));
    const auto started = std::chrono::steady_clock::now();
    const auto report = [&] {
        if (progress) {
            progress(num_total_pipelines, std::max(expected, num_total_pipelines));
        }
    };

    Storage::DataBase::Instance().ForEachBlob(
        Storage::BlobType::PipelineKey, [&](std::vector<u8>&& data, const std::string& name) {
            ++num_total_pipelines;
            report();

            CachedPipelineKey key{};
            if (!ReadPipelineKey(std::move(data), key)) {
                RemoveStale(Storage::BlobType::PipelineKey, name, "damaged or of another build");
                return;
            }
            const auto result = key.compute ? LoadComputePipeline(key) : LoadGraphicsPipeline(key);
            if (result == CacheLoad::Stale) {
                RemoveStaleKey(name, "a stage was compiled again or is no longer cached");
            }
            num_pipelines += result == CacheLoad::Loaded;
        });

    std::printf("Pipeline warm-up: %u of %u pipelines in %.2f s, %u stale entries removed\n",
                num_pipelines, num_total_pipelines,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(),
                g_stale_entries.load());
    LOG_INFO(Render, "Preloaded {} pipelines", num_pipelines);
    ReportPipelineBinaryCache();
    if (num_total_pipelines > num_pipelines) {
        LOG_WARNING(Render, "{} stale pipelines were found. Consider re-generating the cache",
                    num_total_pipelines - num_pipelines);
    }

    Storage::DataBase::Instance().FinishPreload();
}

void PipelineCache::StartOnDemand() {
    CheckCacheProfile();
    Storage::DataBase::Instance().FinishPreload();
    std::printf("Pipeline startup: on demand, no bulk warm-up\n");
}

// osl: one cached shader stage read by the background warm-up, shared by every cached
// pipeline that uses it. A pool thread fills it; the GPU thread merges it into program_cache
// once (MergeWarmStage).
struct PipelineCache::WarmStage {
    vk::Device device;
    std::unique_ptr<Program> program;
    Shader::StageSpecialization spec{};
    std::optional<Shader::Gcn::FetchShaderData> fetch;
    size_t perm_idx{};
    u64 binary_hash{}; ///< its stage print
    vk::ShaderModule module{};
    bool gone = false; ///< no longer in the store
    // GPU thread, from the merge on.
    bool merged = false;
    bool module_kept = false;           ///< the module now belongs to a Program
    const Shader::Info* info = nullptr; ///< null: not usable (read failed or conflict)
    vk::ShaderModule result{};

    ~WarmStage() {
        if (module && !module_kept) {
            device.destroyShaderModule(module);
        }
    }
};

// osl: StartBackgroundWarmUp. The pool reads the store and builds the pipelines at low
// priority; the GPU thread merges the stages and publishes the pipelines a few at a time.
struct PipelineCache::BackgroundWarmup {
    struct Entry {
        std::chrono::steady_clock::time_point queued;
        bool compute = false;
        GraphicsPipelineKey graphics_key{};
        ComputePipelineKey compute_key{};
        GraphicsPipeline::SerializationSupport sdata{};
        std::array<std::shared_ptr<WarmStage>, MaxShaderStages> stages{};
    };
    struct Built {
        GraphicsPipelineKey graphics_key{};
        ComputePipelineKey compute_key{};
        std::unique_ptr<GraphicsPipeline> graphics;
        std::unique_ptr<ComputePipeline> compute;
    };

    std::unordered_map<u64, std::shared_ptr<WarmStage>> stages; ///< read job only
    WarmupInbox<Entry> read;
    WarmupInbox<Built> built;
    std::atomic<u32> total{0}, done{0};
    std::atomic<bool> reading{true}, stopping{false};
    std::atomic<u64> reader_ns{0}, entry_ns{0}, shader_io_ns{0}, module_ns{0};
    std::atomic<u64> read_queue_ns{0}, build_queue_ns{0}, build_ns{0};
    std::atomic<u32> entries{0}, modules{0}, submitted{0}, completed{0};
    u64 pump_ns = 0, pump_max_ns = 0, pump_gap_max_ns = 0;
    u32 batches = 0;
    bool finished = false;
    u32 loaded = 0, threads = 0;
    std::chrono::steady_clock::time_point started, last_batch;
    std::chrono::steady_clock::time_point last_pump{}, last_report{};
    std::unique_ptr<WarmupPool> pool; ///< last: its jobs use the members above
};

bool PipelineCache::StartBackgroundWarmUp() {
    if (!Storage::DataBase::Instance().ConcurrentReads()) {
        return false;
    }
    if (!CheckCacheProfile() || DropCrashedCache()) {
        return true; // nothing cached (any more)
    }
    background = new BackgroundWarmup;
    auto& warm = *background;
    warm.started = std::chrono::steady_clock::now();
    warm.threads = WarmupThreadCount(std::max(1u, std::thread::hardware_concurrency()));
    warm.pool = std::make_unique<WarmupPool>(warm.threads, true);
    // The first job reads the whole store; with one thread the builds queue behind it.
    warm.pool->Push([this, &warm] {
        const auto reader_started = std::chrono::steady_clock::now();
        SCOPE_EXIT {
            warm.stages.clear();
            warm.reader_ns = ElapsedNs(reader_started);
            warm.reading = false;
        };
        auto& db = Storage::DataBase::Instance();
        FindCrowdedShaders();
        warm.total = u32(db.CountBlobs(Storage::BlobType::PipelineKey));
        db.ForEachBlob(
            Storage::BlobType::PipelineKey,
            [&](std::vector<u8>&& data, const std::string& name) {
                ReadWarmEntry(warm, std::move(data), name);
            },
            [&] { return warm.stopping.load(std::memory_order_relaxed); });
        return true;
    });
    std::printf("Pipeline warm-up: loading in the background (%u threads)\n", warm.threads);
    return true;
}

void PipelineCache::ReadWarmEntry(BackgroundWarmup& warm, std::vector<u8>&& data,
                                  const std::string& name) {
    const auto entry_started = std::chrono::steady_clock::now();
    SCOPE_EXIT { warm.entry_ns += ElapsedNs(entry_started); ++warm.entries; };
    const auto skip = [&] { warm.done.fetch_add(1, std::memory_order_relaxed); };
    CachedPipelineKey key{};
    if (!ReadPipelineKey(std::move(data), key)) {
        RemoveStaleKey(name, "damaged or of another build");
        return skip();
    }

    BackgroundWarmup::Entry entry{};
    entry.compute = key.compute;
    entry.compute_key = key.compute_key;
    entry.graphics_key = key.graphics_key;
    entry.sdata = key.sdata;
    std::array<u64, MaxShaderStages> hashes{};
    if (entry.compute) {
        hashes[0] = entry.compute_key.value;
    } else {
        for (u32 i = 0; i < MaxShaderStages; ++i) {
            hashes[i] = entry.graphics_key.stage_hashes[i];
        }
    }

    for (u32 i = 0; i < MaxShaderStages; ++i) {
        if (!hashes[i]) {
            continue;
        }
        auto& stage = warm.stages[hashes[i]];
        if (!stage) {
            stage = std::make_shared<WarmStage>();
            stage->device = instance.GetDevice();
            stage->program = std::make_unique<Program>();
            stage->spec.info = &stage->program->info;
            const auto io_started = std::chrono::steady_clock::now();
            u64 module_time = 0;
            std::vector<u32> spv;
            if (ReadCachedStage(hashes[i], stage->program->info, stage->spec, stage->fetch,
                                stage->perm_idx, stage->binary_hash, spv, stage->gone)) {
                const auto module_started = std::chrono::steady_clock::now();
                stage->module = CompileSPV(spv, stage->device);
                module_time = ElapsedNs(module_started);
                warm.module_ns += module_time;
                ++warm.modules;
            }
            warm.shader_io_ns += ElapsedNs(io_started) - module_time;
            if (!stage->module) {
                stage->merged = true; // unusable: info stays null
            }
        }
        // Only the stages this pipeline was saved with.
        if (!entry.compute && stage->module && stage->binary_hash != key.prints[i]) {
            RemoveStaleKey(name, "a stage was compiled again");
            return skip();
        }
        if (stage->gone && !CompiledThisSession(hashes[i])) {
            RemoveStaleKey(name, "a stage is no longer cached");
            return skip();
        }
        entry.stages[i] = stage;
    }
    entry.queued = std::chrono::steady_clock::now();
    warm.read.Push(std::move(entry));
}

bool PipelineCache::MergeWarmStage(WarmStage& stage) {
    stage.merged = true;
    const u64 hash = stage.program->info.pgm_hash;
    NoteStagePrint(HashCombine(hash, u64(stage.perm_idx)), stage.binary_hash, false);
    const auto it_pgm = program_cache.find(hash);
    if (it_pgm == program_cache.end()) {
        Program* program = stage.program.get();
        program->InsertPermut(stage.module, std::move(stage.spec), stage.perm_idx);
        program_cache.emplace(hash, std::move(stage.program));
        stage.module_kept = true;
        stage.result = stage.module;
        stage.info = &program->info;
        return true;
    }
    // The game already uses this shader: its permutations are the ones that count.
    Program& program = *it_pgm->second;
    if (program.pending) {
        return false; // the game is compiling a permutation that takes the next index
    }
    const auto it = std::ranges::find(program.modules, stage.spec, &Program::Module::spec);
    if (it != program.modules.end()) {
        if (size_t(std::distance(program.modules.begin(), it)) != stage.perm_idx) {
            return false;
        }
        stage.result = it->module;
    } else {
        if (stage.perm_idx < program.modules.size() && program.modules[stage.perm_idx].module) {
            return false;
        }
        stage.spec.info = &program.info;
        program.InsertPermut(stage.module, std::move(stage.spec), stage.perm_idx);
        stage.module_kept = true;
        stage.result = stage.module;
    }
    stage.info = &program.info;
    return true;
}

void PipelineCache::PumpWarmUp() {
    if (!background || background->finished) {
        return;
    }
    auto& warm = *background;
    using namespace std::chrono_literals;
    const auto pump_started = std::chrono::steady_clock::now();
    if (warm.last_pump.time_since_epoch().count()) {
        warm.pump_gap_max_ns = std::max(warm.pump_gap_max_ns,
            u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    pump_started - warm.last_pump).count()));
    }
    warm.last_pump = pump_started;
    SCOPE_EXIT {
        const auto ns = ElapsedNs(pump_started);
        warm.pump_ns += ns;
        warm.pump_max_ns = std::max(warm.pump_max_ns, ns);
    };

    // Built pipelines: the first one ready for a key wins (the game may have compiled it).
    for (auto& built : warm.built.Take(64)) {
        const bool kept =
            built.graphics
                ? PublishFirst(graphics_pipelines, built.graphics_key, std::move(built.graphics))
                : PublishFirst(compute_pipelines, built.compute_key, std::move(built.compute));
        warm.loaded += kept;
        warm.done.fetch_add(1, std::memory_order_relaxed);
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - warm.last_batch < 4ms) {
        return;
    }
    warm.last_batch = now;
    ++warm.batches;
    const u32 done = warm.done + warm.pool->failed;
    OslOverlay::SetBackgroundLoading(done, std::max(warm.total.load(), done));

    static const std::array<Shader::RuntimeInfo, MaxShaderStages> no_runtime_infos{};
    for (auto& entry : warm.read.Take(32)) {
        warm.read_queue_ns += ElapsedNs(entry.queued);
        if (entry.compute ? compute_pipelines.contains(entry.compute_key)
                          : graphics_pipelines.contains(entry.graphics_key)) {
            warm.done.fetch_add(1, std::memory_order_relaxed); // the game compiled it first
            continue;
        }
        std::array<const Shader::Info*, MaxShaderStages> infos{};
        std::array<vk::ShaderModule, MaxShaderStages> modules{};
        std::optional<Shader::Gcn::FetchShaderData> fetch;
        bool usable = true;
        {
            std::unique_lock lock{programs_mutex};
            for (u32 i = 0; i < MaxShaderStages && usable; ++i) {
                if (!entry.stages[i]) {
                    continue;
                }
                auto& stage = *entry.stages[i];
                if (!stage.merged) {
                    MergeWarmStage(stage);
                }
                usable = stage.info != nullptr;
                infos[i] = stage.info;
                modules[i] = stage.result;
                if (stage.fetch) {
                    fetch = stage.fetch; // the vertex stage's, as the game's own lookup keeps
                }
            }
        }
        if (!usable) {
            warm.done.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        ++warm.submitted;
        warm.pool->Push([this, &warm, queued = std::chrono::steady_clock::now(),
                          compute = entry.compute, graphics_key = entry.graphics_key,
                         compute_key = entry.compute_key, sdata = entry.sdata, infos, modules,
                         fetch]() mutable {
            const auto build_started = std::chrono::steady_clock::now();
            warm.build_queue_ns += ElapsedNs(queued);
            SCOPE_EXIT { warm.build_ns += ElapsedNs(build_started); ++warm.completed; };
            BackgroundWarmup::Built built{graphics_key, compute_key};
            if (compute) {
                ComputePipeline::SerializationSupport compute_data{};
                built.compute = std::make_unique<ComputePipeline>(
                    instance, scheduler, desc_heap, profile, *pipeline_cache, compute_key,
                    *infos[0], modules[0], compute_data, true);
                if (built.compute->Failed()) {
                    return false; // left for the game to compile
                }
            } else {
                built.graphics = std::make_unique<GraphicsPipeline>(
                    instance, scheduler, desc_heap, profile, graphics_key, *pipeline_cache, infos,
                    no_runtime_infos, fetch, modules, sdata, true);
                if (built.graphics->Failed()) {
                    return false;
                }
            }
            warm.built.Push(std::move(built));
            return true;
        });
    }

    if (now - warm.last_report >= 1s) {
        warm.last_report = now;
        std::printf("Warm-up progress: %.2f s, read %u, submitted %u, completed %u, done %u/%u, "
                    "batches %u, reading %u\n",
                    std::chrono::duration<double>(now - warm.started).count(),
                    warm.entries.load(), warm.submitted.load(), warm.completed.load(),
                    warm.done.load(), warm.total.load(), warm.batches, unsigned(warm.reading.load()));
    }

    if (warm.reading || !warm.read.Empty() || !warm.pool->Idle() || !warm.built.Empty()) {
        return;
    }
    std::printf("Pipeline warm-up: %u of %u pipelines in %.2f s in the background (%u threads), "
                "%u stale entries removed\n",
                warm.loaded, warm.total.load(),
                std::chrono::duration<double>(now - warm.started).count(), warm.threads,
                g_stale_entries.load());
    LOG_INFO(Render, "Preloaded {} pipelines in the background", warm.loaded);
    ReportPipelineBinaryCache();
    std::printf("Warm-up profile: reader wall %.3f s, entries %.3f s, shader IO/meta %.3f s, "
                "modules %.3f s (%u); GPU pump %.3f s, max %.3f ms, gap max %.3f ms, "
                "batches %u; builds %.3f thread-s, read queue %.3f item-s, "
                "build queue %.3f item-s\n",
                warm.reader_ns.load() / 1e9, warm.entry_ns.load() / 1e9,
                warm.shader_io_ns.load() / 1e9, warm.module_ns.load() / 1e9, warm.modules.load(),
                warm.pump_ns / 1e9, warm.pump_max_ns / 1e6, warm.pump_gap_max_ns / 1e6,
                warm.batches, warm.build_ns.load() / 1e9, warm.read_queue_ns.load() / 1e9,
                warm.build_queue_ns.load() / 1e9);
    std::printf("Pipeline binary profile: keys %.3f thread-s, disk/decode/migration %.3f thread-s, "
                "binary handles %.3f thread-s, driver restore %.3f thread-s, "
                "driver compile %.3f thread-s\n",
                binary_key_ns.load() / 1e9, binary_load_ns.load() / 1e9,
                binary_handles_ns.load() / 1e9, binary_restore_ns.load() / 1e9,
                binary_compile_ns.load() / 1e9);
    extern std::atomic<u64> g_osl_pipeline_total_ns, g_osl_pipeline_driver_ns;
    extern std::atomic<u32> g_osl_pipelines_built;
    const u32 built = std::max(1u, g_osl_pipelines_built.load());
    std::printf("Pipeline build time: %u graphics pipelines, %.1f ms each (driver %.1f ms, the rest "
                "%.1f ms), %.1f s in all\n",
                built, g_osl_pipeline_total_ns / 1e6 / built, g_osl_pipeline_driver_ns / 1e6 / built,
                (g_osl_pipeline_total_ns - g_osl_pipeline_driver_ns) / 1e6 / built,
                g_osl_pipeline_total_ns / 1e9);
    // The idle threads stay parked (StopWarmUp).
    warm.finished = true;
    OslOverlay::SetBackgroundLoading(0, 0);
    Storage::DataBase::Instance().FinishPreload();
}

void PipelineCache::StopWarmUp() {
    if (!background) {
        return;
    }
    background->stopping = true;
    background->pool->Drain();
    // ponytail: leaked with its parked threads (this runs at exit only): a thread that ended
    // while the game ran crashed in ntdll (RtlWakeAllConditionVariable). Join them once the
    // cause is known.
    background = nullptr;
    OslOverlay::SetBackgroundLoading(0, 0);
}

void PipelineCache::Sync() {
    StopWarmUp();
    FinishCompilations();
    Storage::DataBase::Instance().Close();
}

} // namespace Vulkan

// osl: for the fatal fault handler, on the faulting thread, before the process ends. Allocates
// nothing: the mark's path was prepared when the warm-up started (TakeWarmupMark).
extern "C" void oslgpu_note_fatal_fault(void) {
    if (!Vulkan::t_warmup_thread || !Vulkan::g_warmup_mark[0]) {
        return;
    }
#ifdef _WIN32
    const HANDLE file = CreateFileW(Vulkan::g_warmup_mark, GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
#else
    const int file = open(Vulkan::g_warmup_mark, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file >= 0) {
        close(file);
    }
#endif
    std::fputs("Pipeline cache: the crash happened while cached pipelines were built; the next "
               "start clears the cache\n",
               stderr);
}

namespace Shader {

void Info::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer info{ar};

    info.Write(this, sizeof(InfoPersistent));
    info.Write(flattened_ud_buf);
    srt_info.Serialize(ar);
}

bool Info::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader info{ar};

    info.Read(this, sizeof(Shader::InfoPersistent));
    info.Read(flattened_ud_buf);

    return srt_info.Deserialize(ar);
}

void Gcn::FetchShaderData::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer fetch{ar};
    ar.Grow(6 + attributes.size() * sizeof(VertexAttribute));

    fetch.Write(size);
    fetch.Write(vertex_offset_sgpr);
    fetch.Write(instance_offset_sgpr);
    fetch.Write(attributes);
}

bool Gcn::FetchShaderData::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader fetch{ar};

    fetch.Read(size);
    fetch.Read(vertex_offset_sgpr);
    fetch.Read(instance_offset_sgpr);
    fetch.Read(attributes);

    return true;
}

void PersistentSrtInfo::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer srt{ar};

    srt.Write(this, sizeof(*this));
    if (walker_func_size) {
        srt.Write(reinterpret_cast<void*>(walker_func), walker_func_size);
    }
}

bool PersistentSrtInfo::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader srt{ar};

    srt.Read(this, sizeof(*this));

    // osl: the stored pointer is from the process that wrote the blob.
    walker_func = nullptr;
    if (walker_func_size) {
        if (walker_func_size > ar.Remaining()) {
            throw Serialization::CorruptData{};
        }
        walker_func = RegisterWalkerCode(ar.CurrPtr(), walker_func_size);
        ar.Advance(walker_func_size);
    }

    return true;
}

void StageSpecialization::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer spec{ar};

    spec.Write(start);
    spec.Write(runtime_info);

    spec.Write(bitset.to_string());

    if (fetch_shader_data) {
        spec.Write(sizeof(*fetch_shader_data));
        fetch_shader_data->Serialize(ar);
    } else {
        spec.Write(size_t{0});
    }

    spec.Write(vs_attribs);
    spec.Write(buffers);
    spec.Write(images);
    spec.Write(fmasks);
    spec.Write(samplers);
}

bool StageSpecialization::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader spec{ar};

    spec.Read(start);
    spec.Read(runtime_info);

    std::string bits{};
    spec.Read(bits);
    bitset = std::bitset<MaxStageResources>(bits);

    u64 fetch_data_size{};
    spec.Read(fetch_data_size);

    if (fetch_data_size) {
        Gcn::FetchShaderData fetch_data;
        fetch_data.Deserialize(ar);
        fetch_shader_data = fetch_data;
    }

    spec.Read(vs_attribs);
    spec.Read(buffers);
    spec.Read(images);
    spec.Read(fmasks);
    spec.Read(samplers);

    return true;
}

} // namespace Shader
