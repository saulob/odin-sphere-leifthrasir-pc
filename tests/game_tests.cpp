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
#include <string>
#include <tuple>
#include <vector>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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
    CHECK(builds.size() == 7);
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
          "CUSA05262 1.01 Europe and CUSA03873 1.00 HK");
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

// A DCPX patch turning each `before` into its `after` (same size), as scripts/prompts/make_dcpx.py writes it; with
// `source_blocks` false, a DCPX1 patch whose runs hold only the replacement.
std::string MakePatch(const std::vector<std::pair<std::string, std::pair<std::string, std::string>>>& targets,
                      bool source_blocks = true) {
    std::string out(source_blocks ? "DCPX2\0\0\0" : "DCPX1\0\0\0", 8);
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
            if (source_blocks) out += before.substr(off, 16);
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
                    CHECK(Crc32(original) == match.target->crc_before);
                }
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

std::vector<int16_t> Shorts(const short* bytes, size_t count) {
    return std::vector<int16_t>(bytes, bytes + count);
}

void PlantPattern(MappedExecutable& image, size_t at, const std::vector<int16_t>& pattern) {
    for (size_t i = 0; i < pattern.size(); ++i) {
        image.image[at + i] = pattern[i] < 0 ? 0 : static_cast<uint8_t>(pattern[i]);
    }
}

void PlantRelative(MappedExecutable& image, size_t at, size_t target, size_t instruction_end) {
    const int32_t relative = static_cast<int32_t>(static_cast<int64_t>(target) - static_cast<int64_t>(instruction_end));
    std::memcpy(&image.image[at], &relative, 4);
}

void PlantFloat(MappedExecutable& image, size_t at, float value) {
    std::memcpy(&image.image[at], &value, 4);
}

constexpr size_t kPlantedNotice = 0x100, kPlantedHd = 0x200, kPlantedClassic = 0x300, kPlantedSnapshot = 0x400;
constexpr size_t kPlantedGetter = 0x2000, kPlantedTotal = 0x2100, kPlantedReads = 0x2200, kPlantedMarks = 0x2600;
constexpr size_t kPlantedLzcnt = 0x3000, kPlantedData = 0x31000, kPlantedDecoy = 0x20000;

struct PlantedLayout {
    size_t notice = 0, hd = 0, classic = 0, classic_target = 0, snapshot = 0, data = 0, getter = 0, total = 0, decoy = 0;
    std::vector<size_t> reads, marks, lzcnt;
    bool in_place = false;
    std::vector<OslResolutionSite> resolution; // at the offsets they are planted at
};

void PlantFeatures(MappedExecutable& image, const PlantedLayout& l) {
    PlantPattern(image, l.notice, Shorts(osl_skip_intro_notice_code.bytes, osl_skip_intro_notice_code.count));
    PlantPattern(image, l.hd, Shorts(osl_skip_intro_hd_title_code.bytes, osl_skip_intro_hd_title_code.count));
    PlantPattern(image, l.classic,
                 Shorts(osl_skip_intro_classic_title_code.bytes, osl_skip_intro_classic_title_code.count));
    const size_t call = l.classic + osl_skip_intro_classic_title_code.site;
    PlantRelative(image, call + 1, l.classic_target, call + 5);

    PlantPattern(image, l.snapshot, Shorts(osl_snapshot_window, OSL_SNAPSHOT_WINDOW));
    PlantRelative(image, l.snapshot + OSL_SNAPSHOT_FACTOR, l.data, l.snapshot + OSL_SNAPSHOT_FACTOR + 4);
    PlantRelative(image, l.snapshot + OSL_SNAPSHOT_PAIR, l.data + 8, l.snapshot + OSL_SNAPSHOT_PAIR + 4);
    PlantFloat(image, l.data, 1.5f);
    PlantFloat(image, l.data + 8, 1280.0f);
    PlantFloat(image, l.data + 12, 720.0f);

    for (const OslResolutionSite& site : l.resolution) {
        std::memcpy(&image.image[site.offset], site.opcode, site.opcode_length);
        std::memcpy(&image.image[site.offset + site.opcode_length], &site.value, 4);
        if (site.packed_floats) {
            std::memcpy(&image.image[site.offset + site.opcode_length + 4], &site.value_high, 4);
        }
    }

    PlantPattern(image, l.getter, Shorts(osl_bookshelf_accessor_shape, OSL_BOOKSHELF_ACCESSOR_BYTES));
    std::memcpy(&image.image[l.total], osl_bookshelf_total_code, OSL_BOOKSHELF_TOTAL_BYTES);
    const auto plant_call = [&](size_t at, const unsigned char* signature) {
        image.image[at] = 0xe8;
        PlantRelative(image, at + 1, l.getter, at + 5);
        std::memcpy(&image.image[at + 5], signature, OSL_BOOKSHELF_SIGNATURE_BYTES);
    };
    for (int i = 0; i < OSL_BOOKSHELF_READS; ++i) {
        plant_call(l.reads[i], l.in_place && i == 7 ? osl_bookshelf_in_place_page_signature
                                                    : osl_bookshelf_read_signatures[i]);
    }
    for (int i = 0; i < OSL_BOOKSHELF_MARKS; ++i) {
        plant_call(l.marks[i], osl_bookshelf_mark_signatures[i]);
    }
    if (l.decoy) {
        plant_call(l.decoy, osl_bookshelf_read_signatures[7]);
    }

    for (size_t i = 0; i < l.lzcnt.size(); ++i) {
        std::memcpy(&image.image[l.lzcnt[i]], lzcnt_shapes[i].bytes, lzcnt_shapes[i].length);
    }
}

