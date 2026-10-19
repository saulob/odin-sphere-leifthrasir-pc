#include "game_profile.h"

#include <algorithm>
#include <string_view>

namespace osl {
namespace {

// PS4 metadata is "NN.NN"; the launcher shows "N.NN".
std::string DisplayVersion(std::string_view version) {
    size_t first = 0;
    while (first + 1 < version.size() && version[first] == '0' && version[first + 1] != '.') {
        ++first;
    }
    return std::string(version.substr(first));
}

constexpr PackageLayer kUsaLayers[] = {{"01.00", "01.01", false}};
constexpr PackageLayer kJapanBaseLayers[] = {{"01.00", "01.00", false}};
constexpr PackageLayer kJapan102Layers[] = {{"01.00", "01.00", false}, {"01.02", "01.00", true}};

// The render size instructions of the USA 1.00 eboot: the three render targets the graphics device creates, the two
// GPU buffer allocations sized from the same pair, the two viewports the frame setup sets, and the engine's packed
// float screen size that its 2D projection uses. All load the base PS4 1920x1080 pair; the game has no PS4 Pro path.
constexpr OslResolutionSite kUsaResolutionSites[] = {
    {0x6ac0b8, 1, {0xbe}, 1920, 0, 0, 0}, {0x6ac0bd, 1, {0xba}, 1080, 1, 0, 0},
    {0x6ac1e9, 1, {0xbe}, 1920, 0, 0, 0}, {0x6ac1ee, 1, {0xba}, 1080, 1, 0, 0},
    {0x6ac3a8, 1, {0xbe}, 1920, 0, 0, 0}, {0x6ac3ad, 1, {0xba}, 1080, 1, 0, 0},
    {0x6ac3c5, 1, {0xbe}, 1920, 0, 0, 0}, {0x6ac3ca, 1, {0xba}, 1080, 1, 0, 0},
    {0x6ac3de, 1, {0xbe}, 1920, 0, 0, 0}, {0x6ac3e3, 1, {0xba}, 1080, 1, 0, 0},
    {0x6accd0, 1, {0xb9}, 1920, 0, 0, 0}, {0x6accd5, 2, {0x41, 0xb8}, 1080, 1, 0, 0},
    {0x6ae682, 1, {0xb9}, 1920, 0, 0, 0}, {0x6ae687, 2, {0x41, 0xb8}, 1080, 1, 0, 0},
    {0x6b3d72, 2, {0x48, 0xb8}, 0x44f00000, 0, 0x44870000, 1},
};

// The Japanese eboots have the same fifteen instructions in the same functions; only their offsets differ.
constexpr OslResolutionSite kJapanBaseResolutionSites[] = {
    {0x6a2c78, 1, {0xbe}, 1920, 0, 0, 0}, {0x6a2c7d, 1, {0xba}, 1080, 1, 0, 0},
    {0x6a2da9, 1, {0xbe}, 1920, 0, 0, 0}, {0x6a2dae, 1, {0xba}, 1080, 1, 0, 0},
    {0x6a2f68, 1, {0xbe}, 1920, 0, 0, 0}, {0x6a2f6d, 1, {0xba}, 1080, 1, 0, 0},
    {0x6a2f85, 1, {0xbe}, 1920, 0, 0, 0}, {0x6a2f8a, 1, {0xba}, 1080, 1, 0, 0},
    {0x6a2f9e, 1, {0xbe}, 1920, 0, 0, 0}, {0x6a2fa3, 1, {0xba}, 1080, 1, 0, 0},
    {0x6a3890, 1, {0xb9}, 1920, 0, 0, 0}, {0x6a3895, 2, {0x41, 0xb8}, 1080, 1, 0, 0},
    {0x6a5242, 1, {0xb9}, 1920, 0, 0, 0}, {0x6a5247, 2, {0x41, 0xb8}, 1080, 1, 0, 0},
    {0x6aa932, 2, {0x48, 0xb8}, 0x44f00000, 0, 0x44870000, 1},
};

constexpr OslResolutionSite kJapan102ResolutionSites[] = {
    {0x6a9728, 1, {0xbe}, 1920, 0, 0, 0}, {0x6a972d, 1, {0xba}, 1080, 1, 0, 0},
    {0x6a9859, 1, {0xbe}, 1920, 0, 0, 0}, {0x6a985e, 1, {0xba}, 1080, 1, 0, 0},
    {0x6a9a18, 1, {0xbe}, 1920, 0, 0, 0}, {0x6a9a1d, 1, {0xba}, 1080, 1, 0, 0},
    {0x6a9a35, 1, {0xbe}, 1920, 0, 0, 0}, {0x6a9a3a, 1, {0xba}, 1080, 1, 0, 0},
    {0x6a9a4e, 1, {0xbe}, 1920, 0, 0, 0}, {0x6a9a53, 1, {0xba}, 1080, 1, 0, 0},
    {0x6aa340, 1, {0xb9}, 1920, 0, 0, 0}, {0x6aa345, 2, {0x41, 0xb8}, 1080, 1, 0, 0},
    {0x6abcf2, 1, {0xb9}, 1920, 0, 0, 0}, {0x6abcf7, 2, {0x41, 0xb8}, 1080, 1, 0, 0},
    {0x6b13e2, 2, {0x48, 0xb8}, 0x44f00000, 0, 0x44870000, 1},
};

constexpr const char* kUsaDataFiles[] = {"Odin2_BT_US.CPK", "Odin2_OR_US.CPK", "Odin2_RE_US.CPK"};
constexpr const char* kJapanBaseDataFiles[] = {"Odin2_BOOT.CPK", "Odin2_OR.CPK", "Odin2_ORHD.CPK", "Odin2_RE.CPK"};
constexpr const char* kJapan102DataFiles[] = {"Odin2_BOOT.CPK", "Odin2_OR.CPK", "Odin2_ORHD.CPK", "Odin2_RE.CPK",
                                              "Odin2_PATCH1_OR.CPK", "Odin2_PATCH1_RE.CPK"};
constexpr const char* kJapan102ExtraFiles[] = {"sce_discmap_patch.plt"};

constexpr GameBuild kGameBuilds[] = {
    {
        "CUSA05083",
        "UP0177-CUSA05083_00-ODINSPHERELE0000",
        "1.00",
        "CUSA05083-1.00",
        "7c517194aba4e5eea55d9197efae47d1567bd785b8c0dced7d1b0f4028d3eed6",
        {},
        kUsaDataFiles,
        "CUSA05083 1.00 USA",
        "USA",
        kUsaLayers,
        kUsaResolutionSites,
    },
    {
        "CUSA01290",
        "JP0005-CUSA01290_00-ODINSPHERELE0000",
        "1.00",
        "CUSA01290-1.00",
        "d0add7e35bc5f58d5c9017fdce0878006c32f944fa8eb3d7ce62671257f5bcc7",
        {},
        kJapanBaseDataFiles,
        "CUSA01290 1.00 Japan",
        "Japan",
        kJapanBaseLayers,
        kJapanBaseResolutionSites,
    },
    {
        "CUSA01290",
        "JP0005-CUSA01290_00-ODINSPHERELE0000",
        "1.02",
        "CUSA01290-1.02",
        "db96b839fe31adba4b3eb3547202f761d34881325c2fea650c4c020eded5d071",
        kJapan102ExtraFiles,
        kJapan102DataFiles,
        "CUSA01290 1.02 Japan",
        "Japan",
        kJapan102Layers,
        kJapan102ResolutionSites,
    },
};

bool SameLayer(const PackageLayer& a, const PackageLayer& b) {
    return std::string_view(a.app_ver) == b.app_ver && std::string_view(a.sfo_version) == b.sfo_version &&
           a.update == b.update;
}

} // namespace

std::span<const GameBuild> SupportedBuilds() {
    return kGameBuilds;
}

const PackageLayer& FinishedLayer(const GameBuild& build) {
    return build.layers.back();
}

std::string BuildMetadataText(const GameBuild& build) {
    const PackageLayer& finished = FinishedLayer(build);
    return "APP_VER " + DisplayVersion(finished.app_ver) + " \xC2\xB7 VERSION " + DisplayVersion(finished.sfo_version);
}

const GameBuild* BaseBuildOf(const GameBuild& build, std::span<const GameBuild> builds) {
    if (build.layers.size() < 2) {
        return nullptr;
    }
    for (const GameBuild& candidate : builds) {
        if (std::string_view(candidate.title_id) == build.title_id && candidate.layers.size() == 1 &&
            SameLayer(candidate.layers.front(), build.layers.front())) {
            return &candidate;
        }
    }
    return nullptr;
}

const GameBuild* UpdateBuildOf(const GameBuild& build, std::span<const GameBuild> builds) {
    const GameBuild* found = nullptr;
    for (const GameBuild& candidate : builds) {
        if (std::string_view(candidate.title_id) != build.title_id || candidate.layers.size() <= build.layers.size()) {
            continue;
        }
        if (!std::equal(build.layers.begin(), build.layers.end(), candidate.layers.begin(), SameLayer)) {
            continue;
        }
        if (!found || candidate.layers.size() > found->layers.size()) {
            found = &candidate;
        }
    }
    return found;
}

bool RestrictedToNativeResolution(const GameBuild* build) {
    return build && build->resolution_sites.empty();
}

std::string SupportedBuildsText() {
    const std::span<const GameBuild> builds = SupportedBuilds();
    std::string text;
    for (size_t i = 0; i < builds.size(); ++i) {
        if (i > 0) {
            text += i + 1 == builds.size() ? " and " : ", ";
        }
        text += builds[i].label;
    }
    return text;
}

} // namespace osl
