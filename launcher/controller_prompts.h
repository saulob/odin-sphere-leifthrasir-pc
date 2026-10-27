// The Controller prompts setting: which controller's buttons the game's prompts show. The PlayStation glyphs are the
// game's own; Xbox ones are painted over them in the button art (Interface.ftx and OnMemory.ftx, in the Classic and
// Refined archives of every language) by a block patch applied to the installed archives (cpk_patch.h), so the choice
// is made in the game files, not at run time, and is reversible. Taken from Dragon's Crown Pro PC.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cpk_patch.h"
#include "game_profile.h"

namespace osl {

enum class PromptStyle { kPlayStation, kXbox };

inline constexpr char kPromptsPlayStation[] = "playstation";
inline constexpr char kPromptsXbox[] = "xbox";

const char* PromptStyleKey(PromptStyle style);
const char* PromptStyleLabel(PromptStyle style);
std::optional<PromptStyle> ParsePromptStyle(std::string_view key);

// The build's archives that hold an entry the patch changes.
std::vector<std::filesystem::path> PromptArchives(const std::filesystem::path& app0, const GameBuild& build,
                                                  const BlockPatch& patch);

// What the installed archives show: Xbox once every archive that holds the button art holds `xbox_patch`, PlayStation
// otherwise. A half-patched install reads as PlayStation; choosing Xbox applies the patch again.
PromptStyle InstalledPromptStyle(const std::filesystem::path& app0, const GameBuild& build,
                                 std::span<const uint8_t> xbox_patch);

// Whether any of the build's archives carries a patch (an undo record), whichever it is.
bool AnyPromptPatch(const std::filesystem::path& app0, const GameBuild& build);

struct PromptsResult {
    bool ok = false;
    bool cancelled = false;
    std::string detail; // for the log
};

// Patches or reverts the build's archives so the game shows `style`. `xbox_patch` is the DCPX patch (the embedded
// xbox-prompts.dcpx); it is only needed for kXbox. An Xbox change that fails or is cancelled puts every archive back
// to the original. Progress and cancellation go through the hooks.
PromptsResult SetPromptStyle(const std::filesystem::path& app0, const GameBuild& build, PromptStyle style,
                             std::span<const uint8_t> xbox_patch, const PatchHooks& hooks = {});

} // namespace osl