PlantedLayout SyntheticLayout(size_t skew) {
    PlantedLayout l;
    l.notice = kPlantedNotice + skew;
    l.hd = kPlantedHd + skew;
    l.classic = kPlantedClassic + skew;
    l.classic_target = 0x1000;
    l.snapshot = kPlantedSnapshot + skew;
    l.data = kPlantedData + skew;
    l.getter = kPlantedGetter + skew;
    l.total = kPlantedTotal + skew;
    l.decoy = kPlantedDecoy + skew;
    for (int i = 0; i < OSL_BOOKSHELF_READS; ++i) {
        l.reads.push_back(kPlantedReads + skew + i * 0x20);
    }
    for (int i = 0; i < OSL_BOOKSHELF_MARKS; ++i) {
        l.marks.push_back(kPlantedMarks + skew + i * 0x20);
    }
    for (int i = 0; i < LZCNT_RANGE_COUNT; ++i) {
        l.lzcnt.push_back(kPlantedLzcnt + skew + i * 0x20);
    }
    size_t cursor = 0x1100 + skew;
    uint32_t previous_end = 0;
    for (OslResolutionSite site : SupportedBuilds()[0].resolution_sites) {
        if (site.offset != previous_end) {
            cursor += 0x40;
        }
        const uint32_t template_offset = site.offset;
        site.offset = static_cast<uint32_t>(cursor);
        const size_t length = site.opcode_length + (site.packed_floats ? 8u : 4u);
        l.resolution.push_back(site);
        cursor += length;
        previous_end = template_offset + static_cast<uint32_t>(length);
    }
    return l;
}

