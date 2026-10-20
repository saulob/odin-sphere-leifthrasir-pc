// Focused tests of the Odin Sphere profile, the param.sfo identity, the settings file and the keyboard bindings.
// Synthetic files in a temporary folder: no window, no Vulkan, no game files.
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <SDL3/SDL_scancode.h>

#include "game_files.h"
#include "game_install.h"
#include "game_profile.h"
#include "launcher_quit.h"
#include "options.h"
#include "osl_accent.h"
#include "osl_launcher.h"
#include "runtime_log.h"
#include "settings.h"
#include "utf8.h"

using namespace osl;

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

std::filesystem::path TempDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "osl-tests" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
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

void WriteSfo(const std::filesystem::path& path, const std::vector<std::pair<std::string, std::string>>& values) {
    std::vector<char> keys, data;
    std::vector<std::array<uint32_t, 4>> entries; // key_offset|format packed, length, capacity, data_offset
    for (const auto& [key, value] : values) {
        entries.push_back({static_cast<uint32_t>(keys.size()) | (0x0204u << 16),
                           static_cast<uint32_t>(value.size() + 1), static_cast<uint32_t>(value.size() + 1),
                           static_cast<uint32_t>(data.size())});
        keys.insert(keys.end(), key.begin(), key.end());
        keys.push_back('\0');
        data.insert(data.end(), value.begin(), value.end());
        data.push_back('\0');
    }
    const uint32_t header = 20 + static_cast<uint32_t>(entries.size()) * 16;
    const uint32_t key_table = header;
    const uint32_t data_table = key_table + static_cast<uint32_t>(keys.size()) + 4;
    std::vector<char> out(data_table + data.size() + 4, '\0');
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(out.data() + at, &v, 4); };
    std::memcpy(out.data(), "\0PSF", 4);
    put32(4, 0x00000101);
    put32(8, key_table);
    put32(12, data_table);
    put32(16, static_cast<uint32_t>(entries.size()));
    for (size_t i = 0; i < entries.size(); ++i) {
        put32(20 + i * 16, entries[i][0]);        // key_offset (u16) and format 0x0204 (u16)
        put32(20 + i * 16 + 4, entries[i][1]);
        put32(20 + i * 16 + 8, entries[i][2]);
        put32(20 + i * 16 + 12, entries[i][3]);
    }
    std::memcpy(out.data() + key_table, keys.data(), keys.size());
    std::memcpy(out.data() + data_table, data.data(), data.size());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(out.data(), static_cast<std::streamsize>(out.size()));
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
        CHECK(ico.compare(offset, 8, "\x89PNG\r\n\x1a\n") == 0);
        CHECK(ico.compare(offset + 12, 4, "IHDR") == 0);
        CHECK(Big32(ico, offset + 16) == static_cast<uint32_t>(sizes[i]));
        CHECK(Big32(ico, offset + 20) == static_cast<uint32_t>(sizes[i]));
    }
}

