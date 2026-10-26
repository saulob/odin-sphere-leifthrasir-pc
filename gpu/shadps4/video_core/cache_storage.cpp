// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/elf_info.h"
#include "common/io_file.h"
#include "common/polyfill_thread.h"
#include "common/thread.h"
#include "core/emulator_settings.h"

#include "video_core/cache_storage.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"

#include <miniz.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>

namespace {

std::mutex submit_mutex{};
// Raised by submitters and lowered by the IO thread, which also polls it outside submit_mutex.
std::atomic<u32> num_requests{};
std::condition_variable_any request_cv{};
std::queue<std::packaged_task<void()>> req_queue{};
std::mutex m_request{};

mz_zip_archive zip_ar{};
bool ar_is_read_only{true};

} // namespace

namespace Storage {

void ProcessIO(const std::stop_token& stoken) {
    Common::SetCurrentThreadName("shadPS4:PipelineCacheIO");

    while (!stoken.stop_requested()) {
        {
            std::unique_lock lk{submit_mutex};
            Common::CondvarWait(request_cv, lk, stoken, [&] { return num_requests.load() != 0; });
        }

        if (stoken.stop_requested()) {
            break;
        }

        while (num_requests.load() != 0) {
            std::packaged_task<void()> request{};
            {
                std::scoped_lock lock{m_request};
                if (req_queue.empty()) {
                    continue;
                }
                request = std::move(req_queue.front());
                req_queue.pop();
            }

            if (request.valid()) {
                request();
                request.get_future().wait();
            }

            --num_requests;
        }
    }
}

constexpr std::string GetBlobFileExtension(BlobType type) {
    switch (type) {
    case BlobType::ShaderMeta: {
        return "meta";
    }
    case BlobType::ShaderBinary: {
        return "spv";
    }
    case BlobType::PipelineKey: {
        return "key";
    }
    case BlobType::ShaderProfile: {
        return "bin";
    }
    default:
        UNREACHABLE();
    }
}

static void Submit(std::packaged_task<void()>&& request) {
    {
        std::scoped_lock lock{m_request};
        req_queue.emplace(std::move(request));
    }
    std::scoped_lock lk{submit_mutex};
    ++num_requests;
    request_cv.notify_one();
}

/// Calls `func` for each entry of `dir` while it returns true; a listing error ends the walk.
template <typename Func>
void ForEachEntry(const std::filesystem::path& dir, Func&& func) {
    std::error_code error;
    for (std::filesystem::directory_iterator it{dir, error}, end; !error && it != end;
         it.increment(error)) {
        if (!func(*it)) {
            return;
        }
    }
}

void DataBase::Open() {
    if (opened) {
        return;
    }

    const auto& game_info = Common::ElfInfo::Instance();

    using namespace Common::FS;
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        mz_zip_zero_struct(&zip_ar);

        cache_path = GetUserPath(PathType::CacheDir) /
                     std::filesystem::path{game_info.GameSerial()}.replace_extension(".zip");

        if (!mz_zip_reader_init_file(&zip_ar, cache_path.string().c_str(),
                                     MZ_ZIP_FLAG_READ_ALLOW_WRITING) ||
            !mz_zip_validate_archive(&zip_ar, 0)) {
            LOG_INFO(Render, "Cache archive {} is not found or archive is corrupted",
                     cache_path.string().c_str());
            mz_zip_reader_end(&zip_ar);
            mz_zip_writer_init_file(&zip_ar, cache_path.string().c_str(), 0);
        }
    } else {
        cache_path = GetUserPath(PathType::CacheDir) / game_info.GameSerial();
        if (!std::filesystem::exists(cache_path)) {
            std::filesystem::create_directories(cache_path);
        }
    }

    io_worker = std::jthread{ProcessIO};
    opened = true;
}