// An image holding every pattern the scanner looks for exactly once, in the shapes the production patches expect. A
// skew moves the whole layout, as another build's code would sit elsewhere.
MappedExecutable SyntheticOdinImage(size_t skew = 0) {
    MappedExecutable image;
    image.image.assign(0x40000, 0);
    image.segments = {{0, 0x2f800, 5}, {0x30000, 0x8000, 4}};
    image.entry = 0x1234;
    PlantFeatures(image, SyntheticLayout(skew));
    return image;
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

bool Contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

std::string HexText(uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
}

bool Unresolved(const ScanResult& scan, const std::string& what) {
    return std::find(scan.unresolved.begin(), scan.unresolved.end(), what) != scan.unresolved.end();
}

std::string ShapeInstructionLine(size_t shape, size_t instruction) {
    const LzcntShape& planted = lzcnt_shapes[shape];
    const size_t at = instruction ? planted.second : 0;
    LzcntOperands operands;
    CHECK(lzcnt_compat_decode(planted.bytes + at, planted.length - at, &operands));
    std::string raw;
    for (size_t i = 0; i < operands.length; ++i) {
        char byte[4];
        std::snprintf(byte, sizeof(byte), "%02x", planted.bytes[at + i]);
        raw += (i ? " " : "") + std::string(byte);
    }
    return HexText(kPlantedLzcnt + shape * 0x20 + at) + ": " + raw;
}

void TestBuildDiagnosticsScanner() {
    const MappedExecutable image = SyntheticOdinImage();
    const ScanResult scan = ScanExecutable(image, image.image.size());
    CHECK(scan.complete());
    CHECK(scan.anything_found);
    CHECK(Contains(scan.text, "notice scene: unique " + HexText(kPlantedNotice + 19)));
    CHECK(Contains(scan.text, "hd title check: unique " + HexText(kPlantedHd + 7)));
    CHECK(Contains(scan.text, "classic title check: unique " + HexText(kPlantedClassic + 44)));
    CHECK(Contains(scan.text, "classic button: inside the code 0x1000"));
    CHECK(Contains(scan.text, "stub space: free 0x2f800"));
    CHECK(Contains(scan.text, "getter: unique " + HexText(kPlantedGetter)));
    CHECK(Contains(scan.text, "special test: unique " + HexText(kPlantedTotal)));
    CHECK(Contains(scan.text, "reads: 14/14"));
    CHECK(Contains(scan.text, "marks: 2/2 " + HexText(kPlantedMarks) + " " + HexText(kPlantedMarks + 0x20)));
    CHECK(Contains(scan.text, "read order: consistent"));
    CHECK(Contains(scan.text, "site: unique " + HexText(kPlantedSnapshot)));
    CHECK(Contains(scan.text, "the expected 1.5"));
    const std::string site_count = std::to_string(SupportedBuilds()[0].resolution_sites.size());
    CHECK(Contains(scan.text, "sites: " + site_count + "/" + site_count));
    CHECK(Contains(scan.text, "ranges: unique"));
    CHECK(Contains(scan.text, "unexpected instructions: 0"));
    CHECK(Contains(scan.text, "rewrite check on a scratch copy: ok"));
    size_t complete = 0;
    for (size_t at = scan.text.find("status: complete"); at != std::string::npos; at = scan.text.find("status: complete", at + 1)) {
        ++complete;
    }
    CHECK(complete == 5);

    MappedExecutable duplicated = image;
    PlantPattern(duplicated, 0x10000, Shorts(osl_skip_intro_notice_code.bytes, osl_skip_intro_notice_code.count));
    const ScanResult ambiguous = ScanExecutable(duplicated, duplicated.image.size());
    CHECK(!ambiguous.complete());
    CHECK(Contains(ambiguous.text, "notice scene: ambiguous (2 matches, expected 1)"));
    CHECK(Unresolved(ambiguous, "Skip intro notice scene"));

    MappedExecutable no_mark = image;
    std::fill_n(no_mark.image.begin() + kPlantedMarks + 0x20, 16, uint8_t{0x90});
    const ScanResult missing = ScanExecutable(no_mark, no_mark.image.size());
    CHECK(!missing.complete());
    CHECK(Unresolved(missing, "Bookshelf mark 1"));

    MappedExecutable no_getter = image;
    std::fill_n(no_getter.image.begin() + kPlantedGetter, 22, uint8_t{0x90});
    const ScanResult no_reads = ScanExecutable(no_getter, no_getter.image.size());
    CHECK(Contains(no_reads.text, "reads: not attempted"));
    CHECK(!no_reads.complete());

    MappedExecutable extra = image;
    const unsigned char unknown_lzcnt[] = {0xf3, 0x4c, 0x0f, 0xbd, 0xc1};
    std::memcpy(&extra.image[0x20100], unknown_lzcnt, sizeof(unknown_lzcnt));
    const ScanResult unexpected = ScanExecutable(extra, extra.image.size());
    CHECK(Contains(unexpected.text, "unexpected instructions: 1"));
    CHECK(Unresolved(unexpected, "LZCNT instructions outside the shapes"));

    MappedExecutable wrong_scale = image;
    PlantFloat(wrong_scale, kPlantedData, 2.0f);
    CHECK(!ScanExecutable(wrong_scale, wrong_scale.image.size()).complete());

    constexpr size_t kFirstCluster = 15;
    MappedExecutable eboot_only = image;
    for (size_t i = kFirstCluster; i < LZCNT_RANGE_COUNT; ++i) {
        std::fill_n(eboot_only.image.begin() + kPlantedLzcnt + i * 0x20, lzcnt_shapes[i].length, uint8_t{0x90});
    }
    const unsigned char stray[] = {0xf3, 0x4c, 0x0f, 0xbd, 0xc1};
    std::memcpy(&eboot_only.image[0x20100], stray, sizeof(stray));
    const ScanResult partial = ScanExecutable(eboot_only, eboot_only.image.size());
    CHECK(!partial.complete() && Unresolved(partial, "LZCNT ranges"));
    CHECK(Contains(partial.text, "instructions found: 16 (the shapes hold 50)"));
    CHECK(Contains(partial.text, "ranges: shape 15 has no candidate in order"));
    CHECK(Contains(partial.text, "ordered placement: 15 of 49 shapes placed, stopped at shape 15 expecting f3 48 0f bd df"));
    CHECK(Contains(partial.text, "bundled modules: not in the scanned image"));
    CHECK(Contains(partial.text, "detected instructions outside the placed shapes: 1 of 16"));
    for (size_t i = 0; i < kFirstCluster; ++i) {
        CHECK(Contains(partial.text, ShapeInstructionLine(i, 0) + " | "));
        CHECK(Contains(partial.text, "placed in shape " + std::to_string(i)));
    }
    CHECK(Contains(partial.text, HexText(kPlantedLzcnt) + ": 66 f3 0f bd e8 | 16-bit"));
    CHECK(Contains(partial.text, HexText(kPlantedLzcnt + 3 * 0x20) + ": f3 48 0f bd ca | 64-bit"));
    CHECK(Contains(partial.text, "0x20100: f3 4c 0f bd c1 | 64-bit, destination r8, register source | matches no shape"));
    CHECK(Contains(partial.text, "context: ") && Contains(partial.text, "[f3 4c 0f bd c1]"));
    CHECK(Contains(partial.text, "64-bit, destination r1, register source") &&
          Contains(partial.text, "memory source"));
    CHECK(!Contains(partial.text, "placed in shape 15"));
    CHECK(partial.text.size() < 24 * 1024);

    std::vector<size_t> two_instruction_shapes;
    for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i) {
        if (lzcnt_shapes[i].second != 0) {
            two_instruction_shapes.push_back(i);
        }
    }
    CHECK(two_instruction_shapes.size() == 1);
    const size_t kTwoInstructionShape = two_instruction_shapes.empty() ? 0 : two_instruction_shapes[0];
    MappedExecutable gap = image;
    std::fill_n(gap.image.begin() + kPlantedLzcnt + kFirstCluster * 0x20, lzcnt_shapes[kFirstCluster].length,
                uint8_t{0x90});
    const ScanResult gapped = ScanExecutable(gap, gap.image.size());
    CHECK(Contains(gapped.text, "instructions found: 49 (the shapes hold 50)"));
    CHECK(Contains(gapped.text, "15 of 49 shapes placed, stopped at shape 15"));
    CHECK(Contains(gapped.text, "detected instructions outside the placed shapes: 34 of 49"));
    size_t represented = 0;
    for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i) {
        for (size_t k = 0; k < (lzcnt_shapes[i].second ? 2u : 1u); ++k) {
            if (i == kFirstCluster) {
                continue;
            }
            const bool listed = Contains(gapped.text, ShapeInstructionLine(i, k) + " | ");
            CHECK(listed);
            represented += listed ? 1 : 0;
        }
    }
    CHECK(represented == 49);
    const LzcntShape& pair = lzcnt_shapes[kTwoInstructionShape];
    LzcntOperands first_operands, second_operands;
    CHECK(lzcnt_compat_decode(pair.bytes, pair.length, &first_operands) && first_operands.length == pair.second);
    CHECK(lzcnt_compat_decode(pair.bytes + pair.second, pair.length - pair.second, &second_operands) &&
          pair.second + second_operands.length == pair.length);
    const std::string second_form = std::to_string(second_operands.bits) + "-bit, destination r" +
                                    std::to_string(second_operands.dst) + ", " +
                                    (second_operands.memory ? "memory" : "register") + " source";
    CHECK(Contains(gapped.text, ShapeInstructionLine(kTwoInstructionShape, 1) + " | " + second_form + " | "));
    CHECK(Contains(gapped.text, HexText(kPlantedLzcnt + kTwoInstructionShape * 0x20 + pair.second) + ": "));
    CHECK(ShapeInstructionLine(kTwoInstructionShape, 0) != ShapeInstructionLine(kTwoInstructionShape, 1));
    CHECK(!Contains(gapped.text, "more not listed"));

    const ScanResult with_modules = ScanExecutable(eboot_only, eboot_only.image.size() - 0x1000);
    CHECK(Contains(with_modules.text, "bundled modules: linked after the eboot (0x1000 of "));

    MappedExecutable crowded = image;
    for (size_t i = 0; i < 80; ++i) {
        std::memcpy(&crowded.image[0x21000 + i * 0x20], stray, sizeof(stray));
    }
    const ScanResult listed = ScanExecutable(crowded, crowded.image.size());
    CHECK(Contains(listed.text, "(+16 more not listed)"));

    const MappedExecutable empty;
    const ScanResult nothing = ScanExecutable(empty, 0);
    CHECK(!nothing.complete() && !nothing.anything_found);
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

