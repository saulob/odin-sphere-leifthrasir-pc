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
constexpr PackageLayer kEuBaseLayers[] = {{"01.00", "01.00", false}};
constexpr PackageLayer kEu101Layers[] = {{"01.00", "01.00", false}, {"01.01", "01.00", true}};

// The render size instructions of the USA 1.00 eboot: the size of the render buffer pool its offscreen targets come
// from, the three offscreen targets the graphics device creates, the two GPU buffer allocations sized from the same
// pair, the two viewports the frame setup sets, and the engine's packed float screen size: the one its 2D projection
// uses and the one its partial clears divide their render-target pixel positions by. All are sized for the base PS4
// 1920x1080 target; the game has no PS4 Pro path.
constexpr OslResolutionSite kUsaResolutionSites[] = {
    {0x6ab597, 7, {0xc7, 0x84, 0x24, 0xf8, 0x00, 0x00, 0x00}, 0x4000000, 0, 0, 0, 1},
    {0x6ac0b8, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ac0bd, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ac1e9, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ac1ee, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ac3a8, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ac3ad, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ac3c5, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ac3ca, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ac3de, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ac3e3, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6accd0, 1, {0xb9}, 1920, 0, 0, 0, 0}, {0x6accd5, 2, {0x41, 0xb8}, 1080, 1, 0, 0, 0},
    {0x6ae682, 1, {0xb9}, 1920, 0, 0, 0, 0}, {0x6ae687, 2, {0x41, 0xb8}, 1080, 1, 0, 0, 0},
    {0x6b3d72, 2, {0x48, 0xb8}, 0x44f00000, 0, 0x44870000, 1, 0},
    {0x6b994d, 2, {0x48, 0xb9}, 0x44f00000, 0, 0x44870000, 1, 0},
};

// The Japanese eboots have the same seventeen instructions in the same functions; only their offsets differ.
constexpr OslResolutionSite kJapanBaseResolutionSites[] = {
    {0x6a2157, 7, {0xc7, 0x84, 0x24, 0xf8, 0x00, 0x00, 0x00}, 0x4000000, 0, 0, 0, 1},
    {0x6a2c78, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6a2c7d, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6a2da9, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6a2dae, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6a2f68, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6a2f6d, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6a2f85, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6a2f8a, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6a2f9e, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6a2fa3, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6a3890, 1, {0xb9}, 1920, 0, 0, 0, 0}, {0x6a3895, 2, {0x41, 0xb8}, 1080, 1, 0, 0, 0},
    {0x6a5242, 1, {0xb9}, 1920, 0, 0, 0, 0}, {0x6a5247, 2, {0x41, 0xb8}, 1080, 1, 0, 0, 0},
    {0x6aa932, 2, {0x48, 0xb8}, 0x44f00000, 0, 0x44870000, 1, 0},
    {0x6b050d, 2, {0x48, 0xb9}, 0x44f00000, 0, 0x44870000, 1, 0},
};

constexpr OslResolutionSite kJapan102ResolutionSites[] = {
    {0x6a8c07, 7, {0xc7, 0x84, 0x24, 0xf8, 0x00, 0x00, 0x00}, 0x4000000, 0, 0, 0, 1},
    {0x6a9728, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6a972d, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6a9859, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6a985e, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6a9a18, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6a9a1d, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6a9a35, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6a9a3a, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6a9a4e, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6a9a53, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6aa340, 1, {0xb9}, 1920, 0, 0, 0, 0}, {0x6aa345, 2, {0x41, 0xb8}, 1080, 1, 0, 0, 0},
    {0x6abcf2, 1, {0xb9}, 1920, 0, 0, 0, 0}, {0x6abcf7, 2, {0x41, 0xb8}, 1080, 1, 0, 0, 0},
    {0x6b13e2, 2, {0x48, 0xb8}, 0x44f00000, 0, 0x44870000, 1, 0},
    {0x6b6fbd, 2, {0x48, 0xb9}, 0x44f00000, 0, 0x44870000, 1, 0},
};

