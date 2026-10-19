// Focused tests of the Odin Sphere profile, the param.sfo identity, the settings file and the keyboard bindings.
// Synthetic files in a temporary folder: no window, no Vulkan, no game files.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <SDL3/SDL_scancode.h>

#include "game_files.h"
#include "game_install.h"
#include "game_profile.h"
#include "options.h"
#include "settings.h"

using namespace osl;

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

void TestProfile() {
    const std::span<const GameBuild> builds = SupportedBuilds();
    CHECK(builds.size() == 1);
    const GameBuild& build = builds[0];
    CHECK(std::string(kGameTitle) == "Odin Sphere Leifthrasir");
    CHECK(std::string(build.title_id) == "CUSA05083");
    CHECK(std::string(build.content_id) == "UP0177-CUSA05083_00-ODINSPHERELE0000");
    CHECK(std::string(build.version) == "1.00");
    CHECK(std::string(build.folder) == "CUSA05083-1.00");
    CHECK(std::string(build.eboot_sha256) == "7c517194aba4e5eea55d9197efae47d1567bd785b8c0dced7d1b0f4028d3eed6");
    CHECK(build.regional_archive == nullptr);
    CHECK(build.extra_files.empty());
    CHECK(std::string(build.label) == "CUSA05083 1.00 USA");
    CHECK(std::string(build.region) == "USA");
    CHECK(build.layers.size() == 1);
    CHECK(std::string(FinishedLayer(build).app_ver) == "01.00");
    CHECK(std::string(FinishedLayer(build).sfo_version) == "01.01");
    CHECK(!FinishedLayer(build).update);
    CHECK(BaseBuildOf(build) == nullptr);
    CHECK(SupportedBuildsText() == "CUSA05083 1.00 USA");
    CHECK(BuildMetadataText(build) == "APP_VER 1.00 \xC2\xB7 VERSION 1.01");
    CHECK(std::string(kPreparedMarker) == ".osl_view.json");
    const std::vector<std::string> shared(std::begin(kSharedGameFiles), std::end(kSharedGameFiles));
    for (const char* name : {"eboot.bin", "sce_discmap.plt", "Odin2_BT_US.CPK", "Odin2_OR_US.CPK", "Odin2_RE_US.CPK",
                             "sce_module/libc.prx", "sce_module/libSceFios2.prx", "sce_sys/param.sfo"}) {
        CHECK(std::find(shared.begin(), shared.end(), name) != shared.end());
    }
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
    CHECK(InstalledGameName(GameFileState::kMissing, missing.build) == "Not installed");
    CHECK(std::string(StateName(GameFileState::kDifferentVersion)) == "different version");
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
    CHECK(settings.GetBool(kShowLauncherKey, false));
    CHECK(settings.GetInt(kWindowHeightKey, 0) == 1080);

    CHECK(IsValidSetting(kDisplayModeKey, "windowed"));
    CHECK(IsValidSetting(kDisplayModeKey, "borderless"));
    CHECK(!IsValidSetting(kDisplayModeKey, "fullscreen"));
    CHECK(IsValidSetting(kAnisotropyKey, "16"));
    CHECK(!IsValidSetting(kAnisotropyKey, "3"));
    CHECK(IsValidSetting(kPresentationKey, "stretch"));
    CHECK(!IsValidSetting(kPresentationKey, "cover"));
    CHECK(IsValidSetting(osl_key_actions[OSL_KEY_CROSS].ini_key, "Q"));
    CHECK(!IsValidSetting(osl_key_actions[OSL_KEY_CROSS].ini_key, "not a key"));
    CHECK(IsLauncherPlacementSetting(kLauncherXKey));

    settings.Set(kDisplayModeKey, "borderless");
    settings.Set(kMonitorKey, "2");
    settings.Set(kLauncherXKey, "120");
    CHECK(settings.Save());
    LauncherSettings reloaded(dir / "osl.ini");
    CHECK(reloaded.Get(kDisplayModeKey, "") == "borderless");
    CHECK(reloaded.GetInt(kMonitorKey, 1) == 2);
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
    CHECK(map.scancode[OSL_KEY_HOTKEY_MENU] != 0);
    CHECK(map.scancode[OSL_KEY_HOTKEY_WINDOW_MODE] != 0);
    CHECK(osl_keys_apply(&map, osl_key_actions[OSL_KEY_CROSS].ini_key, "Q"));
    CHECK(map.scancode[OSL_KEY_CROSS] == SDL_SCANCODE_Q);
    CHECK(!osl_keys_apply(&map, osl_key_actions[OSL_KEY_CROSS].ini_key, "escape"));
    CHECK(!osl_keys_apply(&map, "not_an_action", "Q"));
    CHECK(osl_keys_apply(&map, osl_key_actions[OSL_KEY_HOTKEY_MENU].ini_key, "f9"));
    CHECK(map.scancode[OSL_KEY_HOTKEY_MENU] == SDL_SCANCODE_F9);
    char name[32];
    CHECK(std::string(osl_key_name(map.scancode[OSL_KEY_CROSS], name, sizeof(name))) == "q");
    CHECK(!osl_key_bindable(0));
}

void TestOptionsAndInstall() {
    CHECK(WindowSizeOf(1080) == (WindowSize{1920, 1080}));
    CHECK(WindowSizeOf(720) == (WindowSize{1280, 720}));
    const std::vector<WindowSize> sizes = WindowSizes();
    CHECK(sizes.size() == std::size(kWindowHeights));
    CHECK(sizes.back() == (WindowSize{3840, 2160}));
    CHECK(WindowSizeLabel(kGamePictureSize).find("the game's own size") != std::string::npos);
    CHECK(std::string(PageName(PageId::kPlay)) == "Play");
    CHECK(std::string(PageName(PageId::kAbout)) == "About");
    CHECK(std::string(PageName(PageId::kGraphics)) == "Graphics");

    for (const InstallStatus status : {InstallStatus::kInstalled, InstallStatus::kCancelled, InstallStatus::kNotPackage,
                                       InstallStatus::kUnsupportedPackage, InstallStatus::kNotGameFolder,
                                       InstallStatus::kWrongGame, InstallStatus::kWrongVersion, InstallStatus::kIncomplete,
                                       InstallStatus::kReadFailed, InstallStatus::kNoSpace, InstallStatus::kWriteFailed,
                                       InstallStatus::kPrepareFailed}) {
        const InstallMessage message = DescribeInstall(status);
        CHECK(!message.title.empty() && !message.text.empty());
    }
    CHECK(DescribeInstall(InstallStatus::kWrongVersion).text.find("CUSA05083 1.00 USA") != std::string::npos);
}

void TestHash() {
    const std::filesystem::path dir = TempDir("hash");
    const std::filesystem::path file = dir / "abc.bin";
    std::ofstream(file, std::ios::binary) << "abc";
    CHECK(Sha256Hex(file) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256Hex(dir / "missing.bin").empty());
}

} // namespace

int main() {
    TestProfile();
    TestSfoIdentity();
    TestGameTree();
    TestSettings();
    TestKeys();
    TestOptionsAndInstall();
    TestHash();
    std::printf("%s (%d checks, %d failures)\n", g_failures ? "FAILED" : "OK", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