void TestUnsupportedBuildFlow() {
    const std::filesystem::path root = TempDir("unsupported-build");
    const std::filesystem::path game = root / "game";
    std::filesystem::create_directories(game / "sce_sys");
    const std::string eboot_bytes(2048, 'x');
    {
        std::ofstream eboot(game / "eboot.bin", std::ios::binary);
        eboot << eboot_bytes;
    }
    std::ofstream(game / "Odin2_BT_US.CPK", std::ios::binary) << "not a cpk";
    std::ofstream(game / "sce_discmap.plt", std::ios::binary) << "plt";
    WriteSfo(game / "sce_sys" / "param.sfo",
             {{"TITLE_ID", "CUSA05083"},
              {"CONTENT_ID", "UP0177-CUSA05083_00-ODINSPHERELE0000"},
              {"CATEGORY", "gd"},
              {"APP_VER", "01.02"},
              {"VERSION", "01.01"}});
    const GameRoots roots{root / "prepared", root / "image"};

    const InstallResult result = InstallFromFolder(game, roots);
    CHECK(result.status == InstallStatus::kUnsupportedBuild);
    CHECK(!result.ok());
    CHECK(result.report.starts_with("Unsupported build diagnostics begin\n"));
    CHECK(result.report.ends_with("Unsupported build diagnostics end\n"));
    CHECK(Contains(result.report, "eboot.bin: 2048 bytes"));
    CHECK(Contains(result.report, "SHA-256: " + Sha256Hex(game / "eboot.bin")));
    CHECK(Contains(result.report, "Reported build match: none (still unsupported)"));
    CHECK(Contains(result.report, "TITLE_ID CUSA05083, CONTENT_ID UP0177-CUSA05083_00-ODINSPHERELE0000, APP_VER 01.02"));
    CHECK(Contains(result.report, "Executable: not readable"));
    CHECK(Contains(result.report, "Odin2_BT_US.CPK: 9 bytes, CPK NOT READABLE"));
    CHECK(Contains(result.report, "sce_discmap_patch.plt: absent"));
    CHECK(Contains(result.report, "Mapping incomplete"));
    CHECK(!std::filesystem::exists(roots.prepared) && !std::filesystem::exists(roots.image));
    CHECK(ReadFile(game / "eboot.bin") == eboot_bytes);
    const InstallMessage message = DescribeInstall(InstallStatus::kUnsupportedBuild);
    CHECK(message.title == "Unsupported Odin Sphere Leifthrasir build");
    CHECK(Contains(message.text, "not supported yet") && Contains(message.text, "written to the log"));
    CHECK(!Contains(message.text, "0x") && !Contains(message.title, "0x"));

    std::filesystem::remove(game / "Odin2_BT_US.CPK");
    const InstallResult other = InstallFromFolder(game, roots);
    CHECK(other.status == InstallStatus::kWrongVersion && other.report.empty());
    CHECK(!DiagnoseUnsupportedBuild(game, "detail").odin);

    std::ofstream(game / "Odin2_BT_US.CPK", std::ios::binary) << "not a cpk";
    WriteSfo(game / "sce_sys" / "param.sfo",
             {{"TITLE_ID", "CUSA99999"}, {"CONTENT_ID", "UP0000-CUSA99999_00-OTHERGAME000000"}, {"APP_VER", "01.00"}});
    const InstallResult foreign = InstallFromFolder(game, roots);
    CHECK(foreign.status == InstallStatus::kWrongGame && foreign.report.empty());

    WriteSfo(game / "sce_sys" / "param.sfo",
             {{"TITLE_ID", "CUSA05083"},
              {"CONTENT_ID", "UP0177-CUSA05083_00-ODINSPHERELE0000"},
              {"APP_VER", "01.02"},
              {"VERSION", "01.01"},
              {"CATEGORY", "gp"}});
    const UnsupportedBuildReport package =
        DiagnoseUnsupportedPackage(ReadFile(game / "sce_sys" / "param.sfo"), "detail", {}, {}, "encrypted");
    CHECK(package.odin && Contains(package.text, "APP_VER 01.02") && Contains(package.text, "CATEGORY gp"));
    CHECK(Contains(package.text, "eboot.bin: not unpacked (encrypted)"));
    CHECK(!DiagnoseUnsupportedPackage("", "detail", {}, {}).odin);

    const std::filesystem::path supported = root / "supported";
    std::filesystem::create_directories(supported / "sce_sys");
    WriteSfo(supported / "sce_sys" / "param.sfo",
             {{"TITLE_ID", "CUSA05083"},
              {"CONTENT_ID", "UP0177-CUSA05083_00-ODINSPHERELE0000"},
              {"APP_VER", "01.00"},
              {"VERSION", "01.01"}});
    std::ofstream(supported / "eboot.bin", std::ios::binary) << "x";
    const InstallResult supported_identity = InstallFromFolder(supported, roots);
    CHECK(supported_identity.status == InstallStatus::kIncomplete && supported_identity.report.empty());
}

