// The title menu's EXIT GAME label: the game's unused INSTALL label art is repainted by a block patch (title/title-exit.dcpx,
// scripts/title_exit) applied to the installed archives as a layer above the controller prompts patch (cpk_patch.h).
// The game files are always patched, so the title menu shows the same label whichever prompts are chosen.
//
// The layer is all or nothing for a build: the patch reorders the menu rows (the .mbs files) and repaints the label (the
// atlases), and the loader turns the Exit Game row on only when every title file of the build carries it. A build with
// a title file the patch was not made for keeps its original files and the original menu.
#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "cpk_patch.h"
#include "game_profile.h"

namespace osl {

struct TitleExitResult {
    bool ok = false;
    bool cancelled = false;
    bool unavailable = false; // a title file is not a copy the patch was made for; nothing was changed
    std::string detail;       // for the log
    // Per title file the patch was not made for: build, archive, entry, size, CRC32 and why no target took it.
    std::vector<std::string> unsupported;
};

// Whether the layer still has to be put on: an archive that holds a title file of the patch carries neither the
// complete layer of this patch nor the record that this patch cannot be applied to it.
bool TitleExitPending(const std::filesystem::path& app0, const GameBuild& build, std::span<const uint8_t> patch);

// Whether every archive of the build that holds a title file carries the complete layer of this patch; otherwise false
// and the reason in `why`.
bool TitleExitReady(const std::filesystem::path& app0, const GameBuild& build, std::span<const uint8_t> patch,
                    std::string* why = nullptr);

// The loader's gate: TitleExitReady for the build the boot image folder was built from; false for any other folder.
bool TitleExitAssetsReady(const char* app0, const char* image_dir, std::span<const uint8_t> patch, std::string* why = nullptr);

// Puts the patch on every archive that holds a title file it names. A failure, a cancellation or a title file that is
// not a copy the patch was made for takes the layer off every archive again; the last is recorded so it is not retried
// until the patch or the archive changes.
TitleExitResult ApplyTitleExit(const std::filesystem::path& app0, const GameBuild& build, std::span<const uint8_t> patch,
                               const PatchHooks& hooks = {});

// Takes the layer off every archive of the build that carries it.
TitleExitResult RevertTitleExit(const std::filesystem::path& app0, const GameBuild& build);

} // namespace osl