void TestProfile() {
    const std::span<const GameBuild> builds = SupportedBuilds();
    CHECK(builds.size() == 5);
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
    CHECK(build.resolution_sites.size() == 15);
    CHECK(std::string(FinishedLayer(build).app_ver) == "01.00");
    CHECK(std::string(FinishedLayer(build).sfo_version) == "01.01");
    CHECK(!FinishedLayer(build).update);
    CHECK(BaseBuildOf(build) == nullptr);
    CHECK(UpdateBuildOf(build) == nullptr);
    CHECK(BuildMetadataText(build) == "APP_VER 1.00 \xC2\xB7 VERSION 1.01");

    const GameBuild& japan_base = builds[1];
    const GameBuild& japan = builds[2];
    CHECK(std::string(japan_base.title_id) == "CUSA01290");
    CHECK(std::string(japan_base.content_id) == "JP0005-CUSA01290_00-ODINSPHERELE0000");
    CHECK(std::string(japan_base.version) == "1.00");
    CHECK(std::string(japan_base.folder) == "CUSA01290-1.00");
    CHECK(std::string(japan_base.eboot_sha256) ==
          "d0add7e35bc5f58d5c9017fdce0878006c32f944fa8eb3d7ce62671257f5bcc7");
    CHECK(std::string(japan_base.label) == "CUSA01290 1.00 Japan");
    CHECK(std::string(japan_base.region) == "Japan");
    CHECK(japan_base.layers.size() == 1);
    CHECK(japan_base.resolution_sites.size() == 15);
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
    CHECK(japan.resolution_sites.size() == 15);
    CHECK(japan.data_files.size() == 6);
    CHECK(japan.extra_files.size() == 1);
    CHECK(BuildMetadataText(japan) == "APP_VER 1.02 \xC2\xB7 VERSION 1.00");

    const GameBuild& eu_base = builds[3];
    const GameBuild& eu = builds[4];
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
    CHECK(eu_base.resolution_sites.size() == 15);
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
    CHECK(eu.resolution_sites.size() == 15);
    CHECK(eu.data_files.size() == 25);
    CHECK(eu.extra_files.size() == 1);
    CHECK(BuildMetadataText(eu) == "APP_VER 1.01 \xC2\xB7 VERSION 1.00");

    CHECK(SupportedBuildsText() ==
          "CUSA05083 1.00 USA, CUSA01290 1.00 Japan, CUSA01290 1.02 Japan, CUSA05262 1.00 Europe and CUSA05262 1.01 "
          "Europe");
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
    CHECK(CheckSfoIdentity(japan_base).build == &SupportedBuilds()[1]);
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
    CHECK(CheckSfoIdentity(update).build == &SupportedBuilds()[2]);
    CHECK(CheckPackageIdentity(update).state == GameFileState::kValid);
    CHECK(CheckPackageIdentity(update).build == &SupportedBuilds()[2]);
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
    CHECK(CheckSfoIdentity(eu_base).build == &SupportedBuilds()[3]);
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
    CHECK(CheckSfoIdentity(eu_patch).build == &SupportedBuilds()[4]);
    CHECK(CheckPackageIdentity(eu_patch).state == GameFileState::kValid);
    CHECK(CheckPackageIdentity(eu_patch).build == &SupportedBuilds()[4]);
    CHECK(CheckPackageIdentity(eu_patch).layer == 1);
    CHECK(BaseBuildOf(SupportedBuilds()[4]) == &SupportedBuilds()[3]);
    CHECK(BaseBuildOf(SupportedBuilds()[3]) == nullptr);
    CHECK(UpdateBuildOf(SupportedBuilds()[3]) == &SupportedBuilds()[4]);

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
    CHECK(InstalledGameName(GameFileState::kValid, &SupportedBuilds()[2]) == "PS4 CUSA01290 1.02 Japan");
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
                                         std::filesystem::file_time_type(std::chrono::seconds(seconds)));
    };
    write_marker(builds[0], 10);
    write_marker(builds[1], 20);
    write_marker(builds[2], 30);
    CHECK(LastPreparedBuild(roots) == &builds[2]);
    CHECK(ActiveBuildPaths(roots).app0 == BuildPaths(roots, builds[2]).app0);

    write_marker(builds[1], 40);
    CHECK(LastPreparedBuild(roots) == &builds[1]);
    CHECK(UpdateBuildOf(*LastPreparedBuild(roots)) == &builds[2]);
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
    CHECK(settings.Get(kGraphicsCardKey, "") == "automatic");
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
                                       InstallStatus::kWrongGame, InstallStatus::kWrongVersion, InstallStatus::kIncomplete,
                                       InstallStatus::kReadFailed, InstallStatus::kNoSpace, InstallStatus::kWriteFailed,
                                       InstallStatus::kPrepareFailed}) {
        const InstallMessage message = DescribeInstall(status);
        CHECK(!message.title.empty() && !message.text.empty());
    }
    CHECK(DescribeInstall(InstallStatus::kWrongVersion).text.find("CUSA05083 1.00 USA") != std::string::npos);
    CHECK(DescribeInstall(InstallStatus::kWrongVersion).text.find("CUSA01290 1.02 Japan") != std::string::npos);
    CHECK(DescribeInstall(InstallStatus::kWrongGame).text.find("CUSA01290 1.02 Japan") != std::string::npos);
}

void TestTextLanguages() {
    const std::span<const GameBuild> builds = SupportedBuilds();
    const GameBuild& eu = builds[4];
    const GameBuild& usa = builds[0];
    const GameBuild& japan = builds[2];

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
    CHECK(TextLanguagesOf(&builds[3]).size() == 5);
    CHECK(TextLanguagesOf(&usa).empty());
    CHECK(TextLanguagesOf(&japan).empty());
    CHECK(TextLanguagesOf(&builds[1]).empty());
    CHECK(TextLanguagesOf(nullptr).empty());

    CHECK(ResolveTextLanguage(&eu, "french").ps4_id == 2);
    CHECK(ResolveTextLanguage(&eu, "italian").ps4_id == 5);
    CHECK(ResolveTextLanguage(&eu, "bogus").ps4_id == 1);
    CHECK(std::string(ResolveTextLanguage(&eu, "").key) == "english");
    CHECK(ResolveTextLanguage(&usa, "french").ps4_id == 1);
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
    write_image(japan);
    CHECK(osl_text_language(ini.string().c_str(), dir.string().c_str()) == 1);
    write_image(eu);
    CHECK(osl_text_language(ini.string().c_str(), dir.string().c_str()) == 2);
    CHECK(osl_text_language(nullptr, dir.string().c_str()) == 1);
    CHECK(osl_text_language(ini.string().c_str(), nullptr) == 1);

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
    for (const GameBuild& eu : {builds[3], builds[4]}) {
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
}

void TestBuildResolutionSites(const GameBuild& build) {
    CHECK(!build.resolution_sites.empty());
    CHECK(build.resolution_sites.size() == 15);
    CHECK(build.resolution_sites.back().packed_floats);
    CHECK(!RestrictedToNativeResolution(&build));

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
    CHECK(!RestrictedToNativeResolution(nullptr));
    for (const GameBuild& build : SupportedBuilds()) {
        TestBuildResolutionSites(build);
    }
}

} // namespace

int main() {
    TestBranding();
    TestProfile();
    TestSfoIdentity();
    TestGameTree();
    TestBuildSelection();
    TestSettings();
    TestTextLanguages();
    TestVoicePolicy();
    TestKeys();
    TestNoRuntimeOverlay();
    TestOptionsAndInstall();
    TestHash();
    TestSettingsReaders();
    TestDetailedLogging();
    TestLogsDirectory();
    TestResolutionSites();
    TestLogRetention();
    std::printf("%s (%d checks, %d failures)\n", g_failures ? "FAILED" : "OK", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
