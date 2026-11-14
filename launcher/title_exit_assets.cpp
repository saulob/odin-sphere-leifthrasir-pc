#include "title_exit_assets.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <optional>
#include <system_error>

#include "game_files.h"
#include "utf8.h"

namespace osl {
namespace {

std::vector<std::filesystem::path> LayerArchives(const std::filesystem::path& app0, const GameBuild& build) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (const char* name : build.data_files) {
        const std::filesystem::path archive = app0 / name;
        if (std::filesystem::is_regular_file(archive, ec)) {
            out.push_back(archive);
        }
    }
    return out;
}

struct TitleArchive {
    std::filesystem::path path;
    std::vector<std::string> entries; // the title files it holds, spelled as its table of contents spells them
};

std::vector<TitleArchive> TitleArchives(const std::filesystem::path& app0, const GameBuild& build, const BlockPatch& patch) {
    std::vector<TitleArchive> out;
    for (const std::filesystem::path& archive : LayerArchives(app0, build)) {
        CpkArchive cpk;
        if (!cpk.Open(archive)) {
            continue;
        }
        TitleArchive title{archive, {}};
        for (const BlockPatchTarget& target : patch.targets) {
            const CpkEntry* entry = cpk.Find(target.name);
            if (entry && std::find(title.entries.begin(), title.entries.end(), entry->path) == title.entries.end()) {
                title.entries.push_back(entry->path);
            }
        }
        if (!title.entries.empty()) {
            out.push_back(std::move(title));
        }
    }
    return out;
}

std::filesystem::path StampPath(const std::filesystem::path& archive) {
    std::filesystem::path stamp = archive;
    stamp += ".title.stamp";
    return stamp;
}

std::string ReadStamp(const std::filesystem::path& archive) {
    std::ifstream in(StampPath(archive));
    std::string stamp;
    std::getline(in, stamp);
    return stamp;
}

std::string PatchStamp(std::span<const uint8_t> patch) {
    char text[16];
    std::snprintf(text, sizeof(text), "%08x", Crc32(patch));
    return text;
}

// Names the archive as it is now, so a reinstalled or otherwise changed archive is tried again.
std::string UnavailableStamp(const std::filesystem::path& archive, std::span<const uint8_t> patch) {
    std::error_code size_error, time_error;
    const uintmax_t size = std::filesystem::file_size(archive, size_error);
    const auto written = std::filesystem::last_write_time(archive, time_error).time_since_epoch().count();
    if (size_error || time_error) {
        return {};
    }
    return PatchStamp(patch) + " unavailable " + std::to_string(size) + " " + std::to_string(static_cast<long long>(written));
}

enum class LayerState { kPending, kCurrent, kUnavailable };

// Current only when the layer is of this patch and its undo record names every title file the archive holds: a layer an
// older launcher left with a file skipped is not.
LayerState StateOf(const TitleArchive& archive, std::span<const uint8_t> patch) {
    const std::string stamp = ReadStamp(archive.path);
    if (ArchiveState(archive.path, kTitleUndoSuffix) != ArchivePatchState::kPatched) {
        const std::string unavailable = UnavailableStamp(archive.path, patch);
        return !unavailable.empty() && stamp == unavailable ? LayerState::kUnavailable : LayerState::kPending;
    }
    if (stamp != PatchStamp(patch)) {
        return LayerState::kPending;
    }
    const std::vector<std::string> recorded = PatchedEntries(archive.path, kTitleUndoSuffix);
    for (const std::string& entry : archive.entries) {
        if (std::find(recorded.begin(), recorded.end(), entry) == recorded.end()) {
            return LayerState::kPending;
        }
    }
    return LayerState::kCurrent;
}

void Note(std::string& detail, const std::string& step) {
    detail += (detail.empty() ? "" : "; ") + step;
}

std::string UnsupportedLine(const GameBuild& build, const UnmatchedEntry& asset) {
    char tail[64];
    std::snprintf(tail, sizeof(tail), ", size %llu, CRC32 %08x: ", static_cast<unsigned long long>(asset.size), asset.crc);
    return std::string("Title menu unsupported asset: build ") + (build.label ? build.label : "?") + ", archive " + asset.archive +
           ", entry " + asset.entry + tail + asset.analysis;
}

} // namespace

bool TitleExitPending(const std::filesystem::path& app0, const GameBuild& build, std::span<const uint8_t> patch) {
    const std::optional<BlockPatch> parsed = ParseBlockPatch(patch);
    if (!parsed) {
        return false;
    }
    for (const TitleArchive& archive : TitleArchives(app0, build, *parsed)) {
        if (StateOf(archive, patch) == LayerState::kPending) {
            return true;
        }
    }
    return false;
}