void DataBase::Close() {
    if (!IsOpened()) {
        return;
    }

    io_worker.request_stop();
    io_worker.join();

    if (EmulatorSettings.IsPipelineCacheArchived()) {
        mz_zip_writer_finalize_archive(&zip_ar);
        mz_zip_writer_end(&zip_ar);
    }

    opened = false;
    LOG_INFO(Render, "Cache dumped");
}

template <typename T>
bool WriteVector(const BlobType type, std::filesystem::path&& path_, std::vector<T>&& v) {
    Submit(std::packaged_task<void()>{[=]() {
        auto path{path_};
        path.replace_extension(GetBlobFileExtension(type));
        if (EmulatorSettings.IsPipelineCacheArchived()) {
            ASSERT_MSG(!ar_is_read_only,
                       "The archive is read-only. Did you forget to call `FinishPreload`?");
            if (!mz_zip_writer_add_mem(&zip_ar, path.string().c_str(), v.data(),
                                       v.size() * sizeof(T), MZ_BEST_COMPRESSION)) {
                LOG_ERROR(Render, "Failed to add {} to the archive", path.string().c_str());
            }
        } else {
            // osl: write beside and rename into place, so a game that ends during the
            // write (the queue is not drained at exit) never leaves a truncated blob that
            // the next start would load.
            using namespace Common::FS;
            auto temp{path};
            temp += ".tmp";
            bool written{};
            {
                const auto file = IOFile{temp, FileAccessMode::Create};
                written = file.IsOpen() && file.Write(v) == v.size();
            }
            std::error_code error;
            if (!written) {
                std::filesystem::remove(temp, error);
                return;
            }
            // osl: Windows refuses the rename while another thread reads the old blob (the
            // background warm-up). Retried; when it keeps failing this write is dropped, so no
            // .tmp is left beside a blob of another generation.
            for (int attempt = 0; attempt < 20; ++attempt) {
                std::filesystem::rename(temp, path, error);
                if (!error) {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            static std::atomic<u32> reported{0};
            if (reported.fetch_add(1, std::memory_order_relaxed) < 8) {
                LOG_WARNING(Render, "Pipeline cache: {} not replaced ({})",
                            path.filename().string(), error.message());
            }
            std::filesystem::remove(temp, error);
        }
    }});
    return true;
}

template <typename T>
void LoadVector(BlobType type, std::filesystem::path& path, std::vector<T>& v) {
    using namespace Common::FS;
    path.replace_extension(GetBlobFileExtension(type));
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        int index{-1};
        index = mz_zip_reader_locate_file(&zip_ar, path.string().c_str(), nullptr, 0);
        if (index < 0) {
            LOG_WARNING(Render, "File {} is not found in the archive", path.string().c_str());
            return;
        }
        mz_zip_archive_file_stat stat{};
        mz_zip_reader_file_stat(&zip_ar, index, &stat);
        v.resize(stat.m_uncomp_size / sizeof(T));
        mz_zip_reader_extract_to_mem(&zip_ar, index, v.data(), stat.m_uncomp_size, 0);
    } else {
        const auto file = IOFile{path, FileAccessMode::Read};
        v.resize(file.GetSize() / sizeof(T));
        file.Read(v);
    }
}

bool DataBase::Save(BlobType type, const std::string& name, std::vector<u8>&& data) {
    if (!opened) {
        return false;
    }

    auto path = EmulatorSettings.IsPipelineCacheArchived() ? std::filesystem::path{name}
                                                           : cache_path / name;
    return WriteVector(type, std::move(path), std::move(data));
}

bool DataBase::Save(BlobType type, const std::string& name, std::vector<u32>&& data) {
    if (!opened) {
        return false;
    }

    auto path = EmulatorSettings.IsPipelineCacheArchived() ? std::filesystem::path{name}
                                                           : cache_path / name;
    return WriteVector(type, std::move(path), std::move(data));
}

void DataBase::Load(BlobType type, const std::string& name, std::vector<u8>& data) {
    if (!opened) {
        return;
    }

    auto path = EmulatorSettings.IsPipelineCacheArchived() ? std::filesystem::path{name}
                                                           : cache_path / name;
    return LoadVector(type, path, data);
}

void DataBase::Load(BlobType type, const std::string& name, std::vector<u32>& data) {
    if (!opened) {
        return;
    }

    auto path = EmulatorSettings.IsPipelineCacheArchived() ? std::filesystem::path{name}
                                                           : cache_path / name;
    return LoadVector(type, path, data);
}

size_t DataBase::CountBlobs(BlobType type) {
    const auto& ext = GetBlobFileExtension(type);
    size_t count = 0;
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        const auto num_files = mz_zip_reader_get_num_files(&zip_ar);
        for (int index = 0; index < num_files; ++index) {
            std::array<char, MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE> file_name{};
            mz_zip_reader_get_filename(&zip_ar, index, file_name.data(), file_name.size());
            count += std::string{file_name.data()}.ends_with(ext);
        }
        return count;
    }
    ForEachEntry(cache_path, [&](const std::filesystem::directory_entry& entry) {
        count += entry.path().extension().string().ends_with(ext);
        return true;
    });
    return count;
}

