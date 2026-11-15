// Read-only diagnostics for a selection that is Odin Sphere Leifthrasir but matches no supported build, or a supported
// build with unmapped features: everything needed to map the build, written to the log. One engine serves packages and
// folders through GameSource. Nothing here writes to the game files or applies a patch, and a result never makes a build
// supported.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "build_scan.h"
#include "game_profile.h"
#include "game_source.h"

namespace osl {

inline constexpr char kDiagnosticsBegin[] = "Unsupported build diagnostics begin\n";
inline constexpr char kDiagnosticsEnd[] = "Unsupported build diagnostics end\n";

// The DCPX patches whose assets the report checks; either may be empty.
struct DiagnosticPatches {
    std::span<const uint8_t> prompts; // the Xbox prompts patch
    std::span<const uint8_t> title;   // the title menu Exit Game patch
};

struct UnsupportedBuildReport {
    bool odin = false;                 // the selection was identified as Odin Sphere Leifthrasir; `text` is empty otherwise
    std::vector<std::string> evidence; // why it was or was not identified
    std::string text;                  // the log section, lines separated by '\n'
};

// `sfo` is the selection's param.sfo (empty when it has none); `detail` is why the supported-build check failed.
UnsupportedBuildReport DiagnoseUnsupportedBuild(const GameSource& source, const std::string& sfo, const std::string& detail,
                                                const DiagnosticPatches& patches = {});

// True when the source carries an eboot.bin that is not `build`'s executable; false when it carries none.
bool ExecutableDiffers(const GameSource& source, const GameBuild& build);

// For an installed build whose executable is scanned, the same scan as for an unsupported build, so the log holds
// what was resolved, feature by feature; empty for a build with tables. `unmapped` receives the incomplete features.
std::string DiagnoseBuildMapping(const std::filesystem::path& app0, const GameBuild& build,
                                 std::span<const uint8_t> prompt_patch = {},
                                 std::vector<std::string>* unmapped = nullptr);

} // namespace osl