void TestBuildDiagnosticsKnownBuild() {
    const std::filesystem::path game = std::filesystem::path(OSL_SOURCE_DIR) / "game" / "prepared" / "CUSA05083-1.00";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(game / "eboot.bin", ec)) {
        std::printf("skipped: no local CUSA05083 1.00 game folder for the known-build scan\n");
        return;
    }
    const GameBuild& build = SupportedBuilds()[0];
    if (Sha256Hex(game / "eboot.bin") != build.eboot_sha256) {
        std::printf("skipped: the local CUSA05083 game folder is not the supported eboot\n");
        return;
    }
    const std::uintmax_t size_before = std::filesystem::file_size(game / "eboot.bin", ec);
    const auto time_before = std::filesystem::last_write_time(game / "eboot.bin", ec);
    const UnsupportedBuildReport report = DiagnoseUnsupportedBuild(game, "test");
    CHECK(report.odin);
    CHECK(Contains(report.text, "Mapping appears complete"));
    CHECK(Contains(report.text, "SHA-256: " + std::string(build.eboot_sha256)));
    CHECK(Contains(report.text, "notice scene: unique " + HexText(build.skip_intro->notice_scene)));
    CHECK(Contains(report.text, "hd title check: unique " + HexText(build.skip_intro->hd_title_check)));
    CHECK(Contains(report.text, "classic title check: unique " + HexText(build.skip_intro->classic_title_check)));
    CHECK(Contains(report.text, "classic button: inside the code " + HexText(build.skip_intro->classic_button)));
    CHECK(Contains(report.text, "stub space: free " + HexText(build.skip_intro->stubs)));
    CHECK(Contains(report.text, "getter: unique " + HexText(build.bookshelf->accessor)));
    CHECK(Contains(report.text, "special test: unique " + HexText(build.bookshelf->total)));
    std::string reads;
    for (int i = 0; i < OSL_BOOKSHELF_READS; ++i) {
        reads += (i ? " " : "") + HexText(build.bookshelf->reads[i]);
    }
    CHECK(Contains(report.text, reads));
    CHECK(Contains(report.text, "marks: 2/2 " + HexText(build.bookshelf->marks[0]) + " " + HexText(build.bookshelf->marks[1])));
    CHECK(Contains(report.text, "read order: consistent, page read: through a helper"));
    CHECK(Contains(report.text, "site: unique " + HexText(build.snapshot_scale_site)));
    CHECK(Contains(report.text, "the expected 1.5"));
    std::string sites;
    for (size_t i = 0; i < build.resolution_sites.size(); ++i) {
        sites += (i ? " " : "") + HexText(build.resolution_sites[i].offset);
    }
    CHECK(Contains(report.text, sites));
    const LzcntBuild& lzcnt = lzcnt_builds[2];
    CHECK(std::string(lzcnt.build) == "CUSA05083 1.00 USA");
    std::string ranges;
    for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i) {
        ranges += (i ? " " : "") + HexText(lzcnt.offsets[i]);
    }
    CHECK(Contains(report.text, ranges));
    CHECK(Contains(report.text, "linked image: " + HexText(lzcnt.size) + ", entry " + HexText(lzcnt.entry)));
    CHECK(Contains(report.text, "unexpected instructions: 0"));
    CHECK(std::filesystem::file_size(game / "eboot.bin", ec) == size_before);
    CHECK(std::filesystem::last_write_time(game / "eboot.bin", ec) == time_before);
}

