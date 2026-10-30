// Focused tests of the Odin Sphere profile, the param.sfo identity, the settings file and the keyboard bindings.
// Synthetic files in a temporary folder: no window, no Vulkan, no game files.
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <SDL3/SDL_scancode.h>

#include "controller_prompts.h"
#include "cpk_patch.h"
#include "crash_report.h"
#include "game_files.h"
#include "game_install.h"
#include "game_profile.h"
#include "hang_report.h"
#include "launcher_quit.h"
#include "options.h"
#include "osl_accent.h"
#include "osl_graphics.h"
#include "osl_launcher.h"
#include "osl_title.h"
#include "placement.h"
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
    CHECK(build.resolution_sites.size() == 17);
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
        {"CUSA01290-1.00", "CUSA01290 1.00 Japan", "APP_VER 1.00 \xC2\xB7 VERSION 1.00", nullptr},
        {"CUSA01290-1.02", "CUSA01290 1.02 Japan", "APP_VER 1.02 \xC2\xB7 VERSION 1.00", "CUSA01290-1.00"},
        {"CUSA05262-1.00", "CUSA05262 1.00 Europe", "APP_VER 1.00 \xC2\xB7 VERSION 1.00", nullptr},
        {"CUSA05262-1.01", "CUSA05262 1.01 Europe", "APP_VER 1.01 \xC2\xB7 VERSION 1.00", "CUSA05262-1.00"},
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
    CHECK(!RestrictedToNativeResolution(nullptr));
    for (const GameBuild& build : SupportedBuilds()) {
        TestBuildResolutionSites(build);
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
    for (const GameBuild& build : SupportedBuilds()) {
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
    const GameBuild& build = SupportedBuilds().back();
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
    CHECK(osl_game_build(dir.string().c_str(), &loaded) && loaded.skip_intro == build.skip_intro);

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
    for (const GameBuild& build : SupportedBuilds()) {
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

namespace cpk_test {

namespace fs = std::filesystem;

void Be16(std::string& o, uint32_t v) { o += char(v >> 8); o += char(v); }
void Be32(std::string& o, uint32_t v) { for (int i = 3; i >= 0; --i) o += char(v >> (8 * i)); }
void Be64(std::string& o, uint64_t v) { for (int i = 7; i >= 0; --i) o += char(v >> (8 * i)); }
void Le32(std::string& o, uint32_t v) { for (int i = 0; i < 4; ++i) o += char(v >> (8 * i)); }
void Le64(std::string& o, uint64_t v) { for (int i = 0; i < 8; ++i) o += char(v >> (8 * i)); }

struct Column {
    const char* name;
    int type;    // @UTF type: 2 u16, 4 u32, 6 u64, 0xA string
    int storage; // 0x50 per row, 0x30 constant
};
struct Cell {
    uint64_t number;
    std::string text;
};
using Row = std::vector<std::pair<std::string, Cell>>;

Cell N(uint64_t n) { return {n, {}}; }
Cell S(std::string s) { return {0, std::move(s)}; }

// A @UTF table packet, as scripts/prompts/cpk.py's self-test builds it.
std::string Utf(const std::string& name, const std::vector<Column>& columns, const std::vector<Row>& rows) {
    std::string strings("<NULL>", 7);
    const auto intern = [&](const std::string& text) {
        const uint32_t off = uint32_t(strings.size());
        strings += text;
        strings += '\0';
        return off;
    };
    const auto cell = [](const Row& row, const char* key) {
        for (const auto& [k, v] : row) {
            if (k == key) return v;
        }
        return Cell{};
    };
    const uint32_t name_off = intern(name);
    std::string cols;
    for (const Column& c : columns) {
        cols += char(c.storage | c.type);
        Be32(cols, intern(c.name));
        if (c.storage == 0x30) {
            const Cell v = cell(rows.at(0), c.name);
            if (c.type == 0xA) Be32(cols, intern(v.text));
            else if (c.type == 2) Be16(cols, uint32_t(v.number));
            else if (c.type == 4) Be32(cols, uint32_t(v.number));
            else Be64(cols, v.number);
        }
    }
    std::string rowbuf;
    uint32_t row_len = 0;
    for (const Row& row : rows) {
        const size_t start = rowbuf.size();
        for (const Column& c : columns) {
            if (c.storage != 0x50) continue;
            const Cell v = cell(row, c.name);
            if (c.type == 0xA) Be32(rowbuf, intern(v.text));
            else if (c.type == 2) Be16(rowbuf, uint32_t(v.number));
            else if (c.type == 4) Be32(rowbuf, uint32_t(v.number));
            else Be64(rowbuf, v.number);
        }
        row_len = uint32_t(rowbuf.size() - start);
    }
    const uint32_t rows_off = 32 + uint32_t(cols.size()) - 8;
    const uint32_t strings_off = rows_off + uint32_t(rowbuf.size());
    std::string packet("@UTF");
    Be32(packet, 0);
    Be32(packet, rows_off);
    Be32(packet, strings_off);
    Be32(packet, strings_off + uint32_t(strings.size()));
    Be32(packet, name_off);
    Be16(packet, uint32_t(columns.size()));
    Be16(packet, row_len);
    Be32(packet, uint32_t(rows.size()));
    packet += cols + rowbuf + strings;
    const uint32_t size = uint32_t(packet.size() - 8);
    for (int i = 0; i < 4; ++i) packet[4 + i] = char(size >> (8 * (3 - i)));
    return packet;
}

// CRI's XOR, its own inverse.
std::string Crypt(std::string packet) {
    uint32_t m = 0x5F;
    for (char& b : packet) {
        b = char(uint8_t(b) ^ uint8_t(m));
        m = (m * 0x15) & 0xFF;
    }
    return packet;
}

std::string Section(const char* magic, const std::string& packet) {
    std::string out(magic, 4);
    Le32(out, 0xFF);
    Le64(out, packet.size());
    return out + packet;
}

// A CRILAYLA block storing `payload` with literal bytes only: the stream is read backwards and writes the output
// backwards, and the file's first 0x100 bytes are stored raw after it.
std::string CrilaylaLiterals(const std::string& payload) {
    std::string out;
    uint64_t acc = 0;
    int bits = 0;
    for (size_t i = payload.size(); i > 0x100; --i) {
        acc = (acc << 9) | uint8_t(payload[i - 1]);
        bits += 9;
        while (bits >= 8) {
            bits -= 8;
            out += char(acc >> bits);
        }
        acc &= (1u << bits) - 1;
    }
    if (bits) out += char(acc << (8 - bits));
    std::string block("CRILAYLA");
    Le32(block, uint32_t(payload.size() - 0x100));
    Le32(block, uint32_t(out.size()));
    block.append(out.rbegin(), out.rend());
    return block + payload.substr(0, 0x100);
}

struct Entry {
    std::string path;
    std::string data;
    bool compressed;
};

// A CPK with an encrypted header (EnableTocCrc on), an optionally encrypted TOC and 2 KB alignment, as the game's.
// With `shared_dir`, DirName and UserString are stored once for the table, as the 1.01 ES, FR and IT Refined archives
// do; every entry must then be in the same folder.
std::string BuildCpk(const std::vector<Entry>& entries, bool encrypt_toc, bool shared_dir = false) {
    const uint64_t toc_offset = 0x800, content_offset = 0x3000, align = 0x800;
    std::string content;
    std::vector<Row> rows;
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string stored = entries[i].compressed ? CrilaylaLiterals(entries[i].data) : entries[i].data;
        while ((content_offset + content.size()) % align) content += '\0';
        const size_t slash = entries[i].path.rfind('/');
        rows.push_back({{"DirName", S(slash == std::string::npos ? "" : entries[i].path.substr(0, slash))},
                        {"FileName", S(entries[i].path.substr(slash == std::string::npos ? 0 : slash + 1))},
                        {"FileSize", N(stored.size())},
                        {"ExtractSize", N(entries[i].data.size())},
                        {"FileOffset", N(content_offset + content.size() - toc_offset)},
                        {"ID", N(i)},
                        {"UserString", S("<NULL>")}});
        content += stored;
    }
    const int text_storage = shared_dir ? 0x30 : 0x50;
    std::string toc = Utf("CpkTocInfo",
                          {{"DirName", 0xA, text_storage}, {"FileName", 0xA, 0x50}, {"FileSize", 4, 0x50},
                           {"ExtractSize", 4, 0x50}, {"FileOffset", 6, 0x50}, {"ID", 4, 0x50},
                           {"UserString", 0xA, text_storage}},
                          rows);
    if (encrypt_toc) toc = Crypt(toc);
    const std::string header = Utf("CpkHeader",
                                   {{"TocOffset", 6, 0x50}, {"ContentOffset", 6, 0x50}, {"Files", 4, 0x50}, {"Align", 2, 0x50},
                                    {"EnableTocCrc", 2, 0x50}, {"EnableFileCrc", 2, 0x50}, {"Version", 2, 0x30}},
                                   {{{"TocOffset", N(toc_offset)}, {"ContentOffset", N(content_offset)}, {"Files", N(entries.size())},
                                     {"Align", N(align)}, {"EnableTocCrc", N(1)}, {"EnableFileCrc", N(0)}, {"Version", N(7)}}});
    std::string blob = Section("CPK ", Crypt(header));
    blob.resize(toc_offset, '\0');
    blob += Section("TOC ", toc);
    blob.resize(content_offset, '\0');
    return blob + content;
}

std::span<const uint8_t> Span(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }
std::string Bytes(const std::vector<uint8_t>& v) { return std::string(v.begin(), v.end()); }

// A DCPX patch turning each `before` into its `after` (same size), as scripts/prompts/make_dcpx.py writes it.
std::string MakePatch(const std::vector<std::pair<std::string, std::pair<std::string, std::string>>>& targets) {
    std::string out("DCPX1\0\0\0", 8);
    Le32(out, uint32_t(targets.size()));
    for (const auto& [name, versions] : targets) {
        const auto& [before, after] = versions;
        std::vector<size_t> runs;
        for (size_t i = 0; i + 16 <= before.size(); i += 16) {
            if (before.compare(i, 16, after, i, 16) != 0) runs.push_back(i);
        }
        out += char(name.size() & 255);
        out += char(name.size() >> 8);
        out += name;
        Le32(out, uint32_t(before.size()));
        Le32(out, Crc32(Span(before)));
        Le32(out, Crc32(Span(after)));
        Le32(out, uint32_t(runs.size()));
        for (const size_t off : runs) {
            Le32(out, uint32_t(off));
            out += after.substr(off, 16);
        }
    }
    return out;
}

void WriteAll(const fs::path& path, const std::string& data) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << data;
}

std::string Pattern(size_t size, unsigned seed) {
    std::string out(size, '\0');
    for (size_t i = 0; i < size; ++i) out[i] = char((i * seed + i / 64 + seed) & 255);
    return out;
}

std::string Painted(std::string data, std::initializer_list<size_t> offsets, char ink) {
    for (const size_t off : offsets) data.replace(off, 16, 16, ink);
    return data;
}

} // namespace cpk_test

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

} // namespace

int main() {
    TestBranding();
    TestProfile();
    TestBuildMetadata();
    TestSfoIdentity();
    TestGameTree();
    TestBuildSelection();
    TestSettings();
    TestGraphicsCard();
    TestTextLanguages();
    TestVoicePolicy();
    TestKeys();
    TestPresentation();
    TestWindowTitle();
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
    TestSkipIntro();
    TestLogRetention();
    TestControllerPromptsSetting();
    TestShippedPromptsPatch();
    TestControllerPrompts();
    TestSharedDirectoryArchive();
    TestSessionLogFiles();
    TestCrashReport();
    TestHangReport();
    TestPlacementVisibility();
    TestPlacementTracking();
    TestPlacementSettings();
    TestCrashReportReachesSessionLog();
    std::printf("%s (%d checks, %d failures)\n", g_failures ? "FAILED" : "OK", g_checks, g_failures);
    runtime_log_flush();
    return g_failures ? 1 : 0;
}