constexpr OslResolutionSite kEuBaseResolutionSites[] = {
    {0x6ac9b7, 7, {0xc7, 0x84, 0x24, 0xf8, 0x00, 0x00, 0x00}, 0x4000000, 0, 0, 0, 1},
    {0x6ad4d8, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ad4dd, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ad609, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ad60e, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ad7c8, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ad7cd, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ad7e5, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ad7ea, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ad7fe, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ad803, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ae0f0, 1, {0xb9}, 1920, 0, 0, 0, 0}, {0x6ae0f5, 2, {0x41, 0xb8}, 1080, 1, 0, 0, 0},
    {0x6afac2, 1, {0xb9}, 1920, 0, 0, 0, 0}, {0x6afac7, 2, {0x41, 0xb8}, 1080, 1, 0, 0, 0},
    {0x6b51b2, 2, {0x48, 0xb8}, 0x44f00000, 0, 0x44870000, 1, 0},
    {0x6badfd, 2, {0x48, 0xb9}, 0x44f00000, 0, 0x44870000, 1, 0},
};

constexpr OslResolutionSite kEu101ResolutionSites[] = {
    {0x6acb67, 7, {0xc7, 0x84, 0x24, 0xf8, 0x00, 0x00, 0x00}, 0x4000000, 0, 0, 0, 1},
    {0x6ad688, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ad68d, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ad7b9, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ad7be, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ad978, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ad97d, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ad995, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ad99a, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ad9ae, 1, {0xbe}, 1920, 0, 0, 0, 0}, {0x6ad9b3, 1, {0xba}, 1080, 1, 0, 0, 0},
    {0x6ae2a0, 1, {0xb9}, 1920, 0, 0, 0, 0}, {0x6ae2a5, 2, {0x41, 0xb8}, 1080, 1, 0, 0, 0},
    {0x6afc72, 1, {0xb9}, 1920, 0, 0, 0, 0}, {0x6afc77, 2, {0x41, 0xb8}, 1080, 1, 0, 0, 0},
    {0x6b5362, 2, {0x48, 0xb8}, 0x44f00000, 0, 0x44870000, 1, 0},
    {0x6bafad, 2, {0x48, 0xb9}, 0x44f00000, 0, 0x44870000, 1, 0},
};

// Skip intro: the same code in every build, at these offsets.
constexpr OslSkipIntroSites kUsaSkipIntro{0x52d, 0x66c36b, 0x1794bd, 0x146bb0, 0x97e8d0};
constexpr OslSkipIntroSites kJapanBaseSkipIntro{0x51d, 0x6632fb, 0x1781ad, 0x145af0, 0xa6eea0};
constexpr OslSkipIntroSites kJapan102SkipIntro{0x51d, 0x669c6b, 0x1784bd, 0x145df0, 0xa1b5e0};
constexpr OslSkipIntroSites kEuBaseSkipIntro{0x52d, 0x66d5bb, 0x17993d, 0x146e80, 0x965780};
constexpr OslSkipIntroSites kEu101SkipIntro{0x52d, 0x66d770, 0x179a1d, 0x146f60, 0x965950};

// The European executable picks its menu and subtitle archives from the system language; these are the languages it
// maps (UK is its English). The USA and Japanese builds ship one text language each and list none.
constexpr TextLanguage kEuLanguages[] = {
    {"english", "English", 1}, {"french", "French", 2}, {"german", "German", 4},
    {"spanish", "Spanish", 3}, {"italian", "Italian", 5},
};

constexpr const char* kUsaDataFiles[] = {"Odin2_BT_US.CPK", "Odin2_OR_US.CPK", "Odin2_RE_US.CPK"};
constexpr const char* kJapanBaseDataFiles[] = {"Odin2_BOOT.CPK", "Odin2_OR.CPK", "Odin2_ORHD.CPK", "Odin2_RE.CPK"};
constexpr const char* kJapan102DataFiles[] = {"Odin2_BOOT.CPK", "Odin2_OR.CPK", "Odin2_ORHD.CPK", "Odin2_RE.CPK",
                                              "Odin2_PATCH1_OR.CPK", "Odin2_PATCH1_RE.CPK"};
constexpr const char* kJapan102ExtraFiles[] = {"sce_discmap_patch.plt"};