const GameBuild* BuildWithLabel(std::string_view label) {
    for (const GameBuild& build : SupportedBuilds()) {
        if (build.label == label) {
            return &build;
        }
    }
    return nullptr;
}

std::string SfoText(const std::filesystem::path& dir, const std::vector<std::pair<std::string, std::string>>& values) {
    WriteSfo(dir / "param.sfo", values);
    return ReadFile(dir / "param.sfo");
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
    for (const GameBuild& build : SupportedBuilds()) {
        const std::string title = build.title_id, version = build.version, folder = build.folder;
        const PackageLayer& finished = FinishedLayer(build);
        const std::filesystem::path image_dir = dir / "image" / folder;

        // Installation
        PARITY("Installation", title.size() == 9 && title.starts_with("CUSA") &&
                                   std::all_of(title.begin() + 4, title.end(), [](char c) { return c >= '0' && c <= '9'; }));
        PARITY("Installation", std::string_view(build.content_id).ends_with("_00-ODINSPHERELE0000") &&
                                   Contains(build.content_id, title));
        PARITY("Installation", IsHexSha(build.eboot_sha256) && shas.insert(build.eboot_sha256).second);
        PARITY("Installation", folder == title + "-" + version && folders.insert(folder).second);
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
        PARITY("Installation", package.state == GameFileState::kValid && package.build == &build &&
                                   package.layer == build.layers.size() - 1);
        if (finished.update) {
            const GameCheck complete = CheckPackageIdentity(
                SfoText(dir, {{"TITLE_ID", title}, {"CONTENT_ID", build.content_id}, {"CATEGORY", "gd"},
                              {"APP_VER", finished.app_ver},
                              {"VERSION", build.merged_sfo_version ? build.merged_sfo_version : finished.sfo_version}}));
            PARITY("Installation", !!build.merged_sfo_version == (complete.state == GameFileState::kValid &&
                                                                  complete.build == &build && complete.layer == 0));
        }
        PARITY("Installation", CheckSfoIdentity(sfo).build == &build);
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

void TestHongKongBuild() {
    const GameBuild* found = BuildWithLabel("CUSA03873 1.00 HK");
    CHECK(found && found == &SupportedBuilds().back());
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

std::vector<std::pair<std::string, std::string>> UsaSfo(const char* category, const char* app_ver, const char* version) {
    return {{"TITLE", "Odin Sphere Leifthrasir"}, {"TITLE_ID", "CUSA05083"},
            {"CONTENT_ID", "UP0177-CUSA05083_00-ODINSPHERELE0000"}, {"CATEGORY", category},
            {"APP_VER", app_ver}, {"VERSION", version}};
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
        CHECK(!UnresolvedFeatures(partial.mapping).empty() && Contains(partial.text, "status: incomplete"));
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

// Issue #5's 1.01 is a supported build now; a version after it still reaches the unsupported-build diagnostics.
void TestUsa101FutureVersionDiagnostics() {
    const std::filesystem::path root = TempDir("usa-future");
    const std::filesystem::path game = root / "game";
    std::filesystem::create_directories(game / "sce_sys");
    std::ofstream(game / "eboot.bin", std::ios::binary) << std::string(2048, 'x');
    std::ofstream(game / "Odin2_BT_US.CPK", std::ios::binary) << "not a cpk";
    std::ofstream(game / "sce_discmap.plt", std::ios::binary) << "plt";
    const GameRoots roots{root / "prepared", root / "image"};

    WriteSfo(game / "sce_sys" / "param.sfo", UsaSfo("gd", "01.01", "01.01"));
    const InstallResult supported = InstallFromFolder(game, roots);
    CHECK(supported.status == InstallStatus::kIncomplete && supported.report.empty());

    WriteSfo(game / "sce_sys" / "param.sfo", UsaSfo("gd", "01.02", "01.01"));
    const InstallResult future = InstallFromFolder(game, roots);
    CHECK(future.status == InstallStatus::kUnsupportedBuild);
    CHECK(future.report.starts_with("Unsupported build diagnostics begin\n") &&
          future.report.ends_with("Unsupported build diagnostics end\n"));
    CHECK(Contains(future.report, "APP_VER 01.02") && Contains(future.report, "Reported build match: none"));
    CHECK(!std::filesystem::exists(roots.prepared) && !std::filesystem::exists(roots.image));

    for (const char* category : {"gd", "gp"}) {
        const std::string sfo = SfoText(root, UsaSfo(category, "01.02", "01.02"));
        const GameCheck identity = CheckPackageIdentity(sfo);
        CHECK(identity.state == GameFileState::kDifferentVersion);
        const UnsupportedBuildReport package = DiagnoseUnsupportedPackage(sfo, identity.detail, {}, {}, "encrypted");
        CHECK(package.odin && Contains(package.text, "APP_VER 01.02") &&
              Contains(package.text, std::string("CATEGORY ") + category));
        CHECK(Contains(package.text, "Supported-build check: param.sfo APP_VER 01.02 / VERSION 01.02"));
    }
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

// Issue #4: v0.2.0 diagnosed a package only when its TITLE_ID was a supported one, so a new regional build of the game
// got none. Any package whose CONTENT_ID or archives are the game's now reaches the diagnostics, with its executable.
void TestUnsupportedPackageDiagnostics() {
    const std::filesystem::path dir = TempDir("unsupported-package");
    const std::string variant = SfoText(dir, {{"TITLE_ID", "CUSA99998"},
                                              {"CONTENT_ID", "HP0177-CUSA99998_00-ODINSPHERELE0000"},
                                              {"CATEGORY", "gd"},
                                              {"APP_VER", "01.00"},
                                              {"VERSION", "01.00"}});
    const GameCheck identity = CheckPackageIdentity(variant);
    CHECK(identity.state == GameFileState::kDifferentVersion && identity.build == nullptr);
    CHECK(identity.detail == "param.sfo TITLE_ID is CUSA99998");

    const std::vector<PackageFile> files = {
        {"Odin2_BT_XX.CPK", 123}, {"eboot.bin", 2048}, {"sce_module/libc.prx", 9}, {"sce_sys/param.sfo", 64}};
    CHECK(IsOdinPackage(variant, files) && IsOdinPackage(variant, {}));
    const std::filesystem::path executables = dir / "executables";
    std::filesystem::create_directories(executables);
    std::ofstream(executables / "eboot.bin", std::ios::binary) << std::string(2048, 'x');
    const UnsupportedBuildReport report = DiagnoseUnsupportedPackage(variant, identity.detail, executables, files);
    CHECK(report.odin);
    CHECK(report.text.starts_with("Unsupported build diagnostics begin\n"));
    CHECK(report.text.ends_with("Unsupported build diagnostics end\n"));
    CHECK(Contains(report.text, "eboot.bin: 2048 bytes"));
    CHECK(Contains(report.text, "SHA-256: " + Sha256Hex(executables / "eboot.bin")));
    CHECK(Contains(report.text, "TITLE_ID CUSA99998, CONTENT_ID HP0177-CUSA99998_00-ODINSPHERELE0000"));
    CHECK(Contains(report.text, "Supported-build check: param.sfo TITLE_ID is CUSA99998"));
    CHECK(Contains(report.text, "Executable: not readable"));
    CHECK(Contains(report.text, "Package files: 4\n  Odin2_BT_XX.CPK: 123 bytes\n  eboot.bin: 2048 bytes\n"));
    CHECK(Contains(report.text, "sce_discmap.plt: absent"));
    CHECK(Contains(report.text, "Mapping incomplete"));

    const std::string relabeled =
        SfoText(dir, {{"TITLE_ID", "CUSA99997"}, {"CONTENT_ID", "XX0000-CUSA99997_00-OSL0000000000000"}});
    CHECK(IsOdinPackage(relabeled, files));
    CHECK(!IsOdinPackage(relabeled, {}));
    CHECK(!DiagnoseUnsupportedPackage(relabeled, "detail", executables, files).odin);
    const std::vector<PackageFile> nested = {{"data/Odin2_BT_XX.CPK", 1}, {"eboot.bin", 1}};
    CHECK(!IsOdinPackage(relabeled, nested));

    const std::string other_game =
        SfoText(dir, {{"TITLE_ID", "CUSA99999"}, {"CONTENT_ID", "UP0000-CUSA99999_00-OTHERGAME000000"}});
    const std::vector<PackageFile> other_files = {{"game.dat", 10}, {"eboot.bin", 2048}};
    CHECK(!IsOdinPackage(other_game, other_files));
    const UnsupportedBuildReport other = DiagnoseUnsupportedPackage(other_game, "detail", executables, other_files);
    CHECK(!other.odin && other.text.empty());

    CHECK(IsDiagnosedPackageFile("eboot.bin"));
    CHECK(IsDiagnosedPackageFile("sce_module/libc.prx") && IsDiagnosedPackageFile("sce_module/libSceFios2.prx"));
    CHECK(!IsDiagnosedPackageFile("Odin2_BT_US.CPK") && !IsDiagnosedPackageFile("sce_sys/param.sfo"));
    CHECK(!IsDiagnosedPackageFile("sce_module/libSceNpToolkit.prx") && !IsDiagnosedPackageFile("sub/eboot.bin"));

    const std::string source = ReadFile(std::filesystem::path(OSL_SOURCE_DIR) / "launcher" / "game_install.cpp");
    CHECK(Contains(source, "DiagnosePackage(package, roots, hooks, identity.detail)"));
    CHECK(!Contains(source, "if (identity.build) {"));
    CHECK(Contains(source, "return StagedFailure(layout.app0_stage, unpacked, hooks);"));
    CHECK(Contains(source, "return StagedFailure(layout.app0_stage, merged, hooks);"));
    CHECK(Contains(source, "result.report = DiagnoseBuildMapping(layout.app0, build, hooks.prompt_patch, &result.unmapped);"));

    const std::filesystem::path usa = std::filesystem::path(OSL_SOURCE_DIR) / "game" / "prepared" / "CUSA05083-1.00";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(usa / "eboot.bin", ec) ||
        Sha256Hex(usa / "eboot.bin") != SupportedBuilds()[0].eboot_sha256) {
        std::printf("skipped: no local CUSA05083 1.00 eboot for the package executable scan\n");
        return;
    }
    const UnsupportedBuildReport scanned = DiagnoseUnsupportedPackage(relabeled, "detail", usa, files);
    CHECK(scanned.odin && Contains(scanned.text, "Mapping appears complete"));
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
    TestSharedDirectoryArchive();
    TestSessionLogFiles();
    TestCrashReport();
    TestHangReport();
    TestPlacementVisibility();
    TestPlacementTracking();
    TestPlacementSettings();
    TestCrashReportReachesSessionLog();
    TestBuildDiagnosticsScanner();
    TestMapExecutableRejectsMalformedFiles();
    TestUnsupportedBuildFlow();
    TestBuildDiagnosticsKnownBuild();
    TestHongKongBuild();
    TestUsa101Identity();
    TestUsa101Layering();
    TestUsa101Scanned();
    TestUsa101FutureVersionDiagnostics();
    TestBuildContract();
    TestScannerReproducesTables();
    TestScannedMappingFixtures();
    TestUnsupportedPackageDiagnostics();
    TestBuildMappingDiagnostics();
    PrintParitySummary();
    std::printf("%s (%d checks, %d failures)\n", g_failures ? "FAILED" : "OK", g_checks, g_failures);
    runtime_log_flush();
    return g_failures ? 1 : 0;
}