bool TitleExitReady(const std::filesystem::path& app0, const GameBuild& build, std::span<const uint8_t> patch, std::string* why) {
    const auto refuse = [why](std::string reason) {
        if (why) {
            *why = std::move(reason);
        }
        return false;
    };
    const std::optional<BlockPatch> parsed = ParseBlockPatch(patch);
    if (!parsed) {
        return refuse("the title menu patch is unreadable");
    }
    const std::vector<TitleArchive> archives = TitleArchives(app0, build, *parsed);
    if (archives.empty()) {
        return refuse("no archive holds the title menu files");
    }
    for (const TitleArchive& archive : archives) {
        switch (StateOf(archive, patch)) {
        case LayerState::kCurrent:
            break;
        case LayerState::kUnavailable:
            return refuse("the title menu art of " + Utf8(archive.path.filename()) +
                          " is not a copy the patch was made for (see the launcher log)");
        case LayerState::kPending:
            return refuse("the title menu art update is not installed in " + Utf8(archive.path.filename()));
        }
    }
    return true;
}

bool TitleExitAssetsReady(const char* app0, const char* image_dir, std::span<const uint8_t> patch, std::string* why) {
    const GameBuild* build = BuildOfImageDir(image_dir);
    if (!build || !app0) {
        if (why) {
            *why = "not a supported game build";
        }
        return false;
    }
    return TitleExitReady(PathFromUtf8(app0), *build, patch, why);
}

TitleExitResult RevertTitleExit(const std::filesystem::path& app0, const GameBuild& build) {
    TitleExitResult result;
    result.ok = true;
    for (const std::filesystem::path& archive : LayerArchives(app0, build)) {
        std::error_code removed;
        if (ArchiveState(archive, kTitleUndoSuffix) != ArchivePatchState::kPatched) {
            std::filesystem::remove(StampPath(archive), removed);
            continue;
        }
        const PatchResult step = RevertBlockPatch(archive, kTitleUndoSuffix);
        Note(result.detail, step.detail);
        result.ok &= step.ok;
        if (step.ok) {
            std::filesystem::remove(StampPath(archive), removed);
        }
    }
    return result;
}

TitleExitResult ApplyTitleExit(const std::filesystem::path& app0, const GameBuild& build, std::span<const uint8_t> patch,
                               const PatchHooks& hooks) {
    TitleExitResult result;
    std::string error;
    const std::optional<BlockPatch> parsed = ParseBlockPatch(patch, &error);
    if (!parsed) {
        result.detail = "the title menu patch is unreadable: " + error;
        return result;
    }
    const auto undo_all = [&] {
        if (const TitleExitResult undone = RevertTitleExit(app0, build); !undone.ok) {
            Note(result.detail, "could not restore every archive: " + undone.detail);
            return false;
        }
        return true;
    };
    const std::vector<TitleArchive> archives = TitleArchives(app0, build, *parsed);
    std::vector<UnmatchedEntry> unmatched;
    for (const TitleArchive& archive : archives) {
        const LayerState state = StateOf(archive, patch);
        if (state == LayerState::kCurrent) {
            continue;
        }
        if (!unmatched.empty()) {
            // Read only, so the log names every title file the patch was not made for.
            const std::vector<UnmatchedEntry> more = FindUnmatchedEntries(archive.path, *parsed);
            unmatched.insert(unmatched.end(), more.begin(), more.end());
            continue;
        }
        if (ArchiveState(archive.path, kTitleUndoSuffix) == ArchivePatchState::kPatched) {
            const PatchResult stale = RevertBlockPatch(archive.path, kTitleUndoSuffix);
            if (!stale.ok) {
                Note(result.detail, stale.detail);
                undo_all();
                return result;
            }
        }
        if (hooks.phase) {
            hooks.phase(("Updating " + Utf8(archive.path.filename()) + "...").c_str());
        }
        const PatchResult step = ApplyBlockPatch(archive.path, *parsed, hooks, kTitleUndoSuffix);
        if (!step.ok) {
            Note(result.detail, step.detail);
            result.cancelled = step.cancelled;
            undo_all();
            return result;
        }
        if (step.skipped) {
            unmatched.insert(unmatched.end(), step.unmatched.begin(), step.unmatched.end());
            continue;
        }
        Note(result.detail, step.detail);
        std::ofstream(StampPath(archive.path), std::ios::trunc) << PatchStamp(patch);
    }
    if (!unmatched.empty()) {
        result.unavailable = true;
        for (const UnmatchedEntry& asset : unmatched) {
            result.unsupported.push_back(UnsupportedLine(build, asset));
        }
        result.detail = "unavailable: " + std::to_string(unmatched.size()) +
                        " title menu file(s) are not a copy the patch was made for; the original title menu is kept";
        if (undo_all()) {
            for (const TitleArchive& archive : archives) {
                std::ofstream(StampPath(archive.path), std::ios::trunc) << UnavailableStamp(archive.path, patch);
            }
        }
        return result;
    }
    if (result.detail.empty()) {
        result.detail = archives.empty() ? "no archive holds the title menu files" : "already current";
    }
    result.ok = true;
    return result;
}

} // namespace osl
