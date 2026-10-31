// Read-only diagnostics for a game folder or package that looks like Odin Sphere Leifthrasir but matches no supported
// build, or a supported build with unmapped features: the text for the log that lets the build be mapped. Nothing here
// writes to the game files or applies a patch.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "boot_image.h"
#include "game_profile.h"
#include "pkg_extract.h"

namespace osl {

// The sites of a scanned executable; a feature that was not resolved and accepted by its patch stays empty.
struct ScannedMapping {
    std::vector<uint32_t> resolution_offsets; // in the order of the template build's resolution sites
    uint32_t snapshot_scale_site = 0;
    std::optional<OslSkipIntroSites> skip_intro;
    std::optional<OslBookshelfSites> bookshelf;
    std::vector<uint32_t> lzcnt_offsets; // LZCNT_RANGE_COUNT entries
};

struct ScanResult {
    std::string text;                    // one line per finding
    std::vector<std::string> unresolved; // features whose sites could not be mapped uniquely
    bool anything_found = false;         // at least one Odin Sphere Leifthrasir pattern matched
    ScannedMapping mapping;
    bool complete() const { return unresolved.empty(); }
};

// Finds the candidate sites of every executable-dependent feature with the production patterns and the sites of
// `templates` as the shape to expect. `image` is the eboot's mapping with the bundled modules linked after it;
// `eboot_size` is where the eboot's part ends. A pattern that matches other than as often as expected is unresolved.
ScanResult ScanExecutable(const MappedExecutable& image, uint64_t eboot_size,
                          std::span<const GameBuild> templates = SupportedBuilds());

// Maps the folder's eboot.bin with its bundled modules linked after it and scans it; `unresolved` holds "executable"
// when it cannot be read.
ScanResult ScanGameFolder(const std::filesystem::path& app0);

// The resolution sites at `offsets`: the instructions of the first template build that has resolution sites, moved.
// Empty when the offsets do not match that template's site count.
std::vector<OslResolutionSite> ResolutionSitesAt(std::span<const GameBuild> templates,
                                                 const std::vector<uint32_t>& offsets);

struct UnsupportedBuildReport {
    bool odin = false; // the folder is Odin Sphere Leifthrasir; when false `text` is empty
    std::string text;  // the log section, lines separated by '\n'
};

// `detail` is why the supported-build check failed. `prompt_patch` (the Xbox prompts patch) may be empty.
UnsupportedBuildReport DiagnoseUnsupportedBuild(const std::filesystem::path& app0, const std::string& detail,
                                                std::span<const uint8_t> prompt_patch = {});

// A package that matches no supported build is diagnosed when its CONTENT_ID names the game or it carries the game's
// Odin2_*.CPK archives; any other title is refused without being unpacked.
bool IsOdinPackage(const std::string& sfo, std::span<const PackageFile> files);

// The package files the diagnostics unpack: eboot.bin and the bundled modules.
bool IsDiagnosedPackageFile(const std::string& path);

// `executables` holds the package's unpacked IsDiagnosedPackageFile files, or is empty when they could not be unpacked
// (`note` says why); `files` is the package's file list. Empty text for any other title.
UnsupportedBuildReport DiagnoseUnsupportedPackage(const std::string& sfo, const std::string& detail,
                                                  const std::filesystem::path& executables,
                                                  std::span<const PackageFile> files, const std::string& note = {});

// The features the scan did not resolve, in the launcher's names; empty for a fully mapped executable.
std::vector<std::string> UnresolvedFeatures(const ScannedMapping& mapping);

// For an installed build whose executable is scanned, the same scan as for an unsupported build, so the log holds
// what was resolved; empty for a build with tables.
std::string DiagnoseBuildMapping(const std::filesystem::path& app0, const GameBuild& build,
                                 std::span<const uint8_t> prompt_patch = {});

} // namespace osl
