#include "title_exit_assets.h"

#include <cstdio>
#include <fstream>
#include <optional>
#include <system_error>

#include "controller_prompts.h"
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

std::filesystem::path StampPath(const std::filesystem::path& archive) {
    std::filesystem::path stamp = archive;
    stamp += ".title.stamp";
    return stamp;
}

std::string PatchStamp(std::span<const uint8_t> patch) {
    char text[16];
    std::snprintf(text, sizeof(text), "%08x", Crc32(patch));
    return text;
}

// A layer of an older patch counts as not applied: only the patch this build ships is current.
bool CurrentLayer(const std::filesystem::path& archive, std::span<const uint8_t> patch) {
    if (ArchiveState(archive, kTitleUndoSuffix) != ArchivePatchState::kPatched) {
        return false;
    }
    std::ifstream in(StampPath(archive));
    std::string stamp;
    return in >> stamp && stamp == PatchStamp(patch);
}

void Note(std::string& detail, const std::string& step) {
    detail += (detail.empty() ? "" : "; ") + step;
}

} // namespace

bool TitleExitPending(const std::filesystem::path& app0, const GameBuild& build, std::span<const uint8_t> patch) {
    const std::optional<BlockPatch> parsed = ParseBlockPatch(patch);
    if (!parsed) {
        return false;
    }
    for (const std::filesystem::path& archive : PromptArchives(app0, build, *parsed)) {
        if (!CurrentLayer(archive, patch)) {
            return true;
        }
    }
    return false;
}

TitleExitResult RevertTitleExit(const std::filesystem::path& app0, const GameBuild& build) {
    TitleExitResult result;
    result.ok = true;
    for (const std::filesystem::path& archive : LayerArchives(app0, build)) {
        if (ArchiveState(archive, kTitleUndoSuffix) != ArchivePatchState::kPatched) {
            continue;
        }
        const PatchResult step = RevertBlockPatch(archive, kTitleUndoSuffix);
        Note(result.detail, step.detail);
        result.ok &= step.ok;
        std::error_code removed;
        std::filesystem::remove(StampPath(archive), removed);
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
    for (const std::filesystem::path& archive : PromptArchives(app0, build, *parsed)) {
        if (CurrentLayer(archive, patch)) {
            continue;
        }
        if (ArchiveState(archive, kTitleUndoSuffix) == ArchivePatchState::kPatched) {
            const PatchResult stale = RevertBlockPatch(archive, kTitleUndoSuffix);
            if (!stale.ok) {
                Note(result.detail, stale.detail);
                return result;
            }
        }
        if (hooks.phase) {
            hooks.phase(("Updating " + Utf8(archive.filename()) + "...").c_str());
        }
        const PatchResult step = ApplyBlockPatch(archive, *parsed, hooks, kTitleUndoSuffix);
        Note(result.detail, step.detail);
        if (step.ok && ArchiveState(archive, kTitleUndoSuffix) == ArchivePatchState::kPatched) {
            std::ofstream(StampPath(archive), std::ios::trunc) << PatchStamp(patch);
        }
        if (!step.ok) {
            result.cancelled = step.cancelled;
            const TitleExitResult undone = RevertTitleExit(app0, build);
            if (!undone.ok) {
                Note(result.detail, "could not restore every archive");
            }
            return result;
        }
    }
    result.ok = true;
    return result;
}

} // namespace osl
