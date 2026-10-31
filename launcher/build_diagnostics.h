// Read-only diagnostics for a game folder that looks like Odin Sphere Leifthrasir but matches no supported build: the
// text for the log that lets a new build be mapped. Nothing here writes to the game files or applies a patch.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "boot_image.h"
#include "game_profile.h"

namespace osl {

struct ScanResult {
    std::string text;                    // one line per finding
    std::vector<std::string> unresolved; // features whose sites could not be mapped uniquely
    bool anything_found = false;         // at least one Odin Sphere Leifthrasir pattern matched
    bool complete() const { return unresolved.empty(); }
};

// Finds the candidate sites of every executable-dependent feature with the production patterns and the sites of
// `templates` as the shape to expect. `image` is the eboot's mapping with the bundled modules linked after it;
// `eboot_size` is where the eboot's part ends. A pattern that matches other than as often as expected is unresolved.
ScanResult ScanExecutable(const MappedExecutable& image, uint64_t eboot_size,
                          std::span<const GameBuild> templates = SupportedBuilds());

struct UnsupportedBuildReport {
    bool odin = false; // the folder is Odin Sphere Leifthrasir; when false `text` is empty
    std::string text;  // the log section, lines separated by '\n'
};

// `detail` is why the supported-build check failed. `prompt_patch` (the Xbox prompts patch) may be empty.
UnsupportedBuildReport DiagnoseUnsupportedBuild(const std::filesystem::path& app0, const std::string& detail,
                                                std::span<const uint8_t> prompt_patch = {});

// The identity of a package of Odin Sphere Leifthrasir that is no supported build's; empty text for any other title.
UnsupportedBuildReport DiagnoseUnsupportedPackage(const std::string& sfo);

} // namespace osl
