#include "controller_prompts.h"

#include <system_error>

#include "utf8.h"

namespace osl {
namespace {

std::vector<std::filesystem::path> DataArchives(const std::filesystem::path& app0, const GameBuild& build) {
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

void Note(std::string& detail, const std::string& step) {
    detail += (detail.empty() ? "" : "; ") + step;
}

} // namespace

const char* PromptStyleKey(PromptStyle style) {
    return style == PromptStyle::kXbox ? kPromptsXbox : kPromptsPlayStation;
}

const char* PromptStyleLabel(PromptStyle style) {
    return style == PromptStyle::kXbox ? "Xbox" : "PlayStation";
}

std::optional<PromptStyle> ParsePromptStyle(std::string_view key) {
    if (key == kPromptsPlayStation) {
        return PromptStyle::kPlayStation;
    }
    if (key == kPromptsXbox) {
        return PromptStyle::kXbox;
    }
    return std::nullopt;
}

std::vector<std::filesystem::path> PromptArchives(const std::filesystem::path& app0, const GameBuild& build,
                                                  const BlockPatch& patch) {
    std::vector<std::filesystem::path> out;
    for (const std::filesystem::path& archive : DataArchives(app0, build)) {
        CpkArchive cpk;
        if (!cpk.Open(archive)) {
            continue;
        }
        for (const BlockPatchTarget& target : patch.targets) {
            if (cpk.Find(target.name)) {
                out.push_back(archive);
                break;
            }
        }
    }
    return out;
}

PromptStyle InstalledPromptStyle(const std::filesystem::path& app0, const GameBuild& build,
                                 std::span<const uint8_t> xbox_patch) {
    if (!AnyPromptPatch(app0, build)) {
        return PromptStyle::kPlayStation;
    }
    const std::optional<BlockPatch> patch = ParseBlockPatch(xbox_patch);
    if (!patch) {
        return PromptStyle::kPlayStation;
    }
    const std::vector<std::filesystem::path> archives = PromptArchives(app0, build, *patch);
    if (archives.empty()) {
        return PromptStyle::kPlayStation;
    }
    for (const std::filesystem::path& archive : archives) {
        if (!HoldsBlockPatch(archive, *patch)) {
            return PromptStyle::kPlayStation;
        }
    }
    return PromptStyle::kXbox;
}

bool AnyPromptPatch(const std::filesystem::path& app0, const GameBuild& build) {
    for (const std::filesystem::path& archive : DataArchives(app0, build)) {
        if (ArchiveState(archive) == ArchivePatchState::kPatched) {
            return true;
        }
    }
    return false;
}

PromptsResult SetPromptStyle(const std::filesystem::path& app0, const GameBuild& build, PromptStyle style,
                             std::span<const uint8_t> xbox_patch, const PatchHooks& hooks) {
    PromptsResult result;
    const auto restore_all = [&] {
        bool restored = true;
        for (const std::filesystem::path& archive : DataArchives(app0, build)) {
            if (ArchiveState(archive) != ArchivePatchState::kPatched) {
                continue;
            }
            if (hooks.phase) {
                hooks.phase(("Restoring " + Utf8(archive.filename()) + "...").c_str());
            }
            const PatchResult step = RevertBlockPatch(archive);
            Note(result.detail, step.detail);
            restored &= step.ok;
        }
        return restored;
    };
    if (style == PromptStyle::kPlayStation) {
        result.ok = restore_all();
        if (result.detail.empty()) {
            result.detail = "not patched";
        }
        return result;
    }

    std::string error;
    const std::optional<BlockPatch> patch = ParseBlockPatch(xbox_patch, &error);
    if (!patch) {
        result.detail = "the Xbox prompts patch is unreadable: " + error;
        return result;
    }
    const std::vector<std::filesystem::path> archives = PromptArchives(app0, build, *patch);
    if (archives.empty()) {
        result.detail = std::string("no archive of ") + build.label + " holds the button art";
        return result;
    }
    for (const std::filesystem::path& archive : archives) {
        if (hooks.phase) {
            hooks.phase(("Patching " + Utf8(archive.filename()) + "...").c_str());
        }
        const PatchResult step = ApplyBlockPatch(archive, *patch, hooks);
        Note(result.detail, step.detail);
        if (!step.ok || step.skipped) {
            result.cancelled = step.cancelled;
            if (!restore_all()) {
                Note(result.detail, "could not restore every archive");
            }
            return result;
        }
    }
    result.ok = true;
    return result;
}

} // namespace osl