void DataBase::ForEachBlobName(BlobType type,
                               const std::function<void(const std::string& name)>& func) {
    const auto ext = "." + GetBlobFileExtension(type);
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        const auto num_files = mz_zip_reader_get_num_files(&zip_ar);
        for (int index = 0; index < num_files; ++index) {
            std::array<char, MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE> file_name{};
            mz_zip_reader_get_filename(&zip_ar, index, file_name.data(), file_name.size());
            const std::string name{file_name.data()};
            if (name.ends_with(ext)) {
                func(name.substr(0, name.size() - ext.size()));
            }
        }
        return;
    }
    ForEachEntry(cache_path, [&](const std::filesystem::directory_entry& entry) {
        if (entry.path().extension().string() == ext) {
            func(entry.path().stem().string());
        }
        return true;
    });
}

bool DataBase::ConcurrentReads() const {
    return !EmulatorSettings.IsPipelineCacheArchived();
}

void DataBase::ForEachBlob(BlobType type, const BlobVisitor& func,
                           const std::function<bool()>& stop) {
    const auto& ext = GetBlobFileExtension(type);
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        const auto num_files = mz_zip_reader_get_num_files(&zip_ar);
        for (int index = 0; index < num_files; ++index) {
            std::array<char, MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE> file_name{};
            file_name.fill(0);
            mz_zip_reader_get_filename(&zip_ar, index, file_name.data(), file_name.size());
            const std::string name{file_name.data()};
            if (name.ends_with(ext)) {
                mz_zip_archive_file_stat stat{};
                mz_zip_reader_file_stat(&zip_ar, index, &stat);
                std::vector<u8> data(stat.m_uncomp_size);
                mz_zip_reader_extract_to_mem(&zip_ar, index, data.data(), data.size(), 0);
                func(std::move(data), std::filesystem::path{name}.stem().string());
            }
        }
    } else {
        ForEachEntry(cache_path, [&](const std::filesystem::directory_entry& entry) {
            if (stop && stop()) {
                return false;
            }
            if (entry.path().extension().string().ends_with(ext)) {
                using namespace Common::FS;
                const auto& file = IOFile{entry.path(), FileAccessMode::Read};
                if (file.IsOpen()) {
                    std::vector<u8> data(file.GetSize());
                    file.Read(data);
                    func(std::move(data), entry.path().stem().string());
                }
            }
            return true;
        });
    }
}

void DataBase::Clear() {
    if (!opened || EmulatorSettings.IsPipelineCacheArchived()) {
        return;
    }
    u64 removed = 0;
    ForEachEntry(cache_path, [&](const std::filesystem::directory_entry& entry) {
        std::error_code ec;
        removed += entry.is_regular_file(ec) && std::filesystem::remove(entry.path(), ec);
        return true;
    });
    LOG_WARNING(Render, "Pipeline cache cleared ({} files): it is rebuilt for this build", removed);
}

