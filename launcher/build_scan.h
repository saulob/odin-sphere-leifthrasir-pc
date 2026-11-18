// The signature scan of an executable without profile tables: the one source of the sites of a scanned build (the boot
// image records its result) and of the unsupported-build diagnostics. Read-only; patches run on scratch copies only.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "boot_image.h"
#include "game_profile.h"

namespace osl {

// The sites of a scanned executable; a feature that was not resolved and accepted by its patch stays empty.
struct ScannedMapping {
    std::vector<uint32_t> resolution_offsets; // in the order of the template build's resolution sites
    uint32_t snapshot_scale_site = 0;
    std::optional<OslSkipIntroSites> skip_intro;
    std::optional<OslBookshelfSites> bookshelf;
    std::vector<uint32_t> lzcnt_offsets; // LZCNT_RANGE_COUNT entries
};

enum class FeatureState { kComplete, kPartial, kMissing, kAmbiguous, kInvalid, kInherent };
const char* FeatureStateName(FeatureState state);

// One part of a feature: `expected` sites, of which `unique` were placed and `ambiguous` matched too often.
struct ScanComponent {
    std::string name;
    size_t expected = 1, unique = 0, ambiguous = 0;
    size_t missing() const { return expected > unique + ambiguous ? expected - unique - ambiguous : 0; }
};

struct FeatureScan {
    std::string name;
    std::vector<ScanComponent> components;
    std::string invariant; // the scan's own consistency check that failed; empty when every one held
    std::string validator; // "accepted", or why the runtime patch refused the sites; empty when it did not run
    FeatureState state = FeatureState::kMissing;
};

struct ScanResult {
    std::string text;                    // one line per finding
    std::vector<std::string> unresolved; // features whose sites could not be mapped uniquely
    bool anything_found = false;         // at least one Odin Sphere Leifthrasir pattern matched
    ScannedMapping mapping;
    std::vector<FeatureScan> features;   // Skip intro, Unlock Bookshelf, Snapshot scale, Resolution, Legacy CPU LZCNT
    std::vector<FeatureScan> cheats;     // osl_cheats order, the Refined engine's; optional, so never part of `unresolved`
    std::optional<FeatureScan> experience; // the Experience Multiplier, reported apart from the cheats
    std::vector<FeatureScan> classic_cheats; // the same cheats in the Classic engine
    std::optional<FeatureScan> classic_experience;
    bool complete() const { return unresolved.empty(); }
};

// "Cheats: 7/7 COMPLETE" or "Cheats: 5/7 COMPLETE, Infinite PP PARTIAL, Infinite POW MISSING".
std::string CheatSummary(const ScanResult& scan);

// "Experience Multiplier: COMPLETE", or MISSING, AMBIGUOUS, PARTIAL or INVALID.
std::string ExperienceSummary(const ScanResult& scan);

// The same two summaries for the Classic engine, with "INHERENTLY SATISFIED" for what the Classic engine already does.
std::string ClassicCheatSummary(const ScanResult& scan);
std::string ClassicExperienceSummary(const ScanResult& scan);

// Finds the candidate sites of every executable-dependent feature with the production patterns and the sites of
// `templates` as the shape to expect. `image` is the eboot's mapping with the bundled modules linked after it;
// `eboot_size` is where the eboot's part ends. A pattern that matches other than as often as expected is unresolved.
ScanResult ScanExecutable(const MappedExecutable& image, uint64_t eboot_size,
                          std::span<const GameBuild> templates = SupportedBuilds());

struct LinkedModule {
    std::string name;
    uint64_t base = 0, size = 0;
    std::string error; // empty when linked
};

// The eboot mapped with the bundled modules linked after it at kModuleAlignment, the layout the scan's offsets are in.
struct LinkedImage {
    MappedExecutable image;
    uint64_t eboot_size = 0; // where the eboot's part of the image ends
    std::string eboot_error; // empty when the eboot mapped
    std::vector<LinkedModule> modules; // in kBundledModules order
};

// `modules` holds the bytes of each kBundledModules file, empty when it is missing.
LinkedImage LinkExecutable(std::span<const uint8_t> eboot, std::span<const std::vector<uint8_t>> modules);

// Maps the folder's eboot.bin with its bundled modules linked after it and scans it; `unresolved` holds "executable"
// when it cannot be read.
ScanResult ScanGameFolder(const std::filesystem::path& app0);

// The resolution sites at `offsets`: the instructions of the first template build that has resolution sites, moved.
// Empty when the offsets do not match that template's site count.
std::vector<OslResolutionSite> ResolutionSitesAt(std::span<const GameBuild> templates,
                                                 const std::vector<uint32_t>& offsets);

// The features the scan did not resolve, in the launcher's names; empty for a fully mapped executable.
std::vector<std::string> UnresolvedFeatures(const ScannedMapping& mapping);

} // namespace osl