constexpr const char* kEuBaseDataFiles[] = {
    "Odin2_BT_DE.CPK", "Odin2_BT_ES.CPK", "Odin2_BT_FR.CPK", "Odin2_BT_IT.CPK", "Odin2_BT_UK.CPK",
    "Odin2_OR_DE.CPK", "Odin2_OR_ES.CPK", "Odin2_OR_FR.CPK", "Odin2_OR_IT.CPK", "Odin2_OR_UK.CPK",
    "Odin2_RE_DE.CPK", "Odin2_RE_ES.CPK", "Odin2_RE_FR.CPK", "Odin2_RE_IT.CPK", "Odin2_RE_UK.CPK",
};
constexpr const char* kEu101DataFiles[] = {
    "Odin2_BT_DE.CPK", "Odin2_BT_ES.CPK", "Odin2_BT_FR.CPK", "Odin2_BT_IT.CPK", "Odin2_BT_UK.CPK",
    "Odin2_OR_DE.CPK", "Odin2_OR_ES.CPK", "Odin2_OR_FR.CPK", "Odin2_OR_IT.CPK", "Odin2_OR_UK.CPK",
    "Odin2_RE_DE.CPK", "Odin2_RE_ES.CPK", "Odin2_RE_FR.CPK", "Odin2_RE_IT.CPK", "Odin2_RE_UK.CPK",
    "Odin2_EU_PATCH1_OR_DE.CPK", "Odin2_EU_PATCH1_OR_ES.CPK", "Odin2_EU_PATCH1_OR_FR.CPK", "Odin2_EU_PATCH1_OR_IT.CPK",
    "Odin2_EU_PATCH1_OR_UK.CPK", "Odin2_EU_PATCH1_RE_DE.CPK", "Odin2_EU_PATCH1_RE_ES.CPK", "Odin2_EU_PATCH1_RE_FR.CPK",
    "Odin2_EU_PATCH1_RE_IT.CPK", "Odin2_EU_PATCH1_RE_UK.CPK",
};
constexpr const char* kEu101ExtraFiles[] = {"sce_discmap_patch.plt"};

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
        &kUsaSkipIntro,
        {},
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
        &kJapanBaseSkipIntro,
        {},
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
        &kJapan102SkipIntro,
        {},
    },
    {
        "CUSA05262",
        "EP1063-CUSA05262_00-ODINSPHERELE0000",
        "1.00",
        "CUSA05262-1.00",
        "3e6b1741e8a404d0f8fbe10bce93c6887974566f5be09ef1bddc22a00ba29feb",
        {},
        kEuBaseDataFiles,
        "CUSA05262 1.00 Europe",
        "Europe",
        kEuBaseLayers,
        kEuBaseResolutionSites,
        &kEuBaseSkipIntro,
        kEuLanguages,
    },
    {
        "CUSA05262",
        "EP1063-CUSA05262_00-ODINSPHERELE0000",
        "1.01",
        "CUSA05262-1.01",
        "572bf21e57febee415d739cded5516def9ca81e867ad3184c989367248edcd7e",
        kEu101ExtraFiles,
        kEu101DataFiles,
        "CUSA05262 1.01 Europe",
        "Europe",
        kEu101Layers,
        kEu101ResolutionSites,
        &kEu101SkipIntro,
        kEuLanguages,
    },
};

bool SameLayer(const PackageLayer& a, const PackageLayer& b) {
    return std::string_view(a.app_ver) == b.app_ver && std::string_view(a.sfo_version) == b.sfo_version &&
           a.update == b.update;
}

constexpr TextLanguage kDefaultLanguage{"english", "English", 1};

} // namespace

std::span<const GameBuild> SupportedBuilds() {
    return kGameBuilds;
}

std::span<const TextLanguage> TextLanguagesOf(const GameBuild* build) {
    return build ? build->text_languages : std::span<const TextLanguage>{};
}

const TextLanguage& ResolveTextLanguage(const GameBuild* build, std::string_view key) {
    const std::span<const TextLanguage> languages = TextLanguagesOf(build);
    for (const TextLanguage& language : languages) {
        if (language.key == key) {
            return language;
        }
    }
    return languages.empty() ? kDefaultLanguage : languages.front();
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