// osl: the cache holds one GPU's shaders (the profile says which). So that a PC switching between
// its integrated GPU and its graphics card keeps both, the other GPU's cache waits beside this
// one (<serial>.gpu-<n>, at most MaxKept).
bool DataBase::SwapCache(const std::function<bool(const std::vector<u8>&)>& matches) {
    if (!opened || EmulatorSettings.IsPipelineCacheArchived()) {
        return false;
    }
    namespace fs = std::filesystem;
    constexpr size_t MaxKept = 2;
    const fs::path parent = cache_path.parent_path();
    const std::string prefix = cache_path.filename().string() + ".gpu-";
    const auto stored_profile = [&](const fs::path& dir) {
        std::vector<u8> data;
        using namespace Common::FS;
        const IOFile file{dir / ("profile." + GetBlobFileExtension(BlobType::ShaderProfile)),
                          FileAccessMode::Read};
        if (file.IsOpen()) {
            data.resize(file.GetSize());
            file.Read(data);
        }
        return data;
    };
    const auto stamp = [](const fs::path& dir) {
        std::error_code ec;
        return fs::last_write_time(dir, ec);
    };
    std::vector<fs::path> kept;
    ForEachEntry(parent, [&](const fs::directory_entry& entry) {
        std::error_code ec;
        if (entry.is_directory(ec) && entry.path().filename().string().starts_with(prefix)) {
            kept.push_back(entry.path());
        }
        return true;
    });
    std::error_code ec;
    fs::path aside;
    for (u32 n = 1; aside.empty() || fs::exists(aside, ec); ++n) {
        aside = parent / fmt::format("{}{}", prefix, n);
    }
    fs::rename(cache_path, aside, ec);
    if (ec) {
        LOG_WARNING(Render, "Pipeline cache: the other GPU's cache was not put aside ({})",
                    ec.message());
        Clear();
        return false;
    }
    // This GPU's most recently used cache comes back; older ones of the same GPU go.
    std::vector<fs::path> mine;
    for (const auto& dir : kept) {
        if (matches(stored_profile(dir))) {
            mine.push_back(dir);
        }
    }
    std::ranges::sort(mine, std::ranges::greater{}, stamp);
    bool restored = false;
    if (!mine.empty()) {
        fs::rename(mine.front(), cache_path, ec);
        restored = !ec;
        if (restored) {
            for (const auto& dir : mine) {
                if (dir != mine.front()) {
                    fs::remove_all(dir, ec);
                }
                std::erase(kept, dir);
            }
        }
    }
    if (!restored) {
        fs::create_directories(cache_path, ec);
    }
    // A folder's write time moves only when a file is added, so the one just put aside is
    // stamped now; the least recently used others go once more than MaxKept wait.
    fs::last_write_time(aside, fs::file_time_type::clock::now(), ec);
    std::ranges::sort(kept, {}, stamp);
    kept.push_back(aside);
    while (kept.size() > MaxKept) {
        fs::remove_all(kept.front(), ec);
        kept.erase(kept.begin());
    }
    LOG_WARNING(Render, "Pipeline cache: another GPU's cache put aside as {}; {}",
                aside.filename().string(),
                restored ? "this GPU's earlier cache restored" : "this GPU starts a new cache");
    return restored;
}

void DataBase::Remove(BlobType type, const std::string& name) {
    if (!opened || EmulatorSettings.IsPipelineCacheArchived()) {
        return;
    }
    auto path = cache_path / (name + "." + GetBlobFileExtension(type));
    Submit(std::packaged_task<void()>{[path = std::move(path)] {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        std::filesystem::remove(std::filesystem::path{path} += ".tmp", ignored);
    }});
}

void DataBase::FinishPreload() {
    if (EmulatorSettings.IsPipelineCacheArchived()) {
        mz_zip_writer_init_from_reader(&zip_ar, cache_path.string().c_str());
        ar_is_read_only = false;
    }
}

} // namespace Storage
