// Focused tests of the Odin Sphere profile, the param.sfo identity, the settings file and the keyboard bindings.
// Synthetic files in a temporary folder: no window, no Vulkan, no game files.
#include <algorithm>
#include <array>
#include <cctype>
#include <cpuid.h>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include "build_diagnostics.h"
#include "controller_prompts.h"
#include "cpk_patch.h"
#include "crash_report.h"
#include "game_files.h"
#include "game_install.h"
#include "game_profile.h"
#include "hang_report.h"
#include "launcher_quit.h"
#include "lzcnt_compat.h"
#include "options.h"
#include "pad_capture.h"
#include "osl_accent.h"
#include "osl_frame_pacing.h"
#include "osl_graphics.h"
#include "osl_renderer.h"
#include "osl_settings.h"
#include "osl_launcher.h"
#include "osl_title.h"
#include "placement.h"
#include "runtime_log.h"
#include "settings.h"
#include "test_support.h"
#include "title_exit_assets.h"
#include "utf8.h"

using namespace osl;
using namespace osl_test;

#ifndef OSL_SOURCE_DIR
#define OSL_SOURCE_DIR "."
#endif

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition)                                                                                       \
    do {                                                                                                       \
        ++g_checks;                                                                                            \
        if (!(condition)) {                                                                                    \
            ++g_failures;                                                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
        }                                                                                                      \
    } while (0)

struct ParityRow {
    const GameBuild* build;
    std::map<std::string, bool> ok;
};
std::vector<ParityRow> g_parity;

ParityRow& ParityOf(const GameBuild& build) {
    for (ParityRow& row : g_parity) {
        if (row.build == &build) {
            return row;
        }
    }
    g_parity.push_back({&build, {}});
    return g_parity.back();
}

void Parity(const GameBuild& build, const char* category, const char* expression, bool condition) {
    ++g_checks;
    const auto entry = ParityOf(build).ok.emplace(category, true).first;
    if (!condition) {
        ++g_failures;
        entry->second = false;
        std::printf("FAIL %s [%s]: %s\n", build.label, category, expression);
    }
}

#define PARITY(category, condition) Parity(build, category, #condition, (condition))

bool TableComplete(const GameBuild& build) {
    return !build.resolution_sites.empty() && build.snapshot_scale_site != 0 && build.skip_intro && build.bookshelf;
}

// The builds whose executable sites are in the profile's own tables; TestBuildContract fails one with a gap.
std::vector<GameBuild> MappedBuilds() {
    std::vector<GameBuild> builds;
    for (const GameBuild& build : SupportedBuilds()) {
        if (build.mapping == ExecutableMapping::kTable && TableComplete(build)) {
            builds.push_back(build);
        }
    }
    return builds;
}

uint32_t Little32(const std::string& data, size_t at) {
    return static_cast<uint32_t>(static_cast<unsigned char>(data[at])) |
           static_cast<uint32_t>(static_cast<unsigned char>(data[at + 1])) << 8 |
           static_cast<uint32_t>(static_cast<unsigned char>(data[at + 2])) << 16 |
           static_cast<uint32_t>(static_cast<unsigned char>(data[at + 3])) << 24;
}

uint32_t Big32(const std::string& data, size_t at) {
    return static_cast<uint32_t>(static_cast<unsigned char>(data[at])) << 24 |
           static_cast<uint32_t>(static_cast<unsigned char>(data[at + 1])) << 16 |
           static_cast<uint32_t>(static_cast<unsigned char>(data[at + 2])) << 8 |
           static_cast<uint32_t>(static_cast<unsigned char>(data[at + 3]));
}

void TestBranding() {
    CHECK(kAccentRed == 209 && kAccentGreen == 42 && kAccentBlue == 120);
    CHECK(kAccentHotRed == 231 && kAccentHotGreen == 71 && kAccentHotBlue == 145);
    CHECK(kAccentPressedRed == 168 && kAccentPressedGreen == 31 && kAccentPressedBlue == 93);
    CHECK(kAccentSubtleRed == 50 && kAccentSubtleGreen == 21 && kAccentSubtleBlue == 37);

    const std::filesystem::path root(OSL_SOURCE_DIR);
    const std::string accent = ReadFile(root / "launcher" / "launcher.cpp");
    CHECK(accent.find("WithAccent(kAccent)") != std::string::npos);
    CHECK(accent.find("theme.accent_hot = kAccentHot") != std::string::npos);
    CHECK(accent.find("kAccentRed = 224") == std::string::npos);
    CHECK(accent.find("52, 40, 10") == std::string::npos);

    const std::string overlay = ReadFile(root / "gpu" / "shim" / "osl_overlay.cpp");
    CHECK(overlay.find("AccentHotColor()") != std::string::npos);
    CHECK(overlay.find("0.85f, 0.72f, 0.45f") == std::string::npos);
    CHECK(overlay.find("255, 230, 140") == std::string::npos);
    CHECK(ReadFile(root / "launcher" / "trophy_page.cpp").find("Dragon's Crown") == std::string::npos);

    const std::string svg = ReadFile(root / "game-console-ecommerce-svgrepo-com.svg");
    CHECK(svg.find("#D12A78") != std::string::npos);
    CHECK(svg.find("#F4EAF0") != std::string::npos);
    const std::string generator = ReadFile(root / "tools" / "make-app-icon.py");
    CHECK(generator.find("osl_accent.h") != std::string::npos);
    CHECK(generator.find("game-console-ecommerce-svgrepo-com.svg") != std::string::npos);

    const std::string ico = ReadFile(root / "packaging" / "icons" / "app-icon.ico");
    CHECK(ico.size() > 6);
    CHECK(static_cast<unsigned char>(ico[0]) == 0 && static_cast<unsigned char>(ico[1]) == 0);
    CHECK(static_cast<unsigned char>(ico[2]) == 1 && static_cast<unsigned char>(ico[3]) == 0);
    const int count = static_cast<unsigned char>(ico[4]) | static_cast<unsigned char>(ico[5]) << 8;
    CHECK(count == 7);
    const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
    for (int i = 0; i < count && i < 7; ++i) {
        const size_t entry = 6 + i * 16;
        CHECK(static_cast<unsigned char>(ico[entry]) == (sizes[i] < 256 ? sizes[i] : 0));
        CHECK(static_cast<unsigned char>(ico[entry + 1]) == (sizes[i] < 256 ? sizes[i] : 0));
        const uint32_t bytes = Little32(ico, entry + 8);
        const uint32_t offset = Little32(ico, entry + 12);
        CHECK(offset + bytes <= ico.size());
        if (sizes[i] == 256) {
            CHECK(ico.compare(offset, 8, "\x89PNG\r\n\x1a\n") == 0);
            CHECK(ico.compare(offset + 12, 4, "IHDR") == 0);
            CHECK(Big32(ico, offset + 16) == static_cast<uint32_t>(sizes[i]));
            CHECK(Big32(ico, offset + 20) == static_cast<uint32_t>(sizes[i]));
        } else {
            // BITMAPINFOHEADER: the height counts the colour rows and the AND mask.
            const uint32_t mask_row = (static_cast<uint32_t>(sizes[i]) + 31) / 32 * 4;
            CHECK(Little32(ico, offset) == 40);
            CHECK(Little32(ico, offset + 4) == static_cast<uint32_t>(sizes[i]));
            CHECK(Little32(ico, offset + 8) == static_cast<uint32_t>(sizes[i]) * 2);
            CHECK((static_cast<unsigned char>(ico[offset + 14]) | static_cast<unsigned char>(ico[offset + 15]) << 8) == 32);
            CHECK(bytes == 40 + static_cast<uint32_t>(sizes[i]) * (static_cast<uint32_t>(sizes[i]) * 4 + mask_row));
        }
    }
}

void TestProfile() {
    const std::span<const GameBuild> builds = SupportedBuilds();
    CHECK(builds.size() == 8);
    const GameBuild& build = builds[0];
    CHECK(std::string(kGameTitle) == "Odin Sphere Leifthrasir");
    CHECK(std::string(build.title_id) == "CUSA05083");
    CHECK(std::string(build.content_id) == "UP0177-CUSA05083_00-ODINSPHERELE0000");
    CHECK(std::string(build.version) == "1.00");
    CHECK(std::string(build.folder) == "CUSA05083-1.00");
    CHECK(std::string(build.eboot_sha256) == "7c517194aba4e5eea55d9197efae47d1567bd785b8c0dced7d1b0f4028d3eed6");
    CHECK(build.extra_files.empty());
    CHECK(build.data_files.size() == 3);
    CHECK(std::string(build.label) == "CUSA05083 1.00 USA");
    CHECK(std::string(build.region) == "USA");
    CHECK(build.layers.size() == 1);
    CHECK(build.resolution_sites.size() == 17);
    CHECK(std::string(FinishedLayer(build).app_ver) == "01.00");
    CHECK(std::string(FinishedLayer(build).sfo_version) == "01.01");
    CHECK(!FinishedLayer(build).update);
    CHECK(BaseBuildOf(build) == nullptr);
    CHECK(UpdateBuildOf(build) == &builds[1]);
    CHECK(BuildMetadataText(build) == "APP_VER 1.00 \xC2\xB7 VERSION 1.01");

    const GameBuild& japan_base = builds[2];
    const GameBuild& japan = builds[3];
    CHECK(std::string(japan_base.title_id) == "CUSA01290");
    CHECK(std::string(japan_base.content_id) == "JP0005-CUSA01290_00-ODINSPHERELE0000");
    CHECK(std::string(japan_base.version) == "1.00");
    CHECK(std::string(japan_base.folder) == "CUSA01290-1.00");
    CHECK(std::string(japan_base.eboot_sha256) ==
          "d0add7e35bc5f58d5c9017fdce0878006c32f944fa8eb3d7ce62671257f5bcc7");
    CHECK(std::string(japan_base.label) == "CUSA01290 1.00 Japan");
    CHECK(std::string(japan_base.region) == "Japan");
    CHECK(japan_base.layers.size() == 1);
    CHECK(japan_base.resolution_sites.size() == 17);
    CHECK(std::string(FinishedLayer(japan_base).app_ver) == "01.00");
    CHECK(std::string(FinishedLayer(japan_base).sfo_version) == "01.00");
    CHECK(BaseBuildOf(japan_base) == nullptr);
    CHECK(UpdateBuildOf(japan_base) == &japan);

    CHECK(std::string(japan.title_id) == "CUSA01290");
    CHECK(std::string(japan.version) == "1.02");
    CHECK(std::string(japan.folder) == "CUSA01290-1.02");
    CHECK(std::string(japan.eboot_sha256) ==
          "db96b839fe31adba4b3eb3547202f761d34881325c2fea650c4c020eded5d071");
    CHECK(std::string(japan.label) == "CUSA01290 1.02 Japan");
    CHECK(japan.layers.size() == 2);
    CHECK(std::string(FinishedLayer(japan).app_ver) == "01.02");
    CHECK(std::string(FinishedLayer(japan).sfo_version) == "01.00");
    CHECK(FinishedLayer(japan).update);
    CHECK(BaseBuildOf(japan) == &japan_base);
    CHECK(UpdateBuildOf(japan) == nullptr);
    CHECK(japan.resolution_sites.size() == 17);
    CHECK(japan.data_files.size() == 6);
    CHECK(japan.extra_files.size() == 1);
    CHECK(BuildMetadataText(japan) == "APP_VER 1.02 \xC2\xB7 VERSION 1.00");

    const GameBuild& eu_base = builds[4];
    const GameBuild& eu = builds[5];
    CHECK(std::string(eu_base.title_id) == "CUSA05262");
    CHECK(std::string(eu_base.content_id) == "EP1063-CUSA05262_00-ODINSPHERELE0000");
    CHECK(std::string(eu_base.version) == "1.00");
    CHECK(std::string(eu_base.folder) == "CUSA05262-1.00");
    CHECK(std::string(eu_base.eboot_sha256) ==
          "3e6b1741e8a404d0f8fbe10bce93c6887974566f5be09ef1bddc22a00ba29feb");
    CHECK(std::string(eu_base.label) == "CUSA05262 1.00 Europe");
    CHECK(std::string(eu_base.region) == "Europe");
    CHECK(eu_base.layers.size() == 1);
    CHECK(eu_base.extra_files.empty());
    CHECK(eu_base.data_files.size() == 15);
    CHECK(eu_base.resolution_sites.size() == 17);
    CHECK(std::string(FinishedLayer(eu_base).app_ver) == "01.00");
    CHECK(std::string(FinishedLayer(eu_base).sfo_version) == "01.00");
    CHECK(BaseBuildOf(eu_base) == nullptr);
    CHECK(UpdateBuildOf(eu_base) == &eu);
    CHECK(BuildMetadataText(eu_base) == "APP_VER 1.00 \xC2\xB7 VERSION 1.00");

    CHECK(std::string(eu.title_id) == "CUSA05262");
    CHECK(std::string(eu.version) == "1.01");
    CHECK(std::string(eu.folder) == "CUSA05262-1.01");
    CHECK(std::string(eu.eboot_sha256) ==
          "572bf21e57febee415d739cded5516def9ca81e867ad3184c989367248edcd7e");
    CHECK(std::string(eu.label) == "CUSA05262 1.01 Europe");
    CHECK(std::string(eu.region) == "Europe");
    CHECK(eu.layers.size() == 2);
    CHECK(std::string(FinishedLayer(eu).app_ver) == "01.01");
    CHECK(std::string(FinishedLayer(eu).sfo_version) == "01.00");
    CHECK(FinishedLayer(eu).update);
    CHECK(BaseBuildOf(eu) == &eu_base);
    CHECK(UpdateBuildOf(eu) == nullptr);
    CHECK(eu.resolution_sites.size() == 17);
    CHECK(eu.data_files.size() == 25);
    CHECK(eu.extra_files.size() == 1);
    CHECK(BuildMetadataText(eu) == "APP_VER 1.01 \xC2\xB7 VERSION 1.00");

    CHECK(SupportedBuildsText() ==
          "CUSA05083 1.00 USA, CUSA05083 1.01 USA, CUSA01290 1.00 Japan, CUSA01290 1.02 Japan, CUSA05262 1.00 Europe, "
          "CUSA05262 1.01 Europe, CUSA03873 1.00 HK and CUSA03873 1.00 Asia");
    CHECK(std::string(kPreparedMarker) == ".osl_view.json");
    const std::vector<std::string> shared(std::begin(kSharedGameFiles), std::end(kSharedGameFiles));
    for (const char* name : {"eboot.bin", "sce_discmap.plt", "sce_module/libc.prx", "sce_module/libSceFios2.prx",
                             "sce_sys/param.sfo"}) {
        CHECK(std::find(shared.begin(), shared.end(), name) != shared.end());
    }
    CHECK(shared.size() == 5);
    CHECK(std::string(kBundledModules[0]) == "libc.prx");
    CHECK(std::string(kBundledModules[1]) == "libSceFios2.prx");
}

// The Play card's label and its APP_VER · VERSION line come from the same finished layer, for every build.
void TestBuildMetadata() {
    struct Expected {
        const char* folder;
        const char* label;
        const char* metadata;
        const char* base_folder; // the build the update is laid over, or nullptr
    };
    const Expected expected[] = {
        {"CUSA05083-1.00", "CUSA05083 1.00 USA", "APP_VER 1.00 \xC2\xB7 VERSION 1.01", nullptr},
        {"CUSA05083-1.01", "CUSA05083 1.01 USA", "APP_VER 1.01 \xC2\xB7 VERSION 1.00", "CUSA05083-1.00"},
        {"CUSA01290-1.00", "CUSA01290 1.00 Japan", "APP_VER 1.00 \xC2\xB7 VERSION 1.00", nullptr},
        {"CUSA01290-1.02", "CUSA01290 1.02 Japan", "APP_VER 1.02 \xC2\xB7 VERSION 1.00", "CUSA01290-1.00"},
        {"CUSA05262-1.00", "CUSA05262 1.00 Europe", "APP_VER 1.00 \xC2\xB7 VERSION 1.00", nullptr},
        {"CUSA05262-1.01", "CUSA05262 1.01 Europe", "APP_VER 1.01 \xC2\xB7 VERSION 1.00", "CUSA05262-1.00"},
        {"CUSA03873-1.00", "CUSA03873 1.00 HK", "APP_VER 1.00 \xC2\xB7 VERSION 1.00", nullptr},
        {"CUSA03873-1.00-Asia", "CUSA03873 1.00 Asia", "APP_VER 1.00 \xC2\xB7 VERSION 1.00", nullptr},
    };
    const std::span<const GameBuild> builds = SupportedBuilds();
    CHECK(builds.size() == std::size(expected));
    for (size_t i = 0; i < std::min(builds.size(), std::size(expected)); ++i) {
        const GameBuild& build = builds[i];
        const Expected& want = expected[i];
        CHECK(std::string(build.folder) == want.folder);
        CHECK(std::string(build.label) == want.label);
        CHECK(BuildMetadataText(build) == want.metadata);
        CHECK(std::string(build.label) == std::string(build.title_id) + " " + build.version + " " + build.region);
        CHECK(BuildMetadataText(build).starts_with(std::string("APP_VER ") + build.version + " \xC2\xB7 VERSION "));
        CHECK(std::string(FinishedLayer(build).app_ver).size() == 5);
        CHECK(std::string(FinishedLayer(build).sfo_version).size() == 5);
        CHECK(FinishedLayer(build).update == (build.layers.size() > 1));
        const GameBuild* base = BaseBuildOf(build);
        CHECK(want.base_folder ? base && std::string(base->folder) == want.base_folder : !base);
    }
}

void TestSfoIdentity() {
    const std::filesystem::path dir = TempDir("sfo");
    const std::filesystem::path sfo = dir / "param.sfo";
    const std::vector<std::pair<std::string, std::string>> good = {
        {"TITLE", "Odin Sphere Leifthrasir"}, {"TITLE_ID", "CUSA05083"},
        {"CONTENT_ID", "UP0177-CUSA05083_00-ODINSPHERELE0000"}, {"CATEGORY", "gd"},
        {"APP_VER", "01.00"}, {"VERSION", "01.01"},
    };
    WriteSfo(sfo, good);
    std::ifstream in(sfo, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    CHECK(SfoString(text, "TITLE_ID") == "CUSA05083");
    CHECK(SfoString(text, "CONTENT_ID") == "UP0177-CUSA05083_00-ODINSPHERELE0000");
    CHECK(SfoString(text, "APP_VER") == "01.00");
    CHECK(SfoString(text, "VERSION") == "01.01");
    CHECK(SfoString(text, "MISSING").empty());
    CHECK(CheckSfoIdentity(text).state == GameFileState::kValid);
    CHECK(CheckPackageIdentity(text).state == GameFileState::kValid);
    CHECK(CheckPackageIdentity(text).layer == 0);

    auto replaced = good;
    for (auto& [key, value] : replaced) {
        if (key == "TITLE_ID") {
            value = "CUSA00000";
        }
    }
    WriteSfo(sfo, replaced);
    std::ifstream other(sfo, std::ios::binary);
    const std::string wrong_title((std::istreambuf_iterator<char>(other)), {});
    CHECK(CheckSfoIdentity(wrong_title).state == GameFileState::kDifferentVersion);
    CHECK(CheckSfoIdentity(wrong_title).detail.starts_with("param.sfo TITLE_ID"));

    auto other_version = good;
    for (auto& [key, value] : other_version) {
        if (key == "VERSION") {
            value = "01.00";
        }
    }
    WriteSfo(sfo, other_version);
    std::ifstream version(sfo, std::ios::binary);
    const std::string wrong_version((std::istreambuf_iterator<char>(version)), {});
    CHECK(CheckSfoIdentity(wrong_version).state == GameFileState::kDifferentVersion);

    const std::vector<std::pair<std::string, std::string>> japan = {
        {"TITLE", "Odin Sphere Leifthrasir"}, {"TITLE_ID", "CUSA01290"},
        {"CONTENT_ID", "JP0005-CUSA01290_00-ODINSPHERELE0000"}, {"CATEGORY", "gd"},
        {"APP_VER", "01.00"}, {"VERSION", "01.00"},
    };
    WriteSfo(sfo, japan);
    std::ifstream base_file(sfo, std::ios::binary);
    const std::string japan_base((std::istreambuf_iterator<char>(base_file)), {});
    CHECK(CheckSfoIdentity(japan_base).state == GameFileState::kValid);
    CHECK(CheckSfoIdentity(japan_base).build == &SupportedBuilds()[2]);
    CHECK(CheckPackageIdentity(japan_base).state == GameFileState::kValid);
    CHECK(CheckPackageIdentity(japan_base).layer == 0);

    auto japan_update = japan;
    for (auto& [key, value] : japan_update) {
        if (key == "CATEGORY") value = "gp";
        if (key == "APP_VER") value = "01.02";
    }
    WriteSfo(sfo, japan_update);
    std::ifstream update_file(sfo, std::ios::binary);
    const std::string update((std::istreambuf_iterator<char>(update_file)), {});
    CHECK(CheckSfoIdentity(update).state == GameFileState::kValid);
    CHECK(CheckSfoIdentity(update).build == &SupportedBuilds()[3]);
    CHECK(CheckPackageIdentity(update).state == GameFileState::kValid);
    CHECK(CheckPackageIdentity(update).build == &SupportedBuilds()[3]);
    CHECK(CheckPackageIdentity(update).layer == 1);

    const std::vector<std::pair<std::string, std::string>> europe = {
        {"TITLE", "Odin Sphere Leifthrasir"}, {"TITLE_ID", "CUSA05262"},
        {"CONTENT_ID", "EP1063-CUSA05262_00-ODINSPHERELE0000"}, {"CATEGORY", "gd"},
        {"APP_VER", "01.00"}, {"VERSION", "01.00"},
    };
    WriteSfo(sfo, europe);
    std::ifstream eu_base_file(sfo, std::ios::binary);
    const std::string eu_base((std::istreambuf_iterator<char>(eu_base_file)), {});
    CHECK(CheckSfoIdentity(eu_base).state == GameFileState::kValid);
    CHECK(CheckSfoIdentity(eu_base).build == &SupportedBuilds()[4]);
    CHECK(CheckPackageIdentity(eu_base).state == GameFileState::kValid);
    CHECK(CheckPackageIdentity(eu_base).layer == 0);

    auto eu_update = europe;
    for (auto& [key, value] : eu_update) {
        if (key == "CATEGORY") value = "gp";
        if (key == "APP_VER") value = "01.01";
    }
    WriteSfo(sfo, eu_update);
    std::ifstream eu_update_file(sfo, std::ios::binary);
    const std::string eu_patch((std::istreambuf_iterator<char>(eu_update_file)), {});
    CHECK(CheckSfoIdentity(eu_patch).state == GameFileState::kValid);
    CHECK(CheckSfoIdentity(eu_patch).build == &SupportedBuilds()[5]);
    CHECK(CheckPackageIdentity(eu_patch).state == GameFileState::kValid);
    CHECK(CheckPackageIdentity(eu_patch).build == &SupportedBuilds()[5]);
    CHECK(CheckPackageIdentity(eu_patch).layer == 1);
    CHECK(BaseBuildOf(SupportedBuilds()[5]) == &SupportedBuilds()[4]);
    CHECK(BaseBuildOf(SupportedBuilds()[4]) == nullptr);
    CHECK(UpdateBuildOf(SupportedBuilds()[4]) == &SupportedBuilds()[5]);

    auto eu_wrong_version = eu_update;
    for (auto& [key, value] : eu_wrong_version) {
        if (key == "APP_VER") value = "01.02";
    }
    WriteSfo(sfo, eu_wrong_version);
    std::ifstream eu_wrong_file(sfo, std::ios::binary);
    const std::string eu_unsupported((std::istreambuf_iterator<char>(eu_wrong_file)), {});
    CHECK(CheckPackageIdentity(eu_unsupported).state == GameFileState::kDifferentVersion);

    // The European update names a different TITLE_ID than the USA and Japanese builds ship, so it never matches them.
    auto eu_on_usa = eu_update;
    for (auto& [key, value] : eu_on_usa) {
        if (key == "TITLE_ID") value = "CUSA05083";
    }
    WriteSfo(sfo, eu_on_usa);
    std::ifstream eu_on_usa_file(sfo, std::ios::binary);
    const std::string foreign_update((std::istreambuf_iterator<char>(eu_on_usa_file)), {});
    CHECK(CheckPackageIdentity(foreign_update).state == GameFileState::kDifferentVersion);
    CHECK(CheckPackageIdentity(foreign_update).detail.starts_with("param.sfo CONTENT_ID"));
}

void TestGameTree() {
    const std::filesystem::path dir = TempDir("tree");
    CHECK(CheckGameTree(dir).state == GameFileState::kMissing);
    std::filesystem::create_directories(dir / "sce_sys");
    WriteSfo(dir / "sce_sys" / "param.sfo",
             {{"TITLE_ID", "CUSA05083"},
              {"CONTENT_ID", "UP0177-CUSA05083_00-ODINSPHERELE0000"},
              {"CATEGORY", "gd"},
              {"APP_VER", "01.00"},
              {"VERSION", "01.01"}});
    const GameCheck missing = CheckGameTree(dir);
    CHECK(missing.state == GameFileState::kMissing);
    CHECK(missing.detail == "missing eboot.bin");
    CHECK(missing.build == &SupportedBuilds()[0]);
    CHECK(InstalledGameName(GameFileState::kValid, missing.build) == "PS4 CUSA05083 1.00 USA");
    CHECK(InstalledGameName(GameFileState::kValid, &SupportedBuilds()[3]) == "PS4 CUSA01290 1.02 Japan");
    CHECK(InstalledGameName(GameFileState::kMissing, missing.build) == "Not installed");
    CHECK(std::string(StateName(GameFileState::kDifferentVersion)) == "different version");
}

void TestBuildSelection() {
    const std::filesystem::path root = TempDir("build-selection");
    const GameRoots roots{root / "prepared", root / "image"};
    const std::span<const GameBuild> builds = SupportedBuilds();
    CHECK(LastPreparedBuild(roots) == nullptr);
    CHECK(ActiveBuildPaths(roots).app0 == BuildPaths(roots, builds[0]).app0);

    const auto write_marker = [&](const GameBuild& build, int seconds) {
        std::filesystem::create_directories(roots.prepared / build.folder);
        std::ofstream marker(roots.prepared / build.folder / kPreparedMarker);
        marker << "{\"version\": \"" << build.version << "\", \"scope\": \"full\"}\n";
        marker.close();
        std::filesystem::last_write_time(roots.prepared / build.folder / kPreparedMarker,
                                         std::filesystem::file_time_type::clock::now() - std::chrono::hours(24) + std::chrono::seconds(seconds));
    };
    write_marker(builds[0], 10);
    write_marker(builds[2], 20);
    write_marker(builds[3], 30);
    CHECK(LastPreparedBuild(roots) == &builds[3]);
    CHECK(ActiveBuildPaths(roots).app0 == BuildPaths(roots, builds[3]).app0);

    write_marker(builds[2], 40);
    CHECK(LastPreparedBuild(roots) == &builds[2]);
    CHECK(UpdateBuildOf(*LastPreparedBuild(roots)) == &builds[3]);
}

void TestSettings() {
    const std::filesystem::path dir = TempDir("settings");
    LauncherSettings settings(dir / "osl.ini");
    settings.ResetToDefaults();
    CHECK(settings.Get(kShowLauncherKey, "") == "1");
    CHECK(settings.Get(kWindowWidthKey, "") == "1920");
    CHECK(settings.Get(kWindowHeightKey, "") == "1080");
    CHECK(settings.Get(kMonitorKey, "") == "1");
    CHECK(settings.Get(kVSyncKey, "") == "1");
    CHECK(settings.Get(kPresentationKey, "") == "letterbox");
    CHECK(settings.Get(kGraphicsCardKey, "") == "");
    CHECK(settings.Get(kAnisotropyKey, "") == "0");
    CHECK(settings.Get(kAntiAliasingKey, "") == "0");
    CHECK(settings.Get(kUpscaleKey, "") == "0");
    CHECK(settings.Get(kSharpenKey, "") == "1");
    CHECK(settings.Get(kRenderResolutionKey, "") == "native");
    CHECK(settings.Get(kAspectRatioKey, "") == "16:9");
    CHECK(settings.Get(kDetailedLoggingKey, "") == "1");
    CHECK(settings.GetBool(kShowLauncherKey, false));
    CHECK(settings.GetInt(kWindowHeightKey, 0) == 1080);

    CHECK(IsValidSetting(kDisplayModeKey, "windowed"));
    CHECK(IsValidSetting(kDisplayModeKey, "borderless"));
    CHECK(!IsValidSetting(kDisplayModeKey, "fullscreen"));
    CHECK(IsValidSetting(kAnisotropyKey, "16"));
    CHECK(!IsValidSetting(kAnisotropyKey, "3"));
    CHECK(IsValidSetting(kPresentationKey, "stretch"));
    CHECK(!IsValidSetting(kPresentationKey, "cover"));
    CHECK(IsValidSetting(kRenderResolutionKey, "1440p"));
    CHECK(!IsValidSetting(kRenderResolutionKey, "9000p"));
    CHECK(IsValidSetting(kAspectRatioKey, "21:9"));
    CHECK(!IsValidSetting(kAspectRatioKey, "1:1"));
    CHECK(IsValidSetting(osl_key_actions[OSL_KEY_CROSS].ini_key, "Q"));
    CHECK(!IsValidSetting(osl_key_actions[OSL_KEY_CROSS].ini_key, "not a key"));
    CHECK(IsLauncherPlacementSetting(kLauncherXKey));

    settings.Set(kDisplayModeKey, "borderless");
    settings.Set(kMonitorKey, "2");
    settings.Set(kRenderResolutionKey, "1440p");
    settings.Set(kAspectRatioKey, "21:9");
    settings.Set(kLauncherXKey, "120");
    CHECK(settings.Save());
    LauncherSettings reloaded(dir / "osl.ini");
    CHECK(reloaded.Get(kDisplayModeKey, "") == "borderless");
    CHECK(reloaded.GetInt(kMonitorKey, 1) == 2);
    CHECK(reloaded.Get(kRenderResolutionKey, "") == "1440p");
    CHECK(reloaded.Get(kAspectRatioKey, "") == "21:9");
    CHECK(reloaded.GetInt(kLauncherXKey, 0) == 120);
    CHECK(!std::filesystem::exists(dir / "osl.ini.tmp"));

    reloaded.ResetToDefaults();
    CHECK(reloaded.Get(kDisplayModeKey, "") == "windowed");
    CHECK(reloaded.GetInt(kLauncherXKey, 0) == 120);
    CHECK(LauncherWanted(true, true, false, false));
    CHECK(LauncherWanted(true, false, true, false));
    CHECK(!LauncherWanted(true, false, false, false));
    CHECK(LauncherWanted(false, false, false, true));
}

void TestKeys() {
    OslKeyMap map;
    osl_keys_defaults(&map);
    for (int i = 0; i < OSL_KEY_COUNT; ++i) {
        CHECK(map.scancode[i] == osl_key_actions[i].default_scancode);
    }
    CHECK(map.scancode[OSL_KEY_HOTKEY_WINDOW_MODE] != 0);
    CHECK(osl_keys_apply(&map, osl_key_actions[OSL_KEY_CROSS].ini_key, "Q"));
    CHECK(map.scancode[OSL_KEY_CROSS] == SDL_SCANCODE_Q);
    CHECK(!osl_keys_apply(&map, osl_key_actions[OSL_KEY_CROSS].ini_key, "escape"));
    CHECK(!osl_keys_apply(&map, "not_an_action", "Q"));
    CHECK(osl_keys_apply(&map, osl_key_actions[OSL_KEY_HOTKEY_WINDOW_MODE].ini_key, "f9"));
    CHECK(map.scancode[OSL_KEY_HOTKEY_WINDOW_MODE] == SDL_SCANCODE_F9);
    char name[32];
    CHECK(std::string(osl_key_name(map.scancode[OSL_KEY_CROSS], name, sizeof(name))) == "q");
    CHECK(!osl_key_bindable(0));
}

// Presentation only fits the finished frame into the window: Letterbox unless Stretch is chosen, read by the launcher
// and the video core alike, and picked by the presenter's blit.
void TestPresentation() {
    CHECK(std::string(kPresentationKey) == "presentation" && std::string(kDefaultPresentation) == "letterbox");
    CHECK(std::string(kPresentationChoices[0].key) == "letterbox" && std::string(kPresentationChoices[1].key) == "stretch");
    CHECK(OslGraphics::ParsePresentation("letterbox") == OslGraphics::Letterbox);
    CHECK(OslGraphics::ParsePresentation("stretch") == OslGraphics::Stretch);
    for (const char* invalid : {"", "fit", "Stretch", "1"}) {
        CHECK(OslGraphics::ParsePresentation(invalid) == OslGraphics::Letterbox);
        CHECK(!IsValidSetting(kPresentationKey, invalid));
    }

    const std::filesystem::path root(OSL_SOURCE_DIR);
    const std::string launcher = ReadFile(root / "launcher" / "launcher.cpp");
    const size_t vsync = launcher.find("ui.Toggle(\"VSync\"");
    const size_t presentation =
        launcher.find("ui.Choice(\"Presentation\", kPresentationHelp, kPresentationKey, kDefaultPresentation");
    const size_t scaling = launcher.find("ui.Heading(\"Scaling and sharpening\");");
    CHECK(vsync != std::string::npos && vsync < presentation && presentation < scaling && scaling != std::string::npos);
    const std::string reader = ReadFile(root / "gpu" / "shim" / "osl_settings.cpp");
    CHECK(reader.find("v.presentation = OslGraphics::ParsePresentation(value);") != std::string::npos);
    const std::string presenter =
        ReadFile(root / "gpu" / "shadps4" / "video_core" / "renderer_vulkan" / "vk_presenter.cpp");
    const size_t pick = presenter.find("const vk::ImageBlit blit = stretch");
    CHECK(presenter.find("OslSettings::Get().presentation.load() == OslGraphics::Stretch") != std::string::npos);
    CHECK(pick != std::string::npos && presenter.find("MakeImageBlitStretch(frame->width", pick) <
                                           presenter.find("MakeImageBlitFit(frame->width", pick));
}

// The Renderer setting: one key and one parser for the launcher and the video core, Vulkan by default.
void TestRenderer() {
    CHECK(std::string(kRendererKey) == "renderer" && std::string(kDefaultRenderer) == "vulkan");
    CHECK(kRendererChoices[0].choice == OslRenderer::Auto && kRendererChoices[1].choice == OslRenderer::Vulkan &&
          kRendererChoices[2].choice == OslRenderer::OpenGL);
    CHECK(std::size(kRendererChoices) == (OslRenderer::Direct3D11Available ? 4u : 3u));
    CHECK(OslRenderer::Direct3D11Available == (std::size(kRendererChoices) == 4));

    const std::filesystem::path file = TempDir("renderer") / "osl.ini";
    LauncherSettings settings(file);
    settings.ResetToDefaults();
    CHECK(settings.Get(kRendererKey, "") == "vulkan");
    for (const RendererChoice& choice : kRendererChoices) {
        CHECK(IsValidSetting(kRendererKey, choice.key()));
        settings.Set(kRendererKey, std::string(choice.key()));
        CHECK(settings.Save());
        OslSettings::Values values;
        CHECK(OslSettings::Read(file.string(), values));
        CHECK(values.renderer.load() == choice.choice);
    }
    for (const char* invalid : {"", "Vulkan", "OpenGL", "direct3d11", "gl"}) {
        CHECK(!IsValidSetting(kRendererKey, invalid));
    }
    CHECK(IsValidSetting(kRendererKey, "d3d11") == OslRenderer::Direct3D11Available);
    settings.Set(kRendererKey, "opengl");
    settings.ResetToDefaults();
    CHECK(settings.Get(kRendererKey, "") == "vulkan");
    {
        std::ofstream(file, std::ios::trunc) << "renderer = bogus\n";
        OslSettings::Values values;
        CHECK(OslSettings::Read(file.string(), values) && values.renderer.load() == OslRenderer::Vulkan);
    }
}

// The Frame rate setting is a host-side VideoOut feature: one key, one accepted set and one default for every supported
// build and every language, read by the launcher and the video core alike.
void TestFrameRate() {
    CHECK(std::string(kFrameRateKey) == "frame_rate");
    const std::span<const GameBuild> builds = SupportedBuilds();
    const char* const expected[] = {"CUSA05083 1.00 USA",    "CUSA05083 1.01 USA",    "CUSA01290 1.00 Japan",
                                    "CUSA01290 1.02 Japan",  "CUSA05262 1.00 Europe", "CUSA05262 1.01 Europe",
                                    "CUSA03873 1.00 HK",     "CUSA03873 1.00 Asia"};
    CHECK(builds.size() == std::size(expected));
    for (size_t i = 0; i < builds.size() && i < std::size(expected); ++i) {
        CHECK(std::string_view(builds[i].label) == expected[i]);
    }

    bool managed = false;
    for (const ManagedSetting& setting : ManagedSettings()) {
        managed |= setting.key == kFrameRateKey && setting.default_value == "60";
    }
    CHECK(managed);

    // The launcher accepts exactly the video core's modes (30, 60, 90, 120, 144, 165, 240, 0 for Unlimited).
    for (int value = -5; value <= 1000; ++value) {
        const std::string text = std::to_string(value);
        CHECK(IsValidSetting(kFrameRateKey, text) == OslFramePacing::IsFrameRateMode(value));
        CHECK(OslFramePacing::ParseFrameRate(text) == (OslFramePacing::IsFrameRateMode(value) ? value : 60));
    }
    for (const char* invalid : {"", "abc", "45", "-1", "090", "120.0", "120fps", " 120", "unlimited"}) {
        CHECK(!IsValidSetting(kFrameRateKey, invalid));
        CHECK(OslFramePacing::ParseFrameRate(invalid) == 60);
    }
    CHECK(FrameRateLabel(0) == "Unlimited" && FrameRateLabel(60) == "60 FPS (Original)" &&
          FrameRateLabel(30) == "30 FPS" && FrameRateLabel(120) == "120 FPS" && FrameRateLabel(240) == "240 FPS");

    // Every build, and every language it offers: the same default, the same modes, the same stored value.
    for (const GameBuild& build : builds) {
        const std::string name = std::string("frame-rate-") + build.title_id + (build.version ? build.version : "");
        const std::filesystem::path file = TempDir(name.c_str()) / "osl.ini";
        {
            LauncherSettings fresh(file);
            fresh.ResetToDefaults();
            CHECK(fresh.Get(kFrameRateKey, "") == "60");
            for (const int mode : OslFramePacing::kFrameRateModes) {
                const std::string text = std::to_string(mode);
                CHECK(IsValidSetting(kFrameRateKey, text));
                fresh.Set(kFrameRateKey, text);
                CHECK(fresh.Get(kFrameRateKey, "") == text);
            }
        }
        std::vector<std::string_view> languages{""};
        for (const TextLanguage& language : TextLanguagesOf(&build)) {
            languages.push_back(language.key);
        }
        for (const std::string_view language : languages) {
            std::ofstream(file, std::ios::trunc) << "";
            LauncherSettings settings(file);
            settings.ResetToDefaults();
            if (!language.empty()) {
                settings.Set(kTextLanguageKey, std::string(language));
            }
            settings.Set(kFrameRateKey, "120");
            CHECK(settings.Save());
            const LauncherSettings reloaded(file);
            CHECK(reloaded.Get(kFrameRateKey, "") == "120");
            OslSettings::Values values;
            CHECK(OslSettings::Read(file.string(), values) && values.frame_rate.load() == 120);
            // Changing the language afterwards leaves the frame rate alone.
            LauncherSettings other(file);
            other.Set(kTextLanguageKey, std::string(language.empty() ? "english" : language));
            CHECK(other.Save() && other.Get(kFrameRateKey, "") == "120");
            OslSettings::Values again;
            CHECK(OslSettings::Read(file.string(), again) && again.frame_rate.load() == 120);
            CHECK(OslFramePacing::RepeatCadenceHz(again.frame_rate.load()) == 120);
        }
    }

    // Unset, invalid and reset values are the original 60; unrelated lines of osl.ini are kept.
    const std::filesystem::path file = TempDir("frame-rate") / "osl.ini";
    {
        OslSettings::Values values;
        std::ofstream(file, std::ios::trunc) << "";
        CHECK(OslSettings::Read(file.string(), values) && values.frame_rate.load() == 60);
        for (const char* invalid : {"45", "-1", "garbage", ""}) {
            std::ofstream(file, std::ios::trunc) << "frame_rate = " << invalid << "\n";
            OslSettings::Values loaded;
            CHECK(OslSettings::Read(file.string(), loaded) && loaded.frame_rate.load() == 60);
            CHECK(LauncherSettings(file).Get(kFrameRateKey, "60") == "60");
        }
    }
    std::ofstream(file, std::ios::trunc) << "# kept comment\nunrelated_key = kept value\nframe_rate = 144\n";
    {
        LauncherSettings settings(file);
        CHECK(settings.Get(kFrameRateKey, "") == "144");
        settings.Set(kFrameRateKey, "240");
        CHECK(settings.Save());
        const std::string text = ReadFile(file);
        CHECK(text.find("# kept comment") != std::string::npos && text.find("unrelated_key = kept value") != std::string::npos);
        CHECK(text.find("frame_rate = 240") != std::string::npos && text.find("frame_rate = 144") == std::string::npos);
        settings.ResetToDefaults();
        CHECK(settings.Get(kFrameRateKey, "") == "60");
        CHECK(settings.Save());
        const std::string reset = ReadFile(file);
        CHECK(reset.find("frame_rate = 60") != std::string::npos && reset.find("unrelated_key = kept value") != std::string::npos);
        OslSettings::Values values;
        CHECK(OslSettings::Read(file.string(), values) && values.frame_rate.load() == 60);
    }
}

// The game window's title: the game's title, then the render resolution and the frame rate when shown. A text entry
// takes the place of the frame rate and keeps the resolution.
void TestWindowTitle() {
    const auto compose = [](bool resolution, bool fps) {
        return OslTitle::Compose({.base = "Odin Sphere Leifthrasir", .show_resolution = resolution, .width = 2560,
                                  .height = 1440, .show_fps = fps, .fps = 60.0});
    };
    CHECK(compose(false, false) == "Odin Sphere Leifthrasir");
    CHECK(compose(true, false) == "Odin Sphere Leifthrasir | 2560x1440");
    CHECK(compose(false, true) == "Odin Sphere Leifthrasir | FPS: 60.00");
    CHECK(compose(true, true) == "Odin Sphere Leifthrasir | 2560x1440 | FPS: 60.00");
    CHECK(OslTitle::Compose({.base = "G", .show_resolution = true, .show_fps = true}) == "G");
    CHECK(OslTitle::Compose({.base = "G", .show_fps = true, .fps = 59.999}) == "G | FPS: 60.00");
    CHECK(OslTitle::Compose({.base = "G", .show_resolution = true, .width = 1280, .height = 720, .show_fps = true,
                             .fps = 30.0, .entry = " — Name: ab_"}) == "G | 1280x720 — Name: ab_");

    const std::filesystem::path file = TempDir("window-title") / "osl.ini";
    LauncherSettings settings(file);
    settings.ResetToDefaults();
    CHECK(settings.Get(kShowResolutionTitleKey, "") == "0" && settings.Get(kShowFpsTitleKey, "") == "0");
    settings.SetBool(kShowResolutionTitleKey, true);
    CHECK(settings.Save());
    CHECK(LauncherSettings(file).GetBool(kShowResolutionTitleKey, false));

    const std::filesystem::path root(OSL_SOURCE_DIR);
    const std::string reader = ReadFile(root / "gpu" / "shim" / "osl_settings.cpp");
    CHECK(reader.find("key == \"show_resolution_title\"") != std::string::npos);
    const std::string window = ReadFile(root / "gpu" / "shim" / "window.cpp");
    CHECK(window.find(".show_resolution = settings.show_resolution_title") != std::string::npos);
    CHECK(window.find("if (title != title_shown)") != std::string::npos);
}

// The Frame counter setting, its F2 hotkey and the startup Gameplay hints: default state, save/load, and that the
// pieces are wired to each other the way osl_notice.h's pure logic (covered by osl-notice-tests) expects.
void TestFrameCounterAndNotices() {
    const std::filesystem::path file = TempDir("frame-counter") / "osl.ini";
    LauncherSettings settings(file);
    settings.ResetToDefaults();
    CHECK(settings.Get(kShowFrameCounterKey, "") == "0"); // Hidden by default
    settings.SetBool(kShowFrameCounterKey, true);
    CHECK(settings.Save());
    CHECK(LauncherSettings(file).GetBool(kShowFrameCounterKey, false));
    CHECK(LauncherSettings(file).GetBool(kSkipIntroKey, true) == false);
    CHECK(LauncherSettings(file).GetBool(kUnlockBookshelfKey, true) == false);

    const std::filesystem::path root(OSL_SOURCE_DIR);

    // The launcher's Gameplay page: Skip intro and Unlock Bookshelf before the title/counter rows, and the
    // Frame counter row documents F2 directly (not only in the README).
    const std::string launcher = ReadFile(root / "launcher" / "launcher.cpp");
    CHECK(launcher.find("constexpr char kFrameCounterHelp[] = \"Shows the game's frame rate in the corner. "
                        "F2 toggles it while playing.\";") != std::string::npos);
    const size_t page = launcher.find("void PageGameplay(trg::Ui& ui) {");
    const size_t page_end = launcher.find("void PageControls(trg::Ui& ui) {");
    CHECK(page != std::string::npos && page_end != std::string::npos && page < page_end);
    const std::string gameplay = launcher.substr(page, page_end - page);
    const size_t skip = gameplay.find("ui.Toggle(\"Skip intro\"");
    const size_t unlock = gameplay.find("ui.Toggle(\"Unlock Bookshelf\"");
    const size_t counter =
        gameplay.find("ui.Toggle(\"Frame counter\", kFrameCounterHelp, kShowFrameCounterKey, false, \"Hidden\", \"Shown\");");
    CHECK(skip != std::string::npos && unlock != std::string::npos && counter != std::string::npos);
    CHECK(skip < unlock && unlock < counter);

    // The video core mirrors Skip intro and Unlock Bookshelf for the startup notice only.
    const std::string reader = ReadFile(root / "gpu" / "shim" / "osl_settings.cpp");
    CHECK(reader.find("key == \"skip_intro\"") != std::string::npos);
    CHECK(reader.find("key == \"unlock_bookshelf\"") != std::string::npos);

    // F2 toggles only the frame counter's runtime atomic: no repeat retoggle, no file write.
    const std::string window = ReadFile(root / "gpu" / "shim" / "window.cpp");
    const size_t f2 = window.find("event.key.scancode == SDL_SCANCODE_F2");
    const size_t toggle_call = window.find("ToggleFrameCounter();");
    const size_t repeat_guard = window.find("frame_counter_shortcut && !event.key.repeat");
    CHECK(f2 != std::string::npos && toggle_call != std::string::npos && repeat_guard != std::string::npos);
    const size_t toggle_def = window.find("void WindowSDL::ToggleFrameCounter() {");
    const size_t toggle_def_end = window.find("\n}", toggle_def);
    CHECK(toggle_def != std::string::npos && toggle_def_end != std::string::npos);
    const std::string toggle_body = window.substr(toggle_def, toggle_def_end - toggle_def);
    CHECK(toggle_body.find("show_fps = !show_fps.load();") != std::string::npos);
    CHECK(toggle_body.find("Save") == std::string::npos); // runtime-only: no osl.ini write

    // Queued once per launch, right after settings load, in the Frame counter / Skip Intro / Unlock Bookshelf order.
    const std::string gpu = ReadFile(root / "gpu" / "shim" / "oslgpu.cpp");
    const size_t load = gpu.find("OslSettings::Load(config->settings_file);");
    const size_t skip_notice = gpu.find("QueueStartupNotice(\"Skip Intro: On\");");
    const size_t bookshelf_notice = gpu.find("QueueStartupNotice(\"Unlock Bookshelf: On\");");
    CHECK(load != std::string::npos && skip_notice != std::string::npos && bookshelf_notice != std::string::npos);
    CHECK(load < skip_notice && skip_notice < bookshelf_notice);
    CHECK(gpu.find("if (loaded.skip_intro) {") != std::string::npos);
    CHECK(gpu.find("if (loaded.unlock_bookshelf) {") != std::string::npos);

    // The overlay draws the frame counter, then the startup notices, for 5 seconds each.
    const std::string overlay = ReadFile(root / "gpu" / "shim" / "osl_overlay.cpp");
    CHECK(overlay.find("constexpr double kStartupNoticeSeconds = 5.0;") != std::string::npos);
    const size_t fps_counter_call = overlay.find("FpsCounter();");
    const size_t notices_call = overlay.find("StartupNotices();");
    CHECK(fps_counter_call != std::string::npos && notices_call != std::string::npos && fps_counter_call < notices_call);
    CHECK(overlay.find("AnyStartupNoticePending()") != std::string::npos);
}

void TestNoRuntimeOverlay() {
    const std::filesystem::path root(OSL_SOURCE_DIR);
    const std::string overlay = ReadFile(root / "gpu" / "shim" / "osl_overlay.cpp");
    CHECK(overlay.find("menu ready") == std::string::npos);
    CHECK(overlay.find("R3+L2") == std::string::npos);
    CHECK(overlay.find("SDLK_INSERT") == std::string::npos);
    CHECK(overlay.find("ImageTab") == std::string::npos);
    CHECK(overlay.find("OverlayTab") == std::string::npos);
    CHECK(overlay.find("void Menu()") == std::string::npos);
    CHECK(overlay.find("menu_open") == std::string::npos);
    CHECK(overlay.find("menu_key") == std::string::npos);
    const std::string keys = ReadFile(root / "src" / "osl_keys.c");
    CHECK(keys.find("hotkey_menu") == std::string::npos);
    const std::string settings = ReadFile(root / "gpu" / "shim" / "osl_settings.h");
    CHECK(settings.find("menu_key") == std::string::npos);
    CHECK(settings.find("menu_tab") == std::string::npos);
    const std::string renderer = ReadFile(root / "gpu" / "shim" / "osl_settings.cpp");
    CHECK(renderer.find("key == \"vsync\"") != std::string::npos);
    CHECK(renderer.find("key == \"fsr1\"") != std::string::npos);
    CHECK(renderer.find("key == \"rcas\"") != std::string::npos);
    CHECK(renderer.find("key == \"rcas_strength\"") != std::string::npos);
}

void TestOptionsAndInstall() {
    CHECK(WindowSizeOf(1080, osl_aspect_ratios[0]) == (WindowSize{1920, 1080}));
    CHECK(WindowSizeOf(720, osl_aspect_ratios[0]) == (WindowSize{1280, 720}));
    const std::vector<WindowSize> sizes = WindowSizesFor(osl_aspect_ratios[0]);
    CHECK(sizes.size() == std::size(kWindowHeights));
    CHECK(sizes.back() == (WindowSize{3840, 2160}));
    CHECK(WindowSizeOf(1080, osl_aspect_ratios[2]) == (WindowSize{2520, 1080}));
    CHECK(WindowSizeLabel(kGamePictureSize).find("the game's own size") != std::string::npos);
    CHECK(std::string(PageName(PageId::kPlay)) == "Play");
    CHECK(std::string(PageName(PageId::kAbout)) == "About");
    CHECK(std::string(PageName(PageId::kGraphics)) == "Graphics");
    CHECK(std::string(PageName(PageId::kGameplay)) == "Gameplay");
    CHECK(std::string(PageName(PageId::kTrophies)) == "Trophies");
    CHECK(!LauncherQuitAllowed(true));
    CHECK(LauncherQuitAllowed(false));

    CHECK(osl_resolution_index("native") == 0);
    CHECK(osl_resolution_index("4K") == 5);
    CHECK(osl_resolution_index("bogus") == 0);
    CHECK(osl_aspect_index("16:9") == 0);
    CHECK(osl_aspect_index("21:9") == 2);
    CHECK(osl_aspect_index("bogus") == 0);
    CHECK(ResolutionLabel(osl_resolutions[0]).find("1920") != std::string::npos);
    const OslResolution target = osl_render_target(&osl_resolutions[3], &osl_aspect_ratios[0]);
    CHECK(target.width == 2560 && target.height == 1440);
    const OslResolution wide = osl_render_target(&osl_resolutions[0], &osl_aspect_ratios[2]);
    CHECK(wide.height == 1080 && wide.width == 2520);

    for (const InstallStatus status : {InstallStatus::kInstalled, InstallStatus::kCancelled, InstallStatus::kNotPackage,
                                       InstallStatus::kUnsupportedPackage, InstallStatus::kNotGameFolder,
                                       InstallStatus::kWrongGame, InstallStatus::kWrongVersion, InstallStatus::kUnsupportedBuild,
                                       InstallStatus::kIncomplete,
                                       InstallStatus::kReadFailed, InstallStatus::kNoSpace, InstallStatus::kWriteFailed,
                                       InstallStatus::kPrepareFailed}) {
        const InstallMessage message = DescribeInstall(status);
        CHECK(!message.title.empty() && !message.text.empty());
    }
    CHECK(DescribeInstall(InstallStatus::kWrongVersion).text.find("CUSA05083 1.00 USA") != std::string::npos);
    CHECK(DescribeInstall(InstallStatus::kWrongVersion).text.find("CUSA01290 1.02 Japan") != std::string::npos);
    CHECK(DescribeInstall(InstallStatus::kWrongGame).text.find("CUSA01290 1.02 Japan") != std::string::npos);
}

// Graphics card: the launcher's list (no Automatic row), the card a saved value selects, and the Vulkan device a
// listed card runs on.
void TestGraphicsCard() {
    using OslGraphics::GpuDevice;
    using OslGraphics::ListGpus;
    using OslGraphics::ResolveGpu;
    const std::string nvidia = "NVIDIA GeForce RTX 4070", intel = "Intel(R) UHD Graphics 770";

    // Vulkan enumerates Intel first; the ranking puts the discrete card first, as the game's default did.
    const std::vector<std::string> names = {intel, nvidia};
    const std::vector<std::size_t> order = {1, 0};
    const std::vector<std::string> listed = ListGpus(names, order);
    CHECK(listed.size() == 2);
    CHECK(listed == (std::vector<std::string>{nvidia, intel}));
    CHECK(std::none_of(listed.begin(), listed.end(), [](const std::string& name) {
        return name.empty() || name == "automatic" || name == "Automatic";
    }));

    CHECK(ResolveGpu(listed, "") == nvidia);
    CHECK(ResolveGpu(listed, "auto") == nvidia);
    CHECK(ResolveGpu(listed, "automatic") == nvidia);
    CHECK(ResolveGpu(listed, intel) == intel);
    CHECK(ResolveGpu(listed, nvidia) == nvidia);
    CHECK(ResolveGpu(listed, "AMD Radeon RX 7800 XT") == nvidia);

    // Each row runs on its own Vulkan device, with no offset from a former Automatic row.
    CHECK(GpuDevice(names, order, listed[0]) == 1);
    CHECK(GpuDevice(names, order, listed[1]) == 0);
    CHECK(names[GpuDevice(names, order, listed[0])] == listed[0]);
    CHECK(names[GpuDevice(names, order, listed[1])] == listed[1]);
    CHECK(GpuDevice(names, order, "") == -1);
    CHECK(GpuDevice(names, order, "automatic") == -1);
    CHECK(GpuDevice(names, {0, 1}, intel) == 0 && GpuDevice(names, {0, 1}, nvidia) == 1);

    // Unusable devices (empty names) are not listed; a name shared by two devices is listed once, for the better one.
    CHECK(ListGpus({"", nvidia, ""}, {1, 0, 2}) == (std::vector<std::string>{nvidia}));
    CHECK(ListGpus({nvidia, nvidia}, {1, 0}) == (std::vector<std::string>{nvidia}));
    CHECK(GpuDevice({nvidia, nvidia}, {1, 0}, nvidia) == 1);

    const std::vector<std::string> one = ListGpus({nvidia}, {0});
    CHECK(one.size() == 1);
    CHECK(ResolveGpu(one, "") == nvidia && ResolveGpu(one, intel) == nvidia);
    CHECK(GpuDevice({nvidia}, {0}, ResolveGpu(one, "")) == 0);

    CHECK(ListGpus({}, {}).empty());
    CHECK(ListGpus({"", ""}, {0, 1}).empty());
    CHECK(ListGpus({nvidia}, {3}).empty());
    CHECK(ResolveGpu({}, "") == "");
    CHECK(ResolveGpu({}, nvidia) == "");
    CHECK(GpuDevice({}, {}, nvidia) == -1);

    // osl.ini: a new file, the retired "automatic", a saved card, a removed card and Reset all select a listed card,
    // and the selected card is what is written.
    const std::filesystem::path dir = TempDir("graphics-card");
    const auto selected = [&](const char* line) {
        std::ofstream(dir / "osl.ini") << line;
        LauncherSettings settings(dir / "osl.ini");
        return ResolveGpu(listed, settings.Get(kGraphicsCardKey));
    };
    CHECK(selected("") == nvidia);
    CHECK(selected("graphics_card = automatic\n") == nvidia);
    CHECK(selected("graphics_card = auto\n") == nvidia);
    CHECK(selected("graphics_card = Intel(R) UHD Graphics 770\n") == intel);
    CHECK(selected("graphics_card = AMD Radeon RX 7800 XT\n") == nvidia);

    std::ofstream(dir / "osl.ini") << "graphics_card = automatic\n";
    {
        LauncherSettings settings(dir / "osl.ini");
        settings.Set(kGraphicsCardKey, ResolveGpu(listed, settings.Get(kGraphicsCardKey)));
        CHECK(settings.Save());
    }
    CHECK(LauncherSettings(dir / "osl.ini").Get(kGraphicsCardKey) == nvidia);
    {
        LauncherSettings settings(dir / "osl.ini");
        settings.Set(kGraphicsCardKey, intel);
        settings.ResetToDefaults();
        CHECK(settings.Get(kGraphicsCardKey) == "");
        settings.Set(kGraphicsCardKey, ResolveGpu(listed, settings.Get(kGraphicsCardKey)));
        CHECK(settings.Get(kGraphicsCardKey) == nvidia);
    }
}

std::string CapturedStdout(const std::function<void()>& run, const std::filesystem::path& file) {
    std::fflush(stdout);
    const int saved = dup(1);
    const int fd = open(file.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    dup2(fd, 1);
    close(fd);
    run();
    std::fflush(stdout);
    dup2(saved, 1);
    close(saved);
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

void TestTextLanguages() {
    const std::span<const GameBuild> builds = SupportedBuilds();
    const GameBuild& eu = builds[5];
    const GameBuild& usa = builds[0];
    const GameBuild& usa_update = builds[1];
    const GameBuild& japan = builds[3];
    const GameBuild& hk = builds[6];

    CHECK(std::string_view(hk.title_id) == "CUSA03873" && std::string_view(hk.region) == "HK");
    CHECK(hk.text_mode == TextLanguageMode::kSelectable);
    const std::span<const TextLanguage> hk_languages = TextLanguagesOf(&hk);
    CHECK(hk_languages.size() == 2);
    CHECK(hk_languages[0].key == "traditional_chinese" && hk_languages[0].name == "Traditional Chinese" &&
          hk_languages[0].ps4_id == 10);
    CHECK(hk_languages[1].key == "korean" && hk_languages[1].name == "Korean" && hk_languages[1].ps4_id == 9);
    CHECK(ResolveTextLanguage(&hk, "traditional_chinese").ps4_id == 10);
    CHECK(ResolveTextLanguage(&hk, "korean").ps4_id == 9);
    CHECK(ResolveTextLanguage(&hk, "invalid").ps4_id == 10);
    CHECK(ResolveTextLanguage(&hk, "english").ps4_id == 10);
    CHECK(ResolveTextLanguage(&hk, "french").ps4_id == 10);
    CHECK(ResolveTextLanguage(&eu, "traditional_chinese").ps4_id == 1);
    CHECK(ResolveTextLanguage(&eu, "korean").ps4_id == 1);
    for (const size_t fixed : {0, 1, 2, 3}) {
        CHECK(builds[fixed].text_mode == TextLanguageMode::kFixed && TextLanguagesOf(&builds[fixed]).empty());
    }
    CHECK(builds[4].text_mode == TextLanguageMode::kSelectable && eu.text_mode == TextLanguageMode::kSelectable);

    const std::span<const TextLanguage> languages = TextLanguagesOf(&eu);
    CHECK(languages.size() == 5);
    const char* keys[] = {"english", "french", "german", "spanish", "italian"};
    const char* names[] = {"English", "French", "German", "Spanish", "Italian"};
    const int ids[] = {1, 2, 4, 3, 5};
    for (size_t i = 0; i < languages.size(); ++i) {
        CHECK(languages[i].key == keys[i]);
        CHECK(languages[i].name == names[i]);
        CHECK(languages[i].ps4_id == ids[i]);
    }
    CHECK(TextLanguagesOf(&builds[4]).size() == 5);
    CHECK(TextLanguagesOf(&usa).empty());
    CHECK(TextLanguagesOf(&usa_update).empty());
    CHECK(TextLanguagesOf(&japan).empty());
    CHECK(TextLanguagesOf(&builds[2]).empty());
    CHECK(TextLanguagesOf(nullptr).empty());

    CHECK(ResolveTextLanguage(&eu, "french").ps4_id == 2);
    CHECK(ResolveTextLanguage(&eu, "italian").ps4_id == 5);
    CHECK(ResolveTextLanguage(&eu, "bogus").ps4_id == 1);
    CHECK(std::string(ResolveTextLanguage(&eu, "").key) == "english");
    CHECK(ResolveTextLanguage(&usa, "french").ps4_id == 1);
    CHECK(ResolveTextLanguage(&usa_update, "french").ps4_id == 1);
    CHECK(ResolveTextLanguage(&japan, "english").ps4_id == 1);
    CHECK(ResolveTextLanguage(nullptr, "spanish").ps4_id == 1);

    CHECK(IsValidSetting(kTextLanguageKey, "english"));
    CHECK(IsValidSetting(kTextLanguageKey, "italian"));
    CHECK(!IsValidSetting(kTextLanguageKey, "japanese"));
    CHECK(!IsValidSetting(kTextLanguageKey, "bogus"));

    const std::filesystem::path dir = TempDir("language");
    const auto write_image = [&](const GameBuild& build) {
        std::ofstream(dir / "image.json") << "{\n  \"title_id\": \"" << build.title_id << "\",\n  \"app_ver\": \""
                                          << FinishedLayer(build).app_ver << "\",\n  \"eboot_sha256\": \""
                                          << build.eboot_sha256 << "\"\n}\n";
    };
    const std::filesystem::path ini = dir / "osl.ini";
    write_image(eu);
    std::ofstream(ini) << "text_language = english\n";
    CHECK(osl_text_language(ini.string().c_str(), dir.string().c_str()) == 1);
    std::ofstream(ini) << "text_language = german\n";
    CHECK(osl_text_language(ini.string().c_str(), dir.string().c_str()) == 4);
    std::ofstream(ini) << "text_language = spanish\n";
    CHECK(osl_text_language(ini.string().c_str(), dir.string().c_str()) == 3);
    std::ofstream(ini) << "text_language = klingon\n";
    CHECK(osl_text_language(ini.string().c_str(), dir.string().c_str()) == 1);
    std::ofstream(ini) << "render_resolution = 720p\n";
    CHECK(osl_text_language(ini.string().c_str(), dir.string().c_str()) == 1);
    write_image(usa);
    std::ofstream(ini) << "text_language = french\n";
    CHECK(osl_text_language(ini.string().c_str(), dir.string().c_str()) == 1);
    write_image(usa_update);
    CHECK(osl_text_language(ini.string().c_str(), dir.string().c_str()) == 1);
    write_image(japan);
    CHECK(osl_text_language(ini.string().c_str(), dir.string().c_str()) == 1);
    write_image(eu);
    CHECK(osl_text_language(ini.string().c_str(), dir.string().c_str()) == 2);
    CHECK(osl_text_language(nullptr, dir.string().c_str()) == 1);
    CHECK(osl_text_language(ini.string().c_str(), nullptr) == 1);

    write_image(hk);
    const auto hk_language = [&](const char* value) {
        std::ofstream(ini) << "text_language = " << value << "\n";
        return osl_text_language(ini.string().c_str(), dir.string().c_str());
    };
    CHECK(hk_language("traditional_chinese") == 10);
    CHECK(hk_language("korean") == 9);
    CHECK(hk_language("english") == 10);
    CHECK(hk_language("klingon") == 10);
    const auto logged = [&](const char* value) {
        std::ofstream(ini) << "text_language = " << value << "\n";
        return CapturedStdout([&] { osl_log_gameplay_settings(ini.string().c_str(), dir.string().c_str()); },
                              dir / "log.txt");
    };
    const auto has_line = [](const std::string& log, const char* line) {
        return log.find(std::string(line) + "\n") != std::string::npos ||
               log.find(std::string(line) + "\r\n") != std::string::npos;
    };
    CHECK(has_line(logged("traditional_chinese"), "Gameplay: Menu & Subtitles Traditional Chinese"));
    CHECK(has_line(logged("korean"), "Gameplay: Menu & Subtitles Korean"));
    write_image(eu);
    CHECK(has_line(logged("french"), "Gameplay: Menu & Subtitles French"));
    CHECK(hk_language("korean") == 1);
    CHECK(hk_language("traditional_chinese") == 1);
    CHECK(IsValidSetting(kTextLanguageKey, "traditional_chinese"));
    CHECK(IsValidSetting(kTextLanguageKey, "korean"));
    CHECK(!IsValidSetting(kTextLanguageKey, "simplified_chinese"));

    LauncherSettings settings(dir / "other.ini");
    settings.ResetToDefaults();
    CHECK(settings.Get(kTextLanguageKey, "") == "english");
    settings.Set(kTextLanguageKey, "italian");
    CHECK(settings.Save());
    LauncherSettings reloaded(dir / "other.ini");
    CHECK(reloaded.Get(kTextLanguageKey, "") == "italian");
    std::ofstream(dir / "other.ini") << "text_language = klingon\n";
    LauncherSettings invalid(dir / "other.ini");
    CHECK(invalid.Get(kTextLanguageKey, "english") == "english");
    reloaded.ResetToDefaults();
    CHECK(reloaded.Get(kTextLanguageKey, "") == "english");
}

void TestVoicePolicy() {
    for (const ManagedSetting& setting : ManagedSettings()) {
        std::string key(setting.key);
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        CHECK(key.find("voice") == std::string::npos);
    }
    const std::span<const GameBuild> builds = SupportedBuilds();
    for (const GameBuild& eu : {builds[4], builds[5]}) {
        const auto has = [&](const char* name) {
            return std::any_of(eu.data_files.begin(), eu.data_files.end(),
                               [&](const char* file) { return std::string_view(file) == name; });
        };
        CHECK(has("Odin2_BT_UK.CPK"));
        CHECK(has("Odin2_OR_UK.CPK"));
        CHECK(has("Odin2_RE_UK.CPK"));
    }
}

void TestHash() {
    const std::filesystem::path dir = TempDir("hash");
    const std::filesystem::path file = dir / "abc.bin";
    std::ofstream(file, std::ios::binary) << "abc";
    CHECK(Sha256Hex(file) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256Hex(dir / "missing.bin").empty());
}

void TestSettingsReaders() {
    const std::filesystem::path dir = TempDir("readers");
    const GameBuild& build = SupportedBuilds()[0];
    std::ofstream(dir / "image.json") << "{\n  \"title_id\": \"" << build.title_id << "\",\n  \"app_ver\": \""
                                      << FinishedLayer(build).app_ver << "\",\n  \"eboot_sha256\": \""
                                      << build.eboot_sha256 << "\"\n}\n";
    std::ofstream(dir / "osl.ini") << "render_resolution = 1440p\naspect_ratio = 21:9\n";
    CHECK(osl_render_resolution((dir / "osl.ini").string().c_str(), dir.string().c_str()) == 3);
    CHECK(osl_aspect_ratio((dir / "osl.ini").string().c_str(), dir.string().c_str()) == 2);
    std::ofstream(dir / "osl.ini") << "render_resolution = 4K\naspect_ratio = 32:9\n";
    CHECK(osl_render_resolution((dir / "osl.ini").string().c_str(), dir.string().c_str()) == 5);
    CHECK(osl_aspect_ratio((dir / "osl.ini").string().c_str(), dir.string().c_str()) == 3);
    CHECK(osl_render_resolution((dir / "missing.ini").string().c_str(), dir.string().c_str()) == 0);
}

void TestDetailedLogging() {
    const std::filesystem::path dir = TempDir("logging");
    const std::filesystem::path file = dir / "osl.ini";
    bool managed = false;
    for (const ManagedSetting& setting : ManagedSettings()) {
        managed |= setting.key == kDetailedLoggingKey && setting.default_value == "1";
    }
    CHECK(managed);
    CHECK(LauncherSettings(file).GetBool(kDetailedLoggingKey, true));
    CHECK(osl_detailed_logging(file.string().c_str()) == 1);
    CHECK(osl_detailed_logging(nullptr) == 1);

    std::ofstream(file) << "detailed_logging = 0\n";
    CHECK(!LauncherSettings(file).GetBool(kDetailedLoggingKey, true));
    CHECK(osl_detailed_logging(file.string().c_str()) == 0);
    std::ofstream(file) << "detailed_logging = 1\n";
    CHECK(osl_detailed_logging(file.string().c_str()) == 1);
    std::ofstream(file) << "detailed_logging = true\n";
    CHECK(osl_detailed_logging(file.string().c_str()) == 1);
    std::ofstream(file) << "detailed_logging = garbage\n";
    CHECK(osl_detailed_logging(file.string().c_str()) == 1);
    std::ofstream(file) << "vsync = 0\n";
    CHECK(osl_detailed_logging(file.string().c_str()) == 1);

    {
        LauncherSettings settings(file);
        settings.SetBool(kDetailedLoggingKey, false);
        CHECK(settings.Save());
        CHECK(osl_detailed_logging(file.string().c_str()) == 0);
    }
    {
        LauncherSettings settings(file);
        CHECK(!settings.GetBool(kDetailedLoggingKey, true));
        settings.ResetToDefaults();
        CHECK(settings.Save());
    }
    CHECK(osl_detailed_logging(file.string().c_str()) == 1);
}

void TestLogsDirectory() {
    const std::filesystem::path root = TempDir("logs-directory");
    char out[4096];

    const std::filesystem::path default_dir = root / "logs";
    CHECK(osl_logs_directory(Utf8(default_dir).c_str(), nullptr, out, sizeof(out)));
    CHECK(PathFromUtf8(out) == default_dir);
    CHECK(!std::filesystem::exists(default_dir));

    const std::filesystem::path log = root / "nested" / "run.log";
    CHECK(osl_logs_directory(Utf8(default_dir).c_str(), Utf8(log).c_str(), out, sizeof(out)));
    CHECK(PathFromUtf8(out) == log.parent_path());

    CHECK(osl_logs_directory(Utf8(default_dir).c_str(), "", out, sizeof(out)));
    CHECK(PathFromUtf8(out) == default_dir);
    CHECK(!osl_logs_directory(nullptr, nullptr, out, sizeof(out)));
    CHECK(!osl_logs_directory(Utf8(default_dir).c_str(), Utf8(log).c_str(), nullptr, sizeof(out)));
    CHECK(!osl_logs_directory(Utf8(default_dir).c_str(), Utf8(log).c_str(), out, 0));

    const std::filesystem::path unicode =
        root / "logs with space" / PathFromUtf8("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
    CHECK(osl_logs_directory(Utf8(unicode).c_str(), nullptr, out, sizeof(out)));
    CHECK(PathFromUtf8(out) == unicode);
}

void TestLogRetention() {
    CHECK(runtime_log_should_remove(0, 1, 0));
    CHECK(!runtime_log_should_remove(1, 1, 0));
    CHECK(!runtime_log_should_remove(0, 0, 0));
    CHECK(!runtime_log_should_remove(0, 1, 1));

    const std::filesystem::path file = TempDir("log-retention") / "run.log";
    std::ofstream(file) << "run\n";
    CHECK(std::filesystem::exists(file));
    runtime_log_set_session_path(file.string().c_str());
    runtime_log_set_detailed(0);
    runtime_log_mark_clean_exit();
    runtime_log_finalize();
    CHECK(!std::filesystem::exists(file));

    // A new session starts unfinalized, unmarked: only its own clean exit removes it.
    const std::filesystem::path next = file.parent_path() / "next.log";
    std::ofstream(next) << "run\n";
    runtime_log_set_session_path(next.string().c_str());
    runtime_log_finalize();
    CHECK(std::filesystem::exists(next));
    runtime_log_set_session_path(next.string().c_str());
    runtime_log_mark_clean_exit();
    runtime_log_finalize();
    CHECK(!std::filesystem::exists(next));

    const std::string watchdog = ReadFile(std::filesystem::path(OSL_SOURCE_DIR) / "src" / "hang_watchdog.c");
    CHECK(watchdog.find("runtime_log_mark_crash();") != std::string::npos);
}

std::string SessionLogName(unsigned number) {
    char name[64];
    std::snprintf(name, sizeof(name), "odin_sphere_leifthrasir_%03u.log", number);
    return name;
}

void CloseLog(void* handle) {
#ifdef _WIN32
    _close(_open_osfhandle(reinterpret_cast<intptr_t>(handle), 0));
#else
    close(static_cast<int>(reinterpret_cast<intptr_t>(handle)));
#endif
}

void TestSessionLogFiles() {
    char path[4200];
    void* handle = nullptr;
    const std::filesystem::path fresh = TempDir("session-logs-fresh") / "logs";
    CHECK(runtime_log_create(Utf8(fresh).c_str(), nullptr, path, sizeof(path), &handle));
    CloseLog(handle);
    CHECK(PathFromUtf8(path) == fresh / SessionLogName(1));
    CHECK(runtime_log_create(Utf8(fresh).c_str(), nullptr, path, sizeof(path), &handle));
    CloseLog(handle);
    CHECK(PathFromUtf8(path) == fresh / SessionLogName(2));

    const std::filesystem::path dir = TempDir("session-logs");
    for (unsigned number = 1; number <= 60; ++number) {
        std::ofstream(dir / SessionLogName(number)) << "run\n";
    }
    std::ofstream(dir / "odin_sphere_leifthrasir_notes.log") << "kept\n";
    std::ofstream(dir / "trg-updater.log") << "kept\n";
    CHECK(runtime_log_create(Utf8(dir).c_str(), nullptr, path, sizeof(path), &handle));
    CloseLog(handle);
    CHECK(PathFromUtf8(path) == dir / SessionLogName(61));
    CHECK(!std::filesystem::exists(dir / SessionLogName(1)));
    CHECK(!std::filesystem::exists(dir / SessionLogName(11)));
    CHECK(std::filesystem::exists(dir / SessionLogName(12)));
    CHECK(std::filesystem::exists(dir / "odin_sphere_leifthrasir_notes.log"));
    CHECK(std::filesystem::exists(dir / "trg-updater.log"));
    const auto numbered = [&] {
        size_t count = 0;
        for (unsigned number = 1; number <= 70; ++number) {
            count += std::filesystem::exists(dir / SessionLogName(number)) ? 1 : 0;
        }
        return count;
    };
    CHECK(numbered() == RUNTIME_LOG_KEPT_RUNS);

    const std::filesystem::path given = dir / "given" / "run.log";
    CHECK(runtime_log_create(Utf8(dir).c_str(), Utf8(given).c_str(), path, sizeof(path), &handle));
    CloseLog(handle);
    CHECK(PathFromUtf8(path) == given);
    CHECK(numbered() == RUNTIME_LOG_KEPT_RUNS);
}

bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

void TestCrashReport() {
    CHECK(std::string(crash_exception_name(0xC0000005u)) == "ACCESS_VIOLATION");
    CHECK(std::string(crash_exception_name(0xC0000409u)) == "STATUS_STACK_BUFFER_OVERRUN");
    CHECK(crash_exception_name(0x12345678u) == nullptr);
    CHECK(std::string(crash_signal_name(SIGSEGV)) == "SIGSEGV");

    CrashReport report{};
    report.event = "Windows unhandled exception";
    report.version = "9.8.7";
    report.game_build = "CUSA05262 1.01 Europe";
    report.uptime_ms = 3723456;
    report.code = 0xC0000005u;
    report.address = 0x7FF612340000ull;
    report.location = "odin_sphere_leifthrasir.exe+0x1234";
    report.parameter_count = 2;
    report.parameters[0] = 1;
    report.parameters[1] = 0x10;
    report.thread_id = 77;
    report.process_id = 88;
    char text[4096];
    size_t length = crash_report_format(&report, text, sizeof(text));
    std::string block(text, length);
    CHECK(length == std::strlen(text));
    CHECK(block.rfind(CRASH_REPORT_RULE "CRASH REPORT\n" CRASH_REPORT_RULE "Windows unhandled exception\n", 0) == 0);
    CHECK(Has(block, "Product: Odin Sphere Leifthrasir PC 9.8.7\n"));
    CHECK(Has(block, "Game build: CUSA05262 1.01 Europe\n"));
    CHECK(Has(block, "Uptime: 01:02:03\n"));
    CHECK(Has(block, "Exception code: 0xC0000005 (ACCESS_VIOLATION)\n"));
    CHECK(Has(block, "Exception address: 0x00007FF612340000\n"));
    CHECK(Has(block, "Faulting module: odin_sphere_leifthrasir.exe+0x1234\n"));
    CHECK(Has(block, "Exception parameters: 2\n  parameter[0]: 0x0000000000000001\n"));
    CHECK(Has(block, "Memory access: write\nReferenced address: 0x0000000000000010\n"));
    CHECK(Has(block, "Thread ID: 77\nProcess ID: 88\n"));
    CHECK(!Has(block, "Reason:"));

    report.reason = "abort() was called";
    report.game_build = nullptr;
    report.location = nullptr;
    report.code = 0xC0000409u;
    report.parameter_count = 40;
    block.assign(text, crash_report_format(&report, text, sizeof(text)));
    CHECK(Has(block, "Reason: abort() was called\n"));
    CHECK(!Has(block, "Game build:"));
    CHECK(Has(block, "Faulting module: unknown\n"));
    CHECK(Has(block, "Exception parameters: 15\n"));
    CHECK(Has(block, "parameter[14]"));
    CHECK(!Has(block, "parameter[15]"));
    CHECK(!Has(block, "Memory access"));

    CrashReport posix{};
    posix.event = "Linux fatal signal";
    posix.signal = SIGSEGV;
    posix.signal_code = 1;
    posix.address = 0x401000;
    posix.has_referenced_address = 1;
    posix.referenced_address = 0x8;
    block.assign(text, crash_report_format(&posix, text, sizeof(text)));
    CHECK(Has(block, "Signal: 11 (SIGSEGV), code 1\nInstruction address: 0x0000000000401000\n"));
    CHECK(Has(block, "Referenced address: 0x0000000000000008\n"));
    CHECK(!Has(block, "Exception code"));
    CHECK(Has(block, "Product: Odin Sphere Leifthrasir PC unknown\n"));

    char tiny[64];
    CHECK(crash_report_format(&report, tiny, sizeof(tiny)) == sizeof(tiny) - 1);
    CHECK(tiny[sizeof(tiny) - 1] == '\0');
    CHECK(crash_report_format(&report, tiny, 0) == 0);
}

const Rect kScreen{0, 0, 1920, 1040};
const std::vector<Rect> kWorkAreas{kScreen};
const Rect kNormal{200, 100, 1480, 820};

WindowState NormalState(Rect rect) { return {rect, false, false}; }

void TestPlacementVisibility() {
    const auto visible = [](const Rect& rect) { return PlacementIsVisible(WindowPlacement{rect}, kWorkAreas); };
    CHECK(visible(kNormal));
    const std::vector<Rect> two{{-1920, 0, 0, 1040}, kScreen};
    const WindowPlacement straddling{{-600, 100, 680, 820}};
    CHECK(PlacementIsVisible(straddling, two));
    CHECK(PlacementIsVisible(straddling, kWorkAreas));
    CHECK(!visible({3000, 100, 4280, 820}));   // the second monitor was unplugged
    CHECK(!visible({-1500, 100, -220, 820}));
    CHECK(!visible({200, 1030, 1480, 1750}));  // 10 pixels left above the taskbar
    CHECK(!visible({1850, 100, 3130, 820}));   // 70 pixels wide on screen
    CHECK(!visible({0, 0, 0, 0}));
    CHECK(!visible({200, 100, 300, 150}));     // smaller than any launcher
    CHECK(!PlacementIsVisible(WindowPlacement{kNormal}, std::span<const Rect>()));

    // Stored rectangles are in the display's pixels, whatever its scale: the same window at 100%, 150% and 225%.
    const Rect at_150{300, 150, 2220, 1230};
    const Rect at_225{450, 225, 3330, 1845};
    CHECK(PlacementIsVisible(WindowPlacement{at_150}, std::vector<Rect>{{0, 0, 2880, 1560}}));
    CHECK(PlacementIsVisible(WindowPlacement{at_225}, std::vector<Rect>{{0, 0, 3840, 2100}}));
    // The resolution went down: what lay inside a 4K desktop can be outside a 1080p one.
    CHECK(!PlacementIsVisible(WindowPlacement{{2600, 1200, 3500, 1760}}, kWorkAreas));

    const WindowPlacement gone{{3000, 100, 4280, 820}, true, true};
    const WindowPlacement restored = RestorablePlacement(gone, kWorkAreas);
    CHECK(!PlacementIsSet(restored) && restored.maximized && restored.fullscreen);
    CHECK(RestorablePlacement(WindowPlacement{kNormal, true}, kWorkAreas) == (WindowPlacement{kNormal, true}));
}

void TestPlacementTracking() {
    PlacementTracker tracker;
    tracker.Observe(NormalState(kNormal), false, 0.0);
    CHECK(!tracker.TakeSettled(10.0));  // the first observation is what was opened
    const Rect moved{300, 150, 1580, 870};
    tracker.Observe(NormalState(moved), false, 11.0);
    CHECK(!tracker.TakeSettled(11.2));
    const auto settled = tracker.TakeSettled(11.6);
    CHECK(settled && settled->normal == moved && !settled->maximized);
    CHECK(!tracker.TakeSettled(20.0));
    tracker.Observe(NormalState({300, 150, 1000, 700}), false, 21.0);
    tracker.Observe(NormalState({300, 150, 1100, 750}), false, 21.3);
    CHECK(!tracker.TakeSettled(21.6));  // a second resize restarts the delay
    const auto resized = tracker.TakeSettled(21.9);
    CHECK(resized && resized->normal == (Rect{300, 150, 1100, 750}));
    tracker.Observe(NormalState({10, 10, 1290, 730}), false, 22.0);
    const auto closed = tracker.Flush();  // closing does not wait for the delay
    CHECK(closed && closed->normal == (Rect{10, 10, 1290, 730}));
    CHECK(!tracker.Flush());

    PlacementTracker maximized(WindowPlacement{kNormal});
    maximized.Observe(WindowState{kScreen, true, false}, false, 1.0);
    const auto zoomed = maximized.Flush();
    CHECK(zoomed && zoomed->maximized && zoomed->normal == kNormal);
    maximized.Observe(NormalState(kNormal), false, 2.0);
    const auto restored = maximized.Flush();
    CHECK(restored && !restored->maximized && restored->normal == kNormal);

    // SDL may drop the maximized flag of a minimized window; the minimized coordinates mean nothing either.
    PlacementTracker minimized(WindowPlacement{kNormal, true});
    minimized.Observe(WindowState{{-32000, -32000, -31840, -31972}, false, true}, false, 1.0);
    CHECK(!minimized.Flush());
    CHECK(minimized.Current()->maximized && minimized.Current()->normal == kNormal);

    PlacementTracker fullscreen(WindowPlacement{kNormal, true});
    fullscreen.Observe(WindowState{{0, 0, 3840, 2160}, false, false}, true, 1.0);
    const auto full = fullscreen.Flush();
    CHECK(full && full->fullscreen && full->maximized && full->normal == kNormal);
    fullscreen.Observe(WindowState{kScreen, true, false}, false, 2.0);
    const auto left = fullscreen.Flush();
    CHECK(left && !left->fullscreen && left->maximized && left->normal == kNormal);

    // A saved rectangle that was off-screen: the window opened at its default place, which is then kept.
    PlacementTracker recovered(RestorablePlacement(WindowPlacement{{3000, 100, 4280, 820}}, kWorkAreas));
    recovered.Observe(NormalState(kNormal), false, 0.0);
    const auto healed = recovered.TakeSettled(1.0);
    CHECK(healed && healed->normal == kNormal);
}

void TestPlacementSettings() {
    const std::filesystem::path dir = TempDir("placement");
    LauncherSettings settings(dir / "osl.ini");
    settings.Set(kDisplayModeKey, "borderless");
    settings.Set(kWindowWidthKey, "2560");
    settings.Set(kMonitorKey, "2");
    CHECK(settings.Save());

    const WindowPlacement launcher{{-1700, 80, -420, 800}, true, true};
    const Values values = PlacementValues(launcher);
    CHECK(values.size() == 6);
    for (const auto& [key, value] : values) {
        CHECK(IsLauncherPlacementSetting(key));
    }
    for (const char* game : {kDisplayModeKey, kWindowWidthKey, kWindowHeightKey, kMonitorKey}) {
        CHECK(!IsLauncherPlacementSetting(game));
        CHECK(values.find(game) == values.end());
    }
    CHECK(settings.SaveNow(values));

    LauncherSettings reloaded(dir / "osl.ini");
    CHECK(ReadPlacement(reloaded) == launcher);
    CHECK(reloaded.Get(kDisplayModeKey) == "borderless");  // the launcher's F11 is not the game's Display mode
    CHECK(reloaded.GetInt(kWindowWidthKey, 0) == 2560);
    CHECK(reloaded.GetInt(kMonitorKey, 0) == 2);
    reloaded.ResetToDefaults();
    CHECK(reloaded.Save());
    LauncherSettings reset(dir / "osl.ini");
    CHECK(reset.Get(kDisplayModeKey) == "windowed");
    CHECK(ReadPlacement(reset) == launcher);
    CHECK(!PlacementIsSet(ReadPlacement(LauncherSettings(dir / "missing.ini"))));
}

void TestHangReport() {
    HangReport report{};
    report.reason = "the game presented no frame for 15 s";
    report.version = "9.8.7";
    report.game_build = "CUSA05083 1.00 USA";
    report.os = "Windows 10.0.26200";
    report.time = "2026-10-27T13:45:12";
    report.session_log = "odin_sphere_leifthrasir_012.log";
    report.uptime_ms = 3723456;
    report.process_id = 88;
    report.frames = 4567;
    report.threads = 41;
    report.reported = 39;
    char text[4096];
    std::string block(text, hang_report_format(&report, text, sizeof(text)));
    CHECK(block.rfind(CRASH_REPORT_RULE "HANG REPORT\n" CRASH_REPORT_RULE, 0) == 0);
    CHECK(Has(block, "Reason: the game presented no frame for 15 s\n"));
    CHECK(Has(block, "Product: Odin Sphere Leifthrasir PC 9.8.7\n"));
    CHECK(Has(block, "Game build: CUSA05083 1.00 USA\nOS: Windows 10.0.26200\nTime: 2026-10-27T13:45:12\n"));
    CHECK(Has(block, "Uptime: 01:02:03\nProcess ID: 88\nSession log: odin_sphere_leifthrasir_012.log\n"));
    CHECK(Has(block, "Frames presented: 4567\nThreads: 41, 39 reported"));
    CHECK(!Has(block, "CRASH REPORT"));
    report.game_build = report.os = report.session_log = nullptr;
    block.assign(text, hang_report_format(&report, text, sizeof(text)));
    CHECK(!Has(block, "Game build:") && !Has(block, "OS:") && !Has(block, "Session log:"));

    std::vector<std::string> names;
    for (unsigned i = 0; i < HANG_REPORT_MAX_FRAMES + 10; ++i) {
        names.push_back("ntdll.dll+0x" + std::to_string(i));
    }
    std::vector<const char*> frames;
    for (const std::string& name : names) {
        frames.push_back(name.c_str());
    }
    HangThread thread{4242, "osl:window\n\"x\"", nullptr, 1, 3, frames.data()};
    std::string lines(text, hang_report_format_thread(&thread, text, sizeof(text)));
    CHECK(lines == "\nThread 4242 \"osl:window??x?\" (game window)\n  #00 ntdll.dll+0x0\n  #01 ntdll.dll+0x1\n"
                   "  #02 ntdll.dll+0x2\n");
    thread = {7, nullptr, "stack walk timed out", 0, 1, frames.data()};
    lines.assign(text, hang_report_format_thread(&thread, text, sizeof(text)));
    CHECK(lines == "\nThread 7\n  (stack walk timed out)\n  #00 ntdll.dll+0x0\n");
    thread.frame_count = static_cast<unsigned>(frames.size());
    lines.assign(text, hang_report_format_thread(&thread, text, sizeof(text)));
    CHECK(Has(lines, "  #47 ntdll.dll+0x47\n") && !Has(lines, "#48"));
    char tiny[32];
    CHECK(hang_report_format_thread(&thread, tiny, sizeof(tiny)) == sizeof(tiny) - 1 && tiny[sizeof(tiny) - 1] == '\0');
    CHECK(hang_report_format(&report, tiny, 0) == 0);

    char name[16];
    CHECK(hang_report_file_name(1, name, sizeof(name)) && std::string(name) == "hang_001.txt");
    CHECK(hang_report_file_name(1234, name, sizeof(name)) && std::string(name) == "hang_1234.txt");
    CHECK(!hang_report_file_name(1, name, 8));
    unsigned number = 0;
    CHECK(hang_report_number("hang_007.txt", &number) && number == 7);
    CHECK(hang_report_number("hang_1234.txt", &number) && number == 1234);
    CHECK(!hang_report_number("hang_007.txt.bak", &number));
    CHECK(!hang_report_number("hang_.txt", &number));
    CHECK(!hang_report_number("odin_sphere_leifthrasir_007.log", &number));
    CHECK(std::string(hang_report_base_name("\\Device\\HarddiskVolume3\\Windows\\System32\\ntdll.dll")) == "ntdll.dll");
    CHECK(std::string(hang_report_base_name("C:/Users/someone/Games/odin_sphere_leifthrasir.exe")) ==
          "odin_sphere_leifthrasir.exe");
    CHECK(std::string(hang_report_base_name("SDL3.dll")) == "SDL3.dll");
}

// Run last: it sends this process's stdout and stderr to a session log, which echoes them to the console.
void TestCrashReportReachesSessionLog() {
    char path[4200];
    CHECK(runtime_log_start(Utf8(TempDir("crash-log")).c_str(), nullptr, path, sizeof(path)));
    CrashReport report{};
    report.event = "Windows unhandled exception";
    report.version = "1.2.3";
    char text[1024];
    crash_report_format(&report, text, sizeof(text));
    std::fputs(text, stderr);
    std::fputs(CRASH_REPORT_RULE, stderr);
    runtime_log_flush();
    const std::string log = ReadFile(PathFromUtf8(path));
    CHECK(Has(log, CRASH_REPORT_RULE "CRASH REPORT\n"));
    CHECK(Has(log, "Product: Odin Sphere Leifthrasir PC 1.2.3\n"));
    CHECK(log.size() >= std::strlen(CRASH_REPORT_RULE) &&
          log.compare(log.size() - std::strlen(CRASH_REPORT_RULE), std::string::npos, CRASH_REPORT_RULE) == 0);
}

void TestBuildResolutionSites(const GameBuild& build) {
    CHECK(!build.resolution_sites.empty());
    CHECK(build.resolution_sites.size() == 17);
    CHECK(build.resolution_sites.back().packed_floats);
    CHECK(std::count_if(build.resolution_sites.begin(), build.resolution_sites.end(),
                        [](const OslResolutionSite& site) { return site.packed_floats; }) == 2);
    CHECK(build.resolution_sites.front().pixel_budget);

    size_t image_size = 0;
    for (const OslResolutionSite& site : build.resolution_sites) {
        const size_t immediate = site.packed_floats ? 8 : 4;
        image_size = std::max<size_t>(image_size, site.offset + site.opcode_length + immediate);
    }
    std::vector<unsigned char> image(image_size);
    const auto install_sites = [&] {
        for (const OslResolutionSite& site : build.resolution_sites) {
            std::memcpy(image.data() + site.offset, site.opcode, site.opcode_length);
            std::memcpy(image.data() + site.offset + site.opcode_length, &site.value, 4);
            if (site.packed_floats) {
                std::memcpy(image.data() + site.offset + site.opcode_length + 4, &site.value_high, 4);
            }
        }
    };
    const auto immediate = [&](const OslResolutionSite& site) {
        uint32_t value = 0;
        std::memcpy(&value, image.data() + site.offset + site.opcode_length, 4);
        return value;
    };
    const auto immediate_high = [&](const OslResolutionSite& site) {
        uint32_t value = 0;
        std::memcpy(&value, image.data() + site.offset + site.opcode_length + 4, 4);
        return value;
    };
    const auto float_value = [](uint32_t bits) {
        float value = 0.0f;
        std::memcpy(&value, &bits, 4);
        return value;
    };
    const auto verify = [&](const OslResolution& target) {
        for (const OslResolutionSite& site : build.resolution_sites) {
            if (site.packed_floats) {
                CHECK(float_value(immediate(site)) == float(target.width));
                CHECK(float_value(immediate_high(site)) == float(target.height));
            } else if (site.pixel_budget) {
                CHECK(immediate(site) == osl_scaled_budget(site.value, &target));
            } else {
                CHECK(immediate(site) == static_cast<uint32_t>(site.height ? target.height : target.width));
            }
        }
    };

    char error[320] = "";
    for (const int resolution : {0, 2, 3, 5}) {
        for (const int aspect : {0, 2, 4}) {
            const OslResolution target = osl_render_target(&osl_resolutions[resolution], &osl_aspect_ratios[aspect]);
            install_sites();
            CHECK(osl_apply_resolution(image.data(), image.size(), build.resolution_sites.data(),
                                       build.resolution_sites.size(), &target, error, sizeof(error)));
            verify(target);
        }
    }

    const OslResolution target = osl_render_target(&osl_resolutions[3], &osl_aspect_ratios[2]);
    install_sites();
    image[build.resolution_sites[0].offset + build.resolution_sites[0].opcode_length] ^= 0x01;
    error[0] = 0;
    CHECK(!osl_apply_resolution(image.data(), image.size(), build.resolution_sites.data(),
                                build.resolution_sites.size(), &target, error, sizeof(error)));
    CHECK(error[0] != 0);

    install_sites();
    const OslResolutionSite& packed = build.resolution_sites.back();
    image[packed.offset + packed.opcode_length + 4] ^= 0x01;
    error[0] = 0;
    CHECK(!osl_apply_resolution(image.data(), image.size(), build.resolution_sites.data(),
                                build.resolution_sites.size(), &target, error, sizeof(error)));
    CHECK(error[0] != 0);

    install_sites();
    CHECK(!osl_apply_resolution(image.data(), build.resolution_sites[0].offset, build.resolution_sites.data(),
                                build.resolution_sites.size(), &target, error, sizeof(error)));
}

void TestResolutionSites() {
    CHECK(FeaturesOf(nullptr, {}).custom_resolution);
    for (const GameBuild& build : MappedBuilds()) {
        TestBuildResolutionSites(build);
    }
}

void TestUnlockBookshelfSetting() {
    bool managed = false;
    for (const ManagedSetting& setting : ManagedSettings()) {
        managed |= setting.key == kUnlockBookshelfKey && setting.default_value == "0";
    }
    CHECK(managed);

    const std::filesystem::path dir = TempDir("unlock-bookshelf");
    const std::filesystem::path file = dir / "osl.ini";
    LauncherSettings defaults(file);
    defaults.ResetToDefaults();
    CHECK(defaults.Get(kUnlockBookshelfKey, "") == "0");
    CHECK(osl_unlock_bookshelf(file.string().c_str()) == 0);
    CHECK(osl_unlock_bookshelf(nullptr) == 0);

    {
        LauncherSettings settings(file);
        settings.SetBool(kUnlockBookshelfKey, true);
        CHECK(settings.Save());
    }
    CHECK(ReadFile(file).find("unlock_bookshelf = 1\n") != std::string::npos);
    CHECK(osl_unlock_bookshelf(file.string().c_str()) == 1);
    CHECK(osl_skip_intro(file.string().c_str()) == 0);
    {
        LauncherSettings settings(file);
        CHECK(settings.GetBool(kUnlockBookshelfKey, false));
        settings.ResetToDefaults();
        CHECK(settings.Save());
    }
    CHECK(osl_unlock_bookshelf(file.string().c_str()) == 0);
    std::ofstream(file) << "skip_intro = 1\n";
    CHECK(osl_unlock_bookshelf(file.string().c_str()) == 0);
    std::ofstream(file) << "unlock_bookshelf = on\n";
    CHECK(osl_unlock_bookshelf(file.string().c_str()) == 1);
    std::ofstream(file) << "unlock_bookshelf = garbage\n";
    CHECK(osl_unlock_bookshelf(file.string().c_str()) == 0);
}

void TestUnlockBookshelfWiring() {
    const std::filesystem::path root(OSL_SOURCE_DIR);
    const std::string launcher = ReadFile(root / "launcher" / "launcher.cpp");
    const size_t page = launcher.find("void PageGameplay(trg::Ui& ui) {");
    const size_t page_end = launcher.find("void PageControls(trg::Ui& ui) {");
    CHECK(page != std::string::npos && page_end != std::string::npos && page < page_end);
    const std::string gameplay = launcher.substr(page, page_end - page);
    const size_t skip = gameplay.find("ui.Toggle(\"Skip intro\"");
    const size_t unlock = gameplay.find(
        "ui.Toggle(\"Unlock Bookshelf\", kUnlockBookshelfHelp, kUnlockBookshelfKey, false, \"Off\", \"On\");");
    const size_t frame_rate = gameplay.find("ui.Toggle(\"Frame rate in title\"");
    CHECK(skip != std::string::npos && unlock != std::string::npos && frame_rate != std::string::npos);
    CHECK(skip < unlock && unlock < frame_rate);
    CHECK(launcher.find("\"Unlocks the Bookshelf from the start of the game and unlocks every entry, including "
                        "Prophecies, Alchemy, Notes, \"\n"
                        "    \"Cafe and Kitchen recipes, and Special artwork\";") != std::string::npos);

    // Only an On setting patches, in the pages the loader reserves for it and before the red-zone pass reads the code.
    const std::string loader = ReadFile(root / "src" / "loader.c");
    const size_t apply = loader.find("if (bookshelf_unlock)\n        apply_unlock_bookshelf(");
    const size_t redzone = loader.find("runtime_redzone_install(image,");
    const size_t reserve = loader.find("bookshelf_unlock ? round_page(OSL_BOOKSHELF_STUB_BYTES) : 0");
    CHECK(apply != std::string::npos && redzone != std::string::npos && reserve != std::string::npos);
    CHECK(reserve < apply && apply < redzone);
}

// Every real entry of the Bookshelf, by the game's slot numbers (the list in ArchiveListBookShelf.txt).
void TestBookshelfEntries() {
    struct Category {
        int first, last, count, pages;
    };
    const Category categories[] = {{0, 5, 6, 8}, {6, 31, 26, 26}, {32, 54, 23, 26}, {102, 127, 26, 26},
                                   {128, 161, 34, 34}, {162, 162, 1, 0}};
    int entries = 0;
    for (const Category& category : categories) {
        int count = 0, pages = 0;
        for (int slot = category.first; slot <= category.last; ++slot) {
            count += osl_bookshelf_entry(slot);
            pages += osl_bookshelf_pages(slot);
        }
        CHECK(count == category.count && pages == category.pages);
        entries += count;
    }
    CHECK(entries == 116);
    for (int slot = 55; slot <= 101; ++slot) {
        CHECK(!osl_bookshelf_entry(slot) && osl_bookshelf_pages(slot) == 0);
    }
    CHECK(!osl_bookshelf_entry(-1) && !osl_bookshelf_entry(OSL_BOOKSHELF_SLOTS));

    // The view opens every entry and page and keeps what the save holds.
    unsigned char unlocked[OSL_BOOKSHELF_SLOTS] = {};
    unsigned char pages[OSL_BOOKSHELF_SLOTS] = {};
    unlocked[1] = 1;
    pages[1] = 1;
    unlocked[34] = 2;
    pages[34] = 1;
    unlocked[60] = 1;
    const std::vector<unsigned char> saved_unlocked(unlocked, unlocked + OSL_BOOKSHELF_SLOTS);
    const std::vector<unsigned char> saved_pages(pages, pages + OSL_BOOKSHELF_SLOTS);
    unsigned char view[2 * OSL_BOOKSHELF_SLOTS];
    osl_bookshelf_fill_view(view, unlocked, pages);
    for (int slot = 0; slot < OSL_BOOKSHELF_SLOTS; ++slot) {
        const bool entry = osl_bookshelf_entry(slot);
        CHECK(entry ? view[slot] != 0 : view[slot] == unlocked[slot]);
        CHECK(view[OSL_BOOKSHELF_SLOTS + slot] == (entry ? osl_bookshelf_pages(slot) : pages[slot]));
    }
    CHECK(view[1] == 1 && view[34] == 2 && view[60] == 1 && view[0] == 2 && view[OSL_BOOKSHELF_SPECIAL_SLOT] == 2);
    CHECK(view[70] == 0 && view[OSL_BOOKSHELF_SLOTS + 70] == 0 && view[OSL_BOOKSHELF_SLOTS + 34] == 3);
    CHECK(std::equal(saved_unlocked.begin(), saved_unlocked.end(), unlocked));
    CHECK(std::equal(saved_pages.begin(), saved_pages.end(), pages));

    // The view is built from a save, never into it; opening only marks what the save already holds.
    std::vector<unsigned char> save(0x18610, 0);
    save[0x184c5 + 34] = 1;
    const std::vector<unsigned char> before = save;
    const auto* shown = static_cast<const unsigned char*>(osl_bookshelf_view(save.data()));
    CHECK(shown && shown[0x184c5 + 0] == 2 && shown[0x184c5 + 34] == 1 && shown[0x184c5 + 70] == 0);
    CHECK(shown[0x18568 + 34] == 3 && shown[0x18568 + OSL_BOOKSHELF_SPECIAL_SLOT] == 0);
    CHECK(osl_bookshelf_view(nullptr) == nullptr);
    CHECK(osl_bookshelf_mark(save.data(), 34) == save.data());
    CHECK(osl_bookshelf_mark(save.data(), 0) == nullptr && osl_bookshelf_mark(save.data(), 162) == nullptr);
    CHECK(osl_bookshelf_mark(save.data(), -1) == nullptr && osl_bookshelf_mark(nullptr, 34) == nullptr);
    CHECK(osl_bookshelf_mark(save.data(), OSL_BOOKSHELF_SLOTS) == nullptr);
    CHECK(save == before);
}

// A build's Bookshelf code as the patch expects it: the getter, each call with what follows, the entry count's term.
std::vector<unsigned char> BookshelfImage(const OslBookshelfSites& sites, size_t* image_size) {
    uint32_t end = std::max(sites.accessor, sites.total);
    for (const uint32_t site : sites.reads) {
        end = std::max(end, site);
    }
    for (const uint32_t site : sites.marks) {
        end = std::max(end, site);
    }
    *image_size = (uint64_t{end} | 0xfff) + 1;
    std::vector<unsigned char> image(*image_size + OSL_BOOKSHELF_STUB_BYTES, 0xcc);
    static constexpr unsigned char kGetter[22] = {0x48, 0x8d, 0x05, 0,    0,    0,    0, 0x48, 0x8b, 0x00, 0x48,
                                                  0x8b, 0xb8, 0x88, 0x01, 0x00, 0x00, 0xe9, 0, 0,    0,    0};
    std::memcpy(image.data() + sites.accessor, kGetter, sizeof(kGetter));
    const auto call = [&](uint32_t site, const unsigned char* signature) {
        const int32_t rel = static_cast<int32_t>(int64_t{sites.accessor} - int64_t{site} - 5);
        image[site] = 0xe8;
        std::memcpy(image.data() + site + 1, &rel, 4);
        std::memcpy(image.data() + site + 5, signature, OSL_BOOKSHELF_SIGNATURE_BYTES);
    };
    for (int i = 0; i < OSL_BOOKSHELF_READS; ++i) {
        call(sites.reads[i], i == 7 && sites.in_place_page_read ? osl_bookshelf_in_place_page_signature
                                                                 : osl_bookshelf_read_signatures[i]);
    }
    for (int i = 0; i < OSL_BOOKSHELF_MARKS; ++i) {
        call(sites.marks[i], osl_bookshelf_mark_signatures[i]);
    }
    std::memcpy(image.data() + sites.total, osl_bookshelf_total_code, OSL_BOOKSHELF_TOTAL_BYTES);
    return image;
}

void TestBookshelfSites() {
    for (const GameBuild& build : MappedBuilds()) {
        CHECK(build.bookshelf != nullptr);
        if (!build.bookshelf) {
            continue;
        }
        const OslBookshelfSites& sites = *build.bookshelf;
        size_t size = 0;
        const std::vector<unsigned char> original = BookshelfImage(sites, &size);
        std::vector<uint32_t> calls(sites.reads, sites.reads + OSL_BOOKSHELF_READS);
        calls.insert(calls.end(), sites.marks, sites.marks + OSL_BOOKSHELF_MARKS);
        std::sort(calls.begin(), calls.end());
        CHECK(std::adjacent_find(calls.begin(), calls.end()) == calls.end());
        CHECK(sites.in_place_page_read == (std::string_view(build.label) == "CUSA01290 1.00 Japan" ? 1 : 0));

        std::vector<unsigned char> image = original;
        unsigned char* stubs = image.data() + size;
        char error[320] = "";
        CHECK(osl_apply_bookshelf(image.data(), size, &sites, stubs, OSL_BOOKSHELF_STUB_BYTES, error, sizeof(error)));
        for (const uint32_t site : sites.reads) {
            int32_t rel = 0;
            std::memcpy(&rel, image.data() + site + 1, 4);
            CHECK(image[site] == 0xe8 && int64_t{site} + 5 + rel == static_cast<int64_t>(size));
            CHECK(std::equal(original.begin() + site + 5, original.begin() + site + 14, image.begin() + site + 5));
        }
        for (int i = 0; i < OSL_BOOKSHELF_MARKS; ++i) {
            int32_t rel = 0;
            std::memcpy(&rel, image.data() + sites.marks[i] + 1, 4);
            CHECK(int64_t{sites.marks[i]} + 5 + rel == static_cast<int64_t>(size) + 0x400 * (i + 1));
        }
        CHECK(image[sites.total] == 0xf8);
        size_t changed = 0;
        for (size_t i = 0; i < size; ++i) {
            changed += image[i] != original[i];
        }
        CHECK(changed > 0 && changed <= 16 * 4 + 7);

        // A second patch, other code at a site, a short stub area: refused with nothing written.
        const std::vector<unsigned char> once = image;
        CHECK(!osl_apply_bookshelf(image.data(), size, &sites, stubs, OSL_BOOKSHELF_STUB_BYTES, error, sizeof(error)));
        CHECK(image == once);
        const auto refused = [&](const OslBookshelfSites& changed_sites, uint32_t at, size_t stub_bytes) {
            std::vector<unsigned char> fresh = original;
            if (at) {
                fresh[at] ^= 0x01;
            }
            const std::vector<unsigned char> expected = fresh;
            char message[320] = "";
            const bool applied = osl_apply_bookshelf(fresh.data(), size, &changed_sites, fresh.data() + size,
                                                     stub_bytes, message, sizeof(message));
            return !applied && message[0] != 0 && fresh == expected;
        };
        CHECK(refused(sites, sites.accessor, OSL_BOOKSHELF_STUB_BYTES));
        CHECK(refused(sites, sites.total, OSL_BOOKSHELF_STUB_BYTES));
        CHECK(refused(sites, sites.total + 11, OSL_BOOKSHELF_STUB_BYTES));
        CHECK(refused(sites, sites.reads[0] + 5, OSL_BOOKSHELF_STUB_BYTES));
        CHECK(refused(sites, sites.reads[OSL_BOOKSHELF_READS - 1] + 13, OSL_BOOKSHELF_STUB_BYTES));
        CHECK(refused(sites, sites.marks[1] + 1, OSL_BOOKSHELF_STUB_BYTES));
        CHECK(refused(sites, sites.marks[1] + 13, OSL_BOOKSHELF_STUB_BYTES));
        CHECK(refused(sites, 0, OSL_BOOKSHELF_STUB_BYTES - 1));
        OslBookshelfSites moved = sites;
        moved.reads[3] += 1;
        CHECK(refused(moved, 0, OSL_BOOKSHELF_STUB_BYTES));
        moved = sites;
        moved.in_place_page_read = !sites.in_place_page_read;
        CHECK(refused(moved, 0, OSL_BOOKSHELF_STUB_BYTES));
        moved = sites;
        moved.total = static_cast<uint32_t>(size);
        CHECK(refused(moved, 0, OSL_BOOKSHELF_STUB_BYTES));
    }
}

// The snapshot scale patch rewrites the Text Archive's 1080p factor 1.5 with the target's width / 1280 and height / 720
// and refuses anything else without writing.
void TestSnapshotScale() {
    static constexpr unsigned char kWindow[45] = {
        0xc5, 0xfa, 0x10, 0x0d, 0, 0, 0, 0, 0x48, 0x8d, 0x05, 0, 0, 0, 0, 0xbf, 0, 0, 0, 0, 0xc5, 0xf8, 0x28, 0xd1,
        0xc5, 0xfa, 0x10, 0x28, 0xc5, 0xfa, 0x10, 0x48, 0x04, 0xc5, 0xe2, 0x59, 0xda, 0xc5, 0xca, 0x59, 0xf2, 0xc5,
        0xc2, 0x59, 0xfa};
    constexpr uint32_t kFactor = 0x300, kPair = 0x340;
    const auto make_image = [&](uint32_t site) {
        std::vector<unsigned char> image(site + 0x400);
        std::memcpy(image.data() + site, kWindow, sizeof(kWindow));
        const float factor = 1.5f;
        std::memcpy(image.data() + site + kFactor, &factor, 4);
        const int32_t factor_disp = kFactor - 8, pair_disp = kPair - 15;
        std::memcpy(image.data() + site + 4, &factor_disp, 4);
        std::memcpy(image.data() + site + 11, &pair_disp, 4);
        return image;
    };
    char error[320] = "";
    for (const GameBuild& build : MappedBuilds()) {
        const uint32_t site = build.snapshot_scale_site;
        CHECK(site != 0);
        for (const int resolution : {0, 1, 2, 3, 5}) {
            for (const int aspect : {0, 2, 4}) {
                const OslResolution target =
                    osl_render_target(&osl_resolutions[resolution], &osl_aspect_ratios[aspect]);
                std::vector<unsigned char> image = make_image(site);
                CHECK(osl_apply_snapshot_scale(image.data(), image.size(), site, &target, error, sizeof(error)));
                float x = 0.0f, y = 0.0f;
                std::memcpy(&x, image.data() + site + 1, 4);
                std::memcpy(&y, image.data() + site + 10, 4);
                CHECK(x == float(target.width) / 1280.0f && y == float(target.height) / 720.0f);
                int32_t pair_disp = 0;
                std::memcpy(&pair_disp, image.data() + site + 36, 4);
                CHECK(site + 40 + pair_disp == site + kPair);
                if (resolution == 0 && aspect == 0) {
                    CHECK(x == 1.5f && y == 1.5f);
                }
                const std::vector<unsigned char> patched = image;
                CHECK(!osl_apply_snapshot_scale(image.data(), image.size(), site, &target, error, sizeof(error)));
                CHECK(image == patched);
            }
        }

        const OslResolution target = osl_render_target(&osl_resolutions[3], &osl_aspect_ratios[0]);
        const std::vector<unsigned char> original = make_image(site);
        const auto rejected = [&](std::vector<unsigned char> image, uint32_t at, uint64_t size) {
            const std::vector<unsigned char> before = image;
            error[0] = 0;
            return !osl_apply_snapshot_scale(image.data(), size, at, &target, error, sizeof(error)) && error[0] &&
                   image == before;
        };
        std::vector<unsigned char> changed = original;
        changed[site + 16] ^= 1;
        CHECK(rejected(changed, site, changed.size()));
        changed = original;
        const float other = 1.25f;
        std::memcpy(changed.data() + site + kFactor, &other, 4);
        CHECK(rejected(changed, site, changed.size()));
        CHECK(rejected(std::vector<unsigned char>(original.size()), site, original.size()));
        CHECK(rejected(original, site + 1, original.size()));
        CHECK(rejected(original, site, site + 20));
    }
}

struct PixelSpan {
    int x0, y0, x1, y1; ///< pixel centres inside [x0, x1) x [y0, y1)
};

// Pixels whose centres lie inside a rectangle given in render-target pixels.
PixelSpan PixelsInside(float x0, float y0, float x1, float y1) {
    const auto first = [](float edge) { return int(std::ceil(edge - 0.5f)); };
    return {first(x0), first(y0), first(x1), first(y1)};
}

// The game's partial clear: its quad's corners, in render-target pixels, divided by the screen size of its clear
// constant and clamped to [0, 1], over a viewport covering the whole target.
PixelSpan ClearedPixels(float x0, float y0, float x1, float y1, float screen_w, float screen_h, int width,
                        int height) {
    const auto to_target = [](float p, float screen, int extent) {
        return std::clamp(p / screen, 0.0f, 1.0f) * float(extent);
    };
    return PixelsInside(to_target(x0, screen_w, width), to_target(y0, screen_h, height),
                        to_target(x1, screen_w, width), to_target(y1, screen_h, height));
}

// A reused offscreen target: each scene clears the rectangle it composes, draws into it and later samples exactly
// that rectangle. Returns the pixels of that rectangle still holding an earlier scene's content.
struct ReusedTarget {
    int width, height;
    std::vector<uint8_t> owner; ///< the scene that last wrote each pixel; 1 is content older than every scene

    ReusedTarget(int w, int h) : width{w}, height{h}, owner(size_t(w) * h, 1) {}

    long Scene(uint8_t scene, const float (&native_rect)[4], float screen_w, float screen_h) {
        const float sx = float(width) / 1920.0f, sy = float(height) / 1080.0f;
        const float x0 = native_rect[0] * sx, y0 = native_rect[1] * sy;
        const float x1 = native_rect[2] * sx, y1 = native_rect[3] * sy;
        const PixelSpan cleared = ClearedPixels(x0, y0, x1, y1, screen_w, screen_h, width, height);
        for (int y = std::max(cleared.y0, 0); y < std::min(cleared.y1, height); ++y) {
            for (int x = std::max(cleared.x0, 0); x < std::min(cleared.x1, width); ++x) {
                owner[size_t(y) * width + x] = 0;
            }
        }
        const PixelSpan sampled = PixelsInside(x0, y0, x1, y1);
        long stale = 0;
        for (int y = std::max(sampled.y0, 0); y < std::min(sampled.y1, height); ++y) {
            for (int x = std::max(sampled.x0, 0); x < std::min(sampled.x1, width); ++x) {
                uint8_t& pixel = owner[size_t(y) * width + x];
                stale += pixel != 0 && pixel != scene;
                pixel = scene;
            }
        }
        return stale;
    }
};

const OslResolutionSite& ClearScreenSite(const GameBuild& build) {
    return build.resolution_sites.back();
}

// The clear constant as the patch leaves it for a target, read back from a synthetic image.
std::array<float, 2> PatchedClearScreen(const GameBuild& build, const OslResolution& target) {
    const OslResolutionSite& site = ClearScreenSite(build);
    std::vector<unsigned char> image;
    for (const OslResolutionSite& s : build.resolution_sites) {
        image.resize(std::max<size_t>(image.size(), s.offset + s.opcode_length + (s.packed_floats ? 8 : 4)));
    }
    for (const OslResolutionSite& s : build.resolution_sites) {
        std::memcpy(image.data() + s.offset, s.opcode, s.opcode_length);
        std::memcpy(image.data() + s.offset + s.opcode_length, &s.value, 4);
        if (s.packed_floats) {
            std::memcpy(image.data() + s.offset + s.opcode_length + 4, &s.value_high, 4);
        }
    }
    char error[320] = "";
    if (target.width != 1920 || target.height != 1080) {
        CHECK(osl_apply_resolution(image.data(), image.size(), build.resolution_sites.data(),
                                   build.resolution_sites.size(), &target, error, sizeof(error)));
    }
    std::array<float, 2> screen{};
    std::memcpy(screen.data(), image.data() + site.offset + site.opcode_length, 8);
    return screen;
}

// Scenes reusing one offscreen target (title, ATLUS logo, a corner panel) compose only what they cleared: no pixel
// of an earlier scene survives in what a later one samples, at any render size, up to the four edges.
void TestPartialClearCoverage() {
    const GameBuild build = MappedBuilds().back();
    const OslResolutionSite& site = ClearScreenSite(build);
    CHECK(site.opcode_length == 2 && site.opcode[0] == 0x48 && site.opcode[1] == 0xb9);
    const float title[4] = {0, 0, 1920, 1080};
    const float logo[4] = {388, 356, 1524, 731};
    const float panel[4] = {100, 50, 700, 400};
    const std::pair<int, int> aspects[] = {{0, 0}, {2, 0}, {3, 0}, {5, 0}, {3, 2}};
    for (const auto& [resolution, aspect] : aspects) {
        const OslResolution target = osl_render_target(&osl_resolutions[resolution], &osl_aspect_ratios[aspect]);
        const auto [screen_w, screen_h] = PatchedClearScreen(build, target);
        CHECK(screen_w == float(target.width) && screen_h == float(target.height));
        ReusedTarget rt(target.width, target.height);
        CHECK(rt.Scene(2, title, screen_w, screen_h) == 0);
        CHECK(rt.owner.front() == 2 && rt.owner.back() == 2);
        CHECK(rt.owner[size_t(target.width) - 1] == 2 && rt.owner[size_t(target.height - 1) * target.width] == 2);
        CHECK(rt.Scene(3, logo, screen_w, screen_h) == 0);
        CHECK(rt.Scene(4, panel, screen_w, screen_h) == 0);
        CHECK(rt.Scene(5, title, screen_w, screen_h) == 0);
    }

    // Left at 1920x1080 the clear quad grows with the target from the top left corner: native is unaffected, a
    // 1440p logo scene samples title pixels above and left of what it cleared.
    ReusedTarget native(1920, 1080);
    CHECK(native.Scene(2, title, 1920, 1080) == 0);
    CHECK(native.Scene(3, logo, 1920, 1080) == 0);
    ReusedTarget wqhd(2560, 1440);
    CHECK(wqhd.Scene(2, title, 1920, 1080) == 0);
    CHECK(wqhd.Scene(3, logo, 1920, 1080) > 0);
    const PixelSpan unpatched = ClearedPixels(388 * 4 / 3.0f, 356 * 4 / 3.0f, 1524 * 4 / 3.0f, 731 * 4 / 3.0f, 1920,
                                              1080, 2560, 1440);
    CHECK(unpatched.x0 == 690 && unpatched.y0 == 633);
}

// The game's 64 MiB render buffer pool holds three offscreen color+depth targets and the snapshot's color target, all
// of the render size (sizes from the game's own Gnm surface code).
void TestRenderBufferBudget() {
    constexpr uint32_t kNativePool = 0x4000000;
    struct Target {
        int width, height;
        uint64_t color, depth;
    };
    const Target targets[] = {
        {1920, 1080, 8355840, 9437184},   {2560, 1440, 15073280, 15728640}, {3200, 1800, 23756800, 25559040},
        {3840, 2160, 33423360, 33423360}, {5120, 1440, 30146560, 31457280}, {4384, 1880, 34406400, 35389440},
    };
    for (const Target& t : targets) {
        const OslResolution target{"test", "test", t.width, t.height};
        const uint32_t pool = osl_scaled_budget(kNativePool, &target);
        CHECK(pool >= 3 * (t.color + t.depth) + t.color);
        CHECK(pool % (2u << 20) == 0);
    }
    const OslResolution native{"native", "native", 1920, 1080};
    CHECK(osl_scaled_budget(kNativePool, &native) == kNativePool);
    const OslResolution wqhd{"1440p", "1440p", 2560, 1440};
    CHECK(osl_scaled_budget(kNativePool, &wqhd) == 119537664u);
    const OslResolution smaller[] = {{"720p", "720p", 1280, 720}, {"480p", "480p", 854, 480}};
    for (const OslResolution& target : smaller) {
        CHECK(osl_scaled_budget(kNativePool, &target) == kNativePool);
    }
}

void TestSkipIntroSetting() {
    bool managed = false;
    for (const ManagedSetting& setting : ManagedSettings()) {
        managed |= setting.key == kSkipIntroKey && setting.default_value == "0";
    }
    CHECK(managed);

    const std::filesystem::path dir = TempDir("skip-intro");
    const std::filesystem::path file = dir / "osl.ini";
    LauncherSettings defaults(file);
    defaults.ResetToDefaults();
    CHECK(defaults.Get(kSkipIntroKey, "") == "0");
    CHECK(osl_skip_intro(file.string().c_str()) == 0);
    CHECK(osl_skip_intro(nullptr) == 0);

    {
        LauncherSettings settings(file);
        settings.SetBool(kSkipIntroKey, true);
        CHECK(settings.Save());
    }
    CHECK(ReadFile(file).find("skip_intro = 1\n") != std::string::npos);
    CHECK(osl_skip_intro(file.string().c_str()) == 1);
    {
        LauncherSettings settings(file);
        CHECK(settings.GetBool(kSkipIntroKey, false));
        settings.ResetToDefaults();
        CHECK(settings.Save());
    }
    CHECK(osl_skip_intro(file.string().c_str()) == 0);
    std::ofstream(file) << "skip_intro = on\n";
    CHECK(osl_skip_intro(file.string().c_str()) == 1);
    std::ofstream(file) << "skip_intro = garbage\n";
    CHECK(osl_skip_intro(file.string().c_str()) == 0);
}

void TestSkipIntroWiring() {
    const std::filesystem::path root(OSL_SOURCE_DIR);
    const std::string launcher = ReadFile(root / "launcher" / "launcher.cpp");
    const size_t page = launcher.find("void PageGameplay(trg::Ui& ui) {");
    const size_t page_end = launcher.find("void PageControls(trg::Ui& ui) {");
    CHECK(page != std::string::npos && page_end != std::string::npos && page < page_end);
    const std::string gameplay = launcher.substr(page, page_end - page);
    const size_t languages = gameplay.find("ui.Combo(\"Menu & Subtitles\"");
    const size_t skip = gameplay.find("ui.Toggle(\"Skip intro\", kSkipIntroHelp, kSkipIntroKey, false, \"Off\", \"On\");");
    const size_t frame_rate = gameplay.find("ui.Toggle(\"Frame rate in title\"");
    CHECK(languages != std::string::npos && skip != std::string::npos && frame_rate != std::string::npos);
    CHECK(languages < skip && skip < frame_rate);
    CHECK(launcher.find("kSkipIntroHelp[] = \"Starts directly at the title screen") != std::string::npos);

    // Only an On setting patches, and before the red-zone pass reads the guest code.
    const std::string loader = ReadFile(root / "src" / "loader.c");
    const size_t apply = loader.find("if (osl_skip_intro(settings_file)) apply_skip_intro(");
    const size_t redzone = loader.find("runtime_redzone_install(image,");
    CHECK(apply != std::string::npos && redzone != std::string::npos && apply < redzone);
}

// The page that holds the stubs, as the loader maps it: the code's last page.
uint64_t SkipIntroCodeEnd(const OslSkipIntroSites& sites) {
    return (uint64_t{sites.stubs} | 0xfff) + 1;
}

// A build's original code around every Skip intro site, int3 elsewhere and zeros after the code.
std::vector<unsigned char> SkipIntroImage(const OslSkipIntroSites& sites) {
    std::vector<unsigned char> image(SkipIntroCodeEnd(sites), 0xcc);
    const auto install = [&](const OslSkipIntroCode& code, uint32_t site) {
        for (size_t i = 0; i < code.count; ++i) {
            image[site - code.site + i] = code.bytes[i] < 0 ? 0 : static_cast<unsigned char>(code.bytes[i]);
        }
    };
    install(osl_skip_intro_notice_code, sites.notice_scene);
    install(osl_skip_intro_hd_title_code, sites.hd_title_check);
    install(osl_skip_intro_classic_title_code, sites.classic_title_check);
    const int32_t call = static_cast<int32_t>(int64_t{sites.classic_button} - int64_t{sites.classic_title_check} - 5);
    std::memcpy(image.data() + sites.classic_title_check + 1, &call, 4);
    std::fill(image.begin() + sites.stubs, image.end(), 0);
    return image;
}

std::vector<unsigned char> ExpectedSkipIntroStub(uint64_t claim, const std::vector<unsigned char>& jump) {
    std::vector<unsigned char> stub = {0x57, 0x56, 0x50, 0x48, 0xb8};
    for (int i = 0; i < 8; ++i) {
        stub.push_back(static_cast<unsigned char>(claim >> (8 * i)));
    }
    stub.insert(stub.end(), {0xff, 0xd0, 0x89, 0xc1, 0x58, 0x5e, 0x5f, 0x85, 0xc9, 0x75});
    stub.push_back(static_cast<unsigned char>(jump.size()));
    stub.insert(stub.end(), jump.begin(), jump.end());
    stub.insert(stub.end(), {0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3});
    return stub;
}

int64_t Rel32At(const std::vector<unsigned char>& image, uint64_t at) {
    int32_t value = 0;
    std::memcpy(&value, image.data() + at, 4);
    return value;
}

void TestBuildSkipIntro(const GameBuild& build) {
    CHECK(build.skip_intro != nullptr);
    if (!build.skip_intro) {
        return;
    }
    const OslSkipIntroSites& sites = *build.skip_intro;
    CHECK(sites.stubs % 16 == 0);
    CHECK((sites.stubs & 0xfff) != 0 && (sites.stubs & 0xfff) + OSL_SKIP_INTRO_STUB_BYTES <= 0x1000);

    const std::filesystem::path dir = TempDir("skip-intro-build");
    std::ofstream(dir / "image.json") << "{\n  \"title_id\": \"" << build.title_id << "\",\n  \"app_ver\": \""
                                      << FinishedLayer(build).app_ver << "\",\n  \"eboot_sha256\": \""
                                      << build.eboot_sha256 << "\"\n}\n";
    OslGameBuild loaded{};
    CHECK(osl_game_build(dir.string().c_str(), &loaded) && loaded.skip_intro == build.skip_intro &&
          loaded.bookshelf == build.bookshelf);

    // The Skip intro sites, their stubs and the render resolution sites share no byte.
    struct Range {
        uint64_t begin, end;
    };
    const auto window = [](const OslSkipIntroCode& code, uint32_t site) {
        return Range{site - code.site, site - code.site + code.count};
    };
    std::vector<Range> ranges = {window(osl_skip_intro_notice_code, sites.notice_scene),
                                 window(osl_skip_intro_hd_title_code, sites.hd_title_check),
                                 window(osl_skip_intro_classic_title_code, sites.classic_title_check),
                                 {sites.stubs, sites.stubs + OSL_SKIP_INTRO_STUB_BYTES}};
    for (const OslResolutionSite& site : build.resolution_sites) {
        ranges.push_back({site.offset, site.offset + site.opcode_length + (site.packed_floats ? 8u : 4u)});
    }
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < ranges.size(); ++j) {
            CHECK(i == j || ranges[i].end <= ranges[j].begin || ranges[j].end <= ranges[i].begin);
        }
    }

    const uint64_t claim = 0x0123456789abcdefull;
    const uint64_t code_end = SkipIntroCodeEnd(sites);
    const std::vector<unsigned char> original = SkipIntroImage(sites);
    std::vector<unsigned char> image = original;
    char error[320] = "";
    CHECK(osl_apply_skip_intro(image.data(), code_end, &sites, claim, error, sizeof(error)));

    // The notice task goes from its checks to its last scene; each title's first input check calls a stub that asks
    // the claim, then answers as a press or makes the original check.
    CHECK(image[sites.notice_scene] == 0xbe && image[sites.notice_scene + 1] == 3);
    const uint64_t hd_stub = sites.stubs, classic_stub = sites.stubs + 48;
    CHECK(image[sites.hd_title_check] == 0xe8 && image[sites.hd_title_check + 5] == 0x90);
    CHECK(sites.hd_title_check + 5 + Rel32At(image, sites.hd_title_check + 1) == int64_t(hd_stub));
    CHECK(image[sites.classic_title_check] == 0xe8);
    CHECK(sites.classic_title_check + 5 + Rel32At(image, sites.classic_title_check + 1) == int64_t(classic_stub));
    const std::vector<unsigned char> hd = ExpectedSkipIntroStub(claim, {0xff, 0xa0, 0x48, 0x02, 0x00, 0x00});
    CHECK(std::equal(hd.begin(), hd.end(), image.begin() + hd_stub));
    std::vector<unsigned char> to_button = {0xe9, 0, 0, 0, 0};
    const int32_t button = static_cast<int32_t>(int64_t{sites.classic_button} - int64_t(classic_stub + 24 + 5));
    std::memcpy(to_button.data() + 1, &button, 4);
    const std::vector<unsigned char> classic = ExpectedSkipIntroStub(claim, to_button);
    CHECK(std::equal(classic.begin(), classic.end(), image.begin() + classic_stub));
    const auto patched = [&](uint64_t at) {
        return at == sites.notice_scene + 1 || (at >= sites.hd_title_check && at < sites.hd_title_check + 6) ||
               (at > sites.classic_title_check && at < sites.classic_title_check + 5) ||
               (at >= sites.stubs && at < sites.stubs + OSL_SKIP_INTRO_STUB_BYTES);
    };
    bool kept = true;
    for (uint64_t i = 0; i < image.size(); ++i) {
        kept = kept && (patched(i) || image[i] == original[i]);
    }
    CHECK(kept);

    // A second patch, any other code around a site, another target of the Classic check, used stub space or stub
    // space past the code's mapping: refused, and nothing is written.
    const std::vector<unsigned char> once = image;
    CHECK(!osl_apply_skip_intro(image.data(), code_end, &sites, claim, error, sizeof(error)) && image == once);
    const auto refused = [&](uint64_t at, unsigned char value, uint64_t end) {
        std::vector<unsigned char> changed = original;
        changed[at] = value;
        char message[320] = "";
        const bool applied = osl_apply_skip_intro(changed.data(), end, &sites, claim, message, sizeof(message));
        changed[at] = original[at];
        return !applied && message[0] != 0 && changed == original;
    };
    const auto first = [](const OslSkipIntroCode& code, uint32_t site) { return uint64_t{site} - code.site; };
    const auto last = [&](const OslSkipIntroCode& code, uint32_t site) { return first(code, site) + code.count - 1; };
    CHECK(refused(sites.notice_scene + 1, 3, code_end));
    CHECK(refused(first(osl_skip_intro_notice_code, sites.notice_scene), 0x90, code_end));
    CHECK(refused(last(osl_skip_intro_notice_code, sites.notice_scene), 0x90, code_end));
    CHECK(refused(sites.hd_title_check + 2, 0x50, code_end));
    CHECK(refused(first(osl_skip_intro_hd_title_code, sites.hd_title_check), 0x90, code_end));
    CHECK(refused(last(osl_skip_intro_hd_title_code, sites.hd_title_check), 0x01, code_end));
    CHECK(refused(sites.classic_title_check, 0x90, code_end));
    CHECK(refused(sites.classic_title_check + 1, original[sites.classic_title_check + 1] ^ 1, code_end));
    CHECK(refused(first(osl_skip_intro_classic_title_code, sites.classic_title_check), 0x90, code_end));
    CHECK(refused(sites.stubs + 40, 0xcc, code_end));
    CHECK(refused(sites.stubs, 0, sites.stubs + OSL_SKIP_INTRO_STUB_BYTES - 1));
}

void TestSkipIntro() {
    const OslSkipIntroCode& notice = osl_skip_intro_notice_code;
    CHECK(notice.bytes[notice.site] == 0xbe && notice.bytes[notice.site + 1] == 0x02);
    const OslSkipIntroCode& hd = osl_skip_intro_hd_title_code;
    CHECK(hd.bytes[hd.site] == 0xff && hd.bytes[hd.site + 1] == 0x90 && hd.bytes[hd.site + 2] == 0x48 &&
          hd.bytes[hd.site + 3] == 0x02);
    const OslSkipIntroCode& classic = osl_skip_intro_classic_title_code;
    CHECK(classic.bytes[classic.site] == 0xe8 && classic.bytes[classic.site + 1] < 0);
    for (const GameBuild& build : MappedBuilds()) {
        TestBuildSkipIntro(build);
    }

    // One skip per run: the first title intro after the patch, never a later one.
    CHECK(osl_skip_intro_claim() == 0);
    osl_skip_intro_arm();
    CHECK(osl_skip_intro_claim() == 1);
    CHECK(osl_skip_intro_claim() == 0);
    CHECK(osl_skip_intro_claim() == 0);
}

// ---- Controller prompts: block patches on synthetic CPK archives (from Dragon's Crown Pro PC) ----


void TestControllerPromptsSetting() {
    CHECK(ParsePromptStyle("xbox") == PromptStyle::kXbox && ParsePromptStyle("playstation") == PromptStyle::kPlayStation);
    CHECK(!ParsePromptStyle("Xbox") && !ParsePromptStyle(""));
    CHECK(std::string(PromptStyleKey(PromptStyle::kXbox)) == "xbox" && std::string(PromptStyleLabel(PromptStyle::kPlayStation)) == "PlayStation");
    CHECK(IsValidSetting(kControllerPromptsKey, "xbox") && IsValidSetting(kControllerPromptsKey, "playstation"));
    CHECK(!IsValidSetting(kControllerPromptsKey, "nintendo"));
    bool managed = false;
    for (const ManagedSetting& setting : ManagedSettings()) {
        managed |= setting.key == kControllerPromptsKey && setting.default_value == kPromptsPlayStation;
    }
    CHECK(managed);

    const std::filesystem::path dir = TempDir("prompts_setting");
    LauncherSettings fresh(dir / "osl.ini");
    CHECK(fresh.Get(kControllerPromptsKey, kPromptsPlayStation) == "playstation");
    CHECK(fresh.SaveNow({{kControllerPromptsKey, kPromptsXbox}}));
    LauncherSettings reloaded(dir / "osl.ini");
    CHECK(reloaded.Get(kControllerPromptsKey, kPromptsPlayStation) == "xbox");
    reloaded.ResetToDefaults();
    CHECK(reloaded.Get(kControllerPromptsKey, kPromptsPlayStation) == "xbox");
    std::ofstream(dir / "invalid.ini") << "controller_prompts = nintendo\n";
    LauncherSettings invalid(dir / "invalid.ini");
    CHECK(ParsePromptStyle(invalid.Get(kControllerPromptsKey, kPromptsPlayStation)) == PromptStyle::kPlayStation);
}

// Every button art file of every supported build prepared under game/prepared must resolve to exactly one target of the
// shipped patch, as the original copy or (in an archive already patched) as the patched one. Each is then matched again
// with its own target removed: when other targets take it by source blocks, what they would write must be exactly
// what its own target writes. Builds that are not prepared are skipped.
void CheckShippedPatchCoversLocalBuilds(const BlockPatch& patch) {
    using namespace cpk_test;
    static const char* const kArt[] = {"Interface.ftx", "GUI/HD_Interface.ftx", "OnMemory.ftx", "OnMemory/HD_OnMemory.ftx"};
    size_t builds = 0, assets = 0, by_blocks = 0, refused = 0;
    std::string absent;
    for (const GameBuild& build : SupportedBuilds()) {
        const fs::path app0 = fs::path(OSL_SOURCE_DIR) / "game" / "prepared" / build.folder;
        std::error_code ec;
        if (!fs::is_directory(app0, ec)) {
            absent += std::string(absent.empty() ? "" : ", ") + build.label;
            continue;
        }
        bool covered = true;
        for (const fs::path& archive : PromptArchives(app0, build, patch)) {
            CpkArchive pack;
            if (!pack.Open(archive)) {
                continue;
            }
            for (const char* name : kArt) {
                const CpkEntry* entry = pack.Find(name);
                if (!entry) {
                    continue;
                }
                const std::vector<uint8_t> data = pack.Read(*entry);
                const uint32_t crc = Crc32(data);
                size_t matches = 0;
                for (const BlockPatchTarget& target : patch.targets) {
                    matches += target.name == entry->path && target.size == data.size() &&
                               (target.crc_before == crc || target.crc_after == crc);
                }
                const EntryMatch match = MatchEntry(patch, entry->path, data, crc);
                ++assets;
                if (matches != 1 || match.state == EntryState::kUnknown || !match.exact) {
                    covered = false;
                    std::printf("prompts coverage: %zu target(s) for build %s, archive %s, entry %s, size %zu, CRC32 %08x\n",
                                matches, build.label, archive.filename().string().c_str(), entry->path.c_str(), data.size(), crc);
                    continue;
                }
                std::vector<uint8_t> original = data;
                if (match.state == EntryState::kPatched) {
                    for (const BlockRun& run : match.target->runs) std::memcpy(original.data() + run.offset, run.original.data(), 16);
                }
                CHECK(Crc32(original) == match.target->crc_before);
                BlockPatch others;
                for (const BlockPatchTarget& target : patch.targets) {
                    if (&target != match.target) others.targets.push_back(target);
                }
                const EntryMatch fallback = MatchEntry(others, entry->path, original, match.target->crc_before);
                if (fallback.state != EntryState::kOriginal) {
                    ++refused;
                    CHECK(!fallback.analysis.empty());
                    continue;
                }
                std::vector<uint8_t> written = original;
                for (const BlockRun& run : fallback.target->runs) std::memcpy(written.data() + run.offset, run.data.data(), 16);
                CHECK(!fallback.exact && Crc32(written) == match.target->crc_after);
                ++by_blocks;
            }
        }
        CHECK(covered);
        ++builds;
    }
    std::printf("Controller prompts coverage with local game data: %zu build(s), %zu asset(s) resolved by their own target; "
                "%zu also by other targets' source blocks (same bytes written), %zu refused without their own target; "
                "not prepared: %s\n",
                builds, assets, by_blocks, refused, absent.empty() ? "none" : absent.c_str());
}

// The shipped patch names every copy of the button art the supported builds hold, by its CRC32: the Japanese and USA
// copies, the European languages' (DE, ES, FR, IT, UK) and the 1.01 update's, in the Classic and Refined archives.
void TestShippedPromptsPatch() {
    using namespace cpk_test;
    const std::string shipped = ReadFile(std::filesystem::path(OSL_SOURCE_DIR) / "launcher" / "prompts" / "xbox-prompts.dcpx");
    std::string error;
    const std::optional<BlockPatch> patch = ParseBlockPatch(Span(shipped), &error);
    CHECK(patch && patch->targets.size() == 30);
    if (!patch) {
        return;
    }
    const uint32_t interface_copies[] = {0x3fe97430, 0xe47e29e5, 0xec90f204, 0xfd05f64f, 0xff340ee4, 0x97dd55aa,
                                         0xfe6f717c, 0x7165f61b, 0xdaa34f6f, 0x748043f1, 0x40eb3501, 0x3aa386c2};
    const uint32_t onmemory_copies[] = {0xef5decc1, 0xb949a3bd, 0x3dca4e69};
    const auto find = [&](const char* name, uint32_t crc) -> const BlockPatchTarget* {
        for (const BlockPatchTarget& target : patch->targets) {
            if (target.name == name && target.crc_before == crc) return &target;
        }
        return nullptr;
    };
    for (const uint32_t crc : interface_copies) {
        const BlockPatchTarget* classic = find("Interface.ftx", crc);
        const BlockPatchTarget* refined = find("GUI/HD_Interface.ftx", crc);
        CHECK(classic && refined);
        if (classic && refined) {
            CHECK(classic->runs.size() > 3000 && classic->runs.size() < 4000);
            CHECK(classic->crc_after == refined->crc_after && classic->runs.size() == refined->runs.size());
            CHECK(classic->crc_after != classic->crc_before);
        }
    }
    for (const uint32_t crc : onmemory_copies) {
        const BlockPatchTarget* classic = find("OnMemory.ftx", crc);
        CHECK(classic && find("OnMemory/HD_OnMemory.ftx", crc));
        CHECK(classic && classic->runs.size() > 500 && classic->runs.size() < 1000);
    }
    CHECK(!ParseBlockPatch(Span(shipped.substr(0, 100)), &error) && error.find("truncated") != std::string::npos);
    CHECK(!ParseBlockPatch(Span(std::string("hello")), &error));

    std::set<std::tuple<std::string, uint32_t, uint32_t>> originals;
    for (const BlockPatchTarget& target : patch->targets) {
        std::string lower = target.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        CHECK(originals.emplace(lower, target.size, target.crc_before).second);
        CHECK(target.crc_before != target.crc_after && !target.runs.empty() && target.source_blocks);
        bool inside = true;
        for (size_t i = 0; i < target.runs.size(); ++i) {
            inside = inside && target.runs[i].offset % 16 == 0 && uint64_t(target.runs[i].offset) + 16 <= target.size &&
                     (i == 0 || target.runs[i].offset >= target.runs[i - 1].offset + 16) &&
                     target.runs[i].original != target.runs[i].data;
        }
        CHECK(inside);
    }
    CheckShippedPatchCoversLocalBuilds(*patch);
}

void TestControllerPrompts() {
    using namespace cpk_test;
    const fs::path base = TempDir("prompts");
    const fs::path app0 = base / "app0";
    fs::create_directories(app0);

    // Two languages hold different copies of the same entry names, as the European build's archives do.
    const std::string de = Pattern(20480, 7), es = Pattern(20480, 11), onmem = Pattern(24576, 13);
    const std::string de_x = Painted(de, {0x340, 0x1000, 0x4ff0}, 'X'), es_x = Painted(es, {0x340, 0x2000}, 'Y');
    const std::string onmem_x = Painted(onmem, {0x380, 0x5000}, 'Z');
    const std::string patch_bytes = MakePatch({{"Interface.ftx", {de, de_x}},
                                               {"GUI/HD_Interface.ftx", {de, de_x}},
                                               {"Interface.ftx", {es, es_x}},
                                               {"GUI/HD_Interface.ftx", {es, es_x}},
                                               {"OnMemory.ftx", {onmem, onmem_x}}});
    const std::string other = "BOOT" + std::string(600, '\0');
    const std::string or_de = BuildCpk({{"Interface.ftx", de, true}, {"OnMemory.ftx", onmem, false}}, true);
    const std::string re_de = BuildCpk({{"GUI/HD_Interface.ftx", de, false}, {"other/x.bin", other, true}}, false);
    const std::string or_es = BuildCpk({{"Interface.ftx", es, false}}, true);
    const std::string bt = BuildCpk({{"other/x.bin", other, false}}, false);
    const char* const files[] = {"Odin2_BT_DE.CPK", "Odin2_OR_DE.CPK", "Odin2_RE_DE.CPK", "Odin2_OR_ES.CPK"};
    const auto reset = [&] {
        for (const char* name : files) {
            fs::remove(UndoPath(app0 / name));
        }
        WriteAll(app0 / "Odin2_BT_DE.CPK", bt);
        WriteAll(app0 / "Odin2_OR_DE.CPK", or_de);
        WriteAll(app0 / "Odin2_RE_DE.CPK", re_de);
        WriteAll(app0 / "Odin2_OR_ES.CPK", or_es);
    };
    reset();
    GameBuild build{};
    build.label = "test build";
    build.data_files = files;
    const auto installed = [&] { return InstalledPromptStyle(app0, build, Span(patch_bytes)); };
    const auto originals = [&] {
        return ReadFile(app0 / "Odin2_OR_DE.CPK") == or_de && ReadFile(app0 / "Odin2_RE_DE.CPK") == re_de &&
               ReadFile(app0 / "Odin2_OR_ES.CPK") == or_es && ReadFile(app0 / "Odin2_BT_DE.CPK") == bt &&
               !AnyPromptPatch(app0, build);
    };
    CHECK(PromptArchives(app0, build, *ParseBlockPatch(Span(patch_bytes))).size() == 3);
    CHECK(installed() == PromptStyle::kPlayStation && !AnyPromptPatch(app0, build));

    // Reading: header, TOC (encrypted or not), case-insensitive lookup, CRILAYLA.
    {
        CpkArchive archive;
        std::string error;
        CHECK(archive.Open(app0 / "Odin2_OR_DE.CPK", &error));
        CHECK(archive.entries().size() == 2 && archive.align() == 0x800 && archive.enable_toc_crc() == 1);
        CHECK(archive.Find("interface.FTX") && !archive.Find("GUI/HD_Interface.ftx"));
        CHECK(Bytes(archive.Read(*archive.Find("Interface.ftx"))) == de && archive.Find("Interface.ftx")->size != de.size());
        CpkArchive not_cpk;
        WriteAll(base / "text.CPK", "not an archive at all, just text");
        CHECK(!not_cpk.Open(base / "text.CPK", &error) && !error.empty());
    }

    // Xbox: each archive gets the patched copy of its own language, the CRC flag cleared, the original data untouched.
    std::vector<std::string> phases;
    PatchHooks hooks;
    hooks.phase = [&phases](const char* text) { phases.emplace_back(text); };
    PromptsResult r = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch_bytes), hooks);
    CHECK(r.ok && !r.cancelled);
    CHECK(installed() == PromptStyle::kXbox);
    CHECK(!phases.empty() && phases[0] == "Patching Odin2_OR_DE.CPK...");
    CHECK(ReadFile(app0 / "Odin2_BT_DE.CPK") == bt && !fs::exists(UndoPath(app0 / "Odin2_BT_DE.CPK")));
    {
        CpkArchive or_archive, re_archive, es_archive;
        CHECK(or_archive.Open(app0 / "Odin2_OR_DE.CPK") && or_archive.enable_toc_crc() == 0);
        CHECK(Bytes(or_archive.Read(*or_archive.Find("Interface.ftx"))) == de_x);
        CHECK(Bytes(or_archive.Read(*or_archive.Find("OnMemory.ftx"))) == onmem_x);
        CHECK(or_archive.Find("Interface.ftx")->size == de_x.size() && or_archive.Find("Interface.ftx")->offset >= or_de.size());
        CHECK(or_archive.Find("Interface.ftx")->offset % 0x800 == 0);
        CHECK(re_archive.Open(app0 / "Odin2_RE_DE.CPK") && Bytes(re_archive.Read(*re_archive.Find("GUI/HD_Interface.ftx"))) == de_x);
        CHECK(Bytes(re_archive.Read(*re_archive.Find("other/x.bin"))) == other);
        CHECK(es_archive.Open(app0 / "Odin2_OR_ES.CPK") && Bytes(es_archive.Read(*es_archive.Find("Interface.ftx"))) == es_x);
        const std::string now = ReadFile(app0 / "Odin2_OR_DE.CPK");
        CHECK(now.compare(0x3000, or_de.size() - 0x3000, or_de, 0x3000, std::string::npos) == 0);
    }

    // Choosing Xbox again changes nothing.
    const std::string patched_de = ReadFile(app0 / "Odin2_OR_DE.CPK");
    r = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch_bytes));
    CHECK(r.ok && r.detail.find("already patched") != std::string::npos && ReadFile(app0 / "Odin2_OR_DE.CPK") == patched_de);

    // PlayStation: byte-identical archives, undo records gone; again: nothing to do.
    r = SetPromptStyle(app0, build, PromptStyle::kPlayStation, {});
    CHECK(r.ok && originals() && installed() == PromptStyle::kPlayStation);
    r = SetPromptStyle(app0, build, PromptStyle::kPlayStation, {});
    CHECK(r.ok && originals());

    // Several round trips end where they started.
    for (int i = 0; i < 3; ++i) {
        CHECK(SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch_bytes)).ok && installed() == PromptStyle::kXbox);
        CHECK(SetPromptStyle(app0, build, PromptStyle::kPlayStation, {}).ok && originals());
    }

    // A half-patched install reads as PlayStation; Xbox completes it, PlayStation cleans it up.
    CHECK(ApplyBlockPatch(app0 / "Odin2_RE_DE.CPK", *ParseBlockPatch(Span(patch_bytes))).ok);
    CHECK(installed() == PromptStyle::kPlayStation && AnyPromptPatch(app0, build));
    CHECK(SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch_bytes)).ok && installed() == PromptStyle::kXbox);
    CHECK(SetPromptStyle(app0, build, PromptStyle::kPlayStation, {}).ok && originals());

    // An interrupted write (undo record written, table of contents not yet) is put back by the next change.
    {
        const std::string record_source = ReadFile(app0 / "Odin2_OR_ES.CPK");
        CHECK(ApplyBlockPatch(app0 / "Odin2_OR_ES.CPK", *ParseBlockPatch(Span(patch_bytes))).ok);
        const std::string record = ReadFile(UndoPath(app0 / "Odin2_OR_ES.CPK"));
        WriteAll(app0 / "Odin2_OR_ES.CPK", record_source + std::string(4096, 'j'));
        WriteAll(UndoPath(app0 / "Odin2_OR_ES.CPK"), record);
        CHECK(installed() == PromptStyle::kPlayStation);
        CHECK(SetPromptStyle(app0, build, PromptStyle::kPlayStation, {}).ok && originals());
    }

    // Cancelling while writing leaves every archive as it was.
    hooks.phase = nullptr;
    hooks.progress = [](uint64_t, uint64_t) { return false; };
    r = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch_bytes), hooks);
    CHECK(!r.ok && r.cancelled && originals());

    // A copy the patch was not made for is left alone, and Xbox is refused for the whole build.
    std::string foreign = es;
    foreign[0x2000] = char(foreign[0x2000] + 1);
    WriteAll(app0 / "Odin2_OR_ES.CPK", BuildCpk({{"Interface.ftx", foreign, false}}, false));
    const std::string foreign_es = ReadFile(app0 / "Odin2_OR_ES.CPK");
    r = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch_bytes));
    CHECK(!r.ok && r.detail.find("is not the file the patch was made for") != std::string::npos);
    CHECK(ReadFile(app0 / "Odin2_OR_ES.CPK") == foreign_es && ReadFile(app0 / "Odin2_OR_DE.CPK") == or_de);
    CHECK(installed() == PromptStyle::kPlayStation && !AnyPromptPatch(app0, build));
    reset();

    // An unreadable patch is refused before anything is written.
    r = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(std::string("DCPX1")));
    CHECK(!r.ok && originals());

    // An archive hard-linked from another folder (the extracted game, the base build of an update) gets a file of
    // its own before it is patched or restored: the other folder keeps the original.
    const fs::path source = base / "source";
    fs::create_directories(source);
    WriteAll(source / "Odin2_OR_DE.CPK", or_de);
    fs::remove(app0 / "Odin2_OR_DE.CPK");
    std::error_code ec;
    fs::create_hard_link(source / "Odin2_OR_DE.CPK", app0 / "Odin2_OR_DE.CPK", ec);
    if (!ec) {
        CHECK(SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch_bytes)).ok && installed() == PromptStyle::kXbox);
        CHECK(ReadFile(source / "Odin2_OR_DE.CPK") == or_de && fs::hard_link_count(app0 / "Odin2_OR_DE.CPK") == 1);

        // An update laid over the patched base starts from the originals; the base keeps its patch.
        const fs::path update = base / "update", staged = base / "staged";
        fs::create_directories(update);
        WriteAll(update / "Odin2_PATCH.CPK", bt);
        CHECK(StageUpdate(app0, update, staged).ok());
        CHECK(ReadFile(staged / "Odin2_OR_DE.CPK") == or_de && ReadFile(staged / "Odin2_RE_DE.CPK") == re_de);
        CHECK(!fs::exists(UndoPath(staged / "Odin2_OR_DE.CPK")) && fs::exists(staged / "Odin2_PATCH.CPK"));
        CHECK(installed() == PromptStyle::kXbox);
        CHECK(SetPromptStyle(app0, build, PromptStyle::kPlayStation, {}).ok);
        CHECK(ReadFile(app0 / "Odin2_OR_DE.CPK") == or_de && ReadFile(source / "Odin2_OR_DE.CPK") == or_de);
    }
}

// The 1.01 ES, FR and IT Refined archives store DirName once for the table: their button art is still found, patched
// and restored byte for byte (issue #3).
void TestSharedDirectoryArchive() {
    using namespace cpk_test;
    const fs::path app0 = TempDir("prompts-shared-dir") / "app0";
    fs::create_directories(app0);
    const std::string es = Pattern(20480, 11), title = Pattern(4096, 5);
    const std::string es_x = Painted(es, {0x340, 0x2000}, 'Y');
    const std::string patch_bytes = MakePatch({{"GUI/HD_Interface.ftx", {es, es_x}}});
    const std::string original =
        BuildCpk({{"GUI/HD_Interface.ftx", es, true}, {"GUI/HD_title.ftx", title, true}}, true, true);
    const char* const files[] = {"Odin2_EU_PATCH1_RE_ES.CPK"};
    WriteAll(app0 / files[0], original);
    GameBuild build{};
    build.label = "test build";
    build.data_files = files;

    CpkArchive archive;
    CHECK(archive.Open(app0 / files[0]) && archive.entries().size() == 2);
    CHECK(archive.Find("GUI/HD_Interface.ftx") && archive.Find("gui/hd_title.ftx") && !archive.Find("HD_Interface.ftx"));
    CHECK(archive.Find("GUI/HD_Interface.ftx") && Bytes(archive.Read(*archive.Find("GUI/HD_Interface.ftx"))) == es);
    CHECK(PromptArchives(app0, build, *ParseBlockPatch(Span(patch_bytes))).size() == 1);
    CHECK(InstalledPromptStyle(app0, build, Span(patch_bytes)) == PromptStyle::kPlayStation);

    const PromptsResult xbox = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch_bytes));
    CHECK(xbox.ok && xbox.detail.find("1 file(s) patched") != std::string::npos);
    CHECK(InstalledPromptStyle(app0, build, Span(patch_bytes)) == PromptStyle::kXbox);
    CpkArchive patched;
    CHECK(patched.Open(app0 / files[0]) && patched.Find("GUI/HD_Interface.ftx") && patched.Find("GUI/HD_title.ftx"));
    if (patched.Find("GUI/HD_Interface.ftx") && patched.Find("GUI/HD_title.ftx")) {
        CHECK(Bytes(patched.Read(*patched.Find("GUI/HD_Interface.ftx"))) == es_x);
        CHECK(Bytes(patched.Read(*patched.Find("GUI/HD_title.ftx"))) == title);
    }
    CHECK(SetPromptStyle(app0, build, PromptStyle::kPlayStation, {}).ok);
    CHECK(ReadFile(app0 / files[0]) == original && !AnyPromptPatch(app0, build));
}

// A build's profile tables laid out at their own offsets in an image the size of its executable. The image's eboot
// part ends at `eboot_size`; its linked module code follows.
MappedExecutable TableOdinImage(const GameBuild& build, uint64_t& eboot_size) {
    const LzcntBuild* lzcnt = nullptr;
    for (const LzcntBuild& candidate : lzcnt_builds) {
        if (std::string_view(candidate.build) == build.label) {
            lzcnt = &candidate;
        }
    }
    PlantedLayout l;
    l.notice = build.skip_intro->notice_scene - osl_skip_intro_notice_code.site;
    l.hd = build.skip_intro->hd_title_check - osl_skip_intro_hd_title_code.site;
    l.classic = build.skip_intro->classic_title_check - osl_skip_intro_classic_title_code.site;
    l.classic_target = build.skip_intro->classic_button;
    l.snapshot = build.snapshot_scale_site;
    l.getter = build.bookshelf->accessor;
    l.total = build.bookshelf->total;
    l.reads.assign(build.bookshelf->reads, build.bookshelf->reads + OSL_BOOKSHELF_READS);
    l.marks.assign(build.bookshelf->marks, build.bookshelf->marks + OSL_BOOKSHELF_MARKS);
    l.in_place = build.bookshelf->in_place_page_read != 0;
    l.resolution.assign(build.resolution_sites.begin(), build.resolution_sites.end());
    const size_t code_end = build.skip_intro->stubs;
    const size_t data_at = ((code_end + 0xfff) & ~size_t{0xfff});
    eboot_size = data_at + 0x2000;
    l.data = data_at + 0x800;
    size_t image_size = eboot_size + 0x1000;
    for (int i = 0; lzcnt && i < LZCNT_RANGE_COUNT; ++i) {
        l.lzcnt.push_back(lzcnt->offsets[i]);
        image_size = std::max<size_t>(image_size, (lzcnt->offsets[i] + 0x1000) & ~size_t{0xfff});
    }
    MappedExecutable image;
    image.image.assign(image_size, 0);
    image.segments = {{0, code_end, 5}, {data_at, 0x2000, 4}, {eboot_size, image_size - eboot_size, 5}};
    image.entry = lzcnt ? lzcnt->entry : 0;
    PlantFeatures(image, l);
    return image;
}

void WriteBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void TestMapExecutableRejectsMalformedFiles() {
    const std::filesystem::path dir = TempDir("map-executable");
    MappedExecutable out;
    out.entry = 77;
    CHECK(!MapExecutable(dir / "missing.bin", out).empty());
    WriteBytes(dir / "empty.bin", {});
    CHECK(!MapExecutable(dir / "empty.bin", out).empty());
    WriteBytes(dir / "magic.bin", {'O', 0x15, '=', 0x1d});
    CHECK(!MapExecutable(dir / "magic.bin", out).empty());

    std::vector<uint8_t> header(4096, 0);
    const uint8_t magic[] = {'O', 0x15, '=', 0x1d};
    std::memcpy(header.data(), magic, 4);
    WriteBytes(dir / "no-elf.bin", header);
    CHECK(!MapExecutable(dir / "no-elf.bin", out).empty());

    const uint8_t ident[] = {0x7f, 'E', 'L', 'F', 2, 1, 1};
    std::memcpy(header.data() + 32, ident, sizeof(ident));
    header[32 + 18] = 62;
    header[32 + 54] = 56;
    header[32 + 56] = 1;
    const uint64_t far = 0xfffffffffffffff0ull;
    std::memcpy(header.data() + 32 + 32, &far, 8);
    WriteBytes(dir / "wild-offset.bin", header);
    CHECK(!MapExecutable(dir / "wild-offset.bin", out).empty());

    const uint64_t near_end = 4090;
    std::memcpy(header.data() + 32 + 32, &near_end, 8);
    WriteBytes(dir / "truncated-headers.bin", header);
    CHECK(!MapExecutable(dir / "truncated-headers.bin", out).empty());
    CHECK(out.entry == 77 && out.image.empty());
}

std::string MappingExtra(const ScannedMapping& mapping) {
    std::string extra;
    for (const auto& [key, numbers] : ScannedMappingFields(mapping)) {
        extra += ",\n  \"" + key + "\": " + numbers;
    }
    return extra;
}

void WriteImageReport(const std::filesystem::path& dir, const GameBuild& build, const std::string& extra = {}) {
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "image.json") << "{\n  \"title_id\": \"" << build.title_id << "\",\n  \"app_ver\": \""
                                      << FinishedLayer(build).app_ver << "\",\n  \"eboot_sha256\": \""
                                      << build.eboot_sha256 << "\"" << extra << "\n}\n";
}

bool SameSkipIntro(const std::optional<OslSkipIntroSites>& a, const OslSkipIntroSites* b) {
    return a && b && std::memcmp(&*a, b, sizeof(OslSkipIntroSites)) == 0;
}

bool SameBookshelf(const std::optional<OslBookshelfSites>& a, const OslBookshelfSites* b) {
    return a && b && std::memcmp(&*a, b, sizeof(OslBookshelfSites)) == 0;
}

bool SameResolution(const std::vector<uint32_t>& offsets, std::span<const OslResolutionSite> sites) {
    return offsets.size() == sites.size() &&
           std::equal(offsets.begin(), offsets.end(), sites.begin(),
                      [](uint32_t offset, const OslResolutionSite& site) { return offset == site.offset; });
}

bool SameLzcnt(const std::vector<uint32_t>& offsets, const GameBuild& build) {
    for (const LzcntBuild& lzcnt : lzcnt_builds) {
        if (std::string_view(lzcnt.build) == build.label) {
            return offsets.size() == LZCNT_RANGE_COUNT &&
                   std::equal(offsets.begin(), offsets.end(), std::begin(lzcnt.offsets));
        }
    }
    return false;
}

bool SameMapping(const ScannedMapping& a, const ScannedMapping& b) {
    return a.resolution_offsets == b.resolution_offsets && a.snapshot_scale_site == b.snapshot_scale_site &&
           a.skip_intro.has_value() == b.skip_intro.has_value() &&
           (!a.skip_intro || SameSkipIntro(a.skip_intro, &*b.skip_intro)) &&
           a.bookshelf.has_value() == b.bookshelf.has_value() &&
           (!a.bookshelf || SameBookshelf(a.bookshelf, &*b.bookshelf)) && a.lzcnt_offsets == b.lzcnt_offsets;
}

bool IsHexSha(std::string_view text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

bool SourceHasNoBuildLiterals(const std::filesystem::path& root) {
    const std::set<std::string> allowed = {"launcher/game_profile.cpp", "launcher/build_diagnostics.cpp",
                                           "src/lzcnt_compat.c"};
    bool clean = true;
    for (const char* folder : {"src", "launcher", "gpu/shim"}) {
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(root / folder, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().extension().string();
            if (!it->is_regular_file(ec) || (name != ".c" && name != ".cpp" && name != ".h")) {
                continue;
            }
            const std::string relative = std::filesystem::relative(it->path(), root).generic_string();
            const std::string text = ReadFile(it->path());
            if (!allowed.contains(relative) && (Contains(text, "CUSA0") || Contains(text, "NPWR0"))) {
                std::printf("hardcoded build id in %s\n", relative.c_str());
                clean = false;
            }
        }
    }
    return clean;
}

// The sites of two features never share a byte, whichever build they come from.
bool SitesAreDisjoint(const GameBuild& build) {
    struct Span {
        uint64_t begin, end;
    };
    std::vector<Span> spans;
    for (const OslResolutionSite& site : build.resolution_sites) {
        spans.push_back({site.offset, site.offset + site.opcode_length + (site.packed_floats ? 8u : 4u)});
    }
    if (build.snapshot_scale_site) {
        spans.push_back({build.snapshot_scale_site, build.snapshot_scale_site + OSL_SNAPSHOT_WINDOW});
    }
    if (const OslSkipIntroSites* skip = build.skip_intro) {
        spans.push_back({skip->notice_scene - osl_skip_intro_notice_code.site,
                         skip->notice_scene - osl_skip_intro_notice_code.site + osl_skip_intro_notice_code.count});
        spans.push_back({skip->hd_title_check - osl_skip_intro_hd_title_code.site,
                         skip->hd_title_check - osl_skip_intro_hd_title_code.site + osl_skip_intro_hd_title_code.count});
        spans.push_back({skip->classic_title_check - osl_skip_intro_classic_title_code.site,
                         skip->classic_title_check - osl_skip_intro_classic_title_code.site +
                             osl_skip_intro_classic_title_code.count});
        spans.push_back({skip->stubs, skip->stubs + OSL_SKIP_INTRO_STUB_BYTES});
    }
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.begin < b.begin; });
    for (size_t i = 1; i < spans.size(); ++i) {
        if (spans[i].begin < spans[i - 1].end) {
            return false;
        }
    }
    return true;
}

bool OffsetsAscend(const uint32_t* values, size_t count) {
    for (size_t i = 1; i < count; ++i) {
        if (values[i] <= values[i - 1]) {
            return false;
        }
    }
    return true;
}

// Xbox prompts apply only to archives whose entries are the ones the patch was made for; any other archive leaves
// every archive of the build as it was.
bool PromptsRollBackOnUnknownArchives(const GameBuild& build, const std::filesystem::path& base) {
    using namespace cpk_test;
    namespace fs = std::filesystem;
    const fs::path app0 = base / build.folder;
    fs::remove_all(app0);
    fs::create_directories(app0);
    const std::string known = Pattern(20480, 3), painted = Painted(known, {0x340}, 'X'), unknown = Pattern(20480, 5);
    const std::string patch = MakePatch({{"Interface.ftx", {known, painted}}});
    const std::string matching = BuildCpk({{"Interface.ftx", known, true}}, true);
    const std::string foreign = BuildCpk({{"Interface.ftx", unknown, false}}, false);
    const std::string first = build.data_files[0], second = build.data_files[1];
    WriteAll(app0 / first, matching);
    WriteAll(app0 / second, foreign);
    bool ok = InstalledPromptStyle(app0, build, Span(patch)) == PromptStyle::kPlayStation;
    const PromptsResult refused = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch));
    ok = ok && !refused.ok && ReadFile(app0 / first) == matching && ReadFile(app0 / second) == foreign &&
         !AnyPromptPatch(app0, build) && InstalledPromptStyle(app0, build, Span(patch)) == PromptStyle::kPlayStation;
    char crc_text[16];
    std::snprintf(crc_text, sizeof(crc_text), "%08x", Crc32(Span(unknown)));
    ok = ok && refused.unsupported.size() == 2 && Contains(refused.unsupported[1], "source blocks incompatible") &&
         Contains(refused.unsupported[0], std::string("build ") + build.label + ",") &&
         Contains(refused.unsupported[0], "archive " + second + ",") && Contains(refused.unsupported[0], "entry Interface.ftx,") &&
         Contains(refused.unsupported[0], "size 20480,") && Contains(refused.unsupported[0], std::string("CRC32 ") + crc_text);
    WriteAll(app0 / second, matching);
    const PromptsResult applied = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch));
    ok = ok && applied.ok && InstalledPromptStyle(app0, build, Span(patch)) == PromptStyle::kXbox;
    const PromptsResult reverted = SetPromptStyle(app0, build, PromptStyle::kPlayStation, {});
    return ok && reverted.ok && ReadFile(app0 / first) == matching && ReadFile(app0 / second) == matching &&
           !AnyPromptPatch(app0, build);
}

// The build's patch archive is held to the same rule as its base archives: a copy the patch was not made for refuses
// Xbox for the whole build and leaves every archive as it was; the copy it was made for is patched and restored.
bool PromptsFollowPatchArchive(const GameBuild& build, const std::filesystem::path& base) {
    using namespace cpk_test;
    namespace fs = std::filesystem;
    const fs::path app0 = base / build.folder;
    fs::remove_all(app0);
    fs::create_directories(app0);
    const std::string known = Pattern(20480, 3), painted = Painted(known, {0x340}, 'X'), unknown = Pattern(20480, 5);
    const std::string patch = MakePatch({{"Interface.ftx", {known, painted}}});
    const std::string matching = BuildCpk({{"Interface.ftx", known, true}}, true);
    const std::string foreign = BuildCpk({{"Interface.ftx", unknown, false}}, false);
    const std::string first = build.data_files.front(), last = build.data_files.back();
    WriteAll(app0 / first, matching);
    WriteAll(app0 / last, foreign);
    const PromptsResult refused = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch));
    bool ok = !refused.ok && ReadFile(app0 / first) == matching && ReadFile(app0 / last) == foreign &&
              !AnyPromptPatch(app0, build);
    WriteAll(app0 / last, matching);
    const PromptsResult applied = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch));
    ok = ok && applied.ok && InstalledPromptStyle(app0, build, Span(patch)) == PromptStyle::kXbox;
    const PromptsResult reverted = SetPromptStyle(app0, build, PromptStyle::kPlayStation, {});
    return ok && reverted.ok && ReadFile(app0 / first) == matching && ReadFile(app0 / last) == matching &&
           !AnyPromptPatch(app0, build);
}

std::string Overlay(std::string data, const std::string& from, std::initializer_list<size_t> offsets) {
    for (const size_t off : offsets) data.replace(off, 16, from, off, 16);
    return data;
}

// Every build takes a regional copy of its button art through the same source-block check, and restores it exactly.
bool PromptsAcceptCompatibleVariants(const GameBuild& build, const std::filesystem::path& base) {
    using namespace cpk_test;
    namespace fs = std::filesystem;
    const fs::path app0 = base / build.folder;
    fs::remove_all(app0);
    fs::create_directories(app0);
    const std::string known = Pattern(20480, 3), painted = Painted(known, {0x340, 0x1000}, 'X');
    const std::string variant = Painted(known, {0x2000}, 'V');
    const std::string patch = MakePatch({{"Interface.ftx", {known, painted}}, {"GUI/HD_Interface.ftx", {known, painted}}});
    const std::string classic = BuildCpk({{"Interface.ftx", variant, true}}, true);
    const std::string refined = BuildCpk({{"GUI/HD_Interface.ftx", variant, false}}, false);
    const std::string first = build.data_files.front(), last = build.data_files.back();
    WriteAll(app0 / first, classic);
    WriteAll(app0 / last, refined);
    const PromptsResult applied = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch));
    bool ok = applied.ok && applied.detail.find("accepted by verified source blocks") != std::string::npos &&
              InstalledPromptStyle(app0, build, Span(patch)) == PromptStyle::kXbox;
    CpkArchive archive;
    const std::string expected = Overlay(variant, painted, {0x340, 0x1000});
    ok = ok && archive.Open(app0 / first) && Bytes(archive.Read(*archive.Find("Interface.ftx"))) == expected;
    const PromptsResult reverted = SetPromptStyle(app0, build, PromptStyle::kPlayStation, {});
    ok = ok && reverted.ok && ReadFile(app0 / first) == classic && ReadFile(app0 / last) == refined && !AnyPromptPatch(app0, build);
    WriteAll(app0 / last, BuildCpk({{"GUI/HD_Interface.ftx", Painted(variant, {0x340}, 'W'), false}}, false));
    const std::string changed = ReadFile(app0 / last);
    const PromptsResult refused = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch));
    return ok && !refused.ok && ReadFile(app0 / first) == classic && ReadFile(app0 / last) == changed && !AnyPromptPatch(app0, build) &&
           InstalledPromptStyle(app0, build, Span(patch)) == PromptStyle::kPlayStation;
}

// The fixture every scanned build is checked against: the production patterns at offsets no profile has.
constexpr size_t kFixtureSkew = 0x500;

void TestBuildContract() {
    const std::filesystem::path root(OSL_SOURCE_DIR);
    const std::string mirror = ReadFile(root / "scripts" / "game_profiles.py");
    const std::filesystem::path dir = TempDir("contract");
    const MappedExecutable fixture_image = SyntheticOdinImage(kFixtureSkew);
    const ScanResult fixture = ScanExecutable(fixture_image, fixture_image.image.size());
    CHECK(fixture.complete());
    const bool no_build_literals = SourceHasNoBuildLiterals(root);
    CHECK(no_build_literals);
    const std::string loader = ReadFile(root / "src" / "loader.c");
    const bool save_from_sfo = Contains(loader, "sfo_value(sfo,\"TITLE_ID\"") &&
                                Contains(loader, "runtime_savedata_configure(id)");
    CHECK(save_from_sfo);
    CHECK(lzcnt_compat_needed(IsaHostFeatures{0, 0, bit_LZCNT}) == 0 && lzcnt_compat_needed(IsaHostFeatures{0, 0, 0}) == 1);
    for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i) {
        LzcntOperands operands;
        CHECK(lzcnt_compat_decode(lzcnt_shapes[i].bytes, lzcnt_shapes[i].length, &operands));
    }
    LzcntOperands decoded;
    CHECK(lzcnt_compat_decode(lzcnt_shapes[3].bytes, lzcnt_shapes[3].length, &decoded) && decoded.bits == 64 &&
          decoded.dst == 1 && decoded.length == 5 && !decoded.memory);
    CHECK(lzcnt_compat_decode(lzcnt_shapes[2].bytes, lzcnt_shapes[2].length, &decoded) && decoded.bits == 64 &&
          decoded.dst == 6 && decoded.length == 10 && decoded.memory);
    CHECK(lzcnt_compat_decode(lzcnt_shapes[0].bytes, lzcnt_shapes[0].length, &decoded) && decoded.bits == 16 &&
          decoded.dst == 5 && decoded.length == 5 && !decoded.memory);
    const unsigned char not_lzcnt[] = {0x48, 0x0f, 0xbd, 0xca}, rip_relative[] = {0xf3, 0x0f, 0xbd, 0x05, 0, 0, 0, 0};
    CHECK(!lzcnt_compat_decode(not_lzcnt, sizeof(not_lzcnt), &decoded) &&
          !lzcnt_compat_decode(rip_relative, sizeof(rip_relative), &decoded) &&
          !lzcnt_compat_decode(lzcnt_shapes[3].bytes, 4, &decoded));

    std::set<std::string> shas, folders, labels, contents;
    std::map<std::string, std::string> content_of_title;
    const size_t table_resolution_sites = SupportedBuilds()[0].resolution_sites.size();
    bool pads_build_neutral = true;
    for (const char* file : {"src/osl_pad.c", "src/runtime_pad.c", "launcher/pad_capture.cpp"}) {
        const std::string text = ReadFile(root / file);
        pads_build_neutral = pads_build_neutral && !text.empty() && !Contains(text, "GameBuild") &&
                             !Contains(text, "osl_game_build") && !Contains(text, "title_id");
    }
    for (const GameBuild& build : SupportedBuilds()) {
        const std::string title = build.title_id, version = build.version, folder = build.folder;
        const PackageLayer& finished = FinishedLayer(build);
        const std::filesystem::path image_dir = dir / "image" / folder;
        const std::vector<const GameBuild*> same_package = SamePackageBuilds(build);
        const GameBuild& first = *same_package.front();

        // Installation
        PARITY("Installation", title.size() == 9 && title.starts_with("CUSA") &&
                                   std::all_of(title.begin() + 4, title.end(), [](char c) { return c >= '0' && c <= '9'; }));
        PARITY("Installation", std::string_view(build.content_id).ends_with("_00-ODINSPHERELE0000") &&
                                   Contains(build.content_id, title));
        PARITY("Installation", IsHexSha(build.eboot_sha256) && shas.insert(build.eboot_sha256).second);
        PARITY("Installation", folder == title + "-" + version + (&first == &build ? "" : "-" + std::string(build.region)) &&
                                   folders.insert(folder).second);
        PARITY("Installation", BuildWithEboot(build, build.eboot_sha256) == &build &&
                                   BuildWithEboot(build, std::string(64, '0')) == nullptr);
        PARITY("Installation", same_package.size() == 1 || (build.layers.size() == 1 && UpdateBuildOf(build) == nullptr));
        PARITY("Installation", std::string(build.label) == title + " " + version + " " + build.region &&
                                   labels.insert(build.label).second && std::string(build.region).size() >= 2);
        PARITY("Installation", !build.layers.empty() && finished.update == (build.layers.size() > 1) &&
                                   std::string(finished.app_ver).substr(1) == version);
        PARITY("Installation", (build.layers.size() > 1) == !build.extra_files.empty());
        std::set<std::string> data(build.data_files.begin(), build.data_files.end());
        PARITY("Installation", !build.data_files.empty() && data.size() == build.data_files.size() &&
                                   std::all_of(data.begin(), data.end(), [](const std::string& name) {
                                       return name.starts_with("Odin2_") && name.ends_with(".CPK");
                                   }));
        PARITY("Installation", std::all_of(build.extra_files.begin(), build.extra_files.end(), [](const char* name) {
                                   return std::string_view(name).ends_with(".plt");
                               }));
        const std::string sfo = SfoText(dir, {{"TITLE_ID", title}, {"CONTENT_ID", build.content_id},
                                              {"CATEGORY", finished.update ? "gp" : "gd"}, {"APP_VER", finished.app_ver},
                                              {"VERSION", finished.sfo_version}});
        const GameCheck package = CheckPackageIdentity(sfo);
        PARITY("Installation", package.state == GameFileState::kValid && package.build == &first &&
                                   package.layer == build.layers.size() - 1);
        if (finished.update) {
            const GameCheck complete = CheckPackageIdentity(
                SfoText(dir, {{"TITLE_ID", title}, {"CONTENT_ID", build.content_id}, {"CATEGORY", "gd"},
                              {"APP_VER", finished.app_ver},
                              {"VERSION", build.merged_sfo_version ? build.merged_sfo_version : finished.sfo_version}}));
            PARITY("Installation", !!build.merged_sfo_version == (complete.state == GameFileState::kValid &&
                                                                  complete.build == &build && complete.layer == 0));
        }
        PARITY("Installation", CheckSfoIdentity(sfo).build == &first);
        PARITY("Installation", BuildPaths({"prepared", "image"}, build).app0.filename() == folder &&
                                   BuildPaths({"prepared", "image"}, build).image_dir.filename() == folder);
        const GameBuild* base = BaseBuildOf(build);
        PARITY("Installation", build.layers.size() > 1 ? base && std::string_view(base->title_id) == title &&
                                                             base->layers.size() == 1
                                                       : base == nullptr);
        PARITY("Installation", Contains(mirror, "'" + title + "': {") && Contains(mirror, build.content_id) &&
                                   Contains(mirror, build.eboot_sha256) && Contains(mirror, "'folder': '" + folder + "'") &&
                                   Contains(mirror, "'region': '" + std::string(build.region) + "'"));
        PARITY("Installation", std::all_of(data.begin(), data.end(), [&](const std::string& name) {
                                   return Contains(mirror, "'" + name + "'");
                               }));

        // Languages
        const std::span<const TextLanguage> languages = TextLanguagesOf(&build);
        if (base) {
            PARITY("Languages", base->text_mode == build.text_mode && languages.size() == TextLanguagesOf(base).size() &&
                                    std::all_of(base->data_files.begin(), base->data_files.end(), [&](const char* name) {
                                        return std::find_if(build.data_files.begin(), build.data_files.end(),
                                                            [&](const char* file) {
                                                                return std::string_view(file) == name;
                                                            }) != build.data_files.end();
                                    }));
        }
        WriteImageReport(image_dir, build);
        const std::filesystem::path ini = dir / (folder + ".ini");
        PARITY("Languages", (build.text_mode == TextLanguageMode::kSelectable) == !languages.empty());
        if (build.text_mode == TextLanguageMode::kSelectable) {
            std::set<int> ids;
            std::set<std::string_view> keys;
            bool resolved = true;
            for (const TextLanguage& language : languages) {
                ids.insert(language.ps4_id);
                keys.insert(language.key);
                std::ofstream(ini) << "text_language = " << language.key << "\n";
                resolved = resolved && IsValidSetting(kTextLanguageKey, std::string(language.key)) &&
                           osl_text_language(ini.string().c_str(), image_dir.string().c_str()) == language.ps4_id;
            }
            std::ofstream(ini) << "text_language = invalid\n";
            for (const TextLanguage& language : languages) {
                resolved = resolved && &ResolveTextLanguage(&build, language.key) == &language;
            }
            PARITY("Languages", languages.size() >= 2 && &ResolveTextLanguage(&build, "invalid") == &languages.front() &&
                                    osl_text_language(ini.string().c_str(), image_dir.string().c_str()) ==
                                        languages.front().ps4_id &&
                                    ids.size() == languages.size() && keys.size() == languages.size() &&
                                    std::all_of(languages.begin(), languages.end(),
                                                [](const TextLanguage& language) { return language.ps4_id >= 0; }) &&
                                    resolved);
        } else {
            std::ofstream(ini) << "text_language = german\n";
            PARITY("Languages", ResolveTextLanguage(&build, "german").ps4_id == 1 &&
                                    ResolveTextLanguage(&build, "").ps4_id == 1 &&
                                    osl_text_language(ini.string().c_str(), image_dir.string().c_str()) == 1);
        }

        // Display, Graphics, Gameplay and Runtime Patches: the executable's sites, from tables or from the scan.
        const std::string last_resolution = osl_resolutions[OSL_RESOLUTION_COUNT - 1].key;
        const std::string last_aspect = osl_aspect_ratios[OSL_ASPECT_COUNT - 1].key;
        std::ofstream(ini) << "render_resolution = " << last_resolution << "\naspect_ratio = " << last_aspect << "\n";
        OslGameBuild loaded{};
        if (build.mapping == ExecutableMapping::kTable) {
            const BuildFeatures features = FeaturesOf(&build, image_dir);
            PARITY("Display", features.custom_resolution &&
                                  osl_aspect_ratio(ini.string().c_str(), image_dir.string().c_str()) == OSL_ASPECT_COUNT - 1);
            PARITY("Graphics", build.resolution_sites.size() == table_resolution_sites && build.snapshot_scale_site != 0 &&
                                   osl_render_resolution(ini.string().c_str(), image_dir.string().c_str()) ==
                                       OSL_RESOLUTION_COUNT - 1);
            PARITY("Graphics", TableComplete(build) && std::is_sorted(build.resolution_sites.begin(), build.resolution_sites.end(),
                                              [](const OslResolutionSite& a, const OslResolutionSite& b) {
                                                  return a.offset < b.offset;
                                              }) &&
                                   SitesAreDisjoint(build));
            PARITY("Gameplay", TableComplete(build) && features.skip_intro && features.unlock_bookshelf);
            PARITY("Gameplay", TableComplete(build) && build.skip_intro->stubs % 16 == 0 &&
                                   OffsetsAscend(build.bookshelf->reads, OSL_BOOKSHELF_READS) &&
                                   OffsetsAscend(build.bookshelf->marks, OSL_BOOKSHELF_MARKS));
            PARITY("Runtime Patches", osl_game_build(image_dir.string().c_str(), &loaded) &&
                                          loaded.resolution_sites == build.resolution_sites.data() &&
                                          loaded.skip_intro == build.skip_intro && loaded.bookshelf == build.bookshelf &&
                                          loaded.lzcnt_offsets == nullptr);
            size_t entries = 0;
            for (const LzcntBuild& lzcnt : lzcnt_builds) {
                if (std::string_view(lzcnt.build) == build.label) {
                    ++entries;
                    PARITY("Runtime Patches", lzcnt.size > 0 && lzcnt.entry > 0 &&
                                                  OffsetsAscend(lzcnt.offsets, LZCNT_RANGE_COUNT));
                }
            }
            PARITY("Runtime Patches", entries == 1);
        } else {
            PARITY("Graphics", build.resolution_sites.empty() && build.snapshot_scale_site == 0);
            PARITY("Gameplay", build.skip_intro == nullptr && build.bookshelf == nullptr);
            PARITY("Runtime Patches", std::none_of(std::begin(lzcnt_builds), std::end(lzcnt_builds),
                                                   [&](const LzcntBuild& lzcnt) {
                                                       return std::string_view(lzcnt.build) == build.label;
                                                   }));
            const BuildFeatures before = FeaturesOf(&build, image_dir);
            PARITY("Display", !before.custom_resolution &&
                                  osl_aspect_ratio(ini.string().c_str(), image_dir.string().c_str()) == 0);
            PARITY("Graphics", osl_render_resolution(ini.string().c_str(), image_dir.string().c_str()) == 0);
            PARITY("Gameplay", !before.skip_intro && !before.unlock_bookshelf);
            PARITY("Runtime Patches", osl_game_build(image_dir.string().c_str(), &loaded) &&
                                          loaded.resolution_site_count == 0 && loaded.skip_intro == nullptr &&
                                          loaded.bookshelf == nullptr && loaded.lzcnt_offsets == nullptr &&
                                          !before.lzcnt_rewrite);
            WriteImageReport(image_dir, build, MappingExtra(fixture.mapping));
            const BuildFeatures after = FeaturesOf(&build, image_dir);
            PARITY("Display", after.custom_resolution &&
                                  osl_aspect_ratio(ini.string().c_str(), image_dir.string().c_str()) == OSL_ASPECT_COUNT - 1);
            PARITY("Graphics", osl_render_resolution(ini.string().c_str(), image_dir.string().c_str()) ==
                                   OSL_RESOLUTION_COUNT - 1);
            PARITY("Gameplay", after.skip_intro && after.unlock_bookshelf);
            OslGameBuild scanned{};
            PARITY("Runtime Patches", osl_game_build(image_dir.string().c_str(), &scanned) && after.lzcnt_rewrite &&
                                          scanned.resolution_site_count == table_resolution_sites &&
                                          SameSkipIntro(fixture.mapping.skip_intro, scanned.skip_intro) &&
                                          SameBookshelf(fixture.mapping.bookshelf, scanned.bookshelf) &&
                                          scanned.snapshot_scale_site == fixture.mapping.snapshot_scale_site &&
                                          scanned.lzcnt_offsets &&
                                          std::equal(fixture.mapping.lzcnt_offsets.begin(),
                                                     fixture.mapping.lzcnt_offsets.end(), scanned.lzcnt_offsets));
        }

        // Controls
        PARITY("Controls", pads_build_neutral);
        PARITY("Controls", PromptsRollBackOnUnknownArchives(build, dir / "prompts"));
        PARITY("Controls", PromptsFollowPatchArchive(build, dir / "prompts-patch"));
        PARITY("Controls", PromptsAcceptCompatibleVariants(build, dir / "prompts-compatible"));

        // Trophies and Save Data: read from the game's own files, never from the profile.
        PARITY("Trophies", no_build_literals);
        PARITY("Save Data", save_from_sfo && no_build_literals);
        const auto [position, inserted] = content_of_title.emplace(title, build.content_id);
        PARITY("Save Data", inserted || position->second == build.content_id);
        PARITY("Save Data", !base || (std::string_view(base->title_id) == title &&
                                      std::string_view(base->content_id) == build.content_id &&
                                      std::string_view(base->folder) != folder));
    }
    std::map<std::string, std::string> title_of_content;
    for (const GameBuild& build : SupportedBuilds()) {
        const auto [position, inserted] = title_of_content.emplace(build.content_id, build.title_id);
        CHECK(inserted || position->second == build.title_id);
    }
}

// Each supported table build's own tables, laid out in an executable-sized image, are found again by the scan and
// accepted by every patch: the scan and the tables describe the same code.
void TestScannerReproducesTables() {
    for (const GameBuild& build : SupportedBuilds()) {
        if (build.mapping != ExecutableMapping::kTable || !TableComplete(build)) {
            continue;
        }
        uint64_t eboot_size = 0;
        const MappedExecutable image = TableOdinImage(build, eboot_size);
        const ScanResult scan = ScanExecutable(image, eboot_size);
        PARITY("Runtime Patches", scan.complete());
        PARITY("Runtime Patches", SameResolution(scan.mapping.resolution_offsets, build.resolution_sites) &&
                                      scan.mapping.snapshot_scale_site == build.snapshot_scale_site &&
                                      SameSkipIntro(scan.mapping.skip_intro, build.skip_intro) &&
                                      SameBookshelf(scan.mapping.bookshelf, build.bookshelf) &&
                                      SameLzcnt(scan.mapping.lzcnt_offsets, build));
    }
}

void TestScannedMappingFixtures() {
    const MappedExecutable image = SyntheticOdinImage(kFixtureSkew);
    const uint64_t eboot_size = image.image.size();
    const ScanResult scan = ScanExecutable(image, eboot_size);
    CHECK(scan.complete());
    const ScannedMapping& mapping = scan.mapping;
    CHECK(mapping.resolution_offsets.size() == SupportedBuilds()[0].resolution_sites.size());
    CHECK(mapping.snapshot_scale_site == kPlantedSnapshot + kFixtureSkew);
    CHECK(mapping.skip_intro && mapping.bookshelf && mapping.lzcnt_offsets.size() == LZCNT_RANGE_COUNT);
    CHECK(UnresolvedFeatures(mapping).empty());

    // No table of any build describes the fixture's layout, so a scanned build never reuses another build's addresses.
    for (const GameBuild& build : SupportedBuilds()) {
        if (build.mapping != ExecutableMapping::kTable || !TableComplete(build)) {
            continue;
        }
        CHECK(!SameResolution(mapping.resolution_offsets, build.resolution_sites));
        CHECK(!SameSkipIntro(mapping.skip_intro, build.skip_intro) && !SameBookshelf(mapping.bookshelf, build.bookshelf));
        CHECK(!SameLzcnt(mapping.lzcnt_offsets, build) && mapping.snapshot_scale_site != build.snapshot_scale_site);
    }

    // The patches accept the scanned sites and refuse every table's, writing nothing.
    const OslResolution target = osl_render_target(&osl_resolutions[OSL_RESOLUTION_COUNT - 1], &osl_aspect_ratios[0]);
    const std::vector<OslResolutionSite> sites = ResolutionSitesAt(SupportedBuilds(), mapping.resolution_offsets);
    std::vector<unsigned char> scratch = image.image;
    scratch.resize(0x1100000, 0);
    char error[320] = "";
    CHECK(osl_apply_snapshot_scale(scratch.data(), scratch.size(), mapping.snapshot_scale_site, &target, error, sizeof(error)));
    CHECK(osl_apply_resolution(scratch.data(), scratch.size(), sites.data(), sites.size(), &target, error, sizeof(error)));
    CHECK(osl_apply_skip_intro(scratch.data(), 0x30000, &*mapping.skip_intro, 1, error, sizeof(error)));
    for (const GameBuild& build : SupportedBuilds()) {
        if (build.mapping != ExecutableMapping::kTable || !TableComplete(build)) {
            continue;
        }
        std::vector<unsigned char> foreign = image.image;
        foreign.resize(0x1100000, 0);
        const std::vector<unsigned char> before = foreign;
        CHECK(!osl_apply_resolution(foreign.data(), foreign.size(), build.resolution_sites.data(),
                                    build.resolution_sites.size(), &target, error, sizeof(error)));
        CHECK(!osl_apply_snapshot_scale(foreign.data(), foreign.size(), build.snapshot_scale_site, &target, error, sizeof(error)));
        CHECK(!osl_apply_skip_intro(foreign.data(), foreign.size(), build.skip_intro, 1, error, sizeof(error)));
        CHECK(!osl_apply_bookshelf(foreign.data(), foreign.size() - OSL_BOOKSHELF_STUB_BYTES, build.bookshelf,
                                   foreign.data() + foreign.size() - OSL_BOOKSHELF_STUB_BYTES, OSL_BOOKSHELF_STUB_BYTES,
                                   error, sizeof(error)));
        for (const LzcntBuild& lzcnt : lzcnt_builds) {
            if (std::string_view(lzcnt.build) == build.label) {
                CHECK(lzcnt_compat_apply(foreign.data(), foreign.size() - 0x10000, lzcnt.offsets, lzcnt_shapes,
                                         LZCNT_RANGE_COUNT, foreign.data() + foreign.size() - 0x10000, 0x10000) !=
                      LZCNT_COMPAT_OK);
            }
        }
        CHECK(foreign == before);
    }

    // The LZCNT rewrite on the scanned sites: the instructions become jumps to stubs.
    std::vector<unsigned char> rewritten = image.image;
    const size_t stub_bytes = lzcnt_compat_stub_bytes();
    rewritten.resize(image.image.size() + stub_bytes, 0);
    CHECK(lzcnt_compat_apply(rewritten.data(), image.image.size(), mapping.lzcnt_offsets.data(), lzcnt_shapes,
                             LZCNT_RANGE_COUNT, rewritten.data() + image.image.size(), stub_bytes) == LZCNT_COMPAT_OK);
    for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i) {
        CHECK(rewritten[mapping.lzcnt_offsets[i]] == 0xe9);
    }
    CHECK(rewritten != image.image);

    // Persisted in the boot image report and read back unchanged; a report without the keys maps nothing.
    const std::filesystem::path dir = TempDir("scanned-mapping");
    const GameBuild* hk = BuildWithLabel("CUSA03873 1.00 HK");
    CHECK(hk != nullptr);
    if (!hk) {
        return;
    }
    WriteImageReport(dir, *hk, MappingExtra(mapping));
    CHECK(SameMapping(ScannedMappingOfImageDir(dir), mapping));
    WriteImageReport(dir, *hk);
    const ScannedMapping none = ScannedMappingOfImageDir(dir);
    CHECK(none.resolution_offsets.empty() && !none.skip_intro && !none.bookshelf && none.lzcnt_offsets.empty());
    WriteImageReport(dir, *hk, ",\n  \"map_lzcnt\": [1, 2, 3],\n  \"map_skip_intro\": [1, 2],\n  \"map_bookshelf\": [4]");
    const ScannedMapping short_lists = ScannedMappingOfImageDir(dir);
    CHECK(short_lists.lzcnt_offsets.empty() && !short_lists.skip_intro && !short_lists.bookshelf);
    CHECK(JsonNumbers("{\"k\": [1, 22,\n 333]}", "k") == (std::vector<uint64_t>{1, 22, 333}));
    CHECK(JsonNumbers("{\"k\": [1, x]}", "k").empty() && JsonNumbers("{\"k\": 5}", "k").empty() &&
          JsonNumbers("{}", "k").empty());

    // Missing, duplicated, occupied and corrupted sites: the feature is left unmapped, the others are not affected.
    const auto scanned = [&](const std::function<void(MappedExecutable&)>& damage) {
        MappedExecutable copy = image;
        damage(copy);
        return ScanExecutable(copy, copy.image.size());
    };
    const size_t first_site = mapping.resolution_offsets[3];
    const ScanResult missing = scanned([&](MappedExecutable& m) { std::fill_n(m.image.begin() + first_site, 5, uint8_t{0x90}); });
    CHECK(!missing.complete() && missing.mapping.resolution_offsets.empty() && missing.mapping.skip_intro &&
          missing.mapping.bookshelf && !missing.mapping.lzcnt_offsets.empty());
    const ScanResult duplicate = scanned([&](MappedExecutable& m) {
        std::copy_n(m.image.begin() + first_site, 10, m.image.begin() + 0x2e000);
    });
    CHECK(!duplicate.complete() && duplicate.mapping.resolution_offsets.empty() && duplicate.mapping.skip_intro);
    const ScanResult occupied = scanned([&](MappedExecutable& m) { m.image[mapping.skip_intro->stubs + 8] = 0x90; });
    CHECK(!occupied.complete() && !occupied.mapping.skip_intro && Unresolved(occupied, "Skip intro stub space") &&
          occupied.mapping.bookshelf);
    const struct {
        size_t at;
        const char* feature;
    } corrupted[] = {{kPlantedNotice, "skip"}, {kPlantedHd, "skip"}, {kPlantedClassic, "skip"},
                     {kPlantedSnapshot, "snapshot"}, {kPlantedGetter, "bookshelf"}, {kPlantedTotal, "bookshelf"},
                     {kPlantedLzcnt, "lzcnt"}};
    for (const auto& item : corrupted) {
        const ScanResult damaged = scanned([&](MappedExecutable& m) { m.image[item.at + kFixtureSkew] ^= 0x55; });
        const std::string feature = item.feature;
        CHECK(!damaged.complete());
        CHECK((feature == "skip") == !damaged.mapping.skip_intro);
        CHECK((feature == "snapshot") == (damaged.mapping.snapshot_scale_site == 0));
        CHECK((feature == "bookshelf") == !damaged.mapping.bookshelf);
        CHECK((feature == "lzcnt") == damaged.mapping.lzcnt_offsets.empty());
    }
    const ScanResult rejected_site = scanned([&](MappedExecutable& m) {
        m.image[mapping.bookshelf->reads[2] + 5] ^= 0x01;
    });
    CHECK(!rejected_site.complete() && !rejected_site.mapping.bookshelf);

    // A patch that refuses its site writes nothing.
    std::vector<unsigned char> damaged_image = image.image;
    damaged_image[mapping.resolution_offsets[5]] ^= 0xff;
    const std::vector<unsigned char> untouched = damaged_image;
    CHECK(!osl_apply_resolution(damaged_image.data(), damaged_image.size(), sites.data(), sites.size(), &target, error, sizeof(error)));
    CHECK(damaged_image == untouched);
}

bool AnyContains(const std::vector<std::string>& lines, const std::string& part) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) { return Contains(line, part); });
}

// A file whose CRC32 no target names is taken only when every block a target replaces is exactly that target's original
// block, whichever build, archive or entry it is. What is written is the validated blocks alone, and the archive is
// restored to the file as it was.
void TestCompatiblePromptVariants() {
    using namespace cpk_test;
    const fs::path app0 = TempDir("prompts-compatible") / "app0";
    fs::create_directories(app0);
    const std::initializer_list<size_t> classic_runs = {0x340, 0x1000, 0x4ff0}, memory_runs = {0x380, 0x5000};
    const std::string known = Pattern(20480, 3), known_mem = Pattern(24576, 13);
    const std::string painted = Painted(known, classic_runs, 'X'), painted_mem = Painted(known_mem, memory_runs, 'Z');
    const auto regional = [](std::string data) {
        data[0x80] = char(data[0x80] + 1);
        data[0x3000] = char(data[0x3000] + 1);
        return data;
    };
    const auto flip = [](std::string data, size_t at) {
        data[at] = char(data[at] + 1);
        return data;
    };
    const char* const files[] = {"Odin2_OR_XX.CPK", "Odin2_RE_XX.CPK"};
    GameBuild build{};
    build.label = "test build";
    build.data_files = files;
    const auto classic = [](const std::string& iface, const std::string& mem) {
        return BuildCpk({{"Interface.ftx", iface, true}, {"OnMemory.ftx", mem, false}}, true);
    };
    const auto refined = [](const std::string& iface, const std::string& mem) {
        return BuildCpk({{"GUI/HD_Interface.ftx", iface, false}, {"OnMemory/HD_OnMemory.ftx", mem, true}}, false);
    };
    const auto install = [&](const std::string& classic_bytes, const std::string& refined_bytes) {
        for (const char* name : files) fs::remove(UndoPath(app0 / name));
        WriteAll(app0 / files[0], classic_bytes);
        WriteAll(app0 / files[1], refined_bytes);
    };
    const auto untouched = [&](const std::string& classic_bytes, const std::string& refined_bytes, const std::string& patch) {
        return ReadFile(app0 / files[0]) == classic_bytes && ReadFile(app0 / files[1]) == refined_bytes &&
               !fs::exists(UndoPath(app0 / files[0])) && !fs::exists(UndoPath(app0 / files[1])) && !AnyPromptPatch(app0, build) &&
               InstalledPromptStyle(app0, build, Span(patch)) == PromptStyle::kPlayStation;
    };
    const auto entry = [&](const char* file, const char* name) {
        CpkArchive archive;
        const CpkEntry* found = archive.Open(app0 / file) ? archive.Find(name) : nullptr;
        return found ? Bytes(archive.Read(*found)) : std::string("<missing>");
    };
    const std::string patch = MakePatch({{"Interface.ftx", {known, painted}},
                                         {"GUI/HD_Interface.ftx", {known, painted}},
                                         {"OnMemory.ftx", {known_mem, painted_mem}},
                                         {"OnMemory/HD_OnMemory.ftx", {known_mem, painted_mem}}});
    const std::string accepted = "compatible asset variant accepted by verified source blocks";

    // Known copies: the exact path, nothing reported as a variant.
    install(classic(known, known_mem), refined(known, known_mem));
    PromptsResult r = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch));
    CHECK(r.ok && !Contains(r.detail, accepted) && entry(files[0], "Interface.ftx") == painted);
    CHECK(entry(files[1], "OnMemory/HD_OnMemory.ftx") == painted_mem);
    CHECK(SetPromptStyle(app0, build, PromptStyle::kPlayStation, {}).ok && untouched(classic(known, known_mem), refined(known, known_mem), patch));

    // Unknown CRC32, same names and sizes, other bytes elsewhere: all four entries are taken by their source blocks.
    const std::string v = regional(known), vm = regional(known_mem);
    const std::string v_classic = classic(v, vm), v_refined = refined(v, vm);
    const std::string want = Overlay(v, painted, classic_runs), want_mem = Overlay(vm, painted_mem, memory_runs);
    CHECK(want != painted && want_mem != painted_mem);
    install(v_classic, v_refined);
    r = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch));
    CHECK(r.ok && Contains(r.detail, accepted) && r.unsupported.empty());
    CHECK(entry(files[0], "Interface.ftx") == want && entry(files[0], "OnMemory.ftx") == want_mem);
    CHECK(entry(files[1], "GUI/HD_Interface.ftx") == want && entry(files[1], "OnMemory/HD_OnMemory.ftx") == want_mem);
    CHECK(InstalledPromptStyle(app0, build, Span(patch)) == PromptStyle::kXbox);

    // Already patched: detected, and a second Xbox changes nothing.
    const std::string patched_classic = ReadFile(app0 / files[0]), patched_refined = ReadFile(app0 / files[1]);
    r = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch));
    CHECK(r.ok && Contains(r.detail, "already patched") && ReadFile(app0 / files[0]) == patched_classic &&
          ReadFile(app0 / files[1]) == patched_refined);

    // Back to PlayStation: the very bytes of the regional files, again and again.
    for (int i = 0; i < 3; ++i) {
        CHECK(SetPromptStyle(app0, build, PromptStyle::kPlayStation, {}).ok && untouched(v_classic, v_refined, patch));
        CHECK(SetPromptStyle(app0, build, PromptStyle::kPlayStation, {}).ok && untouched(v_classic, v_refined, patch));
        CHECK(SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch)).ok && entry(files[0], "Interface.ftx") == want);
    }
    CHECK(SetPromptStyle(app0, build, PromptStyle::kPlayStation, {}).ok && untouched(v_classic, v_refined, patch));

    // One replaced block differs: refused, and the log names the run and the check.
    install(classic(flip(v, 0x1000), vm), v_refined);
    const std::string bad_classic = ReadFile(app0 / files[0]);
    r = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch));
    CHECK(!r.ok && untouched(bad_classic, v_refined, patch));
    CHECK(AnyContains(r.unsupported, "archive Odin2_OR_XX.CPK, entry Interface.ftx") &&
          AnyContains(r.unsupported, "source blocks incompatible: 1 candidate(s)") && AnyContains(r.unsupported, "first at offset 0x1000"));

    // The first archive is taken, the second is not: the first is put back byte for byte.
    install(v_classic, refined(flip(v, 0x340), vm));
    const std::string bad_refined = ReadFile(app0 / files[1]);
    r = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch));
    CHECK(!r.ok && untouched(v_classic, bad_refined, patch));
    CHECK(AnyContains(r.unsupported, "archive Odin2_RE_XX.CPK, entry GUI/HD_Interface.ftx"));

    // The size differs: no candidate.
    install(classic(v + std::string(16, 'q'), vm), v_refined);
    const std::string wrong_size = ReadFile(app0 / files[0]);
    r = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch));
    CHECK(!r.ok && untouched(wrong_size, v_refined, patch) && AnyContains(r.unsupported, "no candidate: no copy of size 20496"));

    // A DCPX1 patch has no source blocks: its exact copies work, nothing else is taken.
    const std::string patch1 = MakePatch({{"Interface.ftx", {known, painted}}, {"OnMemory.ftx", {known_mem, painted_mem}}}, false);
    CHECK(ParseBlockPatch(Span(patch1)) && !ParseBlockPatch(Span(patch1))->targets[0].source_blocks);
    install(classic(known, known_mem), refined(v, vm));
    CHECK(SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch1)).ok && entry(files[0], "Interface.ftx") == painted);
    CHECK(SetPromptStyle(app0, build, PromptStyle::kPlayStation, {}).ok);
    install(v_classic, v_refined);
    r = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch1));
    CHECK(!r.ok && untouched(v_classic, v_refined, patch1) && AnyContains(r.unsupported, "same-size copy without them"));

    // Several targets that write the same bytes into the file are one match; ones that write different bytes are refused.
    const fs::path single = TempDir("prompts-compatible-targets") / "app0";
    fs::create_directories(single);
    const char* const one[] = {"Odin2_OR_XX.CPK"};
    GameBuild lone{};
    lone.label = "test build";
    lone.data_files = one;
    const std::string other = regional(flip(known, 0x90));
    const std::string same_ink = MakePatch({{"Interface.ftx", {known, painted}}, {"Interface.ftx", {flip(known, 0x90), Overlay(flip(known, 0x90), painted, classic_runs)}}});
    const std::string other_ink =
        MakePatch({{"Interface.ftx", {known, painted}},
                   {"Interface.ftx", {flip(known, 0x90), Overlay(flip(known, 0x90), Painted(known, classic_runs, 'Y'), classic_runs)}}});
    const std::string lone_cpk = BuildCpk({{"Interface.ftx", other, true}}, true);
    WriteAll(single / one[0], lone_cpk);
    r = SetPromptStyle(single, lone, PromptStyle::kXbox, Span(same_ink));
    CHECK(r.ok && Contains(r.detail, accepted) && Contains(r.detail, "2 equivalent candidate(s)"));
    {
        CpkArchive archive;
        CHECK(archive.Open(single / one[0]) && Bytes(archive.Read(*archive.Find("Interface.ftx"))) == Overlay(other, painted, classic_runs));
    }
    CHECK(SetPromptStyle(single, lone, PromptStyle::kPlayStation, {}).ok && ReadFile(single / one[0]) == lone_cpk);
    r = SetPromptStyle(single, lone, PromptStyle::kXbox, Span(other_ink));
    CHECK(!r.ok && ReadFile(single / one[0]) == lone_cpk && !AnyPromptPatch(single, lone));
    CHECK(AnyContains(r.unsupported, "ambiguous: 2 source-block compatible candidates would write different bytes"));
}

// Issue #4: a build whose button art is a copy the patch was not made for is refused as a whole, every archive is left
// as it was, and the log names each unsupported file by build, archive, entry, size and CRC32.
void TestUnsupportedPromptAssets() {
    using namespace cpk_test;
    const GameBuild* hk = BuildWithLabel("CUSA03873 1.00 HK");
    CHECK(hk != nullptr);
    if (!hk) {
        return;
    }
    const fs::path app0 = TempDir("prompts-unsupported") / "app0";
    fs::create_directories(app0);
    const std::string known = Pattern(20480, 3), painted = Painted(known, {0x340, 0x2000}, 'X');
    const std::string known_mem = Pattern(24576, 13), painted_mem = Painted(known_mem, {0x380}, 'Z');
    const std::string odd = Pattern(20480, 17), odd_mem = Pattern(24576, 19);
    const std::string patch = MakePatch({{"Interface.ftx", {known, painted}},
                                         {"GUI/HD_Interface.ftx", {known, painted}},
                                         {"OnMemory.ftx", {known_mem, painted_mem}},
                                         {"OnMemory/HD_OnMemory.ftx", {known_mem, painted_mem}}});
    const std::string boot = BuildCpk({{"other/x.bin", "BOOT" + std::string(600, '\0'), false}}, false);
    const std::string classic_known = BuildCpk({{"Interface.ftx", known, true}, {"OnMemory.ftx", known_mem, false}}, true);
    const std::string classic_odd = BuildCpk({{"Interface.ftx", odd, true}, {"OnMemory.ftx", odd_mem, false}}, true);
    const std::string refined_odd =
        BuildCpk({{"GUI/HD_Interface.ftx", odd, false}, {"OnMemory/HD_OnMemory.ftx", odd_mem, true}}, false);
    const auto unsupported_line = [&](const char* archive, const char* entry, const std::string& data) {
        char tail[64];
        std::snprintf(tail, sizeof(tail), ", size %zu, CRC32 %08x", data.size(), Crc32(Span(data)));
        return std::string("Controller prompts unsupported asset: build CUSA03873 1.00 HK, archive ") + archive + ", entry " +
               entry + tail;
    };

    // The reporter's case: Classic holds copies the patch does not know, and so does Refined.
    WriteAll(app0 / "Odin2_BT_CN.CPK", boot);
    WriteAll(app0 / "Odin2_OR_CN.CPK", classic_odd);
    WriteAll(app0 / "Odin2_RE_CN.CPK", refined_odd);
    PromptsResult r = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(patch));
    CHECK(!r.ok && !r.cancelled);
    CHECK(Contains(r.detail, "Odin2_OR_CN.CPK: Interface.ftx is not the file the patch was made for, OnMemory.ftx is not the file"));
    CHECK(r.unsupported.size() == 8);
    if (r.unsupported.size() == 8) {
        const std::string expected[] = {unsupported_line("Odin2_OR_CN.CPK", "Interface.ftx", odd),
                                        unsupported_line("Odin2_OR_CN.CPK", "OnMemory.ftx", odd_mem),
                                        unsupported_line("Odin2_RE_CN.CPK", "GUI/HD_Interface.ftx", odd),
                                        unsupported_line("Odin2_RE_CN.CPK", "OnMemory/HD_OnMemory.ftx", odd_mem)};
        for (size_t i = 0; i < 4; ++i) {
            CHECK(r.unsupported[2 * i] == expected[i]);
            CHECK(Contains(r.unsupported[2 * i + 1], "source blocks incompatible: 1 candidate(s); closest copy CRC32 "));
            CHECK(Contains(r.unsupported[2 * i + 1], i % 2 ? "first at offset 0x380" : "first at offset 0x340"));
        }
    }
    CHECK(ReadFile(app0 / "Odin2_BT_CN.CPK") == boot && ReadFile(app0 / "Odin2_OR_CN.CPK") == classic_odd &&
          ReadFile(app0 / "Odin2_RE_CN.CPK") == refined_odd && !AnyPromptPatch(app0, *hk));

    // Classic is a known copy and gets patched before Refined is found unknown: it is put back byte for byte.
    WriteAll(app0 / "Odin2_OR_CN.CPK", classic_known);
    r = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(patch));
    CHECK(!r.ok && r.unsupported.size() == 4 && Contains(r.unsupported[0], "archive Odin2_RE_CN.CPK, entry GUI/HD_Interface.ftx"));
    CHECK(ReadFile(app0 / "Odin2_OR_CN.CPK") == classic_known && ReadFile(app0 / "Odin2_RE_CN.CPK") == refined_odd);
    CHECK(!AnyPromptPatch(app0, *hk) && InstalledPromptStyle(app0, *hk, Span(patch)) == PromptStyle::kPlayStation);

    // Failures that are not an unknown copy name no file.
    r = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(std::string("DCPX1")));
    CHECK(!r.ok && r.unsupported.empty());
}

// Sets the last four bytes so that the CRC32 of the whole is `want`.
std::string WithCrc32(std::string data, uint32_t want) {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) value = value & 1 ? 0xEDB88320u ^ (value >> 1) : value >> 1;
        table[i] = value;
    }
    // CRC32 table entries have distinct top bytes, so the register can be walked back to the index of each step.
    std::array<uint32_t, 4> steps{};
    uint32_t reg = ~want;
    for (int i = 3; i >= 0; --i) {
        const auto step = std::find_if(table.begin(), table.end(), [&](uint32_t value) { return value >> 24 == reg >> 24; });
        steps[size_t(i)] = uint32_t(step - table.begin());
        reg = (reg ^ *step) << 8;
    }
    const size_t tail = data.size() - 4;
    uint32_t state = ~Crc32({reinterpret_cast<const uint8_t*>(data.data()), tail});
    for (size_t i = 0; i < 4; ++i) {
        data[tail + i] = char((state ^ steps[i]) & 255);
        state = (state >> 8) ^ table[steps[i]];
    }
    return data;
}

std::string WithBlockCrc32(std::string data, size_t offset, uint32_t want) {
    data.replace(offset, 16, WithCrc32(data.substr(offset, 16), want));
    return data;
}

// Issue #4: the reporter's Hong Kong button art has no target and its original bytes are not available. Generated bytes
// stand in for it, with the sizes, CRC32s and block mismatch of the reporter's log, and go through SetPromptStyle.
void TestHongKongPromptProfile() {
    using namespace cpk_test;
    const GameBuild* hk = BuildWithLabel("CUSA03873 1.00 HK");
    CHECK(hk != nullptr);
    if (!hk) {
        return;
    }
    constexpr size_t kSize = 4458000, kRuns = 3368, kFirstRun = 0x4004d0, kSharedBlocks = 1205;
    constexpr size_t kMemorySize = 5244048, kMemoryRuns = 713, kFirstMemoryRun = 0x1fef70;
    constexpr uint32_t kHkCrc = 0xc2d4a197, kUsaCrc = 0xe47e29e5, kJapanCrc = 0x3fe97430;
    constexpr uint32_t kHkMemoryCrc = 0x1b15d0db, kMemoryCrcA = 0x3dca4e69, kMemoryCrcB = 0xb949a3bd;
    const auto xbox = [&](std::string data) {
        for (size_t run = 0; run < kRuns; ++run) data.replace(kFirstRun + run * 16, 16, 16, 'X');
        return data;
    };
    const auto memory_xbox = [&](std::string data) {
        for (size_t run = 0; run < kMemoryRuns; ++run) data.replace(kFirstMemoryRun + run * 16, 16, 16, 'Z');
        return data;
    };

    const std::string usa_art = WithCrc32(WithBlockCrc32(Pattern(kSize, 3), 0x4004f0, 0x3f3e109a), kUsaCrc);
    const std::string japan_art = WithCrc32(Pattern(kSize, 5), kJapanCrc);
    std::string fake = Pattern(kSize, 11);
    for (size_t shared = 0; shared < kSharedBlocks; ++shared) {
        const size_t offset = kFirstRun + (shared < 2 ? shared : kRuns - kSharedBlocks + shared) * 16;
        fake.replace(offset, 16, usa_art, offset, 16);
    }
    const std::string hk_art = WithCrc32(WithBlockCrc32(fake, 0x4004f0, 0xc95f59a0), kHkCrc);
    const std::string hk_xbox = xbox(hk_art);
    CHECK(Crc32(Span(usa_art)) == kUsaCrc && Crc32(Span(japan_art)) == kJapanCrc);
    CHECK(Crc32(Span(hk_art)) == kHkCrc && hk_art.size() == kSize);

    const std::string plain_memory = Pattern(kMemorySize, 13);
    const std::string memory_a = WithCrc32(plain_memory, kMemoryCrcA);
    std::string other_memory = plain_memory, regional_memory = plain_memory;
    other_memory[0x90] = char(other_memory[0x90] + 1);
    regional_memory[0x3000] = char(regional_memory[0x3000] + 1);
    const std::string memory_b = WithCrc32(other_memory, kMemoryCrcB), hk_memory = WithCrc32(regional_memory, kHkMemoryCrc);
    const std::string hk_memory_x = memory_xbox(hk_memory);
    CHECK(Crc32(Span(hk_memory)) == kHkMemoryCrc && hk_memory.size() == kMemorySize);

    const std::string atlas = Pattern(6144, 5), atlas_x = Painted(atlas, {0x340}, 'T');
    const std::string layout = Pattern(4096, 9), layout_x = Painted(layout, {0x80}, 'R');
    const std::string title_patch = MakePatch({{"title.ftx", {atlas, atlas_x}},
                                               {"title.mbs", {layout, layout_x}},
                                               {"GUI/HD_title.ftx", {atlas, atlas_x}},
                                               {"GUI/REHD_title.mbs", {layout, layout_x}}});

    const std::string patch_bytes = MakePatch({{"Interface.ftx", {usa_art, xbox(usa_art)}},
                                               {"Interface.ftx", {japan_art, xbox(japan_art)}},
                                               {"GUI/HD_Interface.ftx", {usa_art, xbox(usa_art)}},
                                               {"GUI/HD_Interface.ftx", {japan_art, xbox(japan_art)}},
                                               {"OnMemory.ftx", {memory_a, memory_xbox(memory_a)}},
                                               {"OnMemory.ftx", {memory_b, memory_xbox(memory_b)}},
                                               {"OnMemory/HD_OnMemory.ftx", {memory_a, memory_xbox(memory_a)}},
                                               {"OnMemory/HD_OnMemory.ftx", {memory_b, memory_xbox(memory_b)}}});
    const std::optional<BlockPatch> patch = ParseBlockPatch(Span(patch_bytes));
    const std::string shipped_bytes = ReadFile(fs::path(OSL_SOURCE_DIR) / "launcher" / "prompts" / "xbox-prompts.dcpx");
    const std::optional<BlockPatch> shipped = ParseBlockPatch(Span(shipped_bytes));
    CHECK(patch && shipped);
    if (!patch || !shipped) {
        return;
    }
    const std::span<const KnownEntryProfile> profiles = hk->prompt_profiles;
    CHECK(profiles.size() == 2);

    // Generic matching refuses the reporter's copy with the reporter's own line; only the exact identity is taken.
    const std::string refusal = "source blocks incompatible: 2 candidate(s); closest copy CRC32 e47e29e5 has 2163 of 3368 blocks "
                                "different, first at offset 0x4004f0 (expected block CRC32 3f3e109a, found c95f59a0)";
    for (const std::string name : {"Interface.ftx", "GUI/HD_Interface.ftx"}) {
        const EntryMatch generic = MatchEntry(*patch, name, Span(hk_art), kHkCrc);
        CHECK(generic.state == EntryState::kUnknown && generic.analysis == refusal && !generic.by_profile);
        CHECK(MatchEntryWithProfiles(*patch, {}, name, Span(hk_art), kHkCrc).state == EntryState::kUnknown);
        const EntryMatch taken = MatchEntryWithProfiles(*patch, profiles, name, Span(hk_art), kHkCrc);
        CHECK(taken.state == EntryState::kOriginal && taken.by_profile && !taken.exact && !taken.ambiguous);
        CHECK(taken.target && taken.target->crc_before == kUsaCrc && taken.target->name == name && taken.target->runs.size() == kRuns);
        CHECK(taken.analysis == "Controller prompts HK compatibility: " + name + ", CRC32 c2d4a197, reference e47e29e5");

        const EntryMatch patched = MatchEntryWithProfiles(*patch, profiles, name, Span(hk_xbox), Crc32(Span(hk_xbox)));
        CHECK(patched.state == EntryState::kPatched && !patched.by_profile);
    }

    // The real reference: its runs are a part of the Japanese target's, which writes the same bytes there.
    const auto shipped_target = [&](std::string_view name, uint32_t crc) -> const BlockPatchTarget* {
        for (const BlockPatchTarget& target : shipped->targets) {
            if (target.name == name && target.size == kSize && target.crc_before == crc) return &target;
        }
        return nullptr;
    };
    const BlockPatchTarget* shipped_reference = shipped_target("Interface.ftx", kUsaCrc);
    for (const KnownEntryProfile& profile : profiles) {
        CHECK(profile.size == kSize && profile.crc == kHkCrc && profile.reference_crc == kUsaCrc);
        const BlockPatchTarget* reference = shipped_target(profile.name, profile.reference_crc);
        const BlockPatchTarget* japanese = shipped_target(profile.name, kJapanCrc);
        CHECK(reference && japanese);
        if (!reference || !japanese) {
            continue;
        }
        CHECK(reference->runs.size() == kRuns);
        size_t same = 0;
        for (const BlockRun& run : reference->runs) {
            const auto found = std::find_if(japanese->runs.begin(), japanese->runs.end(), [&](const BlockRun& candidate) { return candidate.offset == run.offset; });
            same += found != japanese->runs.end() && found->data == run.data;
        }
        CHECK(same == kRuns);
        const EntryMatch match = MatchEntryWithProfiles(*shipped, profiles, profile.name, Span(hk_art), kHkCrc);
        CHECK(match.state == EntryState::kOriginal && match.by_profile && match.target == reference);
        CHECK(MatchEntry(*shipped, profile.name, Span(hk_art), kHkCrc).state == EntryState::kUnknown);
    }
    CHECK(shipped_reference != nullptr);
    if (!shipped_reference) {
        return;
    }
    std::string shipped_xbox = hk_art;
    for (const BlockRun& run : shipped_reference->runs) {
        shipped_xbox.replace(run.offset, 16, reinterpret_cast<const char*>(run.data.data()), 16);
    }

    // Wrong CRC32, size or name, and other same-size copies: the generic answer stands.
    const auto refused = [&](std::string_view name, const std::string& data) {
        const EntryMatch match = MatchEntryWithProfiles(*patch, profiles, name, Span(data), Crc32(Span(data)));
        return match.state == EntryState::kUnknown && !match.by_profile && !match.target && !Contains(match.analysis, "HK compatibility");
    };
    std::string wrong_crc = hk_art;
    wrong_crc[0x100] = char(wrong_crc[0x100] + 1);
    CHECK(Crc32(Span(wrong_crc)) != kHkCrc);
    CHECK(refused("Interface.ftx", wrong_crc) && refused("GUI/HD_Interface.ftx", wrong_crc));
    const std::string longer = WithCrc32(hk_art + std::string(16, 'q'), kHkCrc), shorter = WithCrc32(hk_art.substr(0, kSize - 16), kHkCrc);
    for (const std::string& data : {longer, shorter}) {
        CHECK(Crc32(Span(data)) == kHkCrc && data.size() != kSize);
        CHECK(refused("Interface.ftx", data) && refused("GUI/HD_Interface.ftx", data));
    }
    for (const char* name : {"interface.ftx", "INTERFACE.FTX", "GUI/hd_interface.ftx", "HD_Interface.ftx", "GUI/Interface.ftx", "Interface.ftx.bak", "OnMemory.ftx"}) {
        CHECK(refused(name, hk_art));
    }
    const std::string same_size = Pattern(kSize, 17);
    CHECK(refused("Interface.ftx", same_size) && refused("GUI/HD_Interface.ftx", same_size));

    // Rejected despite the identity: the reason is reported.
    const std::string no_reference = MakePatch({{"Interface.ftx", {japan_art, xbox(japan_art)}}});
    const EntryMatch unreferenced = MatchEntryWithProfiles(*ParseBlockPatch(Span(no_reference)), profiles, "Interface.ftx", Span(hk_art), kHkCrc);
    CHECK(unreferenced.state == EntryState::kUnknown &&
          unreferenced.analysis == "Controller prompts HK compatibility: Interface.ftx, CRC32 c2d4a197 rejected: reference target e47e29e5 is not in the patch");
    {
        const std::string base = Pattern(4096, 3), variant = Painted(base, {0x900}, 'v'), ambiguous_file = Painted(base, {0xA00}, 'w');
        const std::string conflicting = MakePatch({{"Interface.ftx", {base, Painted(base, {0x340}, 'X')}},
                                                   {"Interface.ftx", {variant, Painted(variant, {0x340}, 'Y')}}});
        const KnownEntryProfile strict[] = {{"HK", "Interface.ftx", 4096, Crc32(Span(ambiguous_file)), Crc32(Span(base))}};
        const EntryMatch match = MatchEntryWithProfiles(*ParseBlockPatch(Span(conflicting)), strict, "Interface.ftx", Span(ambiguous_file),
                                                        Crc32(Span(ambiguous_file)));
        CHECK(match.state == EntryState::kUnknown && !match.by_profile && Contains(match.analysis, " rejected: ambiguous: 2 source-block"));
    }

    // A synthetic CUSA03873-1.00 folder. An empty string leaves that entry out of its archive.
    const fs::path app0 = TempDir("prompts-hk-profile") / "CUSA03873-1.00";
    fs::create_directories(app0);
    const char* const kBoot = "Odin2_BT_CN.CPK";
    const char* const kClassic = "Odin2_OR_CN.CPK";
    const char* const kRefined = "Odin2_RE_CN.CPK";
    const auto archive_of = [&](const char* art_name, const std::string& art, bool art_compressed, const char* memory_name, const std::string& memory,
                                bool memory_compressed, const char* title_atlas, const char* title_layout, bool encrypt_toc) {
        std::vector<Entry> entries;
        if (!art.empty()) entries.push_back({art_name, art, art_compressed});
        if (!memory.empty()) entries.push_back({memory_name, memory, memory_compressed});
        entries.push_back({title_atlas, atlas, false});
        entries.push_back({title_layout, layout, false});
        return BuildCpk(entries, encrypt_toc);
    };
    const auto classic = [&](const std::string& art, const std::string& memory) {
        return archive_of("Interface.ftx", art, false, "OnMemory.ftx", memory, true, "title.ftx", "title.mbs", true);
    };
    const auto refined = [&](const std::string& art, const std::string& memory) {
        return archive_of("GUI/HD_Interface.ftx", art, false, "OnMemory/HD_OnMemory.ftx", memory, false, "GUI/HD_title.ftx", "GUI/REHD_title.mbs", false);
    };
    const std::string boot = BuildCpk({{"other/x.bin", "BOOT" + std::string(600, '\0'), false}}, false);
    const std::string hk_classic = classic(hk_art, hk_memory), hk_refined = refined(hk_art, hk_memory);
    const auto install = [&](const std::string& classic_bytes, const std::string& refined_bytes) {
        for (const char* file : {kBoot, kClassic, kRefined}) {
            fs::remove(UndoPath(app0 / file));
            fs::remove(UndoPath(app0 / file, kTitleUndoSuffix));
        }
        WriteAll(app0 / kBoot, boot);
        WriteAll(app0 / kClassic, classic_bytes);
        WriteAll(app0 / kRefined, refined_bytes);
    };
    const auto as_installed = [&](const std::string& classic_bytes, const std::string& refined_bytes) {
        return ReadFile(app0 / kBoot) == boot && ReadFile(app0 / kClassic) == classic_bytes && ReadFile(app0 / kRefined) == refined_bytes &&
               !fs::exists(UndoPath(app0 / kClassic)) && !fs::exists(UndoPath(app0 / kRefined));
    };
    const auto entry = [&](const char* archive, const char* name) {
        CpkArchive cpk;
        const CpkEntry* found = cpk.Open(app0 / archive) ? cpk.Find(name) : nullptr;
        return found ? Bytes(cpk.Read(*found)) : std::string("<missing>");
    };
    const auto title_on = [&] {
        return entry(kClassic, "title.ftx") == atlas_x && entry(kRefined, "GUI/HD_title.ftx") == atlas_x &&
               entry(kClassic, "title.mbs") == layout_x && entry(kRefined, "GUI/REHD_title.mbs") == layout_x &&
               fs::exists(UndoPath(app0 / kClassic, kTitleUndoSuffix)) && fs::exists(UndoPath(app0 / kRefined, kTitleUndoSuffix)) &&
               !TitleExitPending(app0, *hk, Span(title_patch));
    };

    // PlayStation -> Xbox -> PlayStation -> Xbox as the launcher runs it, the title menu layer on. Each present Interface
    // entry must arrive through the profile, and an archive without art must stay out of the prompts patch.
    size_t layer_steps = 0;
    bool title_off_for_prompts = true;
    PatchHooks watch;
    watch.phase = [&](const char* text) {
        const std::string step(text);
        if (step.starts_with("Patching Odin2_") || step.starts_with("Restoring Odin2_")) {
            ++layer_steps;
            title_off_for_prompts = title_off_for_prompts && !fs::exists(UndoPath(app0 / kClassic, kTitleUndoSuffix)) &&
                                    !fs::exists(UndoPath(app0 / kRefined, kTitleUndoSuffix));
        }
    };
    const auto cycle = [&](const std::string& used_patch, const std::string& xbox_art, const std::string& classic_art,
                           const std::string& refined_art, const std::string& memory) {
        const std::string classic_bytes = classic(classic_art, memory), refined_bytes = refined(refined_art, memory);
        const std::optional<BlockPatch> parsed = ParseBlockPatch(Span(used_patch));
        CHECK(parsed.has_value());
        if (!parsed) {
            return;
        }
        const auto holds_xbox = [&](const char* archive, const std::string& art) {
            const bool has_prompts = !art.empty() || !memory.empty();
            return has_prompts ? fs::exists(UndoPath(app0 / archive)) && HoldsBlockPatch(app0 / archive, *parsed) : !fs::exists(UndoPath(app0 / archive));
        };
        const auto xbox_on = [&] {
            return holds_xbox(kClassic, classic_art) && holds_xbox(kRefined, refined_art) &&
                   entry(kClassic, "Interface.ftx") == (classic_art.empty() ? "<missing>" : xbox_art) &&
                   entry(kRefined, "GUI/HD_Interface.ftx") == (refined_art.empty() ? "<missing>" : xbox_art) &&
                   (memory.empty() || (entry(kClassic, "OnMemory.ftx") == hk_memory_x && entry(kRefined, "OnMemory/HD_OnMemory.ftx") == hk_memory_x)) &&
                   InstalledPromptStyle(app0, *hk, Span(used_patch)) == PromptStyle::kXbox && AnyPromptPatch(app0, *hk);
        };
        const auto playstation_on = [&] {
            return !AnyPromptPatch(app0, *hk) && !HoldsBlockPatch(app0 / kClassic, *parsed) && !HoldsBlockPatch(app0 / kRefined, *parsed) &&
                   entry(kClassic, "Interface.ftx") == (classic_art.empty() ? "<missing>" : classic_art) &&
                   entry(kRefined, "GUI/HD_Interface.ftx") == (refined_art.empty() ? "<missing>" : refined_art) &&
                   InstalledPromptStyle(app0, *hk, Span(used_patch)) == PromptStyle::kPlayStation;
        };
        const std::string classic_line = "Controller prompts HK compatibility: Interface.ftx, CRC32 c2d4a197, reference e47e29e5";
        const std::string refined_line = "Controller prompts HK compatibility: GUI/HD_Interface.ftx, CRC32 c2d4a197, reference e47e29e5";
        install(classic_bytes, refined_bytes);
        CHECK(ApplyTitleExit(app0, *hk, Span(title_patch)).ok && title_on() && playstation_on());

        layer_steps = 0;
        title_off_for_prompts = true;
        PromptsResult r = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(used_patch), watch, Span(title_patch));
        CHECK(r.ok && !r.cancelled && r.unsupported.empty() && xbox_on() && title_on());
        CHECK(layer_steps > 0 && title_off_for_prompts && Contains(r.detail, "title menu: updated"));
        CHECK(Contains(r.detail, classic_line) == !classic_art.empty() && Contains(r.detail, refined_line) == !refined_art.empty());
        CHECK(!Contains(r.detail, "Interface.ftx (CRC32"));

        r = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(used_patch), {}, Span(title_patch));
        CHECK(r.ok && Contains(r.detail, "already patched") && !Contains(r.detail, "HK compatibility") && xbox_on() && title_on());

        layer_steps = 0;
        title_off_for_prompts = true;
        r = SetPromptStyle(app0, *hk, PromptStyle::kPlayStation, {}, watch, Span(title_patch));
        CHECK(r.ok && playstation_on() && title_on() && layer_steps > 0 && title_off_for_prompts);
        CHECK(RevertTitleExit(app0, *hk).ok && as_installed(classic_bytes, refined_bytes));
        CHECK(ApplyTitleExit(app0, *hk, Span(title_patch)).ok);

        r = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(used_patch), {}, Span(title_patch));
        CHECK(r.ok && r.unsupported.empty() && xbox_on() && title_on());
        CHECK(Contains(r.detail, classic_line) == !classic_art.empty() && Contains(r.detail, refined_line) == !refined_art.empty());

        CHECK(SetPromptStyle(app0, *hk, PromptStyle::kPlayStation, {}).ok && playstation_on());
        CHECK(RevertTitleExit(app0, *hk).ok && as_installed(classic_bytes, refined_bytes));
    };
    cycle(patch_bytes, hk_xbox, hk_art, hk_art, hk_memory);
    cycle(patch_bytes, hk_xbox, hk_art, "", hk_memory);
    cycle(patch_bytes, hk_xbox, "", hk_art, hk_memory);
    cycle(shipped_bytes, shipped_xbox, hk_art, hk_art, "");
    cycle(shipped_bytes, shipped_xbox, hk_art, "", "");
    cycle(shipped_bytes, shipped_xbox, "", hk_art, "");

    // OnMemory is taken by its verified source blocks, under the reporter's own line, and never by the profile.
    install(hk_classic, hk_refined);
    PromptsResult r = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(patch_bytes));
    CHECK(r.ok && entry(kClassic, "OnMemory.ftx") == hk_memory_x && entry(kRefined, "OnMemory/HD_OnMemory.ftx") == hk_memory_x);
    CHECK(Contains(r.detail, "OnMemory.ftx (CRC32 1b15d0db): compatible asset variant accepted by verified source blocks of the copy "
                             "CRC32 3dca4e69 (713 blocks, 2 equivalent candidate(s))"));
    CHECK(Contains(r.detail, "OnMemory/HD_OnMemory.ftx (CRC32 1b15d0db): compatible asset variant accepted by verified source blocks"));
    const EntryMatch memory_match = MatchEntry(*patch, "OnMemory.ftx", Span(hk_memory), kHkMemoryCrc);
    CHECK(memory_match.state == EntryState::kOriginal && !memory_match.by_profile && !memory_match.exact);
    CHECK(SetPromptStyle(app0, *hk, PromptStyle::kPlayStation, {}).ok && as_installed(hk_classic, hk_refined));

    // Every refusal below comes from SetPromptStyle and leaves the archives as they were.
    const auto refused_install = [&](const GameBuild& build, const std::string& classic_bytes, const std::string& refined_bytes) {
        install(classic_bytes, refined_bytes);
        const PromptsResult result = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(patch_bytes));
        return !result.ok && !Contains(result.detail, "HK compatibility") && !AnyContains(result.unsupported, "HK compatibility") &&
               !AnyPromptPatch(app0, build) && as_installed(classic_bytes, refined_bytes);
    };

    // The same files in the same archive names, under every other build.
    size_t other_builds = 0;
    const std::vector<const GameBuild*> same_package = SamePackageBuilds(*hk);
    for (const GameBuild& build : SupportedBuilds()) {
        const bool shares = std::find(same_package.begin(), same_package.end(), &build) != same_package.end();
        CHECK(build.prompt_profiles.empty() == !shares);
        if (shares) {
            continue;
        }
        GameBuild moved = build;
        moved.data_files = hk->data_files;
        CHECK(refused_install(moved, hk_classic, hk_refined));
        CHECK(AnyContains(SetPromptStyle(app0, moved, PromptStyle::kXbox, Span(patch_bytes)).unsupported, refusal));
        CHECK(as_installed(hk_classic, hk_refined));
        ++other_builds;
    }
    CHECK(other_builds == 6);

    // The right build, the wrong file.
    CHECK(refused_install(*hk, classic(wrong_crc, ""), refined("", "")));
    CHECK(refused_install(*hk, classic("", ""), refined(wrong_crc, "")));
    CHECK(refused_install(*hk, classic(longer, ""), refined("", "")));
    CHECK(refused_install(*hk, classic("", ""), refined(shorter, "")));
    CHECK(refused_install(*hk, classic(same_size, ""), refined("", "")));
    CHECK(refused_install(*hk, classic("", ""), refined(same_size, "")));
    CHECK(refused_install(*hk, BuildCpk({{"INTERFACE.FTX", hk_art, false}}, true), refined("", "")));
    CHECK(refused_install(*hk, classic("", ""), BuildCpk({{"GUI/hd_interface.ftx", hk_art, false}}, false)));
    CHECK(refused_install(*hk, BuildCpk({{"Interface2.ftx", hk_art, false}}, true), BuildCpk({{"GUI/HD_Interface2.ftx", hk_art, false}}, false)));

    // A patch without the reference: the identified file is refused, with the reason, and nothing is written.
    install(hk_classic, hk_refined);
    r = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(no_reference));
    CHECK(!r.ok && as_installed(hk_classic, hk_refined) && !AnyPromptPatch(app0, *hk));
    CHECK(AnyContains(r.unsupported, "rejected: reference target e47e29e5 is not in the patch"));

    // Whatever cannot be taken after the profile's file was written puts every archive back, byte for byte.
    const std::string bad_classic = classic(wrong_crc, hk_memory), bad_refined = refined(wrong_crc, hk_memory);
    const std::string bad_memory = refined(hk_art, Pattern(kMemorySize, 23));
    const auto rolled_back = [&](const PromptsResult& result, const char* archive, const char* name, const std::string& classic_bytes,
                                 const std::string& refined_bytes) {
        return !result.ok && !result.cancelled && result.unsupported.size() == 2 &&
               AnyContains(result.unsupported, std::string("archive ") + archive + ", entry " + name) && !AnyPromptPatch(app0, *hk) &&
               InstalledPromptStyle(app0, *hk, Span(patch_bytes)) == PromptStyle::kPlayStation && as_installed(classic_bytes, refined_bytes);
    };
    install(hk_classic, bad_refined);
    r = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(patch_bytes));
    CHECK(rolled_back(r, kRefined, "GUI/HD_Interface.ftx", hk_classic, bad_refined));
    CHECK(Contains(r.detail, "Odin2_OR_CN.CPK: 2 file(s) patched") && Contains(r.detail, "Odin2_OR_CN.CPK: reverted"));
    CHECK(Contains(r.detail, "Controller prompts HK compatibility: Interface.ftx"));
    install(hk_classic, bad_memory);
    r = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(patch_bytes));
    CHECK(rolled_back(r, kRefined, "OnMemory/HD_OnMemory.ftx", hk_classic, bad_memory));
    CHECK(Contains(r.detail, "Odin2_OR_CN.CPK: reverted") && Contains(r.detail, "Odin2_RE_CN.CPK: reverted"));
    install(bad_classic, hk_refined);
    r = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(patch_bytes));
    CHECK(rolled_back(r, kClassic, "Interface.ftx", bad_classic, hk_refined));

    // A refused change leaves the title menu layer on.
    install(hk_classic, bad_refined);
    CHECK(ApplyTitleExit(app0, *hk, Span(title_patch)).ok);
    r = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(patch_bytes), {}, Span(title_patch));
    CHECK(!r.ok && !AnyPromptPatch(app0, *hk) && title_on() && entry(kClassic, "Interface.ftx") == hk_art);
    CHECK(RevertTitleExit(app0, *hk).ok && as_installed(hk_classic, bad_refined));

    // A cancelled change in the second archive leaves neither patched.
    install(hk_classic, hk_refined);
    bool in_refined = false;
    PatchHooks cancel;
    cancel.phase = [&](const char* text) { in_refined = in_refined || Contains(text, kRefined); };
    cancel.progress = [&](uint64_t, uint64_t) { return !in_refined; };
    r = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(patch_bytes), cancel);
    CHECK(!r.ok && r.cancelled && as_installed(hk_classic, hk_refined) && !AnyPromptPatch(app0, *hk));
}

void TestHongKongBuild() {
    const GameBuild* found = BuildWithLabel("CUSA03873 1.00 HK");
    CHECK(found && found + 1 == BuildWithLabel("CUSA03873 1.00 Asia"));
    if (!found) {
        return;
    }
    const GameBuild& hk = *found;
    CHECK(std::string(hk.title_id) == "CUSA03873");
    CHECK(std::string(hk.content_id) == "HP0177-CUSA03873_00-ODINSPHERELE0000");
    CHECK(std::string(hk.version) == "1.00" && std::string(hk.folder) == "CUSA03873-1.00");
    CHECK(std::string(hk.eboot_sha256) == "8c483dc2c06d958ccd84bdbf726df6d23e31592c7c1bb39eb5425718b5bc1eb3");
    CHECK(std::string(hk.region) == "HK" && hk.extra_files.empty() && hk.layers.size() == 1);
    const std::vector<std::string> data(hk.data_files.begin(), hk.data_files.end());
    CHECK((data == std::vector<std::string>{"Odin2_BT_CN.CPK", "Odin2_OR_CN.CPK", "Odin2_RE_CN.CPK"}));
    CHECK(hk.mapping == ExecutableMapping::kScanned && hk.text_mode == TextLanguageMode::kSelectable);
    const std::span<const TextLanguage> languages = TextLanguagesOf(&hk);
    CHECK(languages.size() == 2);
    if (languages.size() == 2) {
        CHECK(languages[0].key == "traditional_chinese" && languages[0].name == "Traditional Chinese" &&
              languages[0].ps4_id == 10);
        CHECK(languages[1].key == "korean" && languages[1].name == "Korean" && languages[1].ps4_id == 9);
        CHECK(&ResolveTextLanguage(&hk, "") == &languages[0]);
    }
    CHECK(BuildMetadataText(hk) == "APP_VER 1.00 \xC2\xB7 VERSION 1.00");
    CHECK(InstalledGameName(GameFileState::kValid, &hk) == "PS4 CUSA03873 1.00 HK");

    const std::filesystem::path dir = TempDir("hong-kong");
    const std::vector<std::pair<std::string, std::string>> base = {
        {"TITLE_ID", "CUSA03873"}, {"CONTENT_ID", "HP0177-CUSA03873_00-ODINSPHERELE0000"},
        {"CATEGORY", "gd"},        {"APP_VER", "01.00"},
        {"VERSION", "01.00"},
    };
    auto update = base;
    update[2].second = "gp";
    update[3].second = "01.01";
    const GameCheck unknown_update = CheckPackageIdentity(SfoText(dir, update));
    CHECK(unknown_update.state == GameFileState::kDifferentVersion && unknown_update.build == &hk);
    auto other_content = base;
    other_content[1].second = "HP0177-CUSA03873_00-OTHERGAME0000000";
    const GameCheck foreign = CheckPackageIdentity(SfoText(dir, other_content));
    CHECK(foreign.state == GameFileState::kDifferentVersion && foreign.detail.starts_with("param.sfo CONTENT_ID"));

    const std::filesystem::path game = dir / "game";
    std::filesystem::create_directories(game / "sce_sys");
    std::filesystem::create_directories(game / "sce_module");
    WriteSfo(game / "sce_sys" / "param.sfo", base);
    for (const char* name : {"eboot.bin", "sce_discmap.plt", "sce_module/libc.prx", "sce_module/libSceFios2.prx",
                             "Odin2_BT_CN.CPK", "Odin2_OR_CN.CPK"}) {
        std::ofstream(game / name, std::ios::binary) << "x";
    }
    const GameCheck tree = CheckGameTree(game);
    CHECK(tree.state == GameFileState::kMissing && tree.build == &hk && tree.detail == "missing Odin2_RE_CN.CPK");
    std::ofstream(game / "Odin2_RE_CN.CPK", std::ios::binary) << "x";
    const GameCheck other_eboot = CheckGameTree(game);
    CHECK(other_eboot.state == GameFileState::kDifferentVersion && other_eboot.build == &hk);
    const GameRoots roots{dir / "prepared", dir / "image"};
    const InstallResult diagnosed = InstallFromFolder(game, roots);
    CHECK(diagnosed.status == InstallStatus::kUnsupportedBuild);
    CHECK(Contains(diagnosed.report, "Supported-build check: eboot.bin is not the supported build"));
    CHECK(Contains(diagnosed.report, "TITLE_ID CUSA03873"));
    CHECK(!std::filesystem::exists(roots.prepared) && !std::filesystem::exists(roots.image));
}

void WriteGameFile(const std::filesystem::path& root, const std::string& name, const std::string& content) {
    std::filesystem::create_directories((root / name).parent_path());
    std::ofstream(root / name, std::ios::binary | std::ios::trunc) << content;
}

std::map<std::string, std::string> TreeContents(const std::filesystem::path& root) {
    std::map<std::string, std::string> files;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            files[std::filesystem::relative(it->path(), root).generic_string()] = ReadFile(it->path());
        }
    }
    return files;
}

void TestUsa101Identity() {
    const GameBuild* base_found = BuildWithLabel("CUSA05083 1.00 USA");
    const GameBuild* update_found = BuildWithLabel("CUSA05083 1.01 USA");
    CHECK(base_found && update_found);
    if (!base_found || !update_found) {
        return;
    }
    const GameBuild& base = *base_found;
    const GameBuild& update = *update_found;
    CHECK(std::string(update.title_id) == "CUSA05083" && std::string(update.content_id) == base.content_id);
    CHECK(std::string(update.version) == "1.01" && std::string(update.folder) == "CUSA05083-1.01");
    CHECK(std::string(update.region) == "USA" && std::string(update.eboot_sha256) !=
                                                     std::string(base.eboot_sha256));
    CHECK(std::string(update.eboot_sha256) == "2865b6cec33f2515def173c1ed0c03afc27a1cd5fad1b0d0f516b4527aba9a88");
    CHECK(update.layers.size() == 2 && !update.layers[0].update && update.layers[1].update);
    CHECK(std::string(update.layers[0].app_ver) == base.layers[0].app_ver &&
          std::string(update.layers[0].sfo_version) == base.layers[0].sfo_version);
    CHECK(std::string(update.layers[1].app_ver) == "01.01" && std::string(update.layers[1].sfo_version) == "01.00");
    CHECK(BaseBuildOf(update) == &base && BaseBuildOf(base) == nullptr);
    CHECK(UpdateBuildOf(base) == &update && UpdateBuildOf(update) == nullptr);
    CHECK(BuildMetadataText(update) == "APP_VER 1.01 \xC2\xB7 VERSION 1.00");
    CHECK(InstalledGameName(GameFileState::kValid, &update) == "PS4 CUSA05083 1.01 USA");

    CHECK(update.mapping == ExecutableMapping::kScanned && update.resolution_sites.empty() &&
          update.snapshot_scale_site == 0 && !update.skip_intro && !update.bookshelf);
    CHECK(base.mapping == ExecutableMapping::kTable && std::string(update.merged_sfo_version ? update.merged_sfo_version : "") == "01.01" &&
          !base.merged_sfo_version);

    const std::vector<std::string> data(update.data_files.begin(), update.data_files.end());
    CHECK((data == std::vector<std::string>{"Odin2_BT_US.CPK", "Odin2_OR_US.CPK", "Odin2_RE_US.CPK",
                                            "Odin2_US_PATCH1_RE_US.CPK"}));
    const std::vector<std::string> extra(update.extra_files.begin(), update.extra_files.end());
    CHECK((extra == std::vector<std::string>{"sce_discmap_patch.plt"}));
    for (const char* name : base.data_files) {
        CHECK(std::find(data.begin(), data.end(), name) != data.end());
    }
    CHECK(update.text_mode == base.text_mode && TextLanguagesOf(&update).empty() && TextLanguagesOf(&base).empty());

    const std::filesystem::path dir = TempDir("usa-101-identity");
    const GameCheck update_package = CheckPackageIdentity(SfoText(dir, UsaSfo("gp", "01.01", "01.00")));
    CHECK(update_package.state == GameFileState::kValid && update_package.build == &update && update_package.layer == 1);
    const GameCheck update_sfo = CheckSfoIdentity(SfoText(dir, UsaSfo("gp", "01.01", "01.00")));
    CHECK(update_sfo.state == GameFileState::kValid && update_sfo.build == &update);
    CHECK(CheckPackageIdentity(SfoText(dir, UsaSfo("gp", "01.01", "01.01"))).state == GameFileState::kDifferentVersion);
    CHECK(CheckPackageIdentity(SfoText(dir, UsaSfo("gd", "01.01", "01.00"))).state == GameFileState::kDifferentVersion);
    const GameCheck merged = CheckPackageIdentity(SfoText(dir, UsaSfo("gd", "01.01", "01.01")));
    CHECK(merged.state == GameFileState::kValid && merged.build == &update && merged.layer == 0);
    const GameCheck base_package = CheckPackageIdentity(SfoText(dir, UsaSfo("gd", "01.00", "01.01")));
    CHECK(base_package.state == GameFileState::kValid && base_package.build == &base && base_package.layer == 0);
    CHECK(CheckPackageIdentity(SfoText(dir, UsaSfo("gp", "01.00", "01.01"))).state == GameFileState::kDifferentVersion);
    for (const char* category : {"gd", "gp"}) {
        const GameCheck future = CheckPackageIdentity(SfoText(dir, UsaSfo(category, "01.02", "01.01")));
        CHECK(future.state == GameFileState::kDifferentVersion && future.detail.find("APP_VER 01.02") != std::string::npos);
        CHECK(CheckPackageIdentity(SfoText(dir, UsaSfo(category, "01.01", "01.02"))).state ==
              GameFileState::kDifferentVersion);
    }
    CHECK(CheckSfoIdentity(SfoText(dir, UsaSfo("gd", "01.01", "01.00"))).build == &update);
    CHECK(CheckSfoIdentity(SfoText(dir, UsaSfo("gd", "01.01", "01.01"))).build == &update);
    CHECK(CheckSfoIdentity(SfoText(dir, UsaSfo("gd", "01.01", "01.02"))).state == GameFileState::kDifferentVersion);
    CHECK(CheckSfoIdentity(SfoText(dir, UsaSfo("gd", "01.00", "01.01"))).build == &base);
    CHECK(CheckSfoIdentity(SfoText(dir, UsaSfo("gd", "01.02", "01.01"))).state == GameFileState::kDifferentVersion);

    // A complete package of an update's version is accepted for this build only.
    for (const GameBuild& other : SupportedBuilds()) {
        if (&other == &update || !FinishedLayer(other).update) {
            continue;
        }
        const PackageLayer& layer = FinishedLayer(other);
        const GameCheck as_complete = CheckPackageIdentity(SfoText(
            dir, {{"TITLE_ID", other.title_id}, {"CONTENT_ID", other.content_id}, {"CATEGORY", "gd"},
                  {"APP_VER", layer.app_ver}, {"VERSION", layer.sfo_version}}));
        CHECK(as_complete.state == GameFileState::kDifferentVersion);
    }
}

// The USA 1.01 update laid over the USA 1.00 base, checked on synthetic game folders: a build list whose executable
// hashes are the synthetic executables' stands in for the real ones.
void TestUsa101Layering() {
    const GameBuild* base_found = BuildWithLabel("CUSA05083 1.00 USA");
    const GameBuild* update_found = BuildWithLabel("CUSA05083 1.01 USA");
    CHECK(base_found && update_found);
    if (!base_found || !update_found) {
        return;
    }
    using namespace cpk_test;
    namespace fs = std::filesystem;
    const fs::path root = TempDir("usa-101-layering");
    const GameRoots roots{root / "prepared", root / "image"};

    const std::string known = Pattern(20480, 3), painted = Painted(known, {0x340}, 'X'), other = "BOOT" + std::string(600, '\0');
    const std::string patch = MakePatch({{"Interface.ftx", {known, painted}}, {"GUI/HD_Interface.ftx", {known, painted}}});
    const std::string bt = BuildCpk({{"other/x.bin", other, false}}, false);
    const std::string original_or = BuildCpk({{"Interface.ftx", known, true}}, true);
    const std::string re = BuildCpk({{"other/y.bin", other, true}}, false);
    const std::string patch_re = BuildCpk({{"GUI/HD_Interface.ftx", known, false}}, false);

    const fs::path base_dir = roots.prepared / base_found->folder;
    fs::create_directories(base_dir / "sce_sys");
    WriteSfo(base_dir / "sce_sys" / "param.sfo", UsaSfo("gd", "01.00", "01.01"));
    WriteGameFile(base_dir, "eboot.bin", "usa-1.00-eboot");
    WriteGameFile(base_dir, "sce_discmap.plt", "plt");
    WriteGameFile(base_dir, "sce_module/libc.prx", "libc");
    WriteGameFile(base_dir, "sce_module/libSceFios2.prx", "fios");
    WriteGameFile(base_dir, "Odin2_BT_US.CPK", bt);
    WriteGameFile(base_dir, "Odin2_OR_US.CPK", original_or);
    WriteGameFile(base_dir, "Odin2_RE_US.CPK", re);

    const fs::path update_files = root / "update-files";
    fs::create_directories(update_files / "sce_sys");
    WriteSfo(update_files / "sce_sys" / "param.sfo", UsaSfo("gp", "01.01", "01.00"));
    WriteGameFile(update_files, "eboot.bin", "usa-1.01-eboot");
    WriteGameFile(update_files, "sce_discmap_patch.plt", "patch plt");
    WriteGameFile(update_files, "Odin2_US_PATCH1_RE_US.CPK", patch_re);

    const std::string base_sha = Sha256Hex(base_dir / "eboot.bin"), update_sha = Sha256Hex(update_files / "eboot.bin");
    std::vector<GameBuild> builds = {*base_found, *update_found};
    builds[0].eboot_sha256 = base_sha.c_str();
    builds[1].eboot_sha256 = update_sha.c_str();
    const GameBuild& base = builds[0];
    const GameBuild& update = builds[1];
    CHECK(BaseBuildOf(update, builds) == &base && UpdateBuildOf(base, builds) == &update);

    const GameCheck base_check = CheckGameTree(base_dir, builds);
    CHECK(base_check.state == GameFileState::kValid && base_check.build == &base);

    // The Xbox prompts of the base are not carried into the update; the base keeps them.
    CHECK(SetPromptStyle(base_dir, base, PromptStyle::kXbox, Span(patch)).ok);
    CHECK(InstalledPromptStyle(base_dir, base, Span(patch)) == PromptStyle::kXbox);
    const std::map<std::string, std::string> base_before = TreeContents(base_dir);

    const fs::path staged = roots.prepared / update.folder;
    CHECK(StageUpdate(base_dir, update_files, staged).ok());
    CHECK(TreeContents(base_dir) == base_before);
    CHECK(CheckGameTree(base_dir, builds).build == &base && CheckGameTree(base_dir, builds).state == GameFileState::kValid);
    const GameCheck merged = CheckGameTree(staged, builds);
    CHECK(merged.state == GameFileState::kValid && merged.build == &update);
    CHECK(ReadFile(staged / "eboot.bin") == "usa-1.01-eboot" && ReadFile(staged / "Odin2_BT_US.CPK") == bt);
    CHECK(ReadFile(staged / "Odin2_OR_US.CPK") == original_or && ReadFile(staged / "Odin2_US_PATCH1_RE_US.CPK") == patch_re);
    CHECK(InstalledPromptStyle(staged, update, Span(patch)) == PromptStyle::kPlayStation);
    CHECK(InstalledPromptStyle(base_dir, base, Span(patch)) == PromptStyle::kXbox);

    // A supported update logs concisely; an unknown or failed package logs the diagnostics.
    {
        const std::string sfo = SfoText(root, UsaSfo("gp", "01.01", "01.00"));
        const PackageLogContext context{"update.pkg", sfo, CheckPackageIdentity(sfo, builds)};
        CHECK(context.identity.state == GameFileState::kValid && context.identity.build == &update);
        InstallResult installed;
        installed.build = &update;
        installed.unmapped = {"Legacy CPU LZCNT"};
        const std::string log = PackageInstallLog(context, installed, roots, builds);
        for (const char* want : {"Package recognized: update.pkg is CUSA05083 1.01 USA (update, layer 2 of 2)",
                                 "Update laid over CUSA05083 1.00 USA", "Package install result:",
                                 "Final prepared build: CUSA05083 1.01 USA (CUSA05083-1.01)",
                                 "Executable features not mapped (the installation is not affected): Legacy CPU LZCNT"}) {
            CHECK(Contains(log, want));
        }
        for (const char* unwanted : {"diagnostics begin", "param.sfo", "SHA-256", "Additional information", "failed"}) {
            CHECK(!Contains(log, unwanted));
        }
        installed.unmapped.clear();
        CHECK(!Contains(PackageInstallLog(context, installed, roots, builds), "not mapped"));

        const std::string old_guess = SfoText(root, UsaSfo("gp", "01.01", "01.01"));
        const PackageLogContext unknown{"future.pkg", old_guess, CheckPackageIdentity(old_guess, builds)};
        CHECK(unknown.identity.state == GameFileState::kDifferentVersion);
        InstallResult refused;
        refused.status = InstallStatus::kWrongVersion;
        refused.detail = unknown.identity.detail;
        refused.report = "Unsupported build diagnostics begin\nUnsupported build diagnostics end\n";
        const std::string full = PackageInstallLog(unknown, refused, roots, builds);
        CHECK(Contains(full, "Package: future.pkg") && Contains(full, "Matched build: none (param.sfo APP_VER 01.01"));
        CHECK(Contains(full, "Result: failed") && Contains(full, refused.report));
        CHECK(Contains(full, "Please attach this complete log when reporting this version"));
        CHECK(Contains(full, "never upload game files"));
        for (const char* forbidden : {"Please upload", "send the PKG", "send your PKG", "eboot.bin to"}) {
            CHECK(!Contains(full, forbidden));
        }
        refused.report.clear();
        CHECK(Contains(PackageInstallLog(unknown, refused, roots, builds), "CATEGORY gp"));

        const GameRoots empty_roots{root / "none-prepared", root / "none-image"};
        InstallResult needs_base;
        needs_base.status = InstallStatus::kUpdateNeedsBase;
        needs_base.detail = "missing";
        const std::string missing_base = PackageInstallLog(context, needs_base, empty_roots, builds);
        CHECK(Contains(missing_base, "Existing base installation: not found, CUSA05083 1.00 USA"));
        CHECK(Contains(missing_base, "Result: failed") && Contains(missing_base, "install the base package first"));

        InstallResult broken;
        broken.status = InstallStatus::kIncomplete;
        broken.detail = "missing Odin2_US_PATCH1_RE_US.CPK";
        const std::string incomplete = PackageInstallLog(context, broken, roots, builds);
        CHECK(Contains(incomplete, "Matched package layer: 1 of 2 (update)") &&
              Contains(incomplete, "missing Odin2_US_PATCH1_RE_US.CPK"));
        CHECK(Contains(incomplete, "whether the launcher reached CUSA05083 1.01 USA"));
    }

    // The update's own archive is patched and restored with the base build's archive; the base is not touched.
    const PromptsResult xbox = SetPromptStyle(staged, update, PromptStyle::kXbox, Span(patch));
    CHECK(xbox.ok && InstalledPromptStyle(staged, update, Span(patch)) == PromptStyle::kXbox);
    CHECK(ReadFile(staged / "Odin2_US_PATCH1_RE_US.CPK") != patch_re && TreeContents(base_dir) == base_before);
    CHECK(SetPromptStyle(staged, update, PromptStyle::kPlayStation, {}).ok);
    CHECK(ReadFile(staged / "Odin2_OR_US.CPK") == original_or && ReadFile(staged / "Odin2_US_PATCH1_RE_US.CPK") == patch_re);
    CHECK(!AnyPromptPatch(staged, update) && TreeContents(base_dir) == base_before);

    // Save data and trophies are keyed by the TITLE_ID the two layers share.
    CHECK(SfoString(ReadFile(base_dir / "sce_sys" / "param.sfo"), "TITLE_ID") ==
          SfoString(ReadFile(staged / "sce_sys" / "param.sfo"), "TITLE_ID"));
    CHECK(std::string(base.title_id) == update.title_id && std::string(base.content_id) == update.content_id);

    // Pieces of the update missing from the staged game are named, never accepted.
    fs::remove(staged / "Odin2_US_PATCH1_RE_US.CPK");
    CHECK(CheckGameTree(staged, builds).detail == "missing Odin2_US_PATCH1_RE_US.CPK");
    WriteGameFile(staged, "Odin2_US_PATCH1_RE_US.CPK", patch_re);
    fs::remove(staged / "sce_discmap_patch.plt");
    CHECK(CheckGameTree(staged, builds).detail == "missing sce_discmap_patch.plt");
    WriteGameFile(staged, "sce_discmap_patch.plt", "patch plt");
    WriteGameFile(staged, "eboot.bin", "usa-1.00-eboot");
    const GameCheck old_eboot = CheckGameTree(staged, builds);
    CHECK(old_eboot.state == GameFileState::kDifferentVersion && old_eboot.build == &update);
    WriteGameFile(staged, "eboot.bin", "usa-1.01-eboot");
    CHECK(CheckGameTree(staged, builds).state == GameFileState::kValid);

    // Both builds prepared: the active build follows the last one installed, in either direction, without touching files.
    const auto prepare = [&](const GameBuild& build, int seconds) {
        const GamePaths paths = BuildPaths(roots, build);
        std::ofstream(paths.app0 / kPreparedMarker)
            << "{\"version\": \"" << build.version << "\", \"scope\": \"full\"}\n";
        std::filesystem::last_write_time(paths.app0 / kPreparedMarker,
                                         std::filesystem::file_time_type::clock::now() - std::chrono::hours(24) + std::chrono::seconds(seconds));
        WriteGameFile(paths.image_dir, "boot.bin", "image");
        WriteImageReport(paths.image_dir, build);
    };
    prepare(base, 10);
    prepare(update, 20);
    CHECK(CheckGameFiles(BuildPaths(roots, base), builds).state == GameFileState::kValid);
    CHECK(CheckGameFiles(BuildPaths(roots, base), builds).build == &base);
    CHECK(CheckGameFiles(BuildPaths(roots, update), builds).state == GameFileState::kValid);
    CHECK(CheckGameFiles(BuildPaths(roots, update), builds).build == &update);
    CHECK(LastPreparedBuild(roots, builds) == &update);

    const fs::path update_source = root / "sources" / "usa-101", base_source = root / "sources" / "usa-100";
    fs::create_directories(update_source.parent_path());
    fs::copy(staged, update_source, fs::copy_options::recursive);
    fs::copy(base_dir, base_source, fs::copy_options::recursive);
    fs::remove(base_source / kPreparedMarker);
    fs::remove(update_source / kPreparedMarker);
    const std::map<std::string, std::string> update_before = TreeContents(staged);
    const std::map<std::string, std::string> base_prepared = TreeContents(base_dir);

    CHECK(InstallFromFolder(update_source, roots, {}, builds).status == InstallStatus::kUpToDate);
    const InstallResult back = InstallFromFolder(base_source, roots, {}, builds);
    CHECK(back.status == InstallStatus::kActivated && back.build == &base);
    CHECK(LastPreparedBuild(roots, builds) == &base && ActiveBuildPaths(roots, builds).app0 == BuildPaths(roots, base).app0);
    CHECK(InstallFromFolder(base_source, roots, {}, builds).status == InstallStatus::kUpToDate);
    const InstallResult forward = InstallFromFolder(update_source, roots, {}, builds);
    CHECK(forward.status == InstallStatus::kActivated && forward.build == &update);
    CHECK(LastPreparedBuild(roots, builds) == &update);
    for (int i = 0; i < 20; ++i) {
        CHECK(InstallFromFolder(base_source, roots, {}, builds).status == InstallStatus::kActivated);
        CHECK(LastPreparedBuild(roots, builds) == &base);
        CHECK(InstallFromFolder(update_source, roots, {}, builds).status == InstallStatus::kActivated);
        CHECK(LastPreparedBuild(roots, builds) == &update);
    }
    CHECK(TreeContents(staged) == update_before && TreeContents(base_dir) == base_prepared);
}

// The USA 1.01 executable has no table, so its features come from the scan recorded at installation, one by one.
void TestUsa101Scanned() {
    const GameBuild* update_found = BuildWithLabel("CUSA05083 1.01 USA");
    const GameBuild* base_found = BuildWithLabel("CUSA05083 1.00 USA");
    CHECK(update_found && base_found);
    if (!update_found || !base_found) {
        return;
    }
    const GameBuild& update = *update_found;
    const GameBuild& base = *base_found;
    const std::filesystem::path dir = TempDir("usa-101-scanned");
    const std::filesystem::path image_dir = dir / "image";
    const std::string image_text = image_dir.string();

    WriteImageReport(image_dir, update);
    const BuildFeatures unscanned = FeaturesOf(&update, image_dir);
    CHECK(!unscanned.custom_resolution && !unscanned.skip_intro && !unscanned.unlock_bookshelf && !unscanned.lzcnt_rewrite);
    OslGameBuild loaded{};
    CHECK(osl_game_build(image_text.c_str(), &loaded) && std::string(loaded.label) == update.label);
    CHECK(loaded.resolution_site_count == 0 && loaded.snapshot_scale_site == 0 && !loaded.skip_intro && !loaded.bookshelf &&
          !loaded.lzcnt_offsets);

    const MappedExecutable image = SyntheticOdinImage(kFixtureSkew);
    const ScanResult scan = ScanExecutable(image, image.image.size());
    CHECK(scan.complete() && UnresolvedFeatures(scan.mapping).empty());
    WriteImageReport(image_dir, update, MappingExtra(scan.mapping));
    const BuildFeatures scanned = FeaturesOf(&update, image_dir);
    CHECK(scanned.custom_resolution && scanned.skip_intro && scanned.unlock_bookshelf && scanned.lzcnt_rewrite);
    OslGameBuild mapped{};
    CHECK(osl_game_build(image_text.c_str(), &mapped) && mapped.resolution_site_count == base.resolution_sites.size());
    for (size_t i = 0; i < mapped.resolution_site_count; ++i) {
        CHECK(mapped.resolution_sites[i].offset == scan.mapping.resolution_offsets[i]);
        CHECK(mapped.resolution_sites[i].value == base.resolution_sites[i].value);
    }
    CHECK(SameSkipIntro(scan.mapping.skip_intro, mapped.skip_intro) && SameBookshelf(scan.mapping.bookshelf, mapped.bookshelf));
    CHECK(mapped.snapshot_scale_site == scan.mapping.snapshot_scale_site && mapped.lzcnt_offsets &&
          std::equal(scan.mapping.lzcnt_offsets.begin(), scan.mapping.lzcnt_offsets.end(), mapped.lzcnt_offsets));

    // None of the USA 1.00 table's addresses is the scan's, and every USA 1.00 patch refuses this layout.
    CHECK(!SameResolution(scan.mapping.resolution_offsets, base.resolution_sites) &&
          !SameSkipIntro(scan.mapping.skip_intro, base.skip_intro) && !SameBookshelf(scan.mapping.bookshelf, base.bookshelf) &&
          !SameLzcnt(scan.mapping.lzcnt_offsets, base) && scan.mapping.snapshot_scale_site != base.snapshot_scale_site);
    const OslResolution target = osl_render_target(&osl_resolutions[OSL_RESOLUTION_COUNT - 1], &osl_aspect_ratios[0]);
    std::vector<unsigned char> foreign = image.image;
    foreign.resize(0x1100000, 0);
    const std::vector<unsigned char> before = foreign;
    char error[320] = "";
    CHECK(!osl_apply_resolution(foreign.data(), foreign.size(), base.resolution_sites.data(), base.resolution_sites.size(),
                                &target, error, sizeof(error)));
    CHECK(!osl_apply_snapshot_scale(foreign.data(), foreign.size(), base.snapshot_scale_site, &target, error, sizeof(error)));
    CHECK(!osl_apply_skip_intro(foreign.data(), foreign.size(), base.skip_intro, 1, error, sizeof(error)));
    CHECK(!osl_apply_bookshelf(foreign.data(), foreign.size() - OSL_BOOKSHELF_STUB_BYTES, base.bookshelf,
                               foreign.data() + foreign.size() - OSL_BOOKSHELF_STUB_BYTES, OSL_BOOKSHELF_STUB_BYTES, error,
                               sizeof(error)));
    CHECK(foreign == before);

    // The scan's sites never overlap; a colliding stub is detected.
    GameBuild with_scan = update;
    const std::vector<OslResolutionSite> sites = ResolutionSitesAt(SupportedBuilds(), scan.mapping.resolution_offsets);
    with_scan.resolution_sites = sites;
    with_scan.snapshot_scale_site = scan.mapping.snapshot_scale_site;
    OslSkipIntroSites skip = *scan.mapping.skip_intro;
    with_scan.skip_intro = &skip;
    CHECK(!sites.empty() && SitesAreDisjoint(with_scan));
    skip.stubs = sites[0].offset;
    CHECK(!SitesAreDisjoint(with_scan));

    // A feature that cannot be mapped is hidden alone; the rest of the build keeps its features.
    const struct {
        const char* what;
        std::function<void(MappedExecutable&)> damage;
        BuildFeatures expected;
    } cases[] = {
        {"a missing resolution site",
         [&](MappedExecutable& m) { std::fill_n(m.image.begin() + scan.mapping.resolution_offsets[3], 5, uint8_t{0x90}); },
         {false, true, true, true}},
        {"a duplicated resolution site",
         [&](MappedExecutable& m) {
             std::copy_n(m.image.begin() + scan.mapping.resolution_offsets[3], 10, m.image.begin() + 0x2e000);
         },
         {false, true, true, true}},
        {"a corrupted snapshot scale fingerprint",
         [&](MappedExecutable& m) { m.image[kPlantedSnapshot + kFixtureSkew] ^= 0x55; },
         {false, true, true, true}},
        {"a corrupted skip intro fingerprint",
         [&](MappedExecutable& m) { m.image[kPlantedHd + kFixtureSkew] ^= 0x55; },
         {true, false, true, true}},
        {"an occupied skip intro stub",
         [&](MappedExecutable& m) { m.image[scan.mapping.skip_intro->stubs + 8] = 0x90; },
         {true, false, true, true}},
        {"a corrupted bookshelf getter", [&](MappedExecutable& m) { m.image[kPlantedGetter + kFixtureSkew] ^= 0x55; },
         {true, true, false, true}},
        {"a bookshelf site its patch refuses",
         [&](MappedExecutable& m) { m.image[scan.mapping.bookshelf->reads[2] + 5] ^= 0x01; },
         {true, true, false, true}},
        {"a corrupted LZCNT shape", [&](MappedExecutable& m) { m.image[kPlantedLzcnt + kFixtureSkew] ^= 0x55; },
         {true, true, true, false}},
    };
    for (const auto& item : cases) {
        MappedExecutable damaged = image;
        item.damage(damaged);
        const ScanResult partial = ScanExecutable(damaged, damaged.image.size());
        WriteImageReport(image_dir, update, MappingExtra(partial.mapping));
        const BuildFeatures features = FeaturesOf(&update, image_dir);
        const bool as_expected = !partial.complete() && features.custom_resolution == item.expected.custom_resolution &&
                                 features.skip_intro == item.expected.skip_intro &&
                                 features.unlock_bookshelf == item.expected.unlock_bookshelf &&
                                 features.lzcnt_rewrite == item.expected.lzcnt_rewrite;
        if (!as_expected) {
            std::printf("unexpected features for %s\n", item.what);
        }
        CHECK(as_expected);
        const bool reported = std::any_of(partial.features.begin(), partial.features.end(), [](const FeatureScan& feature) {
            return feature.state != FeatureState::kComplete;
        });
        CHECK(!UnresolvedFeatures(partial.mapping).empty() && reported && !Contains(partial.text, "status: incomplete"));
    }

    // Nothing mapped at all: the log names every feature, and the game is still usable at its native settings.
    const std::filesystem::path game = dir / "game";
    std::filesystem::create_directories(game / "sce_sys");
    std::ofstream(game / "eboot.bin", std::ios::binary) << std::string(64, 'x');
    WriteSfo(game / "sce_sys" / "param.sfo", {{"TITLE_ID", "CUSA05083"}, {"CONTENT_ID", update.content_id}});
    const std::string diagnosed = DiagnoseBuildMapping(game, update);
    CHECK(diagnosed.starts_with("Build mapping diagnostics begin\nIdentity:\n  Build: CUSA05083 1.01 USA\n"));
    CHECK(Contains(diagnosed, "Features not resolved by the scan: Render resolution, Skip intro, Unlock Bookshelf, "
                              "Legacy CPU (LZCNT)\n"));
    CHECK(Contains(diagnosed, "Feature mapping:\n") && Contains(diagnosed, "Resolution: incomplete (0/17)"));
    CHECK(Contains(diagnosed, "Snapshot scale: incomplete") && Contains(diagnosed, "Legacy CPU LZCNT: incomplete"));
    CHECK(Contains(diagnosed, "Legacy CPU LZCNT mapping incomplete:\n  installation is not affected on CPUs with native LZCNT\n"));
    CHECK(Contains(diagnosed, "attach the complete log when reporting this mapping issue; do not upload game files"));
    CHECK(!Contains(diagnosed, "eboot.bin to") && !Contains(diagnosed, "send the PKG"));
    std::vector<std::string> unmapped;
    CHECK(!DiagnoseBuildMapping(game, update, {}, &unmapped).empty() && unmapped.size() == 5);
    CHECK(DiagnoseBuildMapping(game, base).empty());
    WriteImageReport(image_dir, update);
    std::ofstream(dir / "none.ini") << "render_resolution = 4K" << std::endl << "aspect_ratio = 32:9" << std::endl;
    CHECK(osl_render_resolution((dir / "none.ini").string().c_str(), image_text.c_str()) == 0);
    CHECK(osl_aspect_ratio((dir / "none.ini").string().c_str(), image_text.c_str()) == 0);
}

void TestBuildMappingDiagnostics() {
    const GameBuild* hk = BuildWithLabel("CUSA03873 1.00 HK");
    CHECK(hk != nullptr);
    if (!hk) {
        return;
    }
    const std::filesystem::path game = TempDir("build-mapping");
    std::filesystem::create_directories(game / "sce_sys");
    CHECK(DiagnoseBuildMapping(game, *hk).empty());
    std::ofstream(game / "eboot.bin", std::ios::binary) << std::string(64, 'x');
    WriteSfo(game / "sce_sys" / "param.sfo", {{"TITLE_ID", "CUSA03873"}, {"CONTENT_ID", hk->content_id}});
    const std::string text = DiagnoseBuildMapping(game, *hk);
    CHECK(text.starts_with("Build mapping diagnostics begin\nIdentity:\n  Build: CUSA03873 1.00 HK\n"));
    CHECK(text.ends_with("Build mapping diagnostics end\n"));
    CHECK(Contains(text, "Features not resolved by the scan: Render resolution, Skip intro, Unlock Bookshelf, "
                         "Legacy CPU (LZCNT)\n"));
    CHECK(Contains(text, "Executable: not readable"));
    for (const GameBuild& build : SupportedBuilds()) {
        if (build.mapping == ExecutableMapping::kTable) {
            CHECK(DiagnoseBuildMapping(game, build).empty());
        }
    }

    // The same scan on a local game folder, when there is one, and every local package as an unsupported source.
    const std::filesystem::path root(OSL_SOURCE_DIR);
    size_t real_builds = 0, real_packages = 0;
    for (const GameBuild& build : SupportedBuilds()) {
        const std::filesystem::path app0 = root / "game" / "prepared" / build.folder;
        std::error_code ec;
        if (build.mapping != ExecutableMapping::kTable || !TableComplete(build) ||
            !std::filesystem::is_regular_file(app0 / "eboot.bin", ec) ||
            Sha256Hex(app0 / "eboot.bin") != build.eboot_sha256) {
            continue;
        }
        const ScanResult scan = ScanGameFolder(app0);
        PARITY("Runtime Patches", scan.complete() && SameResolution(scan.mapping.resolution_offsets, build.resolution_sites) &&
                                      scan.mapping.snapshot_scale_site == build.snapshot_scale_site &&
                                      SameSkipIntro(scan.mapping.skip_intro, build.skip_intro) &&
                                      SameBookshelf(scan.mapping.bookshelf, build.bookshelf) &&
                                      SameLzcnt(scan.mapping.lzcnt_offsets, build));
        ++real_builds;
    }
    std::error_code ec;
    for (std::filesystem::directory_iterator it(root / "game", ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".pkg" || !it->is_regular_file(ec)) {
            continue;
        }
        const std::filesystem::path scratch = TempDir("real-package");
        const GameRoots roots{scratch / "prepared", scratch / "image"};
        const std::uintmax_t size = std::filesystem::file_size(it->path(), ec);
        const InstallResult result = InstallFromPackage(it->path(), roots, {}, std::span<const GameBuild>{});
        CHECK(result.status == InstallStatus::kUnsupportedBuild || result.status == InstallStatus::kWrongGame);
        CHECK(result.status != InstallStatus::kUnsupportedBuild || Contains(result.report, "SHA-256: "));
        CHECK(!std::filesystem::exists(roots.image) && std::filesystem::file_size(it->path(), ec) == size);
        ++real_packages;
    }
    std::printf("Additional validation with local game data: %zu prepared build(s) scanned, %zu package(s) diagnosed\n",
                real_builds, real_packages);
}

void TestTitleExitAssets() {
    using namespace cpk_test;
    const fs::path app0 = TempDir("title-assets") / "app0";
    fs::create_directories(app0);

    const std::string atlas = Pattern(24576, 5), atlas_x = Painted(atlas, {0x340, 0x1230, 0x5ff0}, 'T');
    const std::string refined = Pattern(8192, 9), refined_x = Painted(refined, {0x80, 0x1000}, 'R');
    const std::string classic = Pattern(6144, 3), classic_x = Painted(classic, {0x40}, 'C');
    const std::string iface = Pattern(20480, 7), iface_x = Painted(iface, {0x340, 0x1000}, 'X');
    const std::string other = "BOOT" + std::string(600, '\0');
    const std::string title_patch = MakePatch({{"title.ftx", {atlas, atlas_x}},
                                               {"GUI/HD_title.ftx", {atlas, atlas_x}},
                                               {"GUI/REHD_title.mbs", {refined, refined_x}},
                                               {"title.mbs", {classic, classic_x}}});
    const std::string prompts_patch = MakePatch({{"Interface.ftx", {iface, iface_x}}});
    const std::string or_de =
        BuildCpk({{"Interface.ftx", iface, true}, {"title.ftx", atlas, true}, {"title.mbs", classic, false}}, true);
    const std::string re_de = BuildCpk({{"GUI/HD_title.ftx", atlas, false}, {"GUI/REHD_title.mbs", refined, true}}, false);
    const std::string bt = BuildCpk({{"other/x.bin", other, false}}, false);
    const char* const files[] = {"Odin2_BT_DE.CPK", "Odin2_OR_DE.CPK", "Odin2_RE_DE.CPK"};
    const auto reset = [&] {
        for (const char* name : files) {
            fs::remove(UndoPath(app0 / name));
            fs::remove(UndoPath(app0 / name, kTitleUndoSuffix));
            fs::remove(UndoPath(app0 / name, ".title.stamp"));
        }
        WriteAll(app0 / "Odin2_BT_DE.CPK", bt);
        WriteAll(app0 / "Odin2_OR_DE.CPK", or_de);
        WriteAll(app0 / "Odin2_RE_DE.CPK", re_de);
    };
    GameBuild build{};
    build.label = "test build";
    build.data_files = files;
    const auto originals = [&] {
        return ReadFile(app0 / "Odin2_BT_DE.CPK") == bt && ReadFile(app0 / "Odin2_OR_DE.CPK") == or_de &&
               ReadFile(app0 / "Odin2_RE_DE.CPK") == re_de;
    };
    const auto entry = [&](const char* archive, const char* name) {
        CpkArchive cpk;
        CHECK(cpk.Open(app0 / archive));
        const CpkEntry* found = cpk.Find(name);
        return found ? Bytes(cpk.Read(*found)) : std::string();
    };
    reset();
    CHECK(TitleExitPending(app0, build, Span(title_patch)));
    CHECK(!TitleExitPending(app0, build, Span(std::string("not a patch"))));

    // The layer: patched copies of every title file, the other entries and archives as they were, its own undo records.
    CHECK(!TitleExitReady(app0, build, Span(title_patch)));
    TitleExitResult applied = ApplyTitleExit(app0, build, Span(title_patch));
    CHECK(applied.ok && !TitleExitPending(app0, build, Span(title_patch)) && TitleExitReady(app0, build, Span(title_patch)));
    CHECK(entry("Odin2_OR_DE.CPK", "title.ftx") == atlas_x && entry("Odin2_OR_DE.CPK", "title.mbs") == classic_x);
    CHECK(entry("Odin2_RE_DE.CPK", "GUI/HD_title.ftx") == atlas_x && entry("Odin2_RE_DE.CPK", "GUI/REHD_title.mbs") == refined_x);
    CHECK(entry("Odin2_OR_DE.CPK", "Interface.ftx") == iface && entry("Odin2_BT_DE.CPK", "other/x.bin") == other);
    CHECK(fs::exists(UndoPath(app0 / "Odin2_OR_DE.CPK", kTitleUndoSuffix)) && !fs::exists(UndoPath(app0 / "Odin2_OR_DE.CPK")));
    CHECK(!fs::exists(UndoPath(app0 / "Odin2_BT_DE.CPK", kTitleUndoSuffix)) && ReadFile(app0 / "Odin2_BT_DE.CPK") == bt);
    const std::string once = ReadFile(app0 / "Odin2_OR_DE.CPK");
    CHECK(ApplyTitleExit(app0, build, Span(title_patch)).ok && ReadFile(app0 / "Odin2_OR_DE.CPK") == once);

    // Taking it off restores every archive byte for byte, and again changes nothing.
    CHECK(RevertTitleExit(app0, build).ok && originals());
    CHECK(RevertTitleExit(app0, build).ok && originals());

    // A layer of an older patch is replaced by the current one, and taking it off still restores the original.
    const std::string newer_atlas = Painted(atlas, {0x340, 0x2000}, 'N');
    const std::string newer = MakePatch({{"title.ftx", {atlas, newer_atlas}},
                                         {"GUI/HD_title.ftx", {atlas, newer_atlas}},
                                         {"GUI/REHD_title.mbs", {refined, refined_x}},
                                         {"title.mbs", {classic, classic_x}}});
    CHECK(ApplyTitleExit(app0, build, Span(title_patch)).ok && !TitleExitPending(app0, build, Span(title_patch)));
    CHECK(TitleExitPending(app0, build, Span(newer)));
    CHECK(ApplyTitleExit(app0, build, Span(newer)).ok && !TitleExitPending(app0, build, Span(newer)));
    CHECK(entry("Odin2_OR_DE.CPK", "title.ftx") == newer_atlas && entry("Odin2_RE_DE.CPK", "GUI/HD_title.ftx") == newer_atlas);
    CHECK(RevertTitleExit(app0, build).ok && originals());

    // It sits above the controller prompts: either order of choosing, the prompts change under a removed layer.
    CHECK(ApplyTitleExit(app0, build, Span(title_patch)).ok);
    PromptsResult prompts = SetPromptStyle(app0, build, PromptStyle::kXbox, Span(prompts_patch), {}, Span(title_patch));
    CHECK(prompts.ok && InstalledPromptStyle(app0, build, Span(prompts_patch)) == PromptStyle::kXbox);
    CHECK(!TitleExitPending(app0, build, Span(title_patch)) && TitleExitReady(app0, build, Span(title_patch)));
    CHECK(entry("Odin2_OR_DE.CPK", "Interface.ftx") == iface_x && entry("Odin2_OR_DE.CPK", "title.ftx") == atlas_x);
    CHECK(fs::exists(UndoPath(app0 / "Odin2_OR_DE.CPK")) && fs::exists(UndoPath(app0 / "Odin2_OR_DE.CPK", kTitleUndoSuffix)));
    prompts = SetPromptStyle(app0, build, PromptStyle::kPlayStation, {}, {}, Span(title_patch));
    CHECK(prompts.ok && !AnyPromptPatch(app0, build) && !TitleExitPending(app0, build, Span(title_patch)) &&
          TitleExitReady(app0, build, Span(title_patch)));
    CHECK(entry("Odin2_OR_DE.CPK", "Interface.ftx") == iface && entry("Odin2_OR_DE.CPK", "title.ftx") == atlas_x);
    CHECK(RevertTitleExit(app0, build).ok && originals());
    CHECK(SetPromptStyle(app0, build, PromptStyle::kXbox, Span(prompts_patch)).ok && SetPromptStyle(app0, build, PromptStyle::kPlayStation, {}).ok &&
          originals());

    // A cancelled change puts every archive back as it was.
    int phases = 0;
    PatchHooks cancel_second;
    cancel_second.phase = [&phases](const char*) { ++phases; };
    cancel_second.progress = [&phases](uint64_t, uint64_t) { return phases < 2; };
    const TitleExitResult cancelled = ApplyTitleExit(app0, build, Span(title_patch), cancel_second);
    CHECK(!cancelled.ok && cancelled.cancelled && originals());
    CHECK(TitleExitPending(app0, build, Span(title_patch)));

    // A copy of the file with other bytes: taken when the blocks the patch changes are the ones it was made from, refused
    // when they are not or when two targets would write different bytes into it.
    const std::string compatible = Painted(atlas, {0x2000}, 'k'), incompatible = Painted(atlas, {0x340}, 'k');
    const BlockPatch parsed = *ParseBlockPatch(Span(title_patch));
    const EntryMatch exact = MatchEntry(parsed, "title.ftx", Span(atlas), Crc32(Span(atlas)));
    const EntryMatch compat = MatchEntry(parsed, "title.ftx", Span(compatible), Crc32(Span(compatible)));
    const EntryMatch rejected = MatchEntry(parsed, "title.ftx", Span(incompatible), Crc32(Span(incompatible)));
    CHECK(exact.state == EntryState::kOriginal && exact.exact);
    CHECK(compat.state == EntryState::kOriginal && !compat.exact && !compat.ambiguous);
    CHECK(rejected.state == EntryState::kUnknown);
    const std::string twin = Painted(atlas, {0x3000}, 'w');
    const std::string twin_x = Painted(twin, {0x340, 0x1230, 0x5ff0}, 'U');
    const std::string ambiguous_patch =
        MakePatch({{"title.ftx", {atlas, atlas_x}}, {"title.ftx", {twin, twin_x}}, {"GUI/HD_title.ftx", {atlas, atlas_x}}});
    const std::string stranger = Painted(atlas, {0x4000}, 's');
    const EntryMatch refused = MatchEntry(*ParseBlockPatch(Span(ambiguous_patch)), "title.ftx", Span(stranger), Crc32(Span(stranger)));
    CHECK(refused.state == EntryState::kUnknown && refused.ambiguous);
    // One title file the patch was not made for keeps every archive of the build original, recorded so it is not
    // retried until the archive changes.
    WriteAll(app0 / "Odin2_OR_DE.CPK", BuildCpk({{"title.ftx", incompatible, true}}, true));
    const std::string foreign = ReadFile(app0 / "Odin2_OR_DE.CPK");
    const auto untouched = [&] {
        return ReadFile(app0 / "Odin2_OR_DE.CPK") == foreign && ReadFile(app0 / "Odin2_RE_DE.CPK") == re_de &&
               ReadFile(app0 / "Odin2_BT_DE.CPK") == bt && !fs::exists(UndoPath(app0 / "Odin2_OR_DE.CPK", kTitleUndoSuffix)) &&
               !fs::exists(UndoPath(app0 / "Odin2_RE_DE.CPK", kTitleUndoSuffix));
    };
    CHECK(TitleExitPending(app0, build, Span(title_patch)));
    const TitleExitResult skipped = ApplyTitleExit(app0, build, Span(title_patch));
    CHECK(!skipped.ok && skipped.unavailable && !skipped.cancelled && untouched());
    CHECK(skipped.unsupported.size() == 1 && Contains(skipped.unsupported.front(), "archive Odin2_OR_DE.CPK, entry title.ftx"));
    std::string why;
    CHECK(!TitleExitPending(app0, build, Span(title_patch)) && !TitleExitReady(app0, build, Span(title_patch), &why) &&
          Contains(why, "Odin2_OR_DE.CPK"));
    CHECK(!ApplyTitleExit(app0, build, Span(title_patch)).ok && untouched());
    CHECK(TitleExitPending(app0, build, Span(newer)));
    WriteAll(app0 / "Odin2_OR_DE.CPK", BuildCpk({{"title.ftx", incompatible, true}, {"title.mbs", classic, false}}, true));
    CHECK(TitleExitPending(app0, build, Span(title_patch)));
    reset();
    WriteAll(app0 / "Odin2_OR_DE.CPK", BuildCpk({{"title.ftx", compatible, true}}, true));
    CHECK(ApplyTitleExit(app0, build, Span(title_patch)).ok && TitleExitReady(app0, build, Span(title_patch)));
    CHECK(entry("Odin2_OR_DE.CPK", "title.ftx") == Painted(compatible, {0x340, 0x1230, 0x5ff0}, 'T'));
    CHECK(RevertTitleExit(app0, build).ok);
    reset();

    // The shipped patch: every target holds source blocks, names only the four title files, one target per copy.
    const std::filesystem::path shipped = std::filesystem::path(OSL_SOURCE_DIR) / "launcher" / "title" / "title-exit.dcpx";
    const std::string bytes = ReadFile(shipped);
    const std::optional<BlockPatch> patch = ParseBlockPatch(Span(bytes));
    CHECK(patch.has_value());
    if (patch) {
        std::set<std::tuple<std::string, uint32_t, uint32_t>> seen;
        std::set<std::string> names;
        bool sound = !patch->targets.empty();
        for (const BlockPatchTarget& target : patch->targets) {
            names.insert(target.name);
            sound &= target.source_blocks && !target.runs.empty() && target.crc_before != target.crc_after;
            sound &= seen.emplace(target.name, target.size, target.crc_before).second;
        }
        CHECK(sound);
        const std::set<std::string> expected = {"GUI/HD_title.ftx", "GUI/REHD_title.mbs", "title.ftx", "title.mbs"};
        CHECK(names == expected);

        // English atlases hold one label both menus draw: painted once, Classic no longer overwrites the Refined label's
        // right end (EXIT GAM). Before the fix these targets changed 529 (USA, Japan) and 510 (UK) blocks.
        const std::map<uint32_t, size_t> english_runs = {{0xbf7fa72a, 391}, {0x5529e262, 388}, {0x18910f2d, 396}, {0x1d02d31d, 396}};
        for (const auto& [crc, runs] : english_runs) {
            for (const char* name : {"GUI/HD_title.ftx", "title.ftx"}) {
                const auto target = std::find_if(patch->targets.begin(), patch->targets.end(), [&](const BlockPatchTarget& t) {
                    return t.name == name && t.crc_before == crc;
                });
                CHECK(target != patch->targets.end() && target->runs.size() == runs);
            }
        }
    }

    // The artwork is made from the game's own letters: the generator names no font, and none ships with it.
    const std::filesystem::path generator = std::filesystem::path(OSL_SOURCE_DIR) / "scripts" / "title_exit";
    const std::string source = ReadFile(generator / "make_title_exit.py");
    CHECK(!source.empty() && !Contains(source, "ImageFont") && !Contains(source, "truetype") && !Contains(source, "Marcellus") &&
          !Contains(source, ".ttf") && Contains(source, "TEXT = 'EXIT GAME'") && Contains(source, "reference_glyphs"));
    size_t fonts = 0;
    for (const auto& file : std::filesystem::directory_iterator(generator)) {
        const std::string extension = file.path().extension().string();
        fonts += extension == ".ttf" || extension == ".otf" || extension == ".woff";
    }
    CHECK(fonts == 0);
}

namespace title_exit_test {

const BlockPatchTarget* ShippedTarget(const BlockPatch& patch, std::string_view name, uint32_t crc_before) {
    for (const BlockPatchTarget& target : patch.targets) {
        if (target.name == name && target.crc_before == crc_before) {
            return &target;
        }
    }
    return nullptr;
}

// A copy of the file the target was made from as far as the patch knows it: its source blocks, zeros elsewhere.
std::string SourceCopy(const BlockPatchTarget& target) {
    std::string out(target.size, '\0');
    for (const BlockRun& run : target.runs) {
        out.replace(run.offset, 16, reinterpret_cast<const char*>(run.original.data()), 16);
    }
    return out;
}

std::string PatchedCopy(const BlockPatchTarget& target, std::string data) {
    for (const BlockRun& run : target.runs) {
        data.replace(run.offset, 16, reinterpret_cast<const char*>(run.data.data()), 16);
    }
    return data;
}

} // namespace title_exit_test

// Issue #4: CUSA03873 1.00 HK with Xbox prompts showed INSTALL as the last title row, and choosing it quit. The shipped
// patch took HK's .mbs files (row order) but not its atlas (label art), and the layer still counted as installed.
void TestIssue4TitleExitInstallLabel() {
    using namespace cpk_test;
    using namespace title_exit_test;
    const GameBuild* hk = BuildWithLabel("CUSA03873 1.00 HK");
    const std::string title_patch = ReadFile(std::filesystem::path(OSL_SOURCE_DIR) / "launcher" / "title" / "title-exit.dcpx");
    const std::optional<BlockPatch> shipped = ParseBlockPatch(Span(title_patch));
    CHECK(hk && shipped);
    if (!hk || !shipped) {
        return;
    }
    // The USA copies stand in for HK's: its prompts already take the USA art (kHkPromptProfiles).
    const BlockPatchTarget* atlas_target = ShippedTarget(*shipped, "GUI/HD_title.ftx", 0xbf7fa72a);
    const BlockPatchTarget* refined_target = ShippedTarget(*shipped, "GUI/REHD_title.mbs", 0xcb49d54c);
    const BlockPatchTarget* classic_target = ShippedTarget(*shipped, "title.mbs", 0x24c6ab07);
    CHECK(atlas_target && refined_target && classic_target);
    if (!atlas_target || !refined_target || !classic_target) {
        return;
    }
    const std::string atlas = SourceCopy(*atlas_target), refined = SourceCopy(*refined_target), classic = SourceCopy(*classic_target);
    std::string hk_atlas = atlas; // the same art encoded differently in one replaced block
    hk_atlas[atlas_target->runs.front().offset] ^= 0x5a;
    const std::string refined_rows = PatchedCopy(*refined_target, refined), classic_rows = PatchedCopy(*classic_target, classic);
    const std::string exit_art = PatchedCopy(*atlas_target, atlas);
    CHECK(MatchEntry(*shipped, "GUI/REHD_title.mbs", Span(refined), Crc32(Span(refined))).state == EntryState::kOriginal);
    CHECK(MatchEntry(*shipped, "title.mbs", Span(classic), Crc32(Span(classic))).state == EntryState::kOriginal);
    CHECK(MatchEntry(*shipped, "GUI/HD_title.ftx", Span(atlas), Crc32(Span(atlas))).state == EntryState::kOriginal);
    CHECK(MatchEntry(*shipped, "GUI/HD_title.ftx", Span(hk_atlas), Crc32(Span(hk_atlas))).state == EntryState::kUnknown);

    const std::string iface = Pattern(20480, 7), iface_x = Painted(iface, {0x340, 0x1000}, 'X');
    const std::string prompts_patch = MakePatch({{"Interface.ftx", {iface, iface_x}}, {"GUI/HD_Interface.ftx", {iface, iface_x}}});
    const std::string bt = BuildCpk({{"other/x.bin", "BOOT" + std::string(600, '\0'), false}}, false);
    const auto classic_archive = [&](const std::string& art) {
        return BuildCpk({{"Interface.ftx", iface, true}, {"title.ftx", art, false}, {"title.mbs", classic, false}}, true);
    };
    const auto refined_archive = [&](const std::string& art) {
        return BuildCpk({{"GUI/HD_Interface.ftx", iface, true}, {"GUI/HD_title.ftx", art, false}, {"GUI/REHD_title.mbs", refined, false}},
                        false);
    };
    const fs::path app0 = TempDir("issue-4-title") / "app0";
    fs::create_directories(app0);
    const fs::path bt_cpk = app0 / "Odin2_BT_CN.CPK", or_cpk = app0 / "Odin2_OR_CN.CPK", re_cpk = app0 / "Odin2_RE_CN.CPK";
    std::string or_cn = classic_archive(hk_atlas), re_cn = refined_archive(hk_atlas);
    WriteAll(bt_cpk, bt);
    WriteAll(or_cpk, or_cn);
    WriteAll(re_cpk, re_cn);
    const auto entry = [](const fs::path& archive, const char* name) {
        CpkArchive cpk;
        const CpkEntry* found = cpk.Open(archive) ? cpk.Find(name) : nullptr;
        return found ? Bytes(cpk.Read(*found)) : std::string();
    };
    const auto originals = [&] { return ReadFile(bt_cpk) == bt && ReadFile(or_cpk) == or_cn && ReadFile(re_cpk) == re_cn; };
    const auto prompts_are = [&](const std::string& art) {
        return entry(or_cpk, "Interface.ftx") == art && entry(re_cpk, "GUI/HD_Interface.ftx") == art && ReadFile(bt_cpk) == bt;
    };
    const auto no_title_layer = [&] {
        return !fs::exists(UndoPath(or_cpk, kTitleUndoSuffix)) && !fs::exists(UndoPath(re_cpk, kTitleUndoSuffix));
    };

    // The state 0.2.3 left: Xbox prompts, then the title layer with the atlases skipped and the rest applied.
    CHECK(SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(prompts_patch)).ok);
    char stamp[16];
    std::snprintf(stamp, sizeof(stamp), "%08x", Crc32(Span(title_patch)));
    for (const fs::path& archive : {or_cpk, re_cpk}) {
        const PatchResult old = ApplyBlockPatch(archive, *shipped, {}, kTitleUndoSuffix);
        CHECK(old.ok && old.patched == 1 && old.skipped == 1);
        WriteAll(UndoPath(archive, ".title.stamp"), stamp);
    }
    CHECK(entry(re_cpk, "GUI/REHD_title.mbs") == refined_rows && entry(or_cpk, "title.mbs") == classic_rows);
    CHECK(entry(re_cpk, "GUI/HD_title.ftx") == hk_atlas && entry(or_cpk, "title.ftx") == hk_atlas);
    std::string why;
    CHECK(TitleExitPending(app0, *hk, Span(title_patch)) && !TitleExitReady(app0, *hk, Span(title_patch), &why) &&
          Contains(why, "not installed"));

    // The launcher's next start (Xbox saved, title layer pending): the menu and its art stay the game's own, the prompts
    // stay Xbox, the Exit Game row stays off, and the log names both atlases.
    const auto original_menu = [&] {
        return entry(re_cpk, "GUI/REHD_title.mbs") == refined && entry(or_cpk, "title.mbs") == classic &&
               entry(re_cpk, "GUI/HD_title.ftx") == hk_atlas && entry(or_cpk, "title.ftx") == hk_atlas && no_title_layer() &&
               !TitleExitPending(app0, *hk, Span(title_patch)) && !TitleExitReady(app0, *hk, Span(title_patch));
    };
    const auto names_both_atlases = [](const PromptsResult& result) {
        return result.title_unsupported.size() == 2 &&
               Contains(result.title_unsupported[0], "build CUSA03873 1.00 HK, archive Odin2_OR_CN.CPK, entry title.ftx") &&
               Contains(result.title_unsupported[1], "build CUSA03873 1.00 HK, archive Odin2_RE_CN.CPK, entry GUI/HD_title.ftx");
    };
    PromptsResult switched = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(prompts_patch), {}, Span(title_patch));
    CHECK(switched.ok && names_both_atlases(switched) && Contains(switched.detail, "title menu: unavailable"));
    CHECK(InstalledPromptStyle(app0, *hk, Span(prompts_patch)) == PromptStyle::kXbox && prompts_are(iface_x) && original_menu());

    switched = SetPromptStyle(app0, *hk, PromptStyle::kPlayStation, {}, {}, Span(title_patch));
    CHECK(switched.ok && names_both_atlases(switched) && !AnyPromptPatch(app0, *hk) && originals() && original_menu());
    switched = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(prompts_patch), {}, Span(title_patch));
    CHECK(switched.ok && InstalledPromptStyle(app0, *hk, Span(prompts_patch)) == PromptStyle::kXbox && prompts_are(iface_x) &&
          original_menu());
    CHECK(SetPromptStyle(app0, *hk, PromptStyle::kPlayStation, {}, {}, Span(title_patch)).ok && originals());

    // Art the patch was made for: both menus show EXIT GAME, through every prompts change, over the prompts layer.
    or_cn = classic_archive(atlas);
    re_cn = refined_archive(atlas);
    WriteAll(or_cpk, or_cn);
    WriteAll(re_cpk, re_cn);
    CHECK(TitleExitPending(app0, *hk, Span(title_patch)));
    const auto exit_menu = [&] {
        return entry(re_cpk, "GUI/REHD_title.mbs") == refined_rows && entry(or_cpk, "title.mbs") == classic_rows &&
               entry(re_cpk, "GUI/HD_title.ftx") == exit_art && entry(or_cpk, "title.ftx") == exit_art &&
               !TitleExitPending(app0, *hk, Span(title_patch)) && TitleExitReady(app0, *hk, Span(title_patch));
    };
    switched = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(prompts_patch), {}, Span(title_patch));
    CHECK(switched.ok && switched.title_unsupported.empty() && prompts_are(iface_x) && exit_menu());
    CHECK(fs::exists(UndoPath(or_cpk)) && fs::exists(UndoPath(or_cpk, kTitleUndoSuffix)) && !fs::exists(UndoPath(bt_cpk)));
    switched = SetPromptStyle(app0, *hk, PromptStyle::kPlayStation, {}, {}, Span(title_patch));
    CHECK(switched.ok && !AnyPromptPatch(app0, *hk) && prompts_are(iface) && exit_menu());
    switched = SetPromptStyle(app0, *hk, PromptStyle::kXbox, Span(prompts_patch), {}, Span(title_patch));
    CHECK(switched.ok && InstalledPromptStyle(app0, *hk, Span(prompts_patch)) == PromptStyle::kXbox && prompts_are(iface_x) &&
          exit_menu());
    CHECK(RevertTitleExit(app0, *hk).ok && SetPromptStyle(app0, *hk, PromptStyle::kPlayStation, {}).ok && originals() &&
          no_title_layer());

    // One implementation for Windows and Linux, and the loader asks before it turns the row on.
    const std::filesystem::path root = OSL_SOURCE_DIR;
    for (const char* file : {"launcher/title_exit_assets.cpp", "launcher/controller_prompts.cpp"}) {
        const std::string text = ReadFile(root / file);
        CHECK(!text.empty() && !Contains(text, "_WIN32") && !Contains(text, "windows.h") && !Contains(text, "__linux__"));
    }
    const std::string loader = ReadFile(root / "src" / "loader.c");
    const size_t gate = loader.find("if (!osl_title_exit_assets_ready(app0,image_dir,error,sizeof(error)))");
    CHECK(gate != std::string::npos && gate < loader.find("osl_apply_title_menu_exit(image,size"));
}

// Issue #19: a second CUSA03873 1.00 release with the same package identity as HK and Korean archives besides the
// Chinese ones. Only its eboot.bin tells the two apart, and both install side by side.
void TestAsiaBuild() {
    namespace fs = std::filesystem;
    const GameBuild* hk_found = BuildWithLabel("CUSA03873 1.00 HK");
    const GameBuild* asia_found = BuildWithLabel("CUSA03873 1.00 Asia");
    CHECK(hk_found && asia_found);
    if (!hk_found || !asia_found) {
        return;
    }
    const GameBuild& hk = *hk_found;
    const GameBuild& asia = *asia_found;
    CHECK(std::string(asia.title_id) == hk.title_id && std::string(asia.content_id) == hk.content_id);
    CHECK(std::string(asia.version) == "1.00" && std::string(asia.region) == "Asia");
    CHECK(std::string(asia.folder) == "CUSA03873-1.00-Asia" && std::string(hk.folder) == "CUSA03873-1.00");
    CHECK(std::string(asia.eboot_sha256) == "85ea2e4396b94a7b228d11e35a267f401cb2957bd116b3a5d23f5b3cb15a2fb2");
    CHECK(std::string(hk.eboot_sha256) == "8c483dc2c06d958ccd84bdbf726df6d23e31592c7c1bb39eb5425718b5bc1eb3");
    CHECK(asia.layers.size() == 1 && std::string(asia.layers[0].app_ver) == "01.00" &&
          std::string(asia.layers[0].sfo_version) == "01.00" && !asia.layers[0].update);
    CHECK(asia.extra_files.empty() && !asia.merged_sfo_version && !BaseBuildOf(asia) && !UpdateBuildOf(asia));
    const std::vector<std::string> asia_data(asia.data_files.begin(), asia.data_files.end());
    const std::vector<std::string> hk_data(hk.data_files.begin(), hk.data_files.end());
    CHECK((asia_data == std::vector<std::string>{"Odin2_BT_CN.CPK", "Odin2_OR_CN.CPK", "Odin2_RE_CN.CPK", "Odin2_BT_KO.CPK",
                                                 "Odin2_OR_KO.CPK", "Odin2_RE_KO.CPK"}));
    CHECK((hk_data == std::vector<std::string>{"Odin2_BT_CN.CPK", "Odin2_OR_CN.CPK", "Odin2_RE_CN.CPK"}));
    CHECK(asia.mapping == ExecutableMapping::kScanned && asia.resolution_sites.empty() && asia.snapshot_scale_site == 0 &&
          !asia.skip_intro && !asia.bookshelf);
    CHECK(std::none_of(std::begin(lzcnt_builds), std::end(lzcnt_builds),
                       [&](const LzcntBuild& lzcnt) { return std::string_view(lzcnt.build) == asia.label; }));
    CHECK(BuildMetadataText(asia) == "APP_VER 1.00 \xC2\xB7 VERSION 1.00");
    CHECK(InstalledGameName(GameFileState::kValid, &asia) == "PS4 CUSA03873 1.00 Asia");

    // Traditional Chinese (the default) and Korean, as on HK: the game picks its CN or KO archives from that language.
    const std::span<const TextLanguage> languages = TextLanguagesOf(&asia);
    CHECK(asia.text_mode == TextLanguageMode::kSelectable && languages.size() == 2);
    if (languages.size() == 2) {
        CHECK(languages[0].key == "traditional_chinese" && languages[0].ps4_id == 10);
        CHECK(languages[1].key == "korean" && languages[1].ps4_id == 9);
    }
    const fs::path root = TempDir("asia-build");
    for (const GameBuild* build : {&hk, &asia}) {
        const fs::path image_dir = root / "language" / build->folder;
        WriteImageReport(image_dir, *build);
        const fs::path ini = root / "language" / (std::string(build->folder) + ".ini");
        const auto language = [&](const char* key) {
            std::ofstream(ini, std::ios::trunc) << "text_language = " << key << "\n";
            return osl_text_language(ini.string().c_str(), image_dir.string().c_str());
        };
        CHECK(language("korean") == 9 && language("traditional_chinese") == 10 && language("english") == 10 &&
              language("invalid") == 10);
    }

    // The package identity is the same; the eboot.bin decides.
    const std::vector<const GameBuild*> same = SamePackageBuilds(hk);
    CHECK((same == std::vector<const GameBuild*>{&hk, &asia}) && SamePackageBuilds(asia) == same);
    for (const GameBuild& other : SupportedBuilds()) {
        CHECK(&other == &hk || &other == &asia || SamePackageBuilds(other).size() == 1);
    }
    CHECK(BuildWithEboot(hk, hk.eboot_sha256) == &hk && BuildWithEboot(hk, asia.eboot_sha256) == &asia);
    CHECK(BuildWithEboot(asia, hk.eboot_sha256) == &hk && BuildWithEboot(asia, asia.eboot_sha256) == &asia);
    CHECK(BuildWithEboot(hk, std::string(64, 'f')) == nullptr && BuildWithEboot(hk, SupportedBuilds()[0].eboot_sha256) == nullptr);
    const std::vector<std::pair<std::string, std::string>> sfo_values = {
        {"TITLE_ID", "CUSA03873"}, {"CONTENT_ID", "HP0177-CUSA03873_00-ODINSPHERELE0000"},
        {"CATEGORY", "gd"},        {"APP_VER", "01.00"},
        {"VERSION", "01.00"},
    };
    const std::string sfo = SfoText(root, sfo_values);
    const GameCheck package = CheckPackageIdentity(sfo);
    CHECK(package.state == GameFileState::kValid && package.build == &hk && package.layer == 0);
    CHECK(CheckSfoIdentity(sfo).build == &hk);

    // Synthetic games: a build list whose hashes are the synthetic executables' stands in for the real ones.
    std::vector<GameBuild> builds(SupportedBuilds().begin(), SupportedBuilds().end());
    const size_t hk_at = static_cast<size_t>(&hk - SupportedBuilds().data()), asia_at = static_cast<size_t>(&asia - SupportedBuilds().data());
    const auto write_game = [&](const fs::path& dir, const std::string& eboot, std::span<const char* const> data) {
        fs::create_directories(dir / "sce_sys");
        WriteSfo(dir / "sce_sys" / "param.sfo", sfo_values);
        WriteGameFile(dir, "eboot.bin", eboot);
        WriteGameFile(dir, "sce_discmap.plt", "plt");
        WriteGameFile(dir, "sce_module/libc.prx", "libc");
        WriteGameFile(dir, "sce_module/libSceFios2.prx", "fios");
        for (const char* name : data) {
            WriteGameFile(dir, name, std::string("archive ") + name);
        }
    };
    const fs::path hk_source = root / "sources" / "hk", asia_source = root / "sources" / "asia";
    write_game(hk_source, "hk-eboot", hk.data_files);
    write_game(asia_source, "asia-eboot", asia.data_files);
    const std::string hk_sha = Sha256Hex(hk_source / "eboot.bin"), asia_sha = Sha256Hex(asia_source / "eboot.bin");
    builds[hk_at].eboot_sha256 = hk_sha.c_str();
    builds[asia_at].eboot_sha256 = asia_sha.c_str();
    const GameBuild& hk_b = builds[hk_at];
    const GameBuild& asia_b = builds[asia_at];

    const GameCheck asia_tree = CheckGameTree(asia_source, builds);
    CHECK(asia_tree.state == GameFileState::kValid && asia_tree.build == &asia_b && asia_tree.eboot_sha256 == asia_sha);
    const GameCheck hk_tree = CheckGameTree(hk_source, builds);
    CHECK(hk_tree.state == GameFileState::kValid && hk_tree.build == &hk_b);
    for (const char* korean : {"Odin2_BT_KO.CPK", "Odin2_OR_KO.CPK", "Odin2_RE_KO.CPK"}) {
        fs::rename(asia_source / korean, root / korean);
        const GameCheck missing = CheckGameTree(asia_source, builds);
        CHECK(missing.state == GameFileState::kMissing && missing.build == &asia_b && missing.detail == std::string("missing ") + korean);
        fs::rename(root / korean, asia_source / korean);
    }
    const fs::path mixed = root / "mixed";
    write_game(mixed, "hk-eboot", asia.data_files);
    CHECK(CheckGameTree(mixed, builds).state == GameFileState::kValid && CheckGameTree(mixed, builds).build == &hk_b);

    // An unknown CUSA03873 1.00 executable stays unsupported, is checked as HK's family and is diagnosed.
    const fs::path unknown = root / "unknown";
    write_game(unknown, "another-asian-eboot", asia.data_files);
    const GameCheck other = CheckGameTree(unknown, builds);
    CHECK(other.state == GameFileState::kDifferentVersion && other.build == &hk_b &&
          other.detail == "eboot.bin is not the supported build" && other.eboot_sha256 == Sha256Hex(unknown / "eboot.bin"));
    fs::remove(unknown / "Odin2_RE_CN.CPK");
    CHECK(CheckGameTree(unknown, builds).detail == "missing Odin2_RE_CN.CPK" && CheckGameTree(unknown, builds).build == &hk_b);
    WriteGameFile(unknown, "Odin2_RE_CN.CPK", "archive");
    const GameRoots roots{root / "prepared", root / "image"};
    const InstallResult diagnosed = InstallFromFolder(unknown, roots, {}, builds);
    CHECK(diagnosed.status == InstallStatus::kUnsupportedBuild &&
          Contains(diagnosed.report, "SHA-256: " + Sha256Hex(unknown / "eboot.bin")) &&
          Contains(diagnosed.report, "Supported-build check: eboot.bin is not the supported build"));
    fs::remove(unknown / "eboot.bin");
    CHECK(CheckGameTree(unknown, builds).detail == "missing eboot.bin");
    CHECK(!fs::exists(roots.prepared) && !fs::exists(roots.image));

    // Both prepared: separate folders, each checked against its own boot image, the active one the last chosen.
    const GamePaths hk_paths = BuildPaths(roots, hk_b), asia_paths = BuildPaths(roots, asia_b);
    CHECK(hk_paths.app0 != asia_paths.app0 && hk_paths.image_dir != asia_paths.image_dir);
    CHECK(asia_paths.app0.filename() == "CUSA03873-1.00-Asia" && hk_paths.app0.filename() == "CUSA03873-1.00");
    const auto prepare = [&](const GameBuild& build, const fs::path& source, int seconds) {
        const GamePaths paths = BuildPaths(roots, build);
        fs::create_directories(paths.app0.parent_path());
        fs::copy(source, paths.app0, fs::copy_options::recursive);
        std::ofstream(paths.app0 / kPreparedMarker) << "{\"version\": \"" << build.version << "\", \"scope\": \"full\"}\n";
        fs::last_write_time(paths.app0 / kPreparedMarker,
                            fs::file_time_type::clock::now() - std::chrono::hours(24) + std::chrono::seconds(seconds));
        WriteGameFile(paths.image_dir, "boot.bin", "image");
        WriteImageReport(paths.image_dir, build);
    };
    prepare(hk_b, hk_source, 10);
    prepare(asia_b, asia_source, 20);
    const GameCheck asia_ready = CheckGameFiles(asia_paths, builds), hk_ready = CheckGameFiles(hk_paths, builds);
    CHECK(asia_ready.state == GameFileState::kValid && asia_ready.build == &asia_b);
    CHECK(hk_ready.state == GameFileState::kValid && hk_ready.build == &hk_b);
    CHECK(LastPreparedBuild(roots, builds) == &asia_b && ActiveBuildPaths(roots, builds).app0 == asia_paths.app0);
    WriteImageReport(asia_paths.image_dir, hk_b);
    CHECK(CheckGameFiles(asia_paths, builds).detail == "the boot image was built from another eboot.bin");
    WriteImageReport(asia_paths.image_dir, asia_b);

    const std::map<std::string, std::string> hk_prepared = TreeContents(hk_paths.app0), asia_prepared = TreeContents(asia_paths.app0);
    CHECK(InstallFromFolder(asia_source, roots, {}, builds).status == InstallStatus::kUpToDate);
    const InstallResult to_hk = InstallFromFolder(hk_source, roots, {}, builds);
    CHECK(to_hk.status == InstallStatus::kActivated && to_hk.build == &hk_b && LastPreparedBuild(roots, builds) == &hk_b);
    const InstallResult to_asia = InstallFromFolder(asia_source, roots, {}, builds);
    CHECK(to_asia.status == InstallStatus::kActivated && to_asia.build == &asia_b && LastPreparedBuild(roots, builds) == &asia_b);
    CHECK(TreeContents(hk_paths.app0) == hk_prepared && TreeContents(asia_paths.app0) == asia_prepared);

    // A failed installation of one release leaves both installations as they were.
    InstallHooks reinstall;
    reinstall.replace_active = true;
    const InstallResult failed = InstallFromFolder(asia_source, roots, reinstall, builds);
    CHECK(failed.status == InstallStatus::kPrepareFailed);
    CHECK(TreeContents(hk_paths.app0) == hk_prepared && TreeContents(asia_paths.app0) == asia_prepared);
    CHECK(CheckGameFiles(asia_paths, builds).build == &asia_b && LastPreparedBuild(roots, builds) == &asia_b);
    fs::remove_all(asia_paths.app0);
    fs::remove_all(asia_paths.image_dir);
    const InstallResult fresh = InstallFromFolder(asia_source, roots, {}, builds);
    CHECK(fresh.status == InstallStatus::kPrepareFailed && !fs::exists(asia_paths.app0) && !fs::exists(asia_paths.image_dir));
    CHECK(TreeContents(hk_paths.app0) == hk_prepared && LastPreparedBuild(roots, builds) == &hk_b);
    for (const fs::path& folder : {roots.prepared, roots.image}) {
        std::error_code ec;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            CHECK(!Contains(it->path().filename().string(), ".install-"));
        }
    }

    // The installation log names the release the eboot.bin selected, and its own folder.
    PackageLogContext context{"HP0177-CUSA03873_00-ODINSPHERELE0000-A0100-V0100.pkg", sfo, package};
    context.identity.build = &asia;
    InstallResult installed;
    installed.build = &asia;
    const std::string log = PackageInstallLog(context, installed, roots);
    CHECK(Contains(log, "Package recognized: HP0177-CUSA03873_00-ODINSPHERELE0000-A0100-V0100.pkg is CUSA03873 1.00 Asia (base, layer 1 of 1)"));
    CHECK(Contains(log, "Final prepared build: CUSA03873 1.00 Asia (CUSA03873-1.00-Asia)") && !Contains(log, "1.00 HK"));
    InstallResult incomplete;
    incomplete.status = InstallStatus::kIncomplete;
    incomplete.detail = "missing Odin2_RE_KO.CPK";
    const std::string failure = PackageInstallLog(context, incomplete, roots);
    CHECK(Contains(failure, "Matched build: CUSA03873 1.00 Asia") && Contains(failure, "Target prepared folder: CUSA03873-1.00-Asia"));

    // One identity path for Windows and Linux.
    for (const char* file : {"launcher/game_profile.cpp", "launcher/game_files.cpp"}) {
        const std::string text = ReadFile(fs::path(OSL_SOURCE_DIR) / file);
        CHECK(!text.empty() && !Contains(text, "_WIN32") && !Contains(text, "__linux__"));
    }
}

void TestAsiaScannedMapping() {
    const GameBuild* asia = BuildWithLabel("CUSA03873 1.00 Asia");
    const GameBuild* hk = BuildWithLabel("CUSA03873 1.00 HK");
    const GameBuild* usa = BuildWithLabel("CUSA05083 1.00 USA");
    CHECK(asia && hk && usa);
    if (!asia || !hk || !usa) {
        return;
    }
    const MappedExecutable image = Issue19Image();
    const ScanResult scan = ScanExecutable(image, kIssue19EbootImage);
    const ScannedMapping& mapping = scan.mapping;
    CHECK(scan.complete() && scan.unresolved.empty() && UnresolvedFeatures(mapping).empty());
    CHECK(std::equal(mapping.resolution_offsets.begin(), mapping.resolution_offsets.end(), std::begin(kIssue19Resolution),
                     std::end(kIssue19Resolution)));
    CHECK(mapping.snapshot_scale_site == kIssue19Snapshot);
    CHECK(SameSkipIntro(mapping.skip_intro, &kIssue19SkipIntro) && SameBookshelf(mapping.bookshelf, &kIssue19Bookshelf));
    CHECK(std::equal(mapping.lzcnt_offsets.begin(), mapping.lzcnt_offsets.end(), std::begin(kIssue19Lzcnt), std::end(kIssue19Lzcnt)));
    for (const char* line : {"notice scene: unique 0x53d", "hd title check: unique 0x66aa9b", "classic title check: unique 0x1791dd",
                             "classic button: inside the code 0x146980", "stub space: free 0xa8acd8 (808 bytes to the page end)",
                             "getter: unique 0x5c1910", "special test: unique 0x5759b7", "reads: 14/14",
                             "read order: consistent, page read: through a helper", "marks: 2/2 0x5eab81 0x5eae36",
                             "site: unique 0x542f87", "native scale: 1.5 at 0x968b6c (the expected 1.5)",
                             "layer scale base: 0 0 at 0xd6c800", "sites: 17/17", "linked image: 0x1004570, entry 0x67d610",
                             "instructions found: 50 (the shapes hold 50)", "ranges: unique (49 shapes placed in order)",
                             "unexpected instructions: 0", "rewrite check on a scratch copy: ok"}) {
        CHECK(Contains(scan.text, line));
    }

    // The log's sites are the USA 1.00 code shifted as one block: the same instructions, not a lookalike elsewhere.
    for (size_t i = 0; i < std::size(kIssue19Resolution) && i < usa->resolution_sites.size(); ++i) {
        CHECK(usa->resolution_sites[i].offset - kIssue19Resolution[i] == 0x19e0);
    }
    CHECK(usa->bookshelf->accessor - kIssue19Bookshelf.accessor == 0x1740);
    for (int i = 0; i < OSL_BOOKSHELF_READS; ++i) {
        CHECK(usa->bookshelf->reads[i] - kIssue19Bookshelf.reads[i] == 0x1740);
    }
    for (int i = 0; i < OSL_BOOKSHELF_MARKS; ++i) {
        CHECK(usa->bookshelf->marks[i] - kIssue19Bookshelf.marks[i] == 0x1740);
    }
    CHECK(usa->snapshot_scale_site - kIssue19Snapshot == 0x1230 && usa->bookshelf->total - kIssue19Bookshelf.total == 0x1230);
    CHECK(usa->bookshelf->in_place_page_read == kIssue19Bookshelf.in_place_page_read);

    // Deterministic, and no table build's sites.
    CHECK(SameMapping(ScanExecutable(image, kIssue19EbootImage).mapping, mapping));
    for (const GameBuild& build : SupportedBuilds()) {
        if (build.mapping == ExecutableMapping::kTable) {
            CHECK(!SameResolution(mapping.resolution_offsets, build.resolution_sites) && !SameLzcnt(mapping.lzcnt_offsets, build));
            CHECK(!SameSkipIntro(mapping.skip_intro, build.skip_intro) && !SameBookshelf(mapping.bookshelf, build.bookshelf));
        }
    }

    // The patches accept every site on a copy of the image.
    std::vector<unsigned char> scratch(image.image.begin(), image.image.end());
    scratch.resize(image.image.size() + lzcnt_compat_stub_bytes() + OSL_BOOKSHELF_STUB_BYTES, 0);
    const OslResolution target = osl_render_target(&osl_resolutions[OSL_RESOLUTION_COUNT - 1], &osl_aspect_ratios[0]);
    const std::vector<OslResolutionSite> sites = ResolutionSitesAt(SupportedBuilds(), mapping.resolution_offsets);
    char error[320] = "";
    CHECK(sites.size() == 17 && osl_apply_resolution(scratch.data(), image.image.size(), sites.data(), sites.size(), &target, error, sizeof(error)));
    CHECK(osl_apply_snapshot_scale(scratch.data(), image.image.size(), mapping.snapshot_scale_site, &target, error, sizeof(error)));
    CHECK(osl_apply_skip_intro(scratch.data(), 0xa8b000, &*mapping.skip_intro, 1, error, sizeof(error)));
    unsigned char* shelf_stubs = scratch.data() + image.image.size() + lzcnt_compat_stub_bytes();
    CHECK(osl_apply_bookshelf(scratch.data(), image.image.size(), &*mapping.bookshelf, shelf_stubs, OSL_BOOKSHELF_STUB_BYTES, error,
                              sizeof(error)));
    CHECK(lzcnt_compat_apply(scratch.data(), image.image.size(), mapping.lzcnt_offsets.data(), lzcnt_shapes, LZCNT_RANGE_COUNT,
                             scratch.data() + image.image.size(), lzcnt_compat_stub_bytes()) == LZCNT_COMPAT_OK);

    // Recorded at installation and read back by the loader for this release only.
    const std::filesystem::path dir = TempDir("asia-scanned");
    const std::filesystem::path image_dir = dir / "image", ini = dir / "osl.ini";
    WriteImageReport(image_dir, *asia, MappingExtra(mapping));
    CHECK(SameMapping(ScannedMappingOfImageDir(image_dir), mapping));
    OslGameBuild loaded{};
    CHECK(osl_game_build(image_dir.string().c_str(), &loaded) && std::string(loaded.label) == "CUSA03873 1.00 Asia");
    CHECK(loaded.resolution_site_count == 17 && loaded.snapshot_scale_site == kIssue19Snapshot && loaded.lzcnt_offsets &&
          std::equal(mapping.lzcnt_offsets.begin(), mapping.lzcnt_offsets.end(), loaded.lzcnt_offsets));
    CHECK(SameSkipIntro(mapping.skip_intro, loaded.skip_intro) && SameBookshelf(mapping.bookshelf, loaded.bookshelf));
    for (size_t i = 0; i < loaded.resolution_site_count && i < std::size(kIssue19Resolution); ++i) {
        CHECK(loaded.resolution_sites[i].offset == kIssue19Resolution[i]);
    }
    const BuildFeatures features = FeaturesOf(asia, image_dir);
    CHECK(features.custom_resolution && features.skip_intro && features.unlock_bookshelf && features.lzcnt_rewrite);
    std::ofstream(ini) << "render_resolution = " << osl_resolutions[OSL_RESOLUTION_COUNT - 1].key
                       << "\naspect_ratio = " << osl_aspect_ratios[OSL_ASPECT_COUNT - 1].key << "\n";
    CHECK(osl_render_resolution(ini.string().c_str(), image_dir.string().c_str()) == OSL_RESOLUTION_COUNT - 1);
    CHECK(osl_aspect_ratio(ini.string().c_str(), image_dir.string().c_str()) == OSL_ASPECT_COUNT - 1);
    WriteImageReport(image_dir, *hk, MappingExtra(mapping));
    CHECK(osl_game_build(image_dir.string().c_str(), &loaded) && std::string(loaded.label) == "CUSA03873 1.00 HK");
}

// Issue #19: the Asia release's Chinese button art is taken through HK's identity only when it is HK's exact file.
// Its Korean archives are its own data: art in them the patch does not know refuses Xbox for the whole build.
void TestAsiaPrompts() {
    using namespace cpk_test;
    const GameBuild* hk = BuildWithLabel("CUSA03873 1.00 HK");
    const GameBuild* asia = BuildWithLabel("CUSA03873 1.00 Asia");
    CHECK(hk && asia);
    if (!hk || !asia) {
        return;
    }
    CHECK(asia->prompt_profiles.size() == 2 && asia->prompt_profiles.size() == hk->prompt_profiles.size());
    for (size_t i = 0; i < asia->prompt_profiles.size() && i < hk->prompt_profiles.size(); ++i) {
        const KnownEntryProfile &mine = asia->prompt_profiles[i], &theirs = hk->prompt_profiles[i];
        CHECK(std::string_view(mine.label) == "Asia" && mine.name == theirs.name && mine.size == theirs.size &&
              mine.crc == theirs.crc && mine.reference_crc == theirs.reference_crc);
    }
    constexpr size_t kSize = 4458000, kRuns = 3368, kFirstRun = 0x4004d0;
    const auto xbox = [&](std::string data) {
        for (size_t run = 0; run < kRuns; ++run) data.replace(kFirstRun + run * 16, 16, 16, 'X');
        return data;
    };
    const std::string usa_art = WithCrc32(Pattern(kSize, 3), 0xe47e29e5);
    const std::string chinese = WithCrc32(Pattern(kSize, 11), 0xc2d4a197);
    const std::string other_chinese = Pattern(kSize, 19), korean = Pattern(kSize, 29);
    CHECK(Crc32(Span(chinese)) == 0xc2d4a197 && Crc32(Span(korean)) != 0xc2d4a197 && Crc32(Span(other_chinese)) != 0xc2d4a197);
    const std::string patch = MakePatch({{"Interface.ftx", {usa_art, xbox(usa_art)}}, {"GUI/HD_Interface.ftx", {usa_art, xbox(usa_art)}}});

    const fs::path app0 = TempDir("prompts-asia") / asia->folder;
    fs::create_directories(app0);
    const std::string boot = BuildCpk({{"other/x.bin", "BOOT" + std::string(600, '\0'), false}}, false);
    const std::string ko_text = BuildCpk({{"text/ko.bin", Pattern(4096, 31), false}}, true);
    const std::string ko_hd_text = BuildCpk({{"text/ko_hd.bin", Pattern(4096, 37), false}}, false);
    const auto install = [&](const std::string& cn_art, const std::string& or_ko, const std::string& re_ko) {
        const std::map<std::string, std::string> files = {
            {"Odin2_BT_CN.CPK", boot}, {"Odin2_OR_CN.CPK", BuildCpk({{"Interface.ftx", cn_art, false}}, true)},
            {"Odin2_RE_CN.CPK", BuildCpk({{"GUI/HD_Interface.ftx", cn_art, false}}, false)},
            {"Odin2_BT_KO.CPK", boot}, {"Odin2_OR_KO.CPK", or_ko}, {"Odin2_RE_KO.CPK", re_ko}};
        for (const auto& [name, bytes] : files) {
            fs::remove(UndoPath(app0 / name));
            WriteAll(app0 / name, bytes);
        }
        return files;
    };
    const auto unchanged = [&](const std::map<std::string, std::string>& files) {
        return std::all_of(files.begin(), files.end(), [&](const auto& file) {
            return ReadFile(app0 / file.first) == file.second && !fs::exists(UndoPath(app0 / file.first));
        });
    };
    const auto entry = [&](const char* archive, const char* name) {
        CpkArchive cpk;
        const CpkEntry* found = cpk.Open(app0 / archive) ? cpk.Find(name) : nullptr;
        return found ? Bytes(cpk.Read(*found)) : std::string();
    };

    // Korean archives without button art: the Chinese art is HK's file, so it is patched, then restored exactly.
    std::map<std::string, std::string> files = install(chinese, ko_text, ko_hd_text);
    PromptsResult r = SetPromptStyle(app0, *asia, PromptStyle::kXbox, Span(patch));
    CHECK(r.ok && r.unsupported.empty() && InstalledPromptStyle(app0, *asia, Span(patch)) == PromptStyle::kXbox);
    CHECK(Contains(r.detail, "Controller prompts Asia compatibility: Interface.ftx, CRC32 c2d4a197, reference e47e29e5"));
    CHECK(Contains(r.detail, "Controller prompts Asia compatibility: GUI/HD_Interface.ftx, CRC32 c2d4a197, reference e47e29e5"));
    CHECK(entry("Odin2_OR_CN.CPK", "Interface.ftx") == xbox(chinese) && entry("Odin2_RE_CN.CPK", "GUI/HD_Interface.ftx") == xbox(chinese));
    for (const char* korean_archive : {"Odin2_BT_KO.CPK", "Odin2_OR_KO.CPK", "Odin2_RE_KO.CPK"}) {
        CHECK(ReadFile(app0 / korean_archive) == files[korean_archive] && !fs::exists(UndoPath(app0 / korean_archive)));
    }
    CHECK(SetPromptStyle(app0, *asia, PromptStyle::kPlayStation, {}).ok && unchanged(files) && !AnyPromptPatch(app0, *asia));

    // Korean button art the patch does not know: nothing stays patched, and the Korean archive is named.
    files = install(chinese, BuildCpk({{"Interface.ftx", korean, false}}, true), BuildCpk({{"GUI/HD_Interface.ftx", korean, false}}, false));
    r = SetPromptStyle(app0, *asia, PromptStyle::kXbox, Span(patch));
    CHECK(!r.ok && !AnyPromptPatch(app0, *asia) && InstalledPromptStyle(app0, *asia, Span(patch)) == PromptStyle::kPlayStation);
    CHECK(unchanged(files) && AnyContains(r.unsupported, "build CUSA03873 1.00 Asia, archive Odin2_OR_KO.CPK, entry Interface.ftx"));

    // Chinese art that is not HK's file is refused the same way.
    files = install(other_chinese, ko_text, ko_hd_text);
    r = SetPromptStyle(app0, *asia, PromptStyle::kXbox, Span(patch));
    CHECK(!r.ok && unchanged(files) && !Contains(r.detail, "Asia compatibility") &&
          AnyContains(r.unsupported, "build CUSA03873 1.00 Asia, archive Odin2_OR_CN.CPK, entry Interface.ftx"));
}

// Issue #19: title art the Exit Game patch was not made for keeps the Asia release's menu its own and the row off,
// in its Chinese or its Korean archives; art the patch knows in every archive turns it on.
void TestAsiaTitleExit() {
    using namespace cpk_test;
    using namespace title_exit_test;
    const GameBuild* asia = BuildWithLabel("CUSA03873 1.00 Asia");
    const std::string title_patch = ReadFile(std::filesystem::path(OSL_SOURCE_DIR) / "launcher" / "title" / "title-exit.dcpx");
    const std::optional<BlockPatch> shipped = ParseBlockPatch(Span(title_patch));
    CHECK(asia && shipped);
    if (!asia || !shipped) {
        return;
    }
    const BlockPatchTarget* atlas_target = ShippedTarget(*shipped, "GUI/HD_title.ftx", 0xbf7fa72a);
    const BlockPatchTarget* refined_target = ShippedTarget(*shipped, "GUI/REHD_title.mbs", 0xcb49d54c);
    const BlockPatchTarget* classic_target = ShippedTarget(*shipped, "title.mbs", 0x24c6ab07);
    CHECK(atlas_target && refined_target && classic_target);
    if (!atlas_target || !refined_target || !classic_target) {
        return;
    }
    const std::string atlas = SourceCopy(*atlas_target), refined = SourceCopy(*refined_target), classic = SourceCopy(*classic_target);
    std::string unknown_atlas = atlas;
    unknown_atlas[atlas_target->runs.front().offset] ^= 0x5a;
    const std::string boot = BuildCpk({{"other/x.bin", "BOOT" + std::string(600, '\0'), false}}, false);
    const auto classic_archive = [&](const std::string& art) {
        return BuildCpk({{"title.ftx", art, false}, {"title.mbs", classic, false}}, true);
    };
    const auto refined_archive = [&](const std::string& art) {
        return BuildCpk({{"GUI/HD_title.ftx", art, false}, {"GUI/REHD_title.mbs", refined, false}}, false);
    };
    const fs::path root = TempDir("asia-title"), app0 = root / asia->folder, image = root / "image";
    fs::create_directories(app0);
    WriteImageReport(image, *asia);
    const auto gate = [&] { return TitleExitAssetsReady(app0.string().c_str(), image.string().c_str(), Span(title_patch)); };
    const auto install = [&](const std::string& chinese, const std::string& korean) {
        const std::map<std::string, std::string> files = {
            {"Odin2_BT_CN.CPK", boot}, {"Odin2_OR_CN.CPK", classic_archive(chinese)}, {"Odin2_RE_CN.CPK", refined_archive(chinese)},
            {"Odin2_BT_KO.CPK", boot}, {"Odin2_OR_KO.CPK", classic_archive(korean)}, {"Odin2_RE_KO.CPK", refined_archive(korean)}};
        for (const auto& [name, bytes] : files) {
            fs::remove(UndoPath(app0 / name, kTitleUndoSuffix));
            fs::remove(UndoPath(app0 / name, ".title.stamp"));
            WriteAll(app0 / name, bytes);
        }
        return files;
    };
    const auto unchanged = [&](const std::map<std::string, std::string>& files) {
        return std::all_of(files.begin(), files.end(), [&](const auto& file) {
            return ReadFile(app0 / file.first) == file.second && !fs::exists(UndoPath(app0 / file.first, kTitleUndoSuffix));
        });
    };
    const auto refused = [&](const std::string& chinese, const std::string& korean, const char* named) {
        const std::map<std::string, std::string> files = install(chinese, korean);
        const TitleExitResult result = ApplyTitleExit(app0, *asia, Span(title_patch));
        return !result.ok && result.unavailable && unchanged(files) && !gate() && !TitleExitReady(app0, *asia, Span(title_patch)) &&
               !TitleExitPending(app0, *asia, Span(title_patch)) && AnyContains(result.unsupported, std::string("build CUSA03873 1.00 Asia, ") + named);
    };
    CHECK(refused(unknown_atlas, unknown_atlas, "archive Odin2_OR_CN.CPK, entry title.ftx"));
    CHECK(refused(unknown_atlas, atlas, "archive Odin2_RE_CN.CPK, entry GUI/HD_title.ftx"));
    CHECK(refused(atlas, unknown_atlas, "archive Odin2_OR_KO.CPK, entry title.ftx"));

    const std::map<std::string, std::string> files = install(atlas, atlas);
    CHECK(!gate() && TitleExitPending(app0, *asia, Span(title_patch)));
    CHECK(ApplyTitleExit(app0, *asia, Span(title_patch)).ok && gate() && TitleExitReady(app0, *asia, Span(title_patch)));
    CHECK(RevertTitleExit(app0, *asia).ok && unchanged(files) && !gate());
}

struct TitleExitFixture {
    std::vector<unsigned char> image; // the game image, then the stub page
    uint64_t size = 0;
    std::map<std::string, size_t> sites;
};

// The title menu code as the Exit Game patch expects it: each signature once (or twice, or not at all) and the Classic
// row tables behind their lookups.
TitleExitFixture TitleExitImage(const std::set<std::string>& absent = {}, const std::set<std::string>& doubled = {}) {
    TitleExitFixture fixture;
    fixture.size = 0x40000;
    fixture.image.assign(fixture.size + OSL_TITLE_EXIT_STUB_BYTES, 0);
    static const std::map<std::string, std::array<int32_t, 3>> kTables = {
        {"classic_lookup_a", {5, 11, 23}}, {"classic_lookup_b", {9, 15, 27}}, {"classic_lookup_c", {7, 13, 25}}};
    size_t count = 0, at = 0x1000, table = 0x30000;
    const OslTitleExitSignature* signatures = osl_title_exit_signatures(&count);
    for (size_t i = 0; i < count; ++i) {
        const std::string name = signatures[i].name;
        if (absent.count(name)) {
            continue;
        }
        for (int copy = 0; copy < (doubled.count(name) ? 2 : 1); ++copy) {
            const size_t where = at;
            at += 0x100;
            for (size_t k = 0; k < signatures[i].count; ++k) {
                fixture.image[where + k] =
                    signatures[i].bytes[k] < 0 ? 0 : static_cast<unsigned char>(signatures[i].bytes[k]);
            }
            fixture.sites.emplace(name, where);
            const auto rows = kTables.find(name);
            if (rows == kTables.end()) {
                continue;
            }
            size_t lea = 0;
            while (!(signatures[i].bytes[lea] == 0x48 && signatures[i].bytes[lea + 1] == 0x8d &&
                     signatures[i].bytes[lea + 2] == 0x0d)) {
                ++lea;
            }
            const int32_t disp = static_cast<int32_t>(static_cast<int64_t>(table) - static_cast<int64_t>(where + lea + 7));
            std::memcpy(fixture.image.data() + where + lea + 3, &disp, 4);
            std::memcpy(fixture.image.data() + table, rows->second.data(), 12);
            table += 0x20;
        }
    }
    return fixture;
}

int32_t Int32At(const std::vector<unsigned char>& image, size_t at) {
    int32_t value = 0;
    std::memcpy(&value, image.data() + at, 4);
    return value;
}

int g_title_exit_calls = 0;
void CountTitleExit() {
    ++g_title_exit_calls;
}

void TestTitleMenuExit() {
    char error[320] = "";
    const TitleExitFixture fixture = TitleExitImage();
    size_t count = 0;
    const OslTitleExitSignature* signatures = osl_title_exit_signatures(&count);
    CHECK(count == 11);

    OslTitleExitPlan plan;
    CHECK(osl_title_exit_plan(fixture.image.data(), fixture.size, fixture.size, 0x1122334455667788ull, &plan, error,
                              sizeof(error)));
    CHECK(plan.count == 15 && plan.stub_used > 0 && plan.stub_used <= sizeof(plan.stub));
    CHECK(osl_title_exit_validate(&plan, fixture.image.data(), fixture.size, fixture.size, OSL_TITLE_EXIT_STUB_BYTES,
                                  error, sizeof(error)));

    std::vector<unsigned char> patched = fixture.image;
    CHECK(osl_apply_title_menu_exit(patched.data(), fixture.size, fixture.size, OSL_TITLE_EXIT_STUB_BYTES,
                                    0x1122334455667788ull, error, sizeof(error)));
    CHECK(patched != fixture.image);
    const auto site = [&](const char* name) { return fixture.sites.at(name); };

    // Refined: the game's five-row range with no row skipped, and every row plays its four-row animation variant.
    CHECK(patched[site("refined_ctor") + 6] == 5);
    CHECK(patched[site("refined_up") + 8] == 0xff && patched[site("refined_down") + 8] == 0xff);
    const std::array<int32_t, 5> original_rows = {0, 48, 12, 24, 36}, four_row_variants = {1, 49, 13, 25, 37};
    for (size_t i = 0; i < 5; ++i) {
        CHECK(Int32At(fixture.image, site("refined_rows") + 4 * i) == original_rows[i]);
        CHECK(Int32At(patched, site("refined_rows") + 4 * i) == four_row_variants[i]);
    }
    CHECK(patched[site("refined_confirm") + 24] == 0xc7 && Int32At(patched, site("refined_confirm") + 30) == 4);
    CHECK(patched[site("refined_result")] == 0xe9);

    // Classic: the shipped tables stay as they were; the copies play the four-row variants, the INSTALL group third and the MODE group fourth.
    CHECK(Int32At(patched, site("classic_down") + 15) == 3 && patched[site("classic_down") + 21] == 3);
    const struct {
        const char* name;
        size_t bound, disp;
        std::array<int32_t, 4> rows;
    } lookups[] = {{"classic_lookup_a", 12, 18, {6, 12, 18, 24}},
                   {"classic_lookup_b", 10, 16, {10, 16, 22, 28}},
                   {"classic_lookup_c", 12, 21, {8, 14, 20, 26}}};
    for (const auto& lookup : lookups) {
        const size_t at = site(lookup.name);
        CHECK(fixture.image[at + lookup.bound] == 2 && patched[at + lookup.bound] == 3);
        const int64_t before = static_cast<int64_t>(at + lookup.disp) + 4 + Int32At(fixture.image, at + lookup.disp);
        const int64_t after = static_cast<int64_t>(at + lookup.disp) + 4 + Int32At(patched, at + lookup.disp);
        CHECK(after >= static_cast<int64_t>(fixture.size) && before < static_cast<int64_t>(fixture.size));
        for (size_t i = 0; i < 4; ++i) {
            CHECK(Int32At(patched, static_cast<size_t>(after) + 4 * i) == lookup.rows[i]);
            CHECK(i == 3 || Int32At(patched, static_cast<size_t>(before) + 4 * i) == Int32At(fixture.image, static_cast<size_t>(before) + 4 * i));
        }
    }
    CHECK(patched[site("classic_other")] == 0xe9);

    size_t differing = 0, planned = 0;
    for (size_t i = 0; i < fixture.size; ++i) {
        differing += patched[i] != fixture.image[i];
    }
    for (size_t i = 0; i < plan.count; ++i) {
        planned += plan.patches[i].length;
    }
    CHECK(differing <= planned);

    // A site that is missing or ambiguous, or code that differs, writes nothing.
    const auto refused = [&](const TitleExitFixture& broken, const char* expected,
                             size_t stub_bytes = OSL_TITLE_EXIT_STUB_BYTES) {
        std::vector<unsigned char> image = broken.image;
        char message[320] = "";
        const bool applied =
            osl_apply_title_menu_exit(image.data(), broken.size, broken.size, stub_bytes, 1, message, sizeof(message));
        CHECK(!applied && image == broken.image && Contains(message, expected));
    };
    for (size_t i = 0; i < count; ++i) {
        refused(TitleExitImage({signatures[i].name}), "not found");
        refused(TitleExitImage({}, {signatures[i].name}), "ambiguous");
    }
    TitleExitFixture changed_table = TitleExitImage();
    {
        const size_t lookup = changed_table.sites["classic_lookup_a"];
        const int64_t table = static_cast<int64_t>(lookup + 18) + 4 + Int32At(changed_table.image, lookup + 18);
        changed_table.image[static_cast<size_t>(table)] = 6;
    }
    refused(changed_table, "table differs");
    TitleExitFixture busy_stub = TitleExitImage();
    busy_stub.image[busy_stub.size + 3] = 1;
    refused(busy_stub, "stub space in use");
    refused(TitleExitImage(), "stub space too small", 16);

    // Overlap, bounds, expected bytes and stub range are each rejected.
    OslTitleExitPlan broken = plan;
    broken.patches[1].at = broken.patches[0].at;
    CHECK(!osl_title_exit_validate(&broken, fixture.image.data(), fixture.size, fixture.size, OSL_TITLE_EXIT_STUB_BYTES,
                                   error, sizeof(error)) &&
          Contains(error, "overlapping"));
    broken = plan;
    broken.patches[0].at = fixture.size - 1;
    broken.patches[0].length = 2;
    CHECK(!osl_title_exit_validate(&broken, fixture.image.data(), fixture.size, fixture.size, OSL_TITLE_EXIT_STUB_BYTES,
                                   error, sizeof(error)));
    broken = plan;
    broken.patches[0].expected[0] ^= 0xff;
    CHECK(!osl_title_exit_validate(&broken, fixture.image.data(), fixture.size, fixture.size, OSL_TITLE_EXIT_STUB_BYTES,
                                   error, sizeof(error)) &&
          Contains(error, "unexpected"));
    CHECK(!osl_title_exit_validate(&plan, fixture.image.data(), fixture.size, fixture.size - 16,
                                   OSL_TITLE_EXIT_STUB_BYTES, error, sizeof(error)));

    // One confirm asks the host to exit once, whatever else the guest calls.
    g_title_exit_calls = 0;
    osl_title_menu_exit_set_host(CountTitleExit);
    osl_title_menu_exit_rearm();
    osl_title_menu_exit_request();
    osl_title_menu_exit_request();
    CHECK(g_title_exit_calls == 1);
    osl_title_menu_exit_rearm();
    osl_title_menu_exit_request();
    CHECK(g_title_exit_calls == 2);
    osl_title_menu_exit_set_host(nullptr);
    osl_title_menu_exit_rearm();

    // The request ends the process through the shutdown the window close uses, in one place.
    const std::filesystem::path root(OSL_SOURCE_DIR);
    const std::string gpu = ReadFile(root / "gpu" / "shim" / "oslgpu.cpp");
    const std::string session = ReadFile(root / "src" / "runtime_session_log.c");
    const std::string loader = ReadFile(root / "src" / "loader.c");
    const size_t exit_function = session.find("void runtime_exit_clean(void)");
    CHECK(exit_function != std::string::npos);
    CHECK(session.find("runtime_log_mark_clean_exit();", exit_function) != std::string::npos &&
          session.find("runtime_log_finalize();", exit_function) != std::string::npos &&
          session.find("runtime_log_flush();", exit_function) != std::string::npos &&
          session.find("_Exit(0);", exit_function) != std::string::npos);
    CHECK(Contains(gpu, "runtime_exit_clean();") && !Contains(gpu, "std::_Exit(0)"));
    CHECK(Contains(loader, "osl_title_menu_exit_set_host(runtime_exit_clean)") &&
          Contains(loader, "osl_apply_title_menu_exit(") && Contains(loader, "Title menu Exit Game: installed") &&
          Contains(loader, "Title menu Exit Game: unavailable: %s"));

    // Linux parity: the feature uses no Win32 API or path syntax, its patch is embedded like the prompts patch, and the
    // prepared game it patches lives in the portable folder outside the read-only AppImage mount.
    const std::string menu_source = ReadFile(root / "launcher" / "title_menu_exit.c");
    const std::string assets_source = ReadFile(root / "launcher" / "title_exit_assets.cpp");
    const std::string header_source = ReadFile(root / "launcher" / "osl_title_menu_exit.h");
    const std::string paths_source = ReadFile(root / "launcher" / "paths.cpp");
    const std::string launcher_build = ReadFile(root / "launcher" / "CMakeLists.txt");
    for (const std::string* source : {&menu_source, &assets_source, &header_source, &session}) {
        CHECK(!source->empty() && !Contains(*source, "_WIN32") && !Contains(*source, "windows.h") && !Contains(*source, "TerminateProcess"));
    }
    CHECK(Contains(assets_source, ".title.stamp") && Contains(assets_source, "std::filesystem"));
    CHECK(Contains(session, "_Exit(0)"));
    CHECK(Contains(launcher_build, "SYMBOL=kTitleExitPatch") && Contains(launcher_build, "title/title-exit.dcpx"));
    CHECK(Contains(paths_source, "APPIMAGE") && Contains(paths_source, "OSL_PORTABLE_DIR"));
    CHECK(Contains(ReadFile(root / "launcher" / "launcher.cpp"), "TitleExitPatch()"));

    for (const GameBuild& build : SupportedBuilds()) {
        PARITY("Runtime Patches",
               osl_title_exit_plan(fixture.image.data(), fixture.size, fixture.size, 1, &plan, error, sizeof(error)));
    }
}

void PrintParitySummary() {
    static const char* const kCategories[] = {"Installation", "Languages", "Display",       "Graphics",
                                              "Gameplay",     "Controls",  "Trophies",      "Save Data",
                                              "Runtime Patches"};
    std::printf("\nBuild parity\n");
    for (const GameBuild& build : SupportedBuilds()) {
        const ParityRow& row = ParityOf(build);
        bool pass = true;
        for (const char* category : kCategories) {
            const auto found = row.ok.find(category);
            pass = pass && found != row.ok.end() && found->second;
            if (found == row.ok.end()) {
                ++g_failures;
                std::printf("FAIL %s [%s]: no check ran\n", build.label, category);
            }
        }
        std::printf("  %-24s %s\n", build.label, pass ? "PASS" : "FAIL");
    }
    std::printf("\nTitle menu Exit Game: implementation covers every build above; local game files to verify against\n");
    for (const GameBuild& build : SupportedBuilds()) {
        const bool present = std::filesystem::exists(std::filesystem::path(OSL_SOURCE_DIR) / "game" / "prepared" / build.folder);
        std::printf("  %-24s %s\n", build.label, present ? "local files present" : "no local files (not verified)");
    }
    std::printf("\nFeature parity\n  %-24s", "");
    for (const char* category : kCategories) {
        std::printf(" %-15s", category);
    }
    std::printf("\n");
    for (const GameBuild& build : SupportedBuilds()) {
        const ParityRow& row = ParityOf(build);
        std::printf("  %-24s", build.label);
        for (const char* category : kCategories) {
            const auto found = row.ok.find(category);
            std::printf(" %-15s", found != row.ok.end() && found->second ? "PASS" : "FAIL");
        }
        std::printf("\n");
    }
    std::printf("\n");
}

} // namespace

namespace title_art {

namespace fs = std::filesystem;

bool g_report = false;
constexpr char kOldPatchStamp[] = "621981ab";

void Row(const GameBuild& build, const std::string& subject, const std::string& result, const std::string& source,
         const std::string& note = {}) {
    if (g_report) {
        std::printf("TITLEART|%s|%s|%s|%s|%s\n", build.label, subject.c_str(), result.c_str(), source.c_str(), note.c_str());
    }
}

bool Verdict(const GameBuild& build, const std::string& subject, bool ok, const std::string& reason) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("TITLEART|%s|%s|FAIL: %s|checks|\n", build.label, subject.c_str(), reason.c_str());
    }
    return ok;
}

uint32_t Crc32Update(uint32_t crc, const unsigned char* data, size_t size) {
    static const auto tables = [] {
        std::array<std::array<uint32_t, 256>, 8> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[0][i] = c;
        }
        for (uint32_t i = 0; i < 256; ++i) {
            for (int s = 1; s < 8; ++s) t[s][i] = t[0][t[s - 1][i] & 255] ^ (t[s - 1][i] >> 8);
        }
        return t;
    }();
    crc = ~crc;
    for (; size >= 8; size -= 8, data += 8) {
        uint32_t a, b;
        std::memcpy(&a, data, 4);
        std::memcpy(&b, data + 4, 4);
        a ^= crc;
        crc = tables[7][a & 255] ^ tables[6][(a >> 8) & 255] ^ tables[5][(a >> 16) & 255] ^ tables[4][a >> 24] ^
              tables[3][b & 255] ^ tables[2][(b >> 8) & 255] ^ tables[1][(b >> 16) & 255] ^ tables[0][b >> 24];
    }
    for (; size; --size, ++data) crc = tables[0][(crc ^ *data) & 255] ^ (crc >> 8);
    return ~crc;
}

uint32_t FileCrc(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<unsigned char> buffer(4u << 20);
    uint32_t crc = 0;
    while (in) {
        in.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(buffer.size()));
        crc = Crc32Update(crc, buffer.data(), size_t(in.gcount()));
    }
    return crc;
}

struct FileState {
    uintmax_t size = 0;
    fs::file_time_type written{};
    uint32_t crc = 0;
    bool operator==(const FileState& other) const { return size == other.size && written == other.written && crc == other.crc; }
};

FileState StateOf(const fs::path& file) {
    std::error_code ec;
    return {fs::file_size(file, ec), fs::last_write_time(file, ec), FileCrc(file)};
}

std::string Stamp(std::span<const uint8_t> patch) {
    char text[16];
    std::snprintf(text, sizeof(text), "%08x", Crc32(patch));
    return text;
}

std::vector<std::string> SidecarNames(const std::string& archive) {
    return {archive, archive + ".undo", archive + ".title.undo", archive + ".title.stamp"};
}

bool IsRefinedArchive(const std::string& name) {
    return name.find("_RE") != std::string::npos;
}

std::vector<std::string> TitleArchiveNames(const GameBuild& build) {
    std::vector<std::string> names;
    for (const char* name : build.data_files) {
        const std::string text = name;
        if (text.find("_BT") == std::string::npos && text.find("BOOT") == std::string::npos) names.push_back(text);
    }
    return names;
}

// Synthetic stand-ins for the build's own archives: the title and button art entries of each, nothing else.
struct Synthetic {
    std::string atlas = cpk_test::Pattern(24576, 5), atlas_x, refined = cpk_test::Pattern(8192, 9), refined_x;
    std::string classic = cpk_test::Pattern(6144, 3), classic_x, iface = cpk_test::Pattern(20480, 7), iface_x;
    std::string title_patch, prompts_patch, bt = "BOOT" + std::string(600, '\0');

    Synthetic() {
        using namespace cpk_test;
        atlas_x = Painted(atlas, {0x340, 0x1230, 0x5ff0}, 'T');
        refined_x = Painted(refined, {0x80, 0x1000}, 'R');
        classic_x = Painted(classic, {0x40}, 'C');
        iface_x = Painted(iface, {0x340, 0x1000}, 'X');
        title_patch = MakePatch({{"title.ftx", {atlas, atlas_x}},
                                 {"GUI/HD_title.ftx", {atlas, atlas_x}},
                                 {"GUI/REHD_title.mbs", {refined, refined_x}},
                                 {"title.mbs", {classic, classic_x}}});
        prompts_patch = MakePatch({{"Interface.ftx", {iface, iface_x}}, {"GUI/HD_Interface.ftx", {iface, iface_x}}});
    }

    std::string Archive(const std::string& name, const std::string& art) const {
        using namespace cpk_test;
        if (name.find("_BT") != std::string::npos || name.find("BOOT") != std::string::npos) {
            return BuildCpk({{"other/x.bin", bt, false}}, false);
        }
        if (IsRefinedArchive(name)) {
            return BuildCpk({{"GUI/HD_Interface.ftx", iface, true}, {"GUI/HD_title.ftx", art, false}, {"GUI/REHD_title.mbs", refined, false}}, false);
        }
        return BuildCpk({{"Interface.ftx", iface, true}, {"title.ftx", art, false}, {"title.mbs", classic, false}}, true);
    }
};

std::string EntryBytes(const fs::path& archive, const char* name) {
    CpkArchive cpk;
    const CpkEntry* found = cpk.Open(archive) ? cpk.Find(name) : nullptr;
    return found ? cpk_test::Bytes(cpk.Read(*found)) : std::string();
}

void WriteImageReport(const fs::path& dir, const GameBuild& build) {
    fs::create_directories(dir);
    std::ofstream(dir / "image.json") << "{\n  \"title_id\": \"" << build.title_id << "\",\n  \"app_ver\": \"" << FinishedLayer(build).app_ver
                                      << "\",\n  \"eboot_sha256\": \"" << build.eboot_sha256 << "\"\n}\n";
}

// Every build from SupportedBuilds, with its own archive names: an active Exit Game row needs every title file
// updated; art the patch was not made for leaves the originals and keeps the row off, whatever the prompts.
void CheckStructure(const GameBuild& build) {
    using namespace cpk_test;
    const Synthetic data;
    const std::string label = build.label;
    std::string folder = build.folder;
    const fs::path root = TempDir(("title-art-structure-" + folder).c_str());
    const fs::path app0 = root / "app0", image = root / "image";
    fs::create_directories(app0);
    WriteImageReport(image, build);
    const std::vector<std::string> names = TitleArchiveNames(build);
    const std::span<const uint8_t> patch = Span(data.title_patch), prompts = Span(data.prompts_patch);
    const auto write_all = [&](const std::string& art) {
        for (const char* name : build.data_files) {
            for (const std::string& file : SidecarNames(name)) fs::remove(app0 / file);
            WriteAll(app0 / name, data.Archive(name, art));
        }
    };
    const auto gate = [&] { return TitleExitAssetsReady(app0.string().c_str(), image.string().c_str(), patch); };
    const auto entries_are = [&](bool title_patched, bool prompts_patched) {
        for (const std::string& name : names) {
            const bool refined = IsRefinedArchive(name);
            const std::string atlas = EntryBytes(app0 / name, refined ? "GUI/HD_title.ftx" : "title.ftx");
            const std::string mbs = EntryBytes(app0 / name, refined ? "GUI/REHD_title.mbs" : "title.mbs");
            const std::string button = EntryBytes(app0 / name, refined ? "GUI/HD_Interface.ftx" : "Interface.ftx");
            if (atlas != (title_patched ? data.atlas_x : data.atlas) ||
                mbs != (refined ? (title_patched ? data.refined_x : data.refined) : (title_patched ? data.classic_x : data.classic)) ||
                button != (prompts_patched ? data.iface_x : data.iface)) {
                return false;
            }
        }
        return true;
    };
    const auto bytes_of = [&] {
        std::map<std::string, std::string> all;
        for (const char* name : build.data_files) all[name] = ReadFile(app0 / name);
        return all;
    };

    // Art the patch was made for: active in both prompt states, kept through every switch, removed byte for byte.
    write_all(data.atlas);
    const auto pristine = bytes_of();
    Verdict(build, "structure", !gate() && TitleExitPending(app0, build, patch), "Exit Game gate open before the title art update");
    Verdict(build, "structure", ApplyTitleExit(app0, build, patch).ok && gate() && entries_are(true, false), "supported title art not applied to every archive");
    bool layered = SetPromptStyle(app0, build, PromptStyle::kXbox, prompts, {}, patch).ok && gate() && entries_are(true, true);
    layered = layered && SetPromptStyle(app0, build, PromptStyle::kPlayStation, prompts, {}, patch).ok && gate() && entries_are(true, false);
    layered = layered && SetPromptStyle(app0, build, PromptStyle::kXbox, prompts, {}, patch).ok && gate() && entries_are(true, true);
    Verdict(build, "structure", layered, "prompt switching lost the title art or the prompts");
    Verdict(build, "structure", RevertTitleExit(app0, build).ok && !gate() && SetPromptStyle(app0, build, PromptStyle::kPlayStation, prompts).ok && bytes_of() == pristine,
            "rollback is not byte-identical");

    // The state 0.2.3 left on a build whose title art the patch does not know: the menu files updated, an atlas skipped.
    std::string unsupported = Painted(data.atlas, {0x340}, 'k');
    write_all(unsupported);
    const auto original_menu = bytes_of();
    PatchHooks none;
    const BlockPatch parsed = *ParseBlockPatch(patch);
    for (const std::string& name : names) {
        Verdict(build, "unsupported art", ApplyBlockPatch(app0 / name, parsed, none, kTitleUndoSuffix).ok, "cannot set up the 0.2.3 half-patched state");
        std::ofstream(UndoPath(app0 / name, ".title.stamp"), std::ios::trunc) << Stamp(patch);
    }
    Verdict(build, "unsupported art", TitleExitPending(app0, build, patch) && !gate(), "a half-patched title layer counts as installed");
    PromptsResult result = SetPromptStyle(app0, build, PromptStyle::kXbox, prompts, {}, patch);
    Verdict(build, "unsupported art", result.ok && InstalledPromptStyle(app0, build, prompts) == PromptStyle::kXbox, "prompts do not switch beside unsupported title art");
    Verdict(build, "unsupported art", !gate() && !TitleExitReady(app0, build, patch) && !result.title_unsupported.empty(),
            "Exit Game gate open, or the unsupported file is not named");
    bool menu_original = true;
    for (const std::string& name : names) {
        const bool refined = IsRefinedArchive(name);
        menu_original &= EntryBytes(app0 / name, refined ? "GUI/HD_title.ftx" : "title.ftx") == unsupported &&
                         EntryBytes(app0 / name, refined ? "GUI/REHD_title.mbs" : "title.mbs") == (refined ? data.refined : data.classic) &&
                         !fs::exists(UndoPath(app0 / name, kTitleUndoSuffix));
    }
    Verdict(build, "unsupported art", menu_original, "a menu file or atlas is left half-patched");
    Verdict(build, "unsupported art", SetPromptStyle(app0, build, PromptStyle::kPlayStation, prompts, {}, patch).ok && bytes_of() == original_menu,
            "the original menu is not restored byte for byte");
    Row(build, "structure", "PASS", "synthetic archives of its data files",
        "active in both prompt states; unsupported art keeps the row off and the menu original");
}

struct TitleEntry {
    std::string archive, name;
    size_t size = 0;
    uint32_t crc = 0;
};

std::vector<TitleEntry> TitleEntries(const fs::path& app0, const GameBuild& build, const BlockPatch& patch) {
    std::vector<TitleEntry> out;
    for (const fs::path& archive : PromptArchives(app0, build, patch)) {
        CpkArchive cpk;
        if (!cpk.Open(archive)) continue;
        for (const char* name : {"GUI/HD_title.ftx", "GUI/REHD_title.mbs", "title.ftx", "title.mbs"}) {
            if (const CpkEntry* entry = cpk.Find(name)) {
                const std::vector<uint8_t> data = cpk.Read(*entry);
                out.push_back({archive.filename().string(), entry->path, data.size(), Crc32(data)});
            }
        }
    }
    return out;
}

// Every title entry is the copy of the target that `after` selects (the original, or the patched one).
std::string UnexpectedEntry(const std::vector<TitleEntry>& entries, const BlockPatch& patch, bool after) {
    for (const TitleEntry& entry : entries) {
        const bool known = std::any_of(patch.targets.begin(), patch.targets.end(), [&](const BlockPatchTarget& t) {
            return t.name == entry.name && t.size == entry.size && entry.crc == (after ? t.crc_after : t.crc_before);
        });
        if (!known) {
            char text[160];
            std::snprintf(text, sizeof(text), "%s %s size %zu CRC32 %08x is not the %s copy of any target", entry.archive.c_str(),
                          entry.name.c_str(), entry.size, entry.crc, after ? "patched" : "original");
            return text;
        }
    }
    return entries.empty() ? "no title entry found" : std::string();
}

BlockPatch PatchWithOldEnglish(const BlockPatch& shipped, const BlockPatch& old_targets) {
    BlockPatch out = shipped;
    for (BlockPatchTarget& target : out.targets) {
        for (const BlockPatchTarget& old : old_targets.targets) {
            if (old.name == target.name && old.size == target.size && old.crc_before == target.crc_before) target = old;
        }
    }
    return out;
}

// The real prepared archives of the build, copied: original -> title art -> Xbox -> PlayStation -> rollback, and the
// 0.2.3 English layer replaced. The prepared folder is only read; its files must be the same afterwards.
void CheckRealFiles(const GameBuild& build, const std::string& title_bytes, const std::string& xbox_bytes) {
    using namespace cpk_test;
    const BlockPatch shipped = *ParseBlockPatch(Span(title_bytes));
    const fs::path source = fs::path(OSL_SOURCE_DIR) / "game" / "prepared" / build.folder;
    std::error_code ec;
    const std::vector<fs::path> sources = fs::is_directory(source, ec) ? PromptArchives(source, build, shipped) : std::vector<fs::path>{};
    if (sources.empty()) {
        Row(build, "real files", "DATA UNAVAILABLE", fs::is_directory(source, ec) ? "no prepared title archives" : "no prepared folder",
            "structure only");
        return;
    }
    const fs::path app0 = TempDir(("title-art-real-" + std::string(build.folder)).c_str()) / "app0";
    fs::create_directories(app0);
    std::vector<std::string> names;
    std::map<std::string, FileState> before;
    for (const fs::path& archive : sources) {
        const std::string name = archive.filename().string();
        names.push_back(name);
        before[name] = StateOf(archive);
        for (const std::string& file : SidecarNames(name)) {
            if (fs::exists(source / file)) fs::copy_file(source / file, app0 / file, fs::copy_options::overwrite_existing);
        }
    }
    std::vector<const char*> data_files;
    for (const std::string& name : names) data_files.push_back(name.c_str());
    GameBuild copy = build;
    copy.data_files = data_files;

    const std::span<const uint8_t> patch = Span(title_bytes), xbox = Span(xbox_bytes);
    const auto state_of_copies = [&] {
        std::map<std::string, uint32_t> crcs;
        for (const std::string& name : names) crcs[name] = FileCrc(app0 / name);
        return crcs;
    };
    const auto ready = [&] { return TitleExitReady(app0, copy, patch) && !TitleExitPending(app0, copy, patch); };

    Verdict(build, "real files", RevertTitleExit(app0, copy).ok && SetPromptStyle(app0, copy, PromptStyle::kPlayStation, xbox).ok,
            "cannot bring the copies back to the original");
    const auto pristine = state_of_copies();
    std::string why = UnexpectedEntry(TitleEntries(app0, copy, shipped), shipped, false);
    Verdict(build, "real files", why.empty(), "unsupported title art in the prepared original: " + why);
    if (!why.empty()) {
        Row(build, "real files", "FAIL: unsupported title art", "prepared archives", why);
        return;
    }

    const TitleExitResult applied = ApplyTitleExit(app0, copy, patch);
    why = UnexpectedEntry(TitleEntries(app0, copy, shipped), shipped, true);
    Verdict(build, "real files", applied.ok && why.empty() && ready(), "title art not applied to the real archives: " + applied.detail + why);
    bool layered = SetPromptStyle(app0, copy, PromptStyle::kXbox, xbox, {}, patch).ok && InstalledPromptStyle(app0, copy, xbox) == PromptStyle::kXbox;
    layered = layered && ready() && UnexpectedEntry(TitleEntries(app0, copy, shipped), shipped, true).empty();
    layered = layered && SetPromptStyle(app0, copy, PromptStyle::kPlayStation, xbox, {}, patch).ok && !AnyPromptPatch(app0, copy);
    layered = layered && ready() && UnexpectedEntry(TitleEntries(app0, copy, shipped), shipped, true).empty();
    Verdict(build, "real files", layered, "prompt switching lost the title art on the real archives");
    Verdict(build, "real files", SetPromptStyle(app0, copy, PromptStyle::kXbox, xbox, {}, patch).ok && ready(), "Xbox after PlayStation lost the title art");

    // The 0.2.3 layer: the English atlases painted as EXIT GAM, replaced in place by the current patch.
    const std::string fixture = ReadFile(fs::path(OSL_SOURCE_DIR) / "tests" / "fixtures" / "title-exit-0.2.3-english.dcpx");
    const std::optional<BlockPatch> old_targets = ParseBlockPatch(Span(fixture));
    Verdict(build, "0.2.3 migration", old_targets.has_value(), "the 0.2.3 English fixture is unreadable");
    std::string migration = "no 0.2.3 English art in this build";
    if (old_targets && RevertTitleExit(app0, copy).ok) {
        const std::vector<TitleEntry> originals = TitleEntries(app0, copy, shipped);
        Verdict(build, "0.2.3 migration", UnexpectedEntry(originals, shipped, false).empty(), "cannot reach the original before the 0.2.3 layer");
        const bool english = std::any_of(originals.begin(), originals.end(), [&](const TitleEntry& entry) {
            return std::any_of(old_targets->targets.begin(), old_targets->targets.end(), [&](const BlockPatchTarget& t) {
                return t.name == entry.name && t.size == entry.size && t.crc_before == entry.crc;
            });
        });
        if (english) {
            const BlockPatch old_patch = PatchWithOldEnglish(shipped, *old_targets);
            bool layered_old = true;
            for (const std::string& name : names) {
                layered_old &= ApplyBlockPatch(app0 / name, old_patch, {}, kTitleUndoSuffix).ok;
                std::ofstream(UndoPath(app0 / name, ".title.stamp"), std::ios::trunc) << kOldPatchStamp;
            }
            Verdict(build, "0.2.3 migration", layered_old && TitleExitPending(app0, copy, patch) && !TitleExitReady(app0, copy, patch),
                    "the 0.2.3 title layer is not seen as stale");
            const TitleExitResult replaced = ApplyTitleExit(app0, copy, patch);
            Verdict(build, "0.2.3 migration", replaced.ok && ready() && UnexpectedEntry(TitleEntries(app0, copy, shipped), shipped, true).empty(),
                    "the current patch did not replace the 0.2.3 layer: " + replaced.detail);
            Verdict(build, "0.2.3 migration", SetPromptStyle(app0, copy, PromptStyle::kXbox, xbox, {}, patch).ok && ready() &&
                    InstalledPromptStyle(app0, copy, xbox) == PromptStyle::kXbox, "prompts lost after replacing the 0.2.3 layer");
            migration = "0.2.3 English layer replaced, prompts intact";
        }
    }

    Verdict(build, "real files", RevertTitleExit(app0, copy).ok && SetPromptStyle(app0, copy, PromptStyle::kPlayStation, xbox).ok && state_of_copies() == pristine,
            "rollback is not byte-identical to the prepared original");
    std::map<std::string, FileState> after;
    for (const std::string& name : names) after[name] = StateOf(source / name);
    Verdict(build, "real files", before == after, "the prepared folder changed");
    Row(build, "real files", "PASS", "temporary copies of " + std::to_string(names.size()) + " prepared archive(s)",
        "original -> title -> Xbox -> PlayStation -> rollback; " + migration + "; prepared files unchanged (size, mtime, CRC32)");
}

void WriteManifest(const fs::path& file) {
    std::ofstream out(file, std::ios::trunc);
    for (const GameBuild& build : SupportedBuilds()) {
        out << build.label << '\t' << build.folder << '\t';
        for (size_t i = 0; i < build.data_files.size(); ++i) out << (i ? ";" : "") << build.data_files[i];
        out << '\t';
        const std::span<const TextLanguage> languages = TextLanguagesOf(&build);
        for (size_t i = 0; i < languages.size(); ++i) out << (i ? ";" : "") << languages[i].key << '=' << languages[i].name;
        out << '\n';
    }
}

} // namespace title_art

void TestTitleArtStructure() {
    for (const GameBuild& build : SupportedBuilds()) title_art::CheckStructure(build);
}

int RunTitleArtReport(const char* manifest) {
    title_art::g_report = true;
    title_art::WriteManifest(manifest);
    const std::filesystem::path root = std::filesystem::path(OSL_SOURCE_DIR);
    const std::string title = ReadFile(root / "launcher" / "title" / "title-exit.dcpx");
    const std::string xbox = ReadFile(root / "launcher" / "prompts" / "xbox-prompts.dcpx");
    CHECK(ParseBlockPatch(cpk_test::Span(title)).has_value() && ParseBlockPatch(cpk_test::Span(xbox)).has_value());
    for (const GameBuild& build : SupportedBuilds()) {
        title_art::CheckStructure(build);
        title_art::CheckRealFiles(build, title, xbox);
    }
    std::printf("%s (%d checks, %d failures)\n", g_failures ? "FAILED" : "OK", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

OslPadInput PadInput(std::initializer_list<int> buttons, int left = 0, int right = 0) {
    OslPadInput input{};
    for (const int button : buttons) input.buttons |= 1u << button;
    input.left_trigger = static_cast<uint8_t>(left);
    input.right_trigger = static_cast<uint8_t>(right);
    return input;
}

OslPadOutput PadOutput(const OslPadMap& map, const OslPadInput& input) {
    OslPadOutput output;
    osl_pad_translate(&map, &input, &output);
    return output;
}

constexpr uint32_t Bit(OslPadAction action) { return 1u << action; }

std::string PadFile(const std::filesystem::path& path) { return path.string(); }

void TestPadDefaults() {
    OslPadMap map;
    osl_pad_defaults(&map);
    CHECK(osl_pad_changed(&map) == 0);
    const char* const names[OSL_PAD_COUNT] = {"a", "b", "x", "y", "leftshoulder", "rightshoulder", "lefttrigger",
                                              "righttrigger", "leftstick", "rightstick", "start", "back", "dpup",
                                              "dpdown", "dpleft", "dpright"};
    for (int action = 0; action < OSL_PAD_COUNT; ++action) {
        char name[32];
        CHECK(std::string(osl_pad_source_name(osl_pad_actions[action].default_source, name, sizeof(name))) == names[action]);
        CHECK(osl_pad_source_from_name(names[action]) == osl_pad_actions[action].default_source);
        CHECK(!std::string(osl_pad_source_label(osl_pad_actions[action].default_source)).empty());
        if (action != OSL_PAD_L2 && action != OSL_PAD_R2) {
            const OslPadOutput output = PadOutput(map, PadInput({osl_pad_actions[action].default_source}));
            CHECK(output.pressed == (1u << action) && output.l2 == 0 && output.r2 == 0);
        }
    }
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_TOUCHPAD})).pressed == Bit(OSL_PAD_TOUCHPAD));
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_MISC1})).pressed == 0);

    // Triggers keep their analog value; a value up to 30 is not a press.
    OslPadOutput output = PadOutput(map, PadInput({}, 200, 31));
    CHECK(output.l2 == 200 && output.r2 == 31 && output.pressed == (Bit(OSL_PAD_L2) | Bit(OSL_PAD_R2)));
    output = PadOutput(map, PadInput({}, 30, 0));
    CHECK(output.l2 == 30 && output.pressed == 0);

    CHECK(osl_pad_source_from_name("A") == SDL_GAMEPAD_BUTTON_SOUTH);
    CHECK(osl_pad_source_from_name("LeftTrigger") == OSL_PAD_SOURCE_LEFT_TRIGGER);
    for (const char* invalid : {"", "guide", "nonsense", "lefttrigger2", "south trigger"}) {
        CHECK(osl_pad_source_from_name(invalid) == OSL_PAD_SOURCE_NONE);
    }
    CHECK(!osl_pad_source_bindable(SDL_GAMEPAD_BUTTON_GUIDE) && !osl_pad_source_bindable(OSL_PAD_SOURCE_NONE));
    CHECK(!osl_pad_source_bindable(SDL_GAMEPAD_BUTTON_COUNT));
    char name[32];
    CHECK(std::string(osl_pad_source_name(SDL_GAMEPAD_BUTTON_GUIDE, name, sizeof(name))).empty());
}

void TestPadRebinding() {
    OslPadMap map;
    osl_pad_defaults(&map);
    // Physical B drives R1 and, still, Circle; the default R1 source no longer drives R1.
    map.source[OSL_PAD_R1] = SDL_GAMEPAD_BUTTON_EAST;
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_EAST})).pressed == (Bit(OSL_PAD_R1) | Bit(OSL_PAD_CIRCLE)));
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER})).pressed == 0);

    // Swapped: physical A is Circle and physical B is Cross.
    map.source[OSL_PAD_CROSS] = SDL_GAMEPAD_BUTTON_EAST;
    map.source[OSL_PAD_CIRCLE] = SDL_GAMEPAD_BUTTON_SOUTH;
    map.source[OSL_PAD_R1] = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_SOUTH})).pressed == Bit(OSL_PAD_CIRCLE));
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_EAST})).pressed == Bit(OSL_PAD_CROSS));
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_WEST})).pressed == Bit(OSL_PAD_SQUARE));
    CHECK(osl_pad_changed(&map) == 2);

    // One physical button may drive several PS4 buttons; they are pressed together.
    map.source[OSL_PAD_TRIANGLE] = SDL_GAMEPAD_BUTTON_SOUTH;
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_SOUTH})).pressed == (Bit(OSL_PAD_CIRCLE) | Bit(OSL_PAD_TRIANGLE)));

    // Buttons on L2 and R2 give the full value, and the physical triggers stop driving them.
    osl_pad_defaults(&map);
    map.source[OSL_PAD_L2] = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    map.source[OSL_PAD_R2] = SDL_GAMEPAD_BUTTON_NORTH;
    OslPadOutput output = PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_NORTH}));
    CHECK(output.l2 == 255 && output.r2 == 255);
    CHECK(output.pressed == (Bit(OSL_PAD_L1) | Bit(OSL_PAD_L2) | Bit(OSL_PAD_R2) | Bit(OSL_PAD_TRIANGLE)));
    output = PadOutput(map, PadInput({}, 255, 255));
    CHECK(output.l2 == 0 && output.r2 == 0 && output.pressed == 0);

    // A trigger on a PS4 button, and L2 on the other trigger.
    osl_pad_defaults(&map);
    map.source[OSL_PAD_CIRCLE] = OSL_PAD_SOURCE_RIGHT_TRIGGER;
    map.source[OSL_PAD_L2] = OSL_PAD_SOURCE_RIGHT_TRIGGER;
    output = PadOutput(map, PadInput({}, 0, 90));
    CHECK(output.l2 == 90 && output.r2 == 90 && output.pressed == (Bit(OSL_PAD_CIRCLE) | Bit(OSL_PAD_L2) | Bit(OSL_PAD_R2)));
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_EAST})).pressed == 0);

    // Options, the D-pad and the touchpad take any source.
    osl_pad_defaults(&map);
    map.source[OSL_PAD_OPTIONS] = SDL_GAMEPAD_BUTTON_BACK;
    map.source[OSL_PAD_TOUCHPAD] = SDL_GAMEPAD_BUTTON_START;
    map.source[OSL_PAD_DPAD_UP] = SDL_GAMEPAD_BUTTON_NORTH;
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_BACK})).pressed == Bit(OSL_PAD_OPTIONS));
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_START})).pressed == Bit(OSL_PAD_TOUCHPAD));
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_TOUCHPAD})).pressed == 0);
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_NORTH})).pressed == (Bit(OSL_PAD_TRIANGLE) | Bit(OSL_PAD_DPAD_UP)));
    CHECK(PadOutput(map, PadInput({SDL_GAMEPAD_BUTTON_DPAD_UP})).pressed == 0);
}

void TestPadSettingsFile() {
    const std::filesystem::path dir = TempDir("pad_settings");
    OslPadMap map;
    osl_pad_load(PadFile(dir / "missing.ini").c_str(), &map);
    CHECK(osl_pad_changed(&map) == 0);

    // An osl.ini from before controller bindings: every default, and keyboard lines are not pad lines.
    std::ofstream(dir / "old.ini") << "show_launcher = 1\nkey_cross = q\ncontroller_prompts = xbox\n";
    osl_pad_load(PadFile(dir / "old.ini").c_str(), &map);
    CHECK(osl_pad_changed(&map) == 0);
    OslKeyMap keys;
    osl_keys_load(PadFile(dir / "old.ini").c_str(), &keys);
    CHECK(keys.scancode[OSL_KEY_CROSS] == SDL_SCANCODE_Q);

    std::ofstream(dir / "pad.ini") << "# comment\npad_cross = B   # swapped\npad_circle=a\npad_l1 = guide\npad_r1 = nonsense\n"
                                      "pad_l2 = RightTrigger\npad_square=\nkey_circle = e\npad_dpad_up = misc1\n";
    osl_pad_load(PadFile(dir / "pad.ini").c_str(), &map);
    CHECK(map.source[OSL_PAD_CROSS] == SDL_GAMEPAD_BUTTON_EAST && map.source[OSL_PAD_CIRCLE] == SDL_GAMEPAD_BUTTON_SOUTH);
    CHECK(map.source[OSL_PAD_L1] == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && map.source[OSL_PAD_R1] == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    CHECK(map.source[OSL_PAD_SQUARE] == SDL_GAMEPAD_BUTTON_WEST);
    CHECK(map.source[OSL_PAD_L2] == OSL_PAD_SOURCE_RIGHT_TRIGGER && map.source[OSL_PAD_DPAD_UP] == SDL_GAMEPAD_BUTTON_MISC1);
    CHECK(osl_pad_changed(&map) == 4);
    osl_keys_load(PadFile(dir / "pad.ini").c_str(), &keys);
    CHECK(keys.scancode[OSL_KEY_CIRCLE] == SDL_SCANCODE_E && keys.scancode[OSL_KEY_CROSS] == SDL_SCANCODE_SPACE);

    // OSL_PAD_MAP comes after osl.ini; bad entries are skipped and change nothing.
    CHECK(osl_pad_apply_spec(&map, "cross=a, circle = x ,bogus=a,l1=guide,r1,l3=") == 2);
    CHECK(map.source[OSL_PAD_CROSS] == SDL_GAMEPAD_BUTTON_SOUTH && map.source[OSL_PAD_CIRCLE] == SDL_GAMEPAD_BUTTON_WEST);
    CHECK(map.source[OSL_PAD_L1] == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && map.source[OSL_PAD_L2] == OSL_PAD_SOURCE_RIGHT_TRIGGER);
    CHECK(osl_pad_apply_spec(&map, nullptr) == 0 && osl_pad_apply_spec(&map, "") == 0);
    CHECK(osl_pad_apply_spec(&map, "UP=lefttrigger") == 1 && map.source[OSL_PAD_DPAD_UP] == OSL_PAD_SOURCE_LEFT_TRIGGER);
}

void TestPadSettings() {
    bool managed[OSL_PAD_COUNT] = {};
    for (const ManagedSetting& setting : ManagedSettings()) {
        for (int action = 0; action < OSL_PAD_COUNT; ++action) {
            if (setting.key == osl_pad_actions[action].ini_key) {
                char name[32];
                managed[action] = setting.default_value == osl_pad_source_name(osl_pad_actions[action].default_source, name, sizeof(name));
            }
        }
    }
    CHECK(std::all_of(std::begin(managed), std::end(managed), [](bool value) { return value; }));
    CHECK(IsValidSetting("pad_cross", "b") && IsValidSetting("pad_l2", "righttrigger") && IsValidSetting("pad_r1", "A"));
    CHECK(!IsValidSetting("pad_cross", "guide") && !IsValidSetting("pad_cross", "q") && !IsValidSetting("pad_cross", ""));

    const std::filesystem::path dir = TempDir("pad_settings_file");
    std::ofstream(dir / "osl.ini") << "# Odin\nshow_launcher = 0\nkey_cross = q\ncontroller_prompts = xbox\nextra_line = 7\n"
                                      "pad_circle = not a button\n";
    LauncherSettings settings(dir / "osl.ini");
    for (int action = 0; action < OSL_PAD_COUNT; ++action) {
        CHECK(SavedPadSource(settings, static_cast<OslPadAction>(action)) == osl_pad_actions[action].default_source);
    }

    SetPadSource(settings, OSL_PAD_R1, SDL_GAMEPAD_BUTTON_EAST);
    SetPadSource(settings, OSL_PAD_CROSS, SDL_GAMEPAD_BUTTON_GUIDE);
    SetPadSource(settings, OSL_PAD_L2, OSL_PAD_SOURCE_RIGHT_TRIGGER);
    CHECK(settings.Save());
    const std::string saved = ReadFile(dir / "osl.ini");
    CHECK(saved.find("pad_r1") != std::string::npos && saved.find("righttrigger") != std::string::npos);
    CHECK(saved.find("pad_cross") == std::string::npos && saved.find("extra_line") != std::string::npos);
    CHECK(saved.find("key_cross") != std::string::npos && saved.find("controller_prompts") != std::string::npos);

    LauncherSettings reloaded(dir / "osl.ini");
    CHECK(SavedPadSource(reloaded, OSL_PAD_R1) == SDL_GAMEPAD_BUTTON_EAST);
    CHECK(SavedPadSource(reloaded, OSL_PAD_L2) == OSL_PAD_SOURCE_RIGHT_TRIGGER);
    CHECK(SavedPadSource(reloaded, OSL_PAD_CIRCLE) == SDL_GAMEPAD_BUTTON_EAST);
    CHECK(SavedPadSource(reloaded, OSL_PAD_CROSS) == SDL_GAMEPAD_BUTTON_SOUTH);

    // Reset puts the controller bindings back and touches neither the keyboard nor the prompts.
    reloaded.Set(osl_key_actions[OSL_KEY_SQUARE].ini_key, "r");
    ResetPads(reloaded);
    for (int action = 0; action < OSL_PAD_COUNT; ++action) {
        CHECK(SavedPadSource(reloaded, static_cast<OslPadAction>(action)) == osl_pad_actions[action].default_source);
    }
    CHECK(SavedKey(reloaded, OSL_KEY_CROSS) == SDL_SCANCODE_Q && SavedKey(reloaded, OSL_KEY_SQUARE) == SDL_SCANCODE_R);
    CHECK(reloaded.Get(kControllerPromptsKey, kPromptsPlayStation) == "xbox");
    CHECK(reloaded.Save());
    OslPadMap loaded;
    osl_pad_load(PadFile(dir / "osl.ini").c_str(), &loaded);
    CHECK(osl_pad_changed(&loaded) == 0);

    // The keyboard reset and a prompt change leave the controller bindings alone.
    SetPadSource(reloaded, OSL_PAD_CIRCLE, SDL_GAMEPAD_BUTTON_NORTH);
    ResetKeys(reloaded);
    CHECK(SavedKey(reloaded, OSL_KEY_CROSS) == SDL_SCANCODE_SPACE);
    CHECK(SavedPadSource(reloaded, OSL_PAD_CIRCLE) == SDL_GAMEPAD_BUTTON_NORTH);
    CHECK(reloaded.SaveNow({{kControllerPromptsKey, kPromptsPlayStation}}));
    CHECK(SavedPadSource(reloaded, OSL_PAD_CIRCLE) == SDL_GAMEPAD_BUTTON_NORTH);

    // Reset all settings includes the controller bindings.
    reloaded.ResetToDefaults();
    CHECK(SavedPadSource(reloaded, OSL_PAD_CIRCLE) == SDL_GAMEPAD_BUTTON_EAST);
}

void TestPadCapture() {
    using Kind = PadCaptureResult::Kind;
    PadCapture none;
    CHECK(!none.Begin(OSL_PAD_CIRCLE, 0) && !none.active());

    PadCapture capture;
    CHECK(capture.Begin(OSL_PAD_CIRCLE, 1) && capture.active() && capture.action() == OSL_PAD_CIRCLE);
    // The button that opened the row is still held at first: it is not the new binding.
    const OslPadInput held = PadInput({SDL_GAMEPAD_BUTTON_SOUTH});
    CHECK(capture.Update(std::span(&held, 1), false).kind == Kind::kWaiting);
    const OslPadInput idle = PadInput({});
    CHECK(capture.Update(std::span(&idle, 1), false).kind == Kind::kWaiting);
    const OslPadInput guide = PadInput({SDL_GAMEPAD_BUTTON_GUIDE});
    CHECK(capture.Update(std::span(&guide, 1), false).kind == Kind::kWaiting);
    const OslPadInput pressed = PadInput({SDL_GAMEPAD_BUTTON_EAST});
    PadCaptureResult result = capture.Update(std::span(&pressed, 1), false);
    CHECK(result.kind == Kind::kAssigned && result.source == SDL_GAMEPAD_BUTTON_EAST && !capture.active());

    // A trigger can be captured, a second controller can press, Esc cancels and losing the controller ends it.
    CHECK(capture.Begin(OSL_PAD_L2, 2));
    const OslPadInput pair[2] = {idle, PadInput({}, 0, 255)};
    CHECK(capture.Update(std::span(&idle, 1), false).kind == Kind::kWaiting);
    result = capture.Update(std::span(pair, 2), false);
    CHECK(result.kind == Kind::kAssigned && result.source == OSL_PAD_SOURCE_RIGHT_TRIGGER);
    CHECK(capture.Begin(OSL_PAD_CROSS, 1));
    CHECK(capture.Update(std::span(&idle, 1), true).kind == Kind::kCancelled && !capture.active());
    CHECK(capture.Begin(OSL_PAD_CROSS, 1));
    CHECK(capture.Update({}, false).kind == Kind::kNoController && !capture.active());

    CHECK(osl_pad_first_pressed(&guide) == OSL_PAD_SOURCE_NONE);
    const OslPadInput two = PadInput({SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH});
    CHECK(osl_pad_first_pressed(&two) == SDL_GAMEPAD_BUTTON_SOUTH);
    const OslPadInput half = PadInput({}, 127, 128);
    CHECK(osl_pad_first_pressed(&half) == OSL_PAD_SOURCE_RIGHT_TRIGGER);
    const OslPadInput soft = PadInput({}, 127, 0);
    CHECK(osl_pad_first_pressed(&soft) == OSL_PAD_SOURCE_NONE);
}

// The runtime pad, the launcher and the tests read the same map, which has no platform code of its own.
void TestPadWiring() {
    const std::filesystem::path root(OSL_SOURCE_DIR);
    const std::string runtime = ReadFile(root / "src" / "runtime_pad.c");
    const size_t load = runtime.find("osl_pad_load(keys_file,&pad_map)");
    const size_t spec = runtime.find("osl_pad_apply_spec(&pad_map,getenv(\"OSL_PAD_MAP\"))");
    CHECK(load != std::string::npos && spec != std::string::npos && load < spec);
    CHECK(runtime.find("osl_pad_translate(&pad_map") != std::string::npos);
    CHECK(runtime.find("gamepad_buttons(g,&d->l2,&d->r2)") != std::string::npos);
    CHECK(runtime.find("slot==0 && SDL_WasInit(SDL_INIT_VIDEO)") != std::string::npos);
    const std::string pad = ReadFile(root / "src" / "osl_pad.c");
    for (const char* platform : {"_WIN32", "__linux__", "windows.h", "unistd.h"}) {
        CHECK(pad.find(platform) == std::string::npos);
    }
    CHECK(ReadFile(root / "CMakeLists.txt").find("add_library(osl_keys STATIC src/osl_keys.c src/osl_pad.c)") != std::string::npos);

    const std::string launcher = ReadFile(root / "launcher" / "launcher.cpp");
    const size_t prompts = launcher.find("ControllerPromptsCombo(ui);");
    const size_t bindings = launcher.find("ui.Section(\"Controller bindings\", true)");
    const size_t keyboard = launcher.find("ui.Section(\"Keyboard controls\", true)");
    const size_t hotkeys = launcher.find("ui.Section(\"Port hotkeys\", true)");
    CHECK(prompts != std::string::npos && prompts < bindings && bindings < keyboard && keyboard < hotkeys);
    CHECK(launcher.find("ResetPads(settings_)") != std::string::npos && launcher.find("ResetKeys(settings_)") != std::string::npos);
    const size_t row = launcher.find("void PadRow(");
    const size_t end = launcher.find("PromptStyle InstalledPrompts()");
    CHECK(row != std::string::npos && row < end);
    const std::string rows = launcher.substr(row, end - row);
    for (const char* absolute : {"SetCursorPos", "SetCursorScreenPos", "SetNextItemWidth", "kControllerPromptsKey"}) {
        CHECK(rows.find(absolute) == std::string::npos);
    }
    CHECK(rows.find("ImVec2(-FLT_MIN, 0)") != std::string::npos);
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--title-art") == 0) {
        return RunTitleArtReport(argv[2]);
    }
    if (argc >= 2 && std::strcmp(argv[1], "--frame-rate") == 0) {
        TestFrameRate();
        std::printf("frame rate: %s (%d checks, %d failures)\n", g_failures ? "FAILED" : "OK", g_checks, g_failures);
        return g_failures ? 1 : 0;
    }
    TestBranding();
    TestProfile();
    TestBuildMetadata();
    TestSfoIdentity();
    TestGameTree();
    TestBuildSelection();
    TestSettings();
    TestFrameRate();
    TestGraphicsCard();
    TestTextLanguages();
    TestVoicePolicy();
    TestKeys();
    TestPadDefaults();
    TestPadRebinding();
    TestPadSettingsFile();
    TestPadSettings();
    TestPadCapture();
    TestPadWiring();
    TestPresentation();
    TestRenderer();
    TestWindowTitle();
    TestFrameCounterAndNotices();
    TestNoRuntimeOverlay();
    TestOptionsAndInstall();
    TestHash();
    TestSettingsReaders();
    TestDetailedLogging();
    TestLogsDirectory();
    TestResolutionSites();
    TestSnapshotScale();
    TestPartialClearCoverage();
    TestRenderBufferBudget();
    TestSkipIntroSetting();
    TestSkipIntroWiring();
    TestUnlockBookshelfSetting();
    TestUnlockBookshelfWiring();
    TestBookshelfEntries();
    TestBookshelfSites();
    TestSkipIntro();
    TestLogRetention();
    TestControllerPromptsSetting();
    TestShippedPromptsPatch();
    TestControllerPrompts();
    TestCompatiblePromptVariants();
    TestUnsupportedPromptAssets();
    TestHongKongPromptProfile();
    TestSharedDirectoryArchive();
    TestSessionLogFiles();
    TestCrashReport();
    TestHangReport();
    TestPlacementVisibility();
    TestPlacementTracking();
    TestPlacementSettings();
    TestCrashReportReachesSessionLog();
    TestMapExecutableRejectsMalformedFiles();
    TestHongKongBuild();
    TestUsa101Identity();
    TestUsa101Layering();
    TestUsa101Scanned();
    TestBuildContract();
    TestScannerReproducesTables();
    TestScannedMappingFixtures();
    TestBuildMappingDiagnostics();
    TestTitleMenuExit();
    TestTitleExitAssets();
    TestIssue4TitleExitInstallLabel();
    TestAsiaBuild();
    TestAsiaScannedMapping();
    TestAsiaPrompts();
    TestAsiaTitleExit();
    TestTitleArtStructure();
    PrintParitySummary();
    std::printf("%s (%d checks, %d failures)\n", g_failures ? "FAILED" : "OK", g_checks, g_failures);
    runtime_log_flush();
    return g_failures ? 1 : 0;
}
