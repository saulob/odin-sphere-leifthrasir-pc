#include "game_profile.h"

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

// The render size instructions of the USA 1.00 eboot: the three render targets the graphics device creates, the two
// GPU buffer allocations sized from the same pair, and the two viewports the frame setup sets. All seven load the
// base PS4 1920x1080 pair; the game has no PS4 Pro path.
constexpr OslResolutionSite kUsaResolutionSites[] = {
    {0x6ac0b8, 1, {0xbe}, 1920, 0}, {0x6ac0bd, 1, {0xba}, 1080, 1},
    {0x6ac1e9, 1, {0xbe}, 1920, 0}, {0x6ac1ee, 1, {0xba}, 1080, 1},
    {0x6ac3a8, 1, {0xbe}, 1920, 0}, {0x6ac3ad, 1, {0xba}, 1080, 1},
    {0x6ac3c5, 1, {0xbe}, 1920, 0}, {0x6ac3ca, 1, {0xba}, 1080, 1},
    {0x6ac3de, 1, {0xbe}, 1920, 0}, {0x6ac3e3, 1, {0xba}, 1080, 1},
    {0x6accd0, 1, {0xb9}, 1920, 0}, {0x6accd5, 2, {0x41, 0xb8}, 1080, 1},
    {0x6ae682, 1, {0xb9}, 1920, 0}, {0x6ae687, 2, {0x41, 0xb8}, 1080, 1},
};

constexpr GameBuild kGameBuilds[] = {
    {
        "CUSA05083",
        "UP0177-CUSA05083_00-ODINSPHERELE0000",
        "1.00",
        "CUSA05083-1.00",
        "7c517194aba4e5eea55d9197efae47d1567bd785b8c0dced7d1b0f4028d3eed6",
        nullptr,
        {},
        "CUSA05083 1.00 USA",
        "USA",
        kUsaLayers,
        kUsaResolutionSites,
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
