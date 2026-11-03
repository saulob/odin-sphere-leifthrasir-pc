// The title menu's EXIT GAME label: the game's unused INSTALL label art is repainted by a block patch (title/title-exit.dcpx,
// scripts/title_exit) applied to the installed archives as a layer above the controller prompts patch (cpk_patch.h).
// The game files are always patched, so the title menu shows the same label whichever prompts are chosen.
#pragma once

#include <filesystem>
#include <span>
#include <string>

#include "cpk_patch.h"
#include "game_profile.h"

namespace osl {

struct TitleExitResult {
    bool ok = false;
    bool cancelled = false;
    std::string detail; // for the log
};

// Whether an archive that holds a title file of the patch has not been through the patch yet.
bool TitleExitPending(const std::filesystem::path& app0, const GameBuild& build, std::span<const uint8_t> patch);

// Puts the patch on every archive that holds a title file it names; a file that is not a copy the patch was made for
// stays as it is. A failure or cancellation takes the layer off every archive again.
TitleExitResult ApplyTitleExit(const std::filesystem::path& app0, const GameBuild& build, std::span<const uint8_t> patch,
                               const PatchHooks& hooks = {});

// Takes the layer off every archive of the build that carries it.
TitleExitResult RevertTitleExit(const std::filesystem::path& app0, const GameBuild& build);

} // namespace osl
