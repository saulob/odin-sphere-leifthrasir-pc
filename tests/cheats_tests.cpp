// The cheats (Issue #20): the one table, the settings, the launcher page, the signature locator and the hooks, the
// supplied CUSA03873 reference bytes, every locally prepared build, coexistence with the other image patches, the
// diagnostics, and the game's own instructions run through the hooks with live toggles. The in-game menu's logic is in
// cheats_menu_tests.cpp. Headless; game files are only read.
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "build_scan.h"
#include "cheats_page.h"
#include "cheats_test_support.h"
#include "game_files.h"
#include "game_profile.h"
#include "lzcnt_compat.h"
#include "options.h"
#include "osl_bookshelf.h"
#include "osl_cheats.h"
#include "osl_launcher.h"
#include "osl_resolution.h"
#include "osl_skip_intro.h"
#include "osl_title_menu_exit.h"
#include "page_icons.h"
#include "runtime_cheats.h"
#include "settings.h"
#include "test_support.h"

using namespace osl;
using namespace osl_test;
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
using cheat_test::g_checks;
using cheat_test::g_failures;
using cheat_test::Hex;

namespace {

constexpr unsigned kAll = OSL_CHEAT_ALL;

// The Issue #20 research, kept apart from the production table so the two are compared, not shared.
struct Reference {
    const char* id;
    const char* aob;
    size_t hit;
    const char* original;
    const char* patched;
    uint64_t hk_eboot_offset;
    OslCheat cheat;
};

const Reference kReference[] = {
    {"P1", "0d c4 c1 7a 10 86 98 16 00 00 c5 fa 5d c1 c4 c1 7a 11 86 94 16 00 00 c5 f8", 12, "5d", "5f", 0x444faa,
     OSL_CHEAT_INFINITE_ARROWS},
    {"P2", "84 24 98 16 00 00 c5 fa 5d c1 c4 c1 7a 11 84 24 94", 8, "5d", "5f", 0x45fb53, OSL_CHEAT_INFINITE_POW},
    {"P3", "42 0f 00 b8 3f 42 0f 00 0f 4e c3 41 89 87 ac 03 00 00 e8", 8, "0f 4e c3", "90 90 90", 0x4649c7,
     OSL_CHEAT_MAX_GOLD_GAIN},
    {"P4", "00 48 89 df ff c0 39 c1 0f 42 c1 89 83 44 21 00 00 c4 c1", 8, "0f 42 c1", "89 c8 90", 0x47a216,
     OSL_CHEAT_MAX_PHOZON_GAIN},
    {"P5", "84 24 03 00 00 8b 40 20 29 83 4c 21 00 00 e9 32 03 00 00 41 80 bf", 8, "29 83 4c 21 00 00",
     "90 90 90 90 90 90", 0x5de726, OSL_CHEAT_NO_AP_CONSUMPTION},
    {"P6", "42 0f 00 b9 3f 42 0f 00 0f 4e ca 89 88 ac 03 00 00 41 c7", 8, "0f 4e ca", "90 90 90", 0x6746b6,
     OSL_CHEAT_MAX_GOLD_GAIN},
    {"P7", "77 27 48 8b 03 48 89 df ff 90 a0 0a 00 00 c5 fa 10 4d f4 c5 f8 2e", 8, "ff 90 a0 0a 00 00",
     "90 90 90 90 90 90", 0x76e9b2, OSL_CHEAT_INFINITE_PP},
};

const OslCheatSite* SiteNamed(const char* id, OslCheat* owner = nullptr) {
    for (const OslCheatInfo& info : osl_cheats) {
        for (size_t s = 0; s < info.site_count; ++s) {
            if (std::string(info.sites[s].id) == id) {
                if (owner) *owner = info.cheat;
                return &info.sites[s];
            }
        }
    }
    // A site missing from the table fails here instead of crashing the planting helpers.
    static const unsigned char none[1] = {0};
    static const OslCheatSite absent{"", "", OSL_CHEAT_SITE_HOOK, nullptr, 0, none, 0, 0, none, 0, 0, 0, nullptr, 0, -1, -1, nullptr, -1, -1};
    CHECK(!"site in the cheat table");
    return &absent;
}

// Noise that cannot hold a signature by chance: no byte of it is 0x0d, 0x84, 0x42, 0x00 or 0x77, the first bytes of
// the seven signatures.
Bytes Noise(size_t size, uint32_t seed) {
    Bytes out(size);
    for (size_t i = 0; i < size; ++i) {
        seed = seed * 1664525u + 1013904223u;
        uint8_t b = static_cast<uint8_t>(seed >> 24);
        while (b == 0x0d || b == 0x84 || b == 0x42 || b == 0x00 || b == 0x77) b = static_cast<uint8_t>(b + 3);
        out[i] = b;
    }
    return out;
}

void PlantSite(Bytes& image, const OslCheatSite& site, uint64_t patch) {
    std::memcpy(image.data() + patch - site.patch_at, site.signature, site.signature_size);
    if (site.prefix_size) std::memcpy(image.data() + patch - site.patch_at - site.prefix_size, site.prefix, site.prefix_size);
}

// Infinite Health's two sites laid out from `at`: the HP change, and 0x100 later the HUD's call to the player accessor,
// which reaches the accessor at +0x200 reading its global at +0x300.
constexpr uint64_t kHealthChangePatch = 26, kHealthCallAt = 0x100, kHealthAccessorAt = 0x200, kHealthGlobalAt = 0x300;

void PlantHealth(Bytes& image, uint64_t at) {
    const OslCheatSite& change = *SiteNamed("H1");
    const OslCheatSite& call = *SiteNamed("H2");
    std::memcpy(image.data() + at, change.signature, change.signature_size);
    std::memcpy(image.data() + at + kHealthCallAt, call.signature, call.signature_size);
    const int32_t rel = static_cast<int32_t>(kHealthAccessorAt - (kHealthCallAt + 5));
    std::memcpy(image.data() + at + kHealthCallAt + 1, &rel, 4);
    const Bytes accessor = Hex("48 8d 05 00 00 00 00 48 8b 00 48 8b 80 f8 00 00 00 c3");
    std::copy(accessor.begin(), accessor.end(), image.begin() + static_cast<std::ptrdiff_t>(at + kHealthAccessorAt));
    const int32_t disp = static_cast<int32_t>(kHealthGlobalAt - (kHealthAccessorAt + 7));
    std::memcpy(image.data() + at + kHealthAccessorAt + 3, &disp, 4);
    std::memset(image.data() + at + kHealthGlobalAt, 0, 8);
}

std::vector<OslCodeRange> Whole(const Bytes& image) { return {{0, image.size()}}; }

void Scan(const Bytes& image, OslCheatScan* scans, std::vector<OslCodeRange> ranges = {}) {
    if (ranges.empty()) ranges = Whole(image);
    osl_cheat_scan(image.data(), image.size(), ranges.data(), ranges.size(), scans);
}

// The code with the hooks installed as the loader lays them out: the stubs, then the switches, after the image.
struct HookedImage {
    Bytes memory;
    size_t code_size = 0;
    unsigned installed = 0;
    OslCheatScan scans[OSL_CHEAT_COUNT];

    explicit HookedImage(const Bytes& code, std::vector<OslCodeRange> ranges = {}) : memory(code), code_size(code.size()) {
        memory.resize(code_size + OSL_CHEAT_STUBS_BYTES + OSL_CHEAT_COUNT, 0);
        if (ranges.empty()) ranges = {{0, code_size}};
        installed = osl_install_cheats(memory.data(), code_size, ranges.data(), ranges.size(), memory.data() + code_size,
                                       OSL_CHEAT_STUBS_BYTES, memory.data() + code_size + OSL_CHEAT_STUBS_BYTES, scans);
    }
    Bytes Code() const { return Bytes(memory.begin(), memory.begin() + static_cast<std::ptrdiff_t>(code_size)); }
    const uint8_t* Stub(size_t index) const { return memory.data() + code_size + index * OSL_CHEAT_STUB_BYTES; }
};

// [from, from + size) of the code is as it was before the hooks went in.
bool Untouched(const Bytes& original, const HookedImage& hooked, uint64_t from, size_t size) {
    return std::equal(original.begin() + static_cast<std::ptrdiff_t>(from),
                      original.begin() + static_cast<std::ptrdiff_t>(from + size),
                      hooked.memory.begin() + static_cast<std::ptrdiff_t>(from));
}

std::vector<size_t> Differences(const Bytes& a, const Bytes& b) {
    std::vector<size_t> out;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (a[i] != b[i]) out.push_back(i);
    }
    return out;
}

// The seven Issue #20 sites at `offsets` (P1 to P7) and Infinite Health's at `health_at`, in noise.
Bytes PlantedImage(size_t size, const std::array<uint64_t, 7>& offsets, uint64_t health_at) {
    Bytes image = Noise(size, 20);
    for (size_t i = 0; i < std::size(kReference); ++i) PlantSite(image, *SiteNamed(kReference[i].id), offsets[i]);
    PlantHealth(image, health_at);
    return image;
}

std::array<uint64_t, 7> HkOffsets() {
    std::array<uint64_t, 7> offsets{};
    for (size_t i = 0; i < std::size(kReference); ++i) offsets[i] = kReference[i].hk_eboot_offset;
    return offsets;
}

constexpr uint64_t kHkHealthAt = 0x100000;

// The seven sites at their CUSA03873 1.00 HK eboot offsets, the reference's own coordinates, and Infinite Health's.
Bytes HkReferenceImage() { return PlantedImage(0x780000, HkOffsets(), kHkHealthAt); }

void TestDefinitions() {
    CHECK(OSL_CHEAT_COUNT == 7 && std::size(osl_cheats) == 7);
    const char* const keys[] = {"cheat_max_gold_gain", "cheat_max_phozon_gain", "cheat_no_ap_consumption", "cheat_infinite_pp",
                                "cheat_infinite_pow", "cheat_infinite_health", "cheat_infinite_arrows"};
    const char* const labels[] = {"Max Gold Gain", "Max Phozon Gain", "No AP Consumption", "Infinite PP",
                                  "Infinite POW", "Infinite Health", "Infinite Arrows"};
    const char* const sections[] = {"Resources", "Resources", "Resources", "Combat", "Combat", "Combat", "Combat"};
    std::set<std::string> seen_keys, seen_labels, seen_ids;
    for (size_t c = 0; c < OSL_CHEAT_COUNT; ++c) {
        const OslCheatInfo& info = osl_cheats[c];
        CHECK(static_cast<size_t>(info.cheat) == c);
        CHECK(std::string(info.key) == keys[c] && std::string(info.label) == labels[c] &&
              std::string(info.section) == sections[c]);
        CHECK(seen_keys.insert(info.key).second && seen_labels.insert(info.label).second);
        CHECK(std::strlen(info.description) > 20 && std::strlen(info.description) < 100);
        CHECK(info.site_count >= 1 && info.site_count <= OSL_CHEAT_MAX_SITES);
        for (size_t s = 0; s < info.site_count; ++s) {
            const OslCheatSite& site = info.sites[s];
            CHECK(seen_ids.insert(site.id).second);
            // The first byte anchors the search and must not be a patch byte; the patch lies inside the signature.
            CHECK(site.patch_at > 0 && site.patch_size > 0 && site.patch_at + site.patch_size <= site.signature_size);
            if (site.kind == OSL_CHEAT_SITE_PLAYER_ACCESSOR) {
                CHECK(site.region_size == 0 && !site.replacement && !site.changed && site.signature[0] == 0xe8 &&
                      site.patch_at == 1 && site.patch_size == 4);
                continue;
            }
            // A hook carries either the replacement bytes or code of its own, never both.
            CHECK(!site.replacement != !site.changed);
            if (site.replacement) {
                CHECK(std::memcmp(site.signature + site.patch_at, site.replacement, site.patch_size) != 0);
            } else {
                CHECK(site.changed_size > site.region_size && site.changed_size <= OSL_CHEAT_CHANGED_MAX &&
                      site.changed_player_at >= 0 && size_t(site.changed_player_at) + 4 <= site.changed_size);
                CHECK(site.patch_at == size_t(site.region_at) && site.patch_size == site.region_size);
            }
            // The hook replaces whole instructions: at least a jump long, around the patch, inside what was verified.
            const int patch_end = static_cast<int>(site.patch_at + site.patch_size);
            CHECK(site.region_size >= 5 && site.region_size <= OSL_CHEAT_REGION_MAX);
            CHECK(site.region_at <= static_cast<int>(site.patch_at) && site.region_at + static_cast<int>(site.region_size) >= patch_end);
            CHECK(site.region_at + static_cast<int>(site.region_size) <= static_cast<int>(site.signature_size));
            CHECK(site.prefix_size == (site.region_at < 0 ? static_cast<size_t>(-site.region_at) : 0) &&
                  (site.prefix_size == 0) == (site.prefix == nullptr));
        }
    }
    CHECK(!kCheatDefault);
    CHECK(seen_ids == (std::set<std::string>{"P1", "P2", "P3", "P4", "P5", "P6", "P7", "H1", "H2"}));
    // Indexed flag bytes: Infinite Health takes index 5 and moves Infinite Arrows to 6.
    CHECK(OSL_CHEAT_INFINITE_POW == 4 && OSL_CHEAT_INFINITE_HEALTH == 5 && OSL_CHEAT_INFINITE_ARROWS == 6);
    CHECK(osl_cheat_bit(OSL_CHEAT_MAX_GOLD_GAIN) == 1 && osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH) == 32 &&
          osl_cheat_bit(OSL_CHEAT_INFINITE_ARROWS) == 64 && OSL_CHEAT_ALL == 127);
    CHECK(osl_cheats[OSL_CHEAT_INFINITE_HEALTH].site_count == 2 &&
          osl_cheats[OSL_CHEAT_INFINITE_HEALTH].sites[0].kind == OSL_CHEAT_SITE_HOOK &&
          osl_cheats[OSL_CHEAT_INFINITE_HEALTH].sites[1].kind == OSL_CHEAT_SITE_PLAYER_ACCESSOR);

    // The proven ownership and the exact research bytes.
    for (const Reference& r : kReference) {
        OslCheat owner = OSL_CHEAT_COUNT;
        const OslCheatSite* site = SiteNamed(r.id, &owner);
        CHECK(site && owner == r.cheat);
        if (!site) continue;
        const Bytes aob = Hex(r.aob), original = Hex(r.original), patched = Hex(r.patched);
        CHECK(Bytes(site->signature, site->signature + site->signature_size) == aob && site->patch_at == r.hit);
        CHECK(Bytes(site->signature + site->patch_at, site->signature + site->patch_at + site->patch_size) == original);
        CHECK(Bytes(site->replacement, site->replacement + site->patch_size) == patched);
    }
    CHECK(osl_cheats[OSL_CHEAT_MAX_GOLD_GAIN].site_count == 2);
    CHECK(std::string(osl_cheat_state_name(OSL_CHEAT_COMPLETE)) == "COMPLETE" &&
          std::string(osl_cheat_state_name(OSL_CHEAT_INVALID)) == "INVALID");

    char text[256];
    osl_cheat_summary(0, text, sizeof(text));
    CHECK(std::string(text) == "all off");
    osl_cheat_summary(osl_cheat_bit(OSL_CHEAT_MAX_GOLD_GAIN) | osl_cheat_bit(OSL_CHEAT_INFINITE_PP), text, sizeof(text));
    CHECK(std::string(text) == "Max Gold Gain on, Infinite PP on, 5 off");
    osl_cheat_summary(osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH), text, sizeof(text));
    CHECK(std::string(text) == "Infinite Health on, 6 off");
    osl_cheat_summary(kAll, text, sizeof(text));
    CHECK(std::string(text) == "Max Gold Gain on, Max Phozon Gain on, No AP Consumption on, Infinite PP on, "
                               "Infinite POW on, Infinite Health on, Infinite Arrows on");
}

void TestSettings() {
    const fs::path dir = TempDir("cheats-settings");
    const fs::path file = dir / "osl.ini";
    const std::string path = file.string();
    CHECK(osl_cheats_enabled(path.c_str()) == 0);
    CHECK(osl_cheats_enabled(nullptr) == 0);
    for (const OslCheatInfo& cheat : osl_cheats) {
        const auto managed = ManagedSettings();
        const auto it = std::find_if(managed.begin(), managed.end(),
                                     [&](const ManagedSetting& m) { return m.key == cheat.key; });
        CHECK(it != managed.end() && it->default_value == "0");
        CHECK(IsValidSetting(cheat.key, "0") && IsValidSetting(cheat.key, "1"));
        CHECK(!IsValidSetting(cheat.key, "2") && !IsValidSetting(cheat.key, "true") && !IsValidSetting(cheat.key, "on"));
    }
    {
        LauncherSettings settings(file);
        for (const OslCheatInfo& cheat : osl_cheats) CHECK(!settings.GetBool(cheat.key, kCheatDefault));
        settings.SetBool(osl_cheats[OSL_CHEAT_MAX_GOLD_GAIN].key, true);
        settings.SetBool(osl_cheats[OSL_CHEAT_INFINITE_ARROWS].key, true);
        CHECK(settings.Save());
    }
    const std::string saved = ReadFile(file);
    CHECK(Contains(saved, "cheat_max_gold_gain = 1") || Contains(saved, "cheat_max_gold_gain=1"));
    CHECK(osl_cheats_enabled(path.c_str()) ==
          (osl_cheat_bit(OSL_CHEAT_MAX_GOLD_GAIN) | osl_cheat_bit(OSL_CHEAT_INFINITE_ARROWS)));
    {
        LauncherSettings reloaded(file);
        CHECK(reloaded.GetBool("cheat_max_gold_gain", false) && reloaded.GetBool("cheat_infinite_arrows", false) &&
              !reloaded.GetBool("cheat_infinite_pp", false));
    }

    // Anything but 0 or 1 is Off, for the launcher and the game alike.
    {
        std::ofstream out(file, std::ios::trunc);
        out << "cheat_max_gold_gain = true\ncheat_max_phozon_gain = 2\ncheat_no_ap_consumption = on\n"
               "cheat_infinite_pp =\ncheat_infinite_pow = yes\ncheat_infinite_arrows = 1\n";
    }
    CHECK(osl_cheats_enabled(path.c_str()) == osl_cheat_bit(OSL_CHEAT_INFINITE_ARROWS));
    {
        const LauncherSettings malformed(file);
        CHECK(!malformed.GetBool("cheat_max_gold_gain", false) && !malformed.GetBool("cheat_max_phozon_gain", false) &&
              malformed.GetBool("cheat_infinite_arrows", false));
    }

    // Reset all settings puts every cheat back to Off.
    {
        std::ofstream out(file, std::ios::trunc);
        for (const OslCheatInfo& cheat : osl_cheats) out << cheat.key << " = 1\n";
    }
    CHECK(osl_cheats_enabled(path.c_str()) == kAll);
    {
        LauncherSettings settings(file);
        settings.ResetToDefaults();
        for (const OslCheatInfo& cheat : osl_cheats) CHECK(!settings.GetBool(cheat.key, true));
        CHECK(settings.Save());
    }
    CHECK(osl_cheats_enabled(path.c_str()) == 0);
}

void TestPage() {
    const auto at = [](PageId page) {
        return static_cast<size_t>(std::find(std::begin(kPageOrder), std::end(kPageOrder), page) - std::begin(kPageOrder));
    };
    CHECK(at(PageId::kCheats) < std::size(kPageOrder));
    CHECK(at(PageId::kCheats) == at(PageId::kGameplay) + 1 && at(PageId::kControls) == at(PageId::kCheats) + 1);
    CHECK(std::string(PageName(PageId::kCheats)) == "Cheats");
    CHECK(std::string(kCheatsSubtitle) == "Optional gameplay modifiers");
    CHECK(Contains(kCheatsHelp, "Sets the state used when the game starts") && Contains(kCheatsHelp, "Press F10 while playing") &&
          Contains(kCheatsHelp, "current session") && Contains(kCheatsHelp, "No game file is modified"));
    CHECK(!Contains(kCheatsHelp, "next time") && !Contains(kCheatsHelp, "restart") && std::string(kCheatsHotkeyName) == "F10");

    const PageIconAsset* icon = FindPageIcon(PageId::kCheats);
    CHECK(icon && std::string(icon->file) == "cheats.svg" && icon->optical_scale == 0.92f);
    const fs::path icons = fs::path(OSL_SOURCE_DIR) / "assets" / "launcher" / "icons";
    // Byte for byte the Dragon's Crown Pro PC launcher's cheats.svg (Star Alt 1, svgrepo.com/svg/532289, CC0).
    CHECK(Sha256Hex(icons / "cheats.svg") == "90474c3054bf871c5a1a9a623196388b4e1015dd38642e5831af020c1aa728ed");
    const std::string sources = ReadFile(icons / "SOURCES.md");
    CHECK(Contains(sources, "| `cheats.svg` | Star Alt 1 | https://www.svgrepo.com/svg/532289/star-alt-1 | CC0 |"));
    const std::string licenses = ReadFile(fs::path(OSL_SOURCE_DIR) / "packaging" / "licenses" / "THIRD-PARTY-LICENSES.txt");
    CHECK(Contains(licenses, "cheats.svg     Star Alt 1    https://www.svgrepo.com/svg/532289/star-alt-1"));
    for (const int pixels : {16, 21, 24, 32, 42, 64}) {
        const std::vector<uint8_t> rgba = RenderPageIcon(PageId::kCheats, pixels);
        CHECK(rgba.size() == size_t(pixels) * size_t(pixels) * 4);
        size_t inked = 0;
        int min_x = pixels, max_x = -1, min_y = pixels, max_y = -1;
        for (int y = 0; y < pixels && !rgba.empty(); ++y) {
            for (int x = 0; x < pixels; ++x) {
                if (rgba[(size_t(y) * pixels + x) * 4 + 3] == 0) continue;
                ++inked;
                min_x = std::min(min_x, x), max_x = std::max(max_x, x), min_y = std::min(min_y, y), max_y = std::max(max_y, y);
            }
        }
        const int slack = 1 + pixels / 24;
        CHECK(inked > 0 && std::abs(min_x + max_x + 1 - pixels) <= slack && std::abs(min_y + max_y + 1 - pixels) <= slack);
        // 0.92 of the box: the star's longer side fills it up to the rounding of one pixel.
        CHECK(std::max(max_x - min_x, max_y - min_y) + 1 >= int(pixels * 0.92f) - 1);
    }

    const std::vector<CheatPageRow> rows = CheatPageRows();
    CHECK(rows.size() == 11);
    std::vector<std::string> shown;
    size_t toggles = 0;
    for (const CheatPageRow& row : rows) {
        CHECK(int(!!row.heading) + int(!!row.cheat) + int(!!row.setting) == 1);
        shown.push_back(row.heading ? std::string("# ") + row.heading
                                    : std::string(row.cheat ? row.cheat->label : row.setting->label));
        toggles += row.cheat != nullptr;
    }
    CHECK(toggles == 7);
    CHECK(shown == (std::vector<std::string>{"# Resources", "Max Gold Gain", "Max Phozon Gain", "No AP Consumption",
                                             "# Combat", "Infinite PP", "Infinite POW", "Infinite Health", "Infinite Arrows",
                                             "# Progression", "Experience Multiplier"}));

    CHECK(!CheatsAvailable(GameFileState::kMissing, nullptr));
    CHECK(!CheatsAvailable(GameFileState::kDifferentVersion, nullptr));
    CHECK(!CheatsAvailable(GameFileState::kValid, nullptr));
    CHECK(!CheatsAvailable(GameFileState::kReadError, &SupportedBuilds()[0]));
    CHECK(CheatsAvailable(GameFileState::kValid, &SupportedBuilds()[0]));

    // The page draws those rows as Off / On toggles, disabled without a supported build, under the shared subtitle.
    const std::string launcher = ReadFile(fs::path(OSL_SOURCE_DIR) / "launcher" / "launcher.cpp");
    const size_t page = launcher.find("void PageCheats(trg::Ui& ui) {");
    const size_t end = launcher.find("void PageControls(trg::Ui& ui) {");
    CHECK(page != std::string::npos && end != std::string::npos && page < end);
    if (page != std::string::npos && end != std::string::npos && page < end) {
        const std::string body = launcher.substr(page, end - page);
        for (const char* part : {"std::string help = kCheatsHelp;", "ui.Help(help.c_str());", "SavedKey(settings_, OSL_KEY_HOTKEY_CHEATS)", "CheatsAvailable(files_, build_)", "ui.Help(kCheatsUnavailableHelp);",
                                 "ImGui::BeginDisabled(!available);", "CheatPageRows()", "ui.Heading(row.heading);",
                                 "row.cheat->key, kCheatDefault, \"Off\", \"On\");", "ImGui::EndDisabled();", "ui.EndRows();"}) {
            CHECK(Contains(body, part));
        }
    }
    CHECK(Contains(launcher, "case PageId::kCheats:\n            return {name, kCheatsSubtitle,"));
}

void TestLocator() {
    // Every site once, wherever it is.
    Bytes image = Noise(0x40000, 1);
    std::map<std::string, uint64_t> where;
    uint64_t next = 0x1000;
    for (const Reference& r : kReference) {
        where[r.id] = next + 0x40;
        PlantSite(image, *SiteNamed(r.id), where[r.id]);
        next += 0x4321;
    }
    PlantHealth(image, next);
    where["H1"] = next + kHealthChangePatch;
    where["H2"] = next + kHealthCallAt + 1;
    OslCheatScan scans[OSL_CHEAT_COUNT];
    Scan(image, scans);
    CHECK(scans[OSL_CHEAT_INFINITE_HEALTH].sites[1].global == next + kHealthGlobalAt);
    for (const OslCheatInfo& info : osl_cheats) {
        const OslCheatScan& scan = scans[info.cheat];
        CHECK(scan.state == OSL_CHEAT_COMPLETE && scan.related && scan.reason[0] == 0);
        for (size_t s = 0; s < info.site_count; ++s) {
            CHECK(scan.sites[s].state == OSL_CHEAT_SITE_UNIQUE && scan.sites[s].hits == 1 &&
                  scan.sites[s].patch == where[info.sites[s].id]);
        }
    }

    // Shifted: the same code one page later is found there.
    Bytes shifted = Noise(0x40000, 2);
    for (const Reference& r : kReference) PlantSite(shifted, *SiteNamed(r.id), where[r.id] + 0x1003);
    PlantHealth(shifted, next + 0x1003);
    Scan(shifted, scans);
    for (const OslCheatInfo& info : osl_cheats) {
        CHECK(scans[info.cheat].state == OSL_CHEAT_COMPLETE);
        for (size_t s = 0; s < info.site_count; ++s) CHECK(scans[info.cheat].sites[s].patch == where[info.sites[s].id] + 0x1003);
    }

    // Missing.
    Bytes missing = image;
    const OslCheatSite& p7 = *SiteNamed("P7");
    std::memset(missing.data() + where["P7"] - p7.patch_at, 0xcc, p7.signature_size);
    Scan(missing, scans);
    CHECK(scans[OSL_CHEAT_INFINITE_PP].state == OSL_CHEAT_MISSING &&
          scans[OSL_CHEAT_INFINITE_PP].sites[0].state == OSL_CHEAT_SITE_MISSING &&
          Contains(scans[OSL_CHEAT_INFINITE_PP].reason, "P7 PP setter: not found"));
    CHECK(scans[OSL_CHEAT_MAX_GOLD_GAIN].state == OSL_CHEAT_COMPLETE && scans[OSL_CHEAT_INFINITE_POW].state == OSL_CHEAT_COMPLETE);

    // Duplicate.
    Bytes duplicate = image;
    PlantSite(duplicate, *SiteNamed("P5"), 0x30000);
    Scan(duplicate, scans);
    CHECK(scans[OSL_CHEAT_NO_AP_CONSUMPTION].state == OSL_CHEAT_AMBIGUOUS &&
          scans[OSL_CHEAT_NO_AP_CONSUMPTION].sites[0].hits == 2 &&
          Contains(scans[OSL_CHEAT_NO_AP_CONSUMPTION].reason, "P5 ability purchase: 2 matches"));
    const HookedImage hooked_duplicate(duplicate);
    CHECK(!(hooked_duplicate.installed & osl_cheat_bit(OSL_CHEAT_NO_AP_CONSUMPTION)));
    CHECK(Untouched(duplicate, hooked_duplicate, where["P5"], 6) && Untouched(duplicate, hooked_duplicate, 0x30000, 6));

    // Damaged original bytes, and a site some other tool already changed: found, refused, nothing written.
    Bytes damaged = image;
    damaged[where["P3"]] = 0x0e;
    Scan(damaged, scans);
    CHECK(scans[OSL_CHEAT_MAX_GOLD_GAIN].state == OSL_CHEAT_INVALID &&
          scans[OSL_CHEAT_MAX_GOLD_GAIN].sites[0].state == OSL_CHEAT_SITE_MODIFIED &&
          Contains(scans[OSL_CHEAT_MAX_GOLD_GAIN].reason, "P3 gold pickup: other bytes"));
    const HookedImage hooked_damaged(damaged);
    CHECK(hooked_damaged.installed == (kAll & ~osl_cheat_bit(OSL_CHEAT_MAX_GOLD_GAIN)));
    CHECK(Untouched(damaged, hooked_damaged, where["P6"] - 5, 8) && Untouched(damaged, hooked_damaged, where["P3"] - 5, 8));
    Bytes patched = image;
    const OslCheatSite& p1 = *SiteNamed("P1");
    patched[where["P1"]] = p1.replacement[0];
    Scan(patched, scans);
    CHECK(scans[OSL_CHEAT_INFINITE_ARROWS].state == OSL_CHEAT_INVALID &&
          scans[OSL_CHEAT_INFINITE_ARROWS].sites[0].state == OSL_CHEAT_SITE_PATCHED &&
          Contains(scans[OSL_CHEAT_INFINITE_ARROWS].reason, "already changed"));

    // A multi-site cheat is all or nothing: Max Gold Gain with only its pickup site hooks neither.
    Bytes half = image;
    const OslCheatSite& p6 = *SiteNamed("P6");
    std::memset(half.data() + where["P6"] - p6.patch_at, 0xcc, p6.signature_size);
    Scan(half, scans);
    CHECK(scans[OSL_CHEAT_MAX_GOLD_GAIN].state == OSL_CHEAT_PARTIAL);
    const HookedImage hooked_half(half);
    CHECK(!(hooked_half.installed & osl_cheat_bit(OSL_CHEAT_MAX_GOLD_GAIN)) && Untouched(half, hooked_half, where["P3"] - 5, 8));

    // Infinite Health needs both of its sites, and the call must reach the player accessor.
    Bytes no_accessor = image;
    no_accessor[next + kHealthAccessorAt + 10] = 0x90;
    Scan(no_accessor, scans);
    CHECK(scans[OSL_CHEAT_INFINITE_HEALTH].state == OSL_CHEAT_INVALID &&
          scans[OSL_CHEAT_INFINITE_HEALTH].sites[1].state == OSL_CHEAT_SITE_MODIFIED &&
          scans[OSL_CHEAT_INFINITE_HEALTH].sites[1].global == 0 &&
          Contains(scans[OSL_CHEAT_INFINITE_HEALTH].reason, "H2 player accessor call: the call does not reach the player accessor"));
    const HookedImage hooked_no_accessor(no_accessor);
    CHECK(!(hooked_no_accessor.installed & osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH)) &&
          Untouched(no_accessor, hooked_no_accessor, next, 50));
    Bytes no_change = image;
    std::memset(no_change.data() + next, 0xcc, 50);
    Scan(no_change, scans);
    CHECK(scans[OSL_CHEAT_INFINITE_HEALTH].state == OSL_CHEAT_PARTIAL &&
          Contains(scans[OSL_CHEAT_INFINITE_HEALTH].reason, "H1 HP change: not found"));
    Bytes damaged_change = image;
    damaged_change[next + kHealthChangePatch + 9] ^= 0xff; // inside the hooked region: the add
    Scan(damaged_change, scans);
    CHECK(scans[OSL_CHEAT_INFINITE_HEALTH].state == OSL_CHEAT_INVALID &&
          scans[OSL_CHEAT_INFINITE_HEALTH].sites[0].state == OSL_CHEAT_SITE_MODIFIED);
    Bytes twice_change = image;
    PlantSite(twice_change, *SiteNamed("H1"), 0x3f000 + kHealthChangePatch);
    Scan(twice_change, scans);
    CHECK(scans[OSL_CHEAT_INFINITE_HEALTH].state == OSL_CHEAT_AMBIGUOUS);
    for (int c = 0; c < OSL_CHEAT_COUNT; ++c) {
        if (c != OSL_CHEAT_INFINITE_HEALTH) CHECK(scans[c].state == OSL_CHEAT_COMPLETE);
    }

    // Only the code ranges are searched.
    Scan(image, scans, {{0, where["P7"] - 4}});
    CHECK(scans[OSL_CHEAT_INFINITE_PP].state == OSL_CHEAT_MISSING && scans[OSL_CHEAT_NO_AP_CONSUMPTION].state == OSL_CHEAT_COMPLETE);
    Scan(image, scans, {{0, image.size() + 0x1000}, {image.size() - 4, image.size() + 64}});
    CHECK(scans[OSL_CHEAT_INFINITE_PP].state == OSL_CHEAT_COMPLETE);

    // No false positive in code that does not hold the signatures, even with their instructions all over it.
    Bytes random = Noise(0x400000, 3);
    for (size_t at = 0x100; at + 32 < random.size(); at += 0x1777) {
        const Reference& r = kReference[(at / 0x1777) % std::size(kReference)];
        const Bytes original = Hex(r.original);
        std::copy(original.begin(), original.end(), random.begin() + static_cast<std::ptrdiff_t>(at));
    }
    Scan(random, scans);
    for (const OslCheatScan& scan : scans) CHECK(scan.state == OSL_CHEAT_MISSING);
    const HookedImage hooked_random(random);
    CHECK(hooked_random.installed == 0 && hooked_random.Code() == random);
}

// The patch offsets in each local build's linked image, P1 to P7.
const std::map<std::string, std::array<uint64_t, 7>> kLocalSites = {
    {"CUSA05083-1.00", {0x4403ea, 0x45af93, 0x45fe07, 0x475726, 0x5da116, 0x670236, 0x76a642}},
    {"CUSA01290-1.00", {0x43c6ca, 0x456bce, 0x45bae7, 0x471256, 0x5d1636, 0x6671c6, 0x760922}},
    {"CUSA01290-1.02", {0x43eb1a, 0x4596c3, 0x45e527, 0x473cf6, 0x5d7ca6, 0x66db36, 0x767792}},
    {"CUSA05262-1.00", {0x440e3a, 0x45b9e3, 0x460857, 0x476196, 0x5dac76, 0x671486, 0x76baf2}},
    {"CUSA05262-1.01", {0x440f1a, 0x45bac3, 0x460937, 0x476276, 0x5dae16, 0x671636, 0x76bca2}},
};

Bytes ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

uint64_t Le64(const Bytes& b, size_t at) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | b[at + i];
    return v;
}

// boot.bin (launcher/boot_image.cpp): the memory image the loader patches, and its relocation targets.
bool ReadBootImage(const fs::path& path, Bytes& image, std::vector<uint64_t>& relocations) {
    const Bytes file = ReadAll(path);
    if (file.size() < 128 || std::memcmp(file.data(), "OSLBOOT1", 8) != 0) return false;
    const uint64_t size = Le64(file, 8), segments = Le64(file, 24), relocs = Le64(file, 32), names = Le64(file, 40);
    size_t at = 96;
    const uint64_t modules = Le64(file, at);
    at += 8 + modules * 56;
    const uint64_t bindings = Le64(file, at);
    at += 8 + bindings * 24 + segments * 24 + names * 128;
    if (size > file.size() || at + relocs * 32 > file.size() - size) return false;
    for (uint64_t i = 0; i < relocs; ++i) relocations.push_back(Le64(file, at + i * 32));
    image.assign(file.end() - static_cast<std::ptrdiff_t>(size), file.end());
    return true;
}

std::vector<OslCodeRange> EbootCode(const MappedExecutable& image, uint64_t eboot_size) {
    std::vector<OslCodeRange> ranges;
    for (const MappedSegment& s : image.segments) {
        if ((s.flags & 1) && s.vaddr < eboot_size) ranges.push_back({s.vaddr, std::min(s.vaddr + s.memsz, eboot_size)});
    }
    return ranges;
}

struct Span {
    uint64_t begin, end;
};

std::vector<Span> SignatureSpans(const OslCheatScan* scans) {
    std::vector<Span> spans;
    for (const OslCheatInfo& info : osl_cheats) {
        for (size_t s = 0; s < info.site_count; ++s) {
            const uint64_t start = scans[info.cheat].sites[s].patch - info.sites[s].patch_at - info.sites[s].prefix_size;
            spans.push_back({start, start + info.sites[s].prefix_size + info.sites[s].signature_size});
        }
    }
    return spans;
}

// The Classic engine's sites (and its Experience Multiplier's) as spans, found in `image` the way the loader finds them.
std::vector<Span> ClassicSignatureSpans(const Bytes& image, const std::vector<OslCodeRange>& code) {
    std::vector<Span> spans;
    OslCheatScan scans[OSL_CHEAT_COUNT], experience;
    osl_cheat_scan_engine(image.data(), image.size(), code.data(), code.size(), OSL_ENGINE_CLASSIC, scans);
    osl_experience_scan_engine(image.data(), image.size(), code.data(), code.size(), OSL_ENGINE_CLASSIC, &experience);
    const auto add = [&](const OslCheatSite* sites, size_t count, const OslCheatScan& scan) {
        for (size_t s = 0; s < count; ++s) {
            const uint64_t start = scan.sites[s].patch - sites[s].patch_at - sites[s].prefix_size;
            spans.push_back({start, start + sites[s].prefix_size + sites[s].signature_size});
        }
    };
    for (const OslCheatInfo& info : osl_cheats) add(info.classic_sites, info.classic_site_count, scans[info.cheat]);
    add(osl_experience_multiplier.classic_sites, osl_experience_multiplier.classic_site_count, experience);
    return spans;
}

bool Inside(const std::vector<Span>& spans, uint64_t at) {
    return std::any_of(spans.begin(), spans.end(), [&](const Span& s) { return at >= s.begin && at < s.end; });
}

// The hook of a site is a stub: the flag test, the original instructions, then the changed ones.
constexpr size_t kStubOriginalAt = 24;
constexpr size_t kStubChangedGap = 5 + 1 + 8;

// Only hooked sites have a stub, in the table's order.
size_t StubIndex(OslCheat cheat, size_t site) {
    size_t index = 0;
    for (int c = 0; c <= static_cast<int>(cheat); ++c) {
        const size_t sites = c == static_cast<int>(cheat) ? site : osl_cheats[c].site_count;
        for (size_t s = 0; s < sites; ++s) index += osl_cheats[c].sites[s].kind == OSL_CHEAT_SITE_HOOK;
    }
    return index;
}

const Reference& ReferenceOf(const char* id) {
    for (const Reference& r : kReference) {
        if (std::string(r.id) == id) return r;
    }
    return kReference[0];
}

// The seven supplied patches as the hooks carry them: each site's region becomes a jump to a stub that runs the
// original instructions or the changed ones, and the code is never written again.
void TestHooks() {
    const Bytes source = HkReferenceImage();
    const HookedImage hooked(source);
    CHECK(hooked.installed == kAll);
    for (const OslCheatScan& scan : hooked.scans) CHECK(scan.state == OSL_CHEAT_COMPLETE);
    const Bytes code = hooked.Code();
    std::vector<Span> regions;
    size_t written = 0, changed_bytes = 0;
    for (const OslCheatInfo& info : osl_cheats) {
        for (size_t s = 0; s < info.site_count; ++s) {
            const OslCheatSite& site = info.sites[s];
            if (site.id[0] != 'P') continue;
            const Reference& ref = ReferenceOf(site.id);
            const uint64_t patch = hooked.scans[info.cheat].sites[s].patch;
            CHECK(patch == ref.hk_eboot_offset);
            const uint64_t region = patch - site.patch_at + static_cast<uint64_t>(site.region_at);
            regions.push_back({region, region + site.region_size});
            const size_t index = StubIndex(info.cheat, s);
            const uint8_t* at = code.data() + region;
            int32_t jump;
            std::memcpy(&jump, at + 1, 4);
            CHECK(at[0] == 0xe9 &&
                  static_cast<int64_t>(region) + 5 + jump == static_cast<int64_t>(hooked.code_size + index * OSL_CHEAT_STUB_BYTES));
            for (size_t k = 5; k < site.region_size; ++k) CHECK(at[k] == 0x90);

            const uint8_t* stub = hooked.Stub(index);
            const Bytes original(source.begin() + static_cast<std::ptrdiff_t>(region),
                                 source.begin() + static_cast<std::ptrdiff_t>(region + site.region_size));
            Bytes changed = original;
            const Bytes patched = Hex(ref.patched);
            std::copy(patched.begin(), patched.end(), changed.begin() + static_cast<std::ptrdiff_t>(patch - region));
            CHECK(std::equal(original.begin(), original.end(), stub + kStubOriginalAt));
            CHECK(std::equal(changed.begin(), changed.end(), stub + kStubOriginalAt + site.region_size + kStubChangedGap));
            written += Hex(ref.original).size();
            for (size_t k = 0; k < original.size(); ++k) changed_bytes += original[k] != changed[k];

            // lea rsp,[rsp-0x80]; pushfq; cmp byte [rip+disp],0 on this cheat's own flag byte; jne.
            CHECK(Bytes(stub, stub + 8) == Hex("48 8d 64 24 80 9c 80 3d") && stub[12] == 0x00 && stub[13] == 0x75);
            int32_t flag;
            std::memcpy(&flag, stub + 8, 4);
            const int64_t flag_at = static_cast<int64_t>(hooked.code_size + index * OSL_CHEAT_STUB_BYTES) + 13 + flag;
            CHECK(flag_at == static_cast<int64_t>(hooked.code_size + OSL_CHEAT_STUBS_BYTES + static_cast<size_t>(info.cheat)));
        }
    }
    // Infinite Health: the HP change's region jumps to its stub, whose changed block is the port's code with its rel32
    // on the player accessor's global, and the accessor call itself is left alone.
    {
        const OslCheatSite& site = osl_cheats[OSL_CHEAT_INFINITE_HEALTH].sites[0];
        const uint64_t region = kHkHealthAt + kHealthChangePatch;
        CHECK(hooked.scans[OSL_CHEAT_INFINITE_HEALTH].sites[0].patch == region &&
              hooked.scans[OSL_CHEAT_INFINITE_HEALTH].sites[1].global == kHkHealthAt + kHealthGlobalAt);
        regions.push_back({region, region + site.region_size});
        const size_t index = StubIndex(OSL_CHEAT_INFINITE_HEALTH, 0);
        int32_t jump;
        std::memcpy(&jump, code.data() + region + 1, 4);
        CHECK(code[region] == 0xe9 &&
              static_cast<int64_t>(region) + 5 + jump == static_cast<int64_t>(hooked.code_size + index * OSL_CHEAT_STUB_BYTES));
        const uint8_t* stub = hooked.Stub(index);
        CHECK(std::equal(source.begin() + static_cast<std::ptrdiff_t>(region),
                         source.begin() + static_cast<std::ptrdiff_t>(region + site.region_size), stub + kStubOriginalAt));
        const uint8_t* changed = stub + kStubOriginalAt + site.region_size + kStubChangedGap;
        for (size_t k = 0; k < site.changed_size; ++k) {
            if (int(k) < site.changed_player_at || int(k) >= site.changed_player_at + 4) CHECK(changed[k] == site.changed[k]);
        }
        int32_t global;
        std::memcpy(&global, changed + site.changed_player_at, 4);
        const int64_t next = static_cast<int64_t>(hooked.code_size + index * OSL_CHEAT_STUB_BYTES + kStubOriginalAt +
                                                  site.region_size + kStubChangedGap) + site.changed_player_at + 4;
        CHECK(next + global == static_cast<int64_t>(kHkHealthAt + kHealthGlobalAt));
        int32_t flag;
        std::memcpy(&flag, stub + 8, 4);
        CHECK(static_cast<int64_t>(hooked.code_size + index * OSL_CHEAT_STUB_BYTES) + 13 + flag ==
              static_cast<int64_t>(hooked.code_size + OSL_CHEAT_STUBS_BYTES + OSL_CHEAT_INFINITE_HEALTH));
        CHECK(Untouched(source, hooked, kHkHealthAt + kHealthCallAt, 0x220));
        // The stub after Infinite Health's belongs to Infinite Arrows, reading flag 6.
        std::memcpy(&flag, hooked.Stub(index + 1) + 8, 4);
        CHECK(static_cast<int64_t>(hooked.code_size + (index + 1) * OSL_CHEAT_STUB_BYTES) + 13 + flag ==
              static_cast<int64_t>(hooked.code_size + OSL_CHEAT_STUBS_BYTES + OSL_CHEAT_INFINITE_ARROWS));
    }
    CHECK(written == 23 && changed_bytes == 22);
    for (const size_t at : Differences(source, code)) CHECK(Inside(regions, at));
    // Both sites of Max Gold Gain read the same flag.
    int32_t first, second;
    std::memcpy(&first, hooked.Stub(0) + 8, 4);
    std::memcpy(&second, hooked.Stub(1) + 8, 4);
    CHECK(first + 13 + 0 == second + 13 + static_cast<int32_t>(OSL_CHEAT_STUB_BYTES));
    // The switches start at 0: the original behaviour until a cheat is turned on.
    for (size_t c = 0; c < OSL_CHEAT_COUNT; ++c) CHECK(hooked.memory[hooked.code_size + OSL_CHEAT_STUBS_BYTES + c] == 0);
    std::printf("HK reference: 7 sites, %zu bytes written, %zu changed, every site hooked, nothing else in the code changed\n",
                written, changed_bytes);
    std::printf("Infinite Health: HP change hooked with the player test, player accessor resolved to its global\n");
}

constexpr uint64_t RoundPage(uint64_t v) { return (v + 0xfff) & ~uint64_t{0xfff}; }

// Every other change the loader makes to the image, on `buffer` (the image, then stub room). Returns the names that
// refused.
std::string ApplyOtherPatches(const GameBuild& build, const MappedExecutable& mapped, Bytes& buffer) {
    std::string refused;
    char error[320];
    const uint64_t size = mapped.image.size();
    const OslResolution target = osl_render_target(&osl_resolutions[OSL_RESOLUTION_COUNT - 1], &osl_aspect_ratios[2]);
    if (!osl_apply_snapshot_scale(buffer.data(), size, build.snapshot_scale_site, &target, error, sizeof(error))) refused += " snapshot";
    if (!osl_apply_resolution(buffer.data(), size, build.resolution_sites.data(), build.resolution_sites.size(), &target, error,
                              sizeof(error)))
        refused += " resolution";
    uint64_t code_end = 0;
    for (const MappedSegment& s : mapped.segments) {
        if ((s.flags & 1) && build.skip_intro->stubs >= s.vaddr && build.skip_intro->stubs < s.vaddr + RoundPage(s.memsz))
            code_end = s.vaddr + RoundPage(s.memsz);
    }
    if (!code_end || !osl_apply_skip_intro(buffer.data(), code_end, build.skip_intro, 1, error, sizeof(error))) refused += " skip-intro";
    const uint64_t lzcnt_at = RoundPage(size), shelf_at = lzcnt_at + RoundPage(lzcnt_compat_stub_bytes());
    const uint64_t title_at = shelf_at + OSL_BOOKSHELF_STUB_BYTES;
    if (!osl_apply_bookshelf(buffer.data(), size, build.bookshelf, buffer.data() + shelf_at, OSL_BOOKSHELF_STUB_BYTES, error,
                             sizeof(error)))
        refused += " bookshelf";
    if (!osl_apply_title_menu_exit(buffer.data(), size, title_at, OSL_TITLE_EXIT_STUB_BYTES, 1, error, sizeof(error)))
        refused += " title-exit";
    static const uint8_t tcb_load[] = {0x65, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0};
    for (const MappedSegment& s : mapped.segments) {
        if (!(s.flags & 1)) continue;
        for (uint64_t at = s.vaddr; at + sizeof(tcb_load) <= std::min(s.vaddr + s.memsz, size); ++at) {
            if (buffer[at] == 0x65 && !std::memcmp(buffer.data() + at, tcb_load, sizeof(tcb_load))) {
                std::memset(buffer.data() + at + 5, 0xab, 4);
                at += sizeof(tcb_load) - 1;
            }
        }
    }
    const char* name = nullptr;
    if (lzcnt_compat_install(buffer.data(), size, mapped.entry, buffer.data() + lzcnt_at, lzcnt_compat_stub_bytes(), &name) !=
        LZCNT_COMPAT_OK)
        refused += " lzcnt";
    return refused;
}

size_t CheatStubOffset(const MappedExecutable& mapped) {
    return RoundPage(mapped.image.size()) + RoundPage(lzcnt_compat_stub_bytes()) + OSL_BOOKSHELF_STUB_BYTES +
           OSL_TITLE_EXIT_STUB_BYTES;
}

size_t BufferSize(const MappedExecutable& mapped) {
    return CheatStubOffset(mapped) + RoundPage(size_t{OSL_CHEAT_STUBS_BYTES} + size_t{OSL_EXPERIENCE_STUBS_BYTES}) +
           RoundPage(size_t{OSL_CHEAT_COUNT} + 1);
}

// Infinite Health in a real build: the HP change once, its region whole instructions with no jump into it, and the
// HUD's accessor call reaching the accessor whose global lies in writable data.
void CheckHealthSitesInBuild(const GameBuild& build, const MappedExecutable& mapped, uint64_t eboot_size,
                             const OslCheatScan* scans) {
    const OslCheatScan& health = scans[OSL_CHEAT_INFINITE_HEALTH];
    CHECK(health.state == OSL_CHEAT_COMPLETE);
    const uint64_t global = health.sites[1].global;
    bool writable = false;
    for (const MappedSegment& s : mapped.segments) {
        writable = writable || ((s.flags & 2) && global >= s.vaddr && global + 8 <= s.vaddr + s.memsz && global < eboot_size);
    }
    CHECK(writable);
    const uint64_t region = health.sites[0].patch;
    const Bytes add = Hex("49 8b 84 24 50 03 00 00 45 01 f5");
    CHECK(std::equal(add.begin(), add.end(), mapped.image.begin() + static_cast<std::ptrdiff_t>(region)));
    size_t jumps_in = 0;
    for (const MappedSegment& s : mapped.segments) {
        if (!(s.flags & 1) || s.vaddr >= eboot_size) continue;
        for (uint64_t at = s.vaddr; at + 6 <= s.vaddr + s.memsz && at + 6 <= mapped.image.size(); ++at) {
            const uint8_t op = mapped.image[at];
            int64_t target = -1;
            if (op == 0xe8 || op == 0xe9) {
                int32_t rel;
                std::memcpy(&rel, mapped.image.data() + at + 1, 4);
                target = static_cast<int64_t>(at) + 5 + rel;
            } else if (op == 0x0f && (mapped.image[at + 1] & 0xf0) == 0x80) {
                int32_t rel;
                std::memcpy(&rel, mapped.image.data() + at + 2, 4);
                target = static_cast<int64_t>(at) + 6 + rel;
            }
            jumps_in += target > static_cast<int64_t>(region) && target < static_cast<int64_t>(region + 11);
        }
    }
    // The byte scan over-approximates rel32 branches, so zero here means none of any kind.
    CHECK(jumps_in == 0);
    std::printf("    Infinite Health: HP change at %s, player global at %s (%s)\n", HexText(region).c_str(), HexText(global).c_str(),
                build.label);
}

// Phases 6 and 15: every locally prepared build, through the installer's own mapping and the boot image the loader
// reads; the other image patches never touch a cheat's code, and the order of the two does not matter.
void TestLocalBuilds() {
    std::printf("Cheat build parity (real files = game\\prepared eboot and build\\image boot.bin; DATA UNAVAILABLE is never PASS):\n");
    size_t verified = 0;
    for (const GameBuild& build : SupportedBuilds()) {
        const fs::path app0 = fs::path(OSL_SOURCE_DIR) / "game" / "prepared" / build.folder;
        std::error_code ec;
        if (!fs::is_regular_file(app0 / "eboot.bin", ec)) {
            std::printf("  %-26s DATA UNAVAILABLE (no prepared eboot.bin; the signature scan is generic)\n", build.label);
            continue;
        }
        const size_t failures = static_cast<size_t>(g_failures);
        std::string notes;
        CHECK(Sha256Hex(app0 / "eboot.bin") == build.eboot_sha256);
        std::vector<Bytes> modules;
        for (const char* name : kBundledModules) modules.push_back(ReadAll(app0 / "sce_module" / name));
        const LinkedImage linked = LinkExecutable(ReadAll(app0 / "eboot.bin"), modules);
        CHECK(linked.eboot_error.empty());
        if (!linked.eboot_error.empty()) continue;
        const MappedExecutable& mapped = linked.image;
        const std::vector<OslCodeRange> code = EbootCode(mapped, linked.eboot_size);
        OslCheatScan scans[OSL_CHEAT_COUNT];
        osl_cheat_scan(mapped.image.data(), mapped.image.size(), code.data(), code.size(), scans);
        const auto expected = kLocalSites.find(build.folder);
        CHECK(expected != kLocalSites.end());
        for (const OslCheatInfo& info : osl_cheats) {
            CHECK(scans[info.cheat].state == OSL_CHEAT_COMPLETE);
            for (size_t s = 0; s < info.site_count && expected != kLocalSites.end(); ++s) {
                if (info.sites[s].id[0] != 'P') continue;
                const int index = info.sites[s].id[1] - '1';
                CHECK(scans[info.cheat].sites[s].patch == expected->second[static_cast<size_t>(index)]);
            }
        }

        // The build scan reports the same, from the same locator, without changing the image.
        const Bytes pristine = mapped.image;
        const ScanResult scan = ScanExecutable(mapped, linked.eboot_size);
        CHECK(mapped.image == pristine);
        CHECK(scan.cheats.size() == OSL_CHEAT_COUNT && CheatSummary(scan) == "Cheats: 7/7 COMPLETE");
        CheckHealthSitesInBuild(build, mapped, linked.eboot_size, scans);
        CHECK(Contains(scan.text, "    P3 gold pickup: unique at " + HexText(scans[OSL_CHEAT_MAX_GOLD_GAIN].sites[0].patch) +
                                      ", original bytes 0f 4e c3 valid"));

        // The bytes the loader really maps: boot.bin's image, its relocations kept off every signature.
        Bytes boot;
        std::vector<uint64_t> relocations;
        const fs::path boot_file = fs::path(OSL_SOURCE_DIR) / "build" / "image" / build.folder / "boot.bin";
        if (ReadBootImage(boot_file, boot, relocations)) {
            OslCheatScan boot_scans[OSL_CHEAT_COUNT];
            osl_cheat_scan(boot.data(), boot.size(), code.data(), code.size(), boot_scans);
            const std::vector<Span> spans = SignatureSpans(scans);
            for (const OslCheatInfo& info : osl_cheats) {
                CHECK(boot_scans[info.cheat].state == OSL_CHEAT_COMPLETE);
                for (size_t s = 0; s < info.site_count; ++s)
                    CHECK(boot_scans[info.cheat].sites[s].patch == scans[info.cheat].sites[s].patch);
            }
            CHECK(std::none_of(relocations.begin(), relocations.end(), [&](uint64_t target) {
                return Inside(spans, target) || Inside(spans, target + 7);
            }));
            notes += "boot image";
        } else {
            notes += "no boot image";
        }

        // Coexistence with the loader's other changes, on the table builds whose sites are all known.
        if (build.mapping == ExecutableMapping::kTable && build.skip_intro && build.bookshelf && !build.resolution_sites.empty()) {
            Bytes buffer(BufferSize(mapped), 0);
            std::copy(mapped.image.begin(), mapped.image.end(), buffer.begin());
            const Bytes original = buffer;
            const std::string refused = ApplyOtherPatches(build, mapped, buffer);
            if (!refused.empty()) std::printf("    refused:%s\n", refused.c_str());
            CHECK(refused.empty());
            std::vector<Span> spans = SignatureSpans(scans);
            for (const Span& classic : ClassicSignatureSpans(mapped.image, code)) spans.push_back(classic);
            const std::vector<size_t> others = Differences(original, buffer);
            CHECK(!others.empty());
            CHECK(std::none_of(others.begin(), others.end(), [&](size_t at) { return Inside(spans, at); }));

            const size_t cheat_at = CheatStubOffset(mapped);
            const size_t flags_at = cheat_at + RoundPage(size_t{OSL_CHEAT_STUBS_BYTES} + size_t{OSL_EXPERIENCE_STUBS_BYTES});
            // As the loader does: the seven cheats, then the Experience Multiplier after their stubs and flags.
            const auto install = [&](Bytes& image) {
                const unsigned cheats = osl_install_cheats(image.data(), mapped.image.size(), code.data(), code.size(),
                                                           image.data() + cheat_at, OSL_CHEAT_STUBS_BYTES,
                                                           image.data() + flags_at, nullptr);
                const int experience = osl_install_experience(image.data(), mapped.image.size(), code.data(), code.size(),
                                                              image.data() + cheat_at + OSL_CHEAT_STUBS_BYTES,
                                                              OSL_EXPERIENCE_STUBS_BYTES, image.data() + flags_at + OSL_CHEAT_COUNT,
                                                              nullptr);
                return experience == 1 ? cheats : 0u;
            };
            Bytes others_then_cheats = buffer;
            CHECK(install(others_then_cheats) == kAll);
            Bytes cheats_then_others = original;
            CHECK(install(cheats_then_others) == kAll);
            CHECK(ApplyOtherPatches(build, mapped, cheats_then_others).empty());
            CHECK(others_then_cheats == cheats_then_others);
            // The hooks change only their sites' regions in the code, and their own stub pages.
            OslCheatScan experience;
            osl_experience_scan(mapped.image.data(), mapped.image.size(), code.data(), code.size(), &experience);
            const uint64_t experience_region = experience.sites[0].patch;
            CHECK(experience.state == OSL_CHEAT_COMPLETE);
            CHECK(std::none_of(others.begin(), others.end(), [&](size_t at) {
                return at >= experience.sites[0].at[0] && at < experience.sites[0].at[0] + 0x23b;
            }));
            for (const size_t at : Differences(original, others_then_cheats)) {
                CHECK(std::binary_search(others.begin(), others.end(), at) || Inside(spans, at) || at >= cheat_at ||
                      (at >= experience_region && at < experience_region + 5));
            }
            notes += ", coexistence";
        }
        const bool pass = static_cast<size_t>(g_failures) == failures;
        verified += pass;
        std::printf("  %-26s %s (7/7 COMPLETE, prepared eboot, %s)\n", build.label, pass ? "PASS" : "FAIL", notes.c_str());
    }
    std::printf("Cheat build parity: %zu build(s) verified on real files\n", verified);
}

// The overlap check itself: a change inside a signature is caught.
void TestOverlapCheck() {
    OslCheatScan scans[OSL_CHEAT_COUNT];
    Bytes image = HkReferenceImage();
    Scan(image, scans);
    const std::vector<Span> spans = SignatureSpans(scans);
    CHECK(spans.size() == 9);
    for (size_t a = 0; a < spans.size(); ++a)
        for (size_t b = a + 1; b < spans.size(); ++b) CHECK(spans[a].end <= spans[b].begin || spans[b].end <= spans[a].begin);
    const Reference& p5 = kReference[4];
    CHECK(Inside(spans, p5.hk_eboot_offset) && Inside(spans, p5.hk_eboot_offset - 8) && !Inside(spans, p5.hk_eboot_offset - 9));
    // Another patch writing into P5's signature (not its patch bytes) breaks the fingerprint: the cheat goes MISSING.
    image[p5.hk_eboot_offset + 7] ^= 0xff;
    Scan(image, scans);
    CHECK(scans[OSL_CHEAT_NO_AP_CONSUMPTION].state == OSL_CHEAT_MISSING);
}

void TestDiagnostics() {
    // The synthetic image of every scanned feature holds no cheat code: reported MISSING, never borrowed.
    const MappedExecutable synthetic = SyntheticOdinImage();
    const ScanResult none = ScanExecutable(synthetic, synthetic.image.size());
    CHECK(none.complete() && none.cheats.size() == OSL_CHEAT_COUNT);
    for (const FeatureScan& cheat : none.cheats) CHECK(cheat.state == FeatureState::kMissing);
    CHECK(Contains(none.text, "    P1 crossbow shot: not found") && Contains(none.text, "Cheats: 0/7 COMPLETE, Max Gold Gain MISSING"));
    CHECK(Contains(none.text, "    H1 HP change: not found") && Contains(none.text, "    H2 player accessor call: not found"));
    CHECK(ExperienceSummary(none) == "Experience Multiplier: MISSING");

    // The same image with the cheats planted in its code, then damaged one way per cheat.
    MappedExecutable planted = synthetic;
    uint64_t code_end = 0;
    for (const MappedSegment& s : planted.segments) {
        if ((s.flags & 1) && s.vaddr == 0) code_end = s.vaddr + s.memsz;
    }
    CHECK(code_end >= 0x2a000);
    std::map<std::string, uint64_t> where;
    uint64_t at = 0x28000;
    for (const Reference& r : kReference) {
        where[r.id] = at;
        PlantSite(planted.image, *SiteNamed(r.id), at);
        at += 0x400;
    }
    PlantHealth(planted.image, at);
    const uint64_t health_at = at;
    const Bytes before = planted.image;
    const ScanResult all = ScanExecutable(planted, planted.image.size());
    CHECK(planted.image == before);
    CHECK(CheatSummary(all) == "Cheats: 7/7 COMPLETE" && all.complete());
    CHECK(Contains(all.text, "    H1 HP change: unique at " + HexText(health_at + kHealthChangePatch) + ", original bytes "));
    CHECK(Contains(all.text, "    H2 player accessor call: unique at " + HexText(health_at + kHealthCallAt) +
                                 ", calls the player accessor (global " + HexText(health_at + kHealthGlobalAt) + ")"));
    for (const FeatureScan& cheat : all.cheats) CHECK(cheat.state == FeatureState::kComplete && cheat.validator == "accepted");
    CHECK(Contains(all.text, "    P5 ability purchase: unique at " + HexText(where["P5"]) + ", original bytes 29 83 4c 21 00 00 valid"));
    CHECK(Contains(all.text, "    relationship: accepted (2 distinct sites, apart from every other cheat's)"));

    MappedExecutable broken = planted;
    const OslCheatSite& p6 = *SiteNamed("P6");
    std::memset(broken.image.data() + where["P6"] - p6.patch_at, 0xcc, p6.signature_size);          // Gold: PARTIAL
    PlantSite(broken.image, *SiteNamed("P4"), 0x2c000);                                              // Phozon: AMBIGUOUS
    broken.image[where["P7"]] = 0x90;                                                                // PP: INVALID
    const OslCheatSite& p2 = *SiteNamed("P2");
    std::memset(broken.image.data() + where["P2"] - p2.patch_at, 0xcc, p2.signature_size);          // POW: MISSING
    const ScanResult mixed = ScanExecutable(broken, broken.image.size());
    CHECK(CheatSummary(mixed) == "Cheats: 3/7 COMPLETE, Max Gold Gain PARTIAL, Max Phozon Gain AMBIGUOUS, Infinite PP INVALID, "
                                 "Infinite POW MISSING");
    // Infinite Health on its own: the accessor call broken, then the HP change gone.
    MappedExecutable no_accessor = planted;
    no_accessor.image[health_at + kHealthAccessorAt] ^= 0xff;
    const ScanResult invalid = ScanExecutable(no_accessor, no_accessor.image.size());
    CHECK(Contains(CheatSummary(invalid), "Infinite Health INVALID") &&
          Contains(invalid.text, "    H2 player accessor call: unique at " + HexText(health_at + kHealthCallAt) +
                                     ", but the call does not reach the player accessor"));
    MappedExecutable no_change = planted;
    std::memset(no_change.image.data() + health_at, 0xcc, 50);
    CHECK(Contains(CheatSummary(ScanExecutable(no_change, no_change.image.size())), "Infinite Health PARTIAL"));
    CHECK(Contains(mixed.text, "    P6 shop sale: not found") && Contains(mixed.text, "    relationship: not checked"));
    CHECK(Contains(mixed.text, "    P4 phozon absorbed: ambiguous (2 matches) at "));
    CHECK(Contains(mixed.text, "    P7 PP setter: unique at " + HexText(where["P7"]) +
                                   ", original bytes ff 90 a0 0a 00 00 invalid: found 90 90 a0 0a 00 00"));
    CHECK(Contains(mixed.text, "    P2 skill POW cost: not found\n    partial:"));
    // Cheats are optional: they never make the executable mapping incomplete.
    CHECK(mixed.complete());
}

// The game's own instructions around each site, as in every local build (checked below), hooked by the production
// installer and run natively with synthetic objects. `run_at`/`run_size` is the executed part; the rest completes a
// signature.
struct Snippet {
    const char* name;
    const char* bytes;
    size_t run_at, run_size;
    bool relocated = false; // holds rel32 offsets that differ per build; laid out by SnippetImage
};

const Snippet kGoldPickup{"gold pickup",
    "31 c0 41 03 9f ac 03 00 00 78 0e 81 fb 3f 42 0f 00 b8 3f 42 0f 00 0f 4e c3 41 89 87 ac 03 00 00 e8", 0, 32};
const Snippet kShopSale{"shop sale",
    "8b 90 ac 03 00 00 31 c9 41 03 94 24 28 03 00 00 78 0e 81 fa 3f 42 0f 00 b9 3f 42 0f 00 0f 4e ca 89 88 ac 03 00 00 "
    "41 c7 84 24 28 03 00 00 00 00 00 00", 0, 50};
const Snippet kGoldSpend{"shop purchase",
    "49 8b 84 24 88 02 00 00 8b 80 28 03 00 00 8b 93 ac 03 00 00 31 c9 29 c2 78 0e 81 fa 3f 42 0f 00 b9 3f 42 0f 00 "
    "0f 4e ca 89 8b ac 03 00 00", 0, 46};
const Snippet kPhozonAbsorbed{"phozon absorbed",
    "8b 83 44 21 00 00 8b 8b 48 21 00 00 48 89 df ff c0 39 c1 0f 42 c1 89 83 44 21 00 00 c4 c1", 0, 28};
const Snippet kPhozonSpend{"phozon spend",
    "49 8b 8f 88 03 00 00 8b 91 44 21 00 00 8b b1 48 21 00 00 2b 50 20 39 d6 0f 42 d6 89 91 44 21 00 00", 0, 33};
const Snippet kAbilityPurchase{"ability purchase",
    "84 24 03 00 00 8b 40 20 29 83 4c 21 00 00 e9 32 03 00 00 41 80 bf", 5, 9};
const Snippet kApGain{"AP gain per level-up", "01 83 4c 21 00 00", 0, 6};
const Snippet kPpSetter{"PP setter",
    "55 48 89 e5 53 50 48 89 fb c5 fa 11 45 f4 48 8b 03 ff 90 a0 0a 00 00 c5 fa 10 4d f4 c5 f8 2e c1 77 10 48 8b 03 48 "
    "89 df ff 90 a0 0a 00 00 c5 f8 28 c8 c5 f8 57 c0 c5 f8 2e c1 77 27 48 8b 03 48 89 df ff 90 a0 0a 00 00 c5 fa 10 4d "
    "f4 c5 f8 2e c1 c5 f8 28 c1 77 0c 48 8b 03 48 89 df ff 90 a0 0a 00 00 c5 fa 11 83 64 03 00 00 48 83 c4 08 5b 5d c3",
    0, 114};
const Snippet kSkillPowCost{"skill POW cost",
    "c4 c1 7a 10 8c 24 94 16 00 00 c5 f8 57 c0 c4 c1 7a 2a c5 c5 f2 5c c8 c5 f8 57 c0 c5 f8 2e c1 77 0e c4 c1 7a 10 84 "
    "24 98 16 00 00 c5 fa 5d c1 c4 c1 7a 11 84 24 94 16 00 00", 0, 57};
const Snippet kCrossbowShot{"Mercedes normal shot (param 0x34a)",
    "c4 c1 7a 10 86 94 16 00 00 c5 fa 5c c9 c5 f8 57 c0 c5 f8 2e c1 77 0d c4 c1 7a 10 86 98 16 00 00 c5 fa 5d c1 c4 c1 "
    "7a 11 86 94 16 00 00 c5 f8", 0, 45};
// Mercedes's other two POW drains (param 0x34d) run the same 45 bytes as her normal shot; what follows tells them apart.
const Snippet kMercedesDrainA{"Mercedes drain A (param 0x34d)",
    "c4 c1 7a 10 86 94 16 00 00 c5 fa 5c c9 c5 f8 57 c0 c5 f8 2e c1 77 0d c4 c1 7a 10 86 98 16 00 00 c5 fa 5d c1 c4 c1 "
    "7a 11 86 94 16 00 00 49 8b", 0, 45};
const Snippet kMercedesDrainB{"Mercedes drain B (param 0x34d)",
    "c4 c1 7a 10 86 94 16 00 00 c5 fa 5c c9 c5 f8 57 c0 c5 f8 2e c1 77 0d c4 c1 7a 10 86 98 16 00 00 c5 fa 5d c1 c4 c1 "
    "7a 11 86 94 16 00 00 e8", 0, 45};

// The shared HP change: from the lea of the maximum's object to the store of HP (actor in r12, delta in r14d).
const Snippet kHpChange{"HP change",
    "49 8d 9c 24 50 03 00 00 48 89 df 48 8b 00 48 89 45 d0 45 8b ac 24 5c 03 00 00 49 8b 84 24 50 03 00 00 45 01 f5 ff "
    "50 10 44 89 e9 44 39 e8 7f 0b 48 8b 03 48 89 df ff 50 10 89 c1 31 c0 85 c9 78 1c 48 8b 03 48 89 df ff 50 10 89 c1 "
    "44 89 e8 44 39 e9 7f 09 48 8b 03 48 89 df ff 50 10 41 89 84 24 5c 03 00 00", 0, 101};
// The HUD's call to the player accessor, the accessor, and its global: laid out by LinkPlayerAccessor.
const Snippet kHudPlayer{"HUD player read", "e8 2e da fc ff 48 8b 98 70 01 00 00 0f b7 83 cc 04 00 00 83 c0 85 0f b7 c0", 0, 0,
                         true};
const Snippet kPlayerAccessor{"player accessor", "48 8d 05 d9 48 66 00 48 8b 00 48 8b 80 f8 00 00 00 c3", 0, 0, true};
const Snippet kPlayerGlobal{"player global", "00 00 00 00 00 00 00 00", 0, 0, true};

const Snippet* const kSnippets[] = {&kGoldPickup,      &kShopSale,       &kGoldSpend, &kPhozonAbsorbed, &kPhozonSpend,
                                    &kAbilityPurchase, &kApGain,         &kPpSetter,  &kSkillPowCost,   &kCrossbowShot,
                                    &kMercedesDrainA,  &kMercedesDrainB, &kHpChange,  &kHudPlayer,      &kPlayerAccessor,
                                    &kPlayerGlobal};
constexpr size_t kSnippetSpacing = 0x100;

size_t SlotOf(const Snippet& s) {
    return static_cast<size_t>(std::find(std::begin(kSnippets), std::end(kSnippets), &s) - std::begin(kSnippets));
}

// Points the HUD's call at the accessor and the accessor at the global, as the build's own linker did.
void LinkPlayerAccessor(uint8_t* base) {
    const int64_t call = SlotOf(kHudPlayer) * kSnippetSpacing, accessor = SlotOf(kPlayerAccessor) * kSnippetSpacing;
    const int64_t global = SlotOf(kPlayerGlobal) * kSnippetSpacing;
    const int32_t rel = static_cast<int32_t>(accessor - (call + 5)), disp = static_cast<int32_t>(global - (accessor + 7));
    std::memcpy(base + call + 1, &rel, 4);
    std::memcpy(base + accessor + 3, &disp, 4);
}


// The cheat a snippet belongs to, or OSL_CHEAT_COUNT for the game code around the sites that no cheat hooks.
OslCheat OwnerOf(const Snippet& s) {
    if (&s == &kGoldPickup || &s == &kShopSale) return OSL_CHEAT_MAX_GOLD_GAIN;
    if (&s == &kPhozonAbsorbed) return OSL_CHEAT_MAX_PHOZON_GAIN;
    if (&s == &kAbilityPurchase) return OSL_CHEAT_NO_AP_CONSUMPTION;
    if (&s == &kPpSetter) return OSL_CHEAT_INFINITE_PP;
    if (&s == &kSkillPowCost) return OSL_CHEAT_INFINITE_POW;
    if (&s == &kCrossbowShot) return OSL_CHEAT_INFINITE_ARROWS;
    if (&s == &kHpChange) return OSL_CHEAT_INFINITE_HEALTH;
    return OSL_CHEAT_COUNT;
}

constexpr size_t kSlotsSize = 0x1800, kStubsAt = 0x1800, kFlagsAt = 0x2800, kThunkAt = 0x2c00, kEpilogueAt = 0x2e00,
                 kBlockSize = 0x4000;

class ExecutableBlock {
public:
    ExecutableBlock() {
#ifdef _WIN32
        memory_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, kBlockSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
#else
        void* m = mmap(nullptr, kBlockSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memory_ = m == MAP_FAILED ? nullptr : static_cast<uint8_t*>(m);
#endif
        if (memory_) std::memset(memory_, 0xcc, kBlockSize);
    }
    ~ExecutableBlock() {
#ifdef _WIN32
        if (memory_) VirtualFree(memory_, 0, MEM_RELEASE);
#else
        if (memory_) munmap(memory_, kBlockSize);
#endif
    }
    ExecutableBlock(const ExecutableBlock&) = delete;
    ExecutableBlock& operator=(const ExecutableBlock&) = delete;
    uint8_t* data() const { return memory_; }
    void Flush(size_t at, size_t size) const {
#ifdef _WIN32
        FlushInstructionCache(GetCurrentProcess(), memory_ + at, size);
#else
        (void)at;
        (void)size;
#endif
    }

private:
    uint8_t* memory_ = nullptr;
};

struct Registers {
    uint64_t rax = 0, rbx = 0, r12 = 0, r13 = 0, r14 = 0, r15 = 0, rbp = 0;
    bool has_xmm1 = false;
    float xmm1 = 0;
};

// A game object as raw bytes: every byte a sentinel until set, so any stray write shows.
struct Object {
    alignas(16) uint8_t bytes[0x3000];
    Object() { std::memset(bytes, 0xa5, sizeof(bytes)); }
    uint64_t Address() const { return reinterpret_cast<uint64_t>(bytes); }
    void U32(size_t at, uint32_t v) { std::memcpy(bytes + at, &v, 4); }
    void F32(size_t at, float v) { std::memcpy(bytes + at, &v, 4); }
    void Ptr(size_t at, const void* p) {
        const uint64_t v = reinterpret_cast<uint64_t>(p);
        std::memcpy(bytes + at, &v, 8);
    }
    uint32_t U32(size_t at) const {
        uint32_t v;
        std::memcpy(&v, bytes + at, 4);
        return v;
    }
    float F32(size_t at) const {
        float v;
        std::memcpy(&v, bytes + at, 4);
        return v;
    }
};

// Only [at, at + size) may differ between the two.
bool OnlyChanged(const Object& before, const Object& after, size_t at, size_t size) {
    for (size_t i = 0; i < sizeof(before.bytes); ++i) {
        if ((i < at || i >= at + size) && before.bytes[i] != after.bytes[i]) return false;
    }
    return true;
}

void SetCheat(OslCheat cheat, bool on) {
    if (cheat != OSL_CHEAT_COUNT && (runtime_cheat_enabled(cheat) != 0) != on) runtime_cheat_toggle(cheat);
}

// Every snippet in its slot, hooked by the production installer. The switches are the runtime's: a run sees whatever
// state runtime_cheat_toggle left, which is what the game would see.
class SnippetImage {
public:
    SnippetImage() {
        uint8_t* base = block_.data();
        for (size_t i = 0; i < std::size(kSnippets); ++i) {
            const Bytes code = Hex(kSnippets[i]->bytes);
            std::copy(code.begin(), code.end(), base + i * kSnippetSpacing);
        }
        LinkPlayerAccessor(base);
        pristine_.assign(base, base + kSlotsSize);
        const OslCodeRange range{0, kSlotsSize};
        installed_ = osl_install_cheats(base, kSlotsSize, &range, 1, base + kStubsAt, OSL_CHEAT_STUBS_BYTES, base + kFlagsAt, scans_);
        // What ends each snippet: a jump to the epilogue, written once the hooks are in.
        for (const Snippet* s : kSnippets) {
            if (s == &kPpSetter || s->run_size == 0) continue;
            uint8_t* end = base + Slot(*s) * kSnippetSpacing + s->run_at + s->run_size;
            const int32_t rel = static_cast<int32_t>(static_cast<int64_t>(kEpilogueAt) - static_cast<int64_t>(end - base) - 5);
            end[0] = 0xe9;
            std::memcpy(end + 1, &rel, 4);
        }
        const Bytes epilogue = Hex("48 83 c4 08 41 5f 41 5e 41 5d 41 5c 5d 5b c3");
        std::copy(epilogue.begin(), epilogue.end(), base + kEpilogueAt);
        block_.Flush(0, kBlockSize);
        runtime_cheats_start(0);
        runtime_cheats_set_available(installed_, base + kFlagsAt);
    }
    ~SnippetImage() {
        runtime_cheats_set_available(0, nullptr);
        runtime_cheats_start(0);
    }

    static size_t Slot(const Snippet& s) {
        return static_cast<size_t>(std::find(std::begin(kSnippets), std::end(kSnippets), &s) - std::begin(kSnippets));
    }
    unsigned installed() const { return installed_; }
    const OslCheatScan* scans() const { return scans_; }
    uint8_t* base() const { return block_.data(); }
    const Bytes& pristine() const { return pristine_; }
    const uint8_t* Flags() const { return block_.data() + kFlagsAt; }
    uint8_t* Global() const { return block_.data() + Slot(kPlayerGlobal) * kSnippetSpacing; }
    uint8_t* Entry(const Snippet& s) const { return block_.data() + Slot(s) * kSnippetSpacing + s.run_at; }

    // `cheat` selects the behaviour first unless the test drives the runtime state itself.
    void Select(const Snippet& s, bool cheat) const {
        if (auto_select) SetCheat(OwnerOf(s), cheat);
    }
    bool Run(bool cheat, const Snippet& s, const Registers& r) const {
        Select(s, cheat);
        // Six pushes and a sub keep the stack 16-byte aligned for the calls the game code makes.
        Bytes thunk = {0x53, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x08};
        const auto put = [&](uint64_t value, int bytes) {
            for (int i = 0; i < bytes; ++i) thunk.push_back(static_cast<uint8_t>(value >> (8 * i)));
        };
        if (r.has_xmm1) {
            uint32_t bits;
            std::memcpy(&bits, &r.xmm1, 4);
            thunk.push_back(0xb8);
            put(bits, 4);
            thunk.insert(thunk.end(), {0xc5, 0xf9, 0x6e, 0xc8});
        }
        const std::pair<uint16_t, uint64_t> moves[] = {{0x48b8, r.rax}, {0x48bb, r.rbx}, {0x49bc, r.r12}, {0x49bd, r.r13},
                                                       {0x49be, r.r14}, {0x49bf, r.r15}, {0x48bd, r.rbp}};
        for (const auto& [opcode, value] : moves) {
            thunk.push_back(static_cast<uint8_t>(opcode >> 8));
            thunk.push_back(static_cast<uint8_t>(opcode));
            put(value, 8);
        }
        const int32_t rel = static_cast<int32_t>(static_cast<int64_t>(Entry(s) - block_.data()) -
                                                 static_cast<int64_t>(kThunkAt + thunk.size()) - 5);
        thunk.push_back(0xe9);
        put(static_cast<uint32_t>(rel), 4);
        if (thunk.size() > kEpilogueAt - kThunkAt) return false;
        std::copy(thunk.begin(), thunk.end(), block_.data() + kThunkAt);
        block_.Flush(kThunkAt, thunk.size());
        reinterpret_cast<void(__attribute__((sysv_abi)) *)()>(block_.data() + kThunkAt)();
        return true;
    }
    // The game code of a snippet as installed matches what was loaded: nothing hooks it.
    bool Same(const Snippet& s) const {
        const size_t at = Slot(s) * kSnippetSpacing + s.run_at;
        return std::equal(pristine_.begin() + static_cast<std::ptrdiff_t>(at),
                          pristine_.begin() + static_cast<std::ptrdiff_t>(at + s.run_size), block_.data() + at);
    }
    bool auto_select = true;

private:
    ExecutableBlock block_;
    Bytes pristine_;
    unsigned installed_ = 0;
    OslCheatScan scans_[OSL_CHEAT_COUNT];
};

// Infinite Health's stub in the snippet image: the original region, then the port's code reading the snippet image's
// player global.
void CheckHealthStub(const SnippetImage& all) {
    const OslCheatSite& site = osl_cheats[OSL_CHEAT_INFINITE_HEALTH].sites[0];
    const OslCheatScan& scan = all.scans()[OSL_CHEAT_INFINITE_HEALTH];
    CHECK(scan.state == OSL_CHEAT_COMPLETE && scan.sites[0].patch == SnippetImage::Slot(kHpChange) * kSnippetSpacing + 26 &&
          scan.sites[1].at[0] == SnippetImage::Slot(kHudPlayer) * kSnippetSpacing &&
          scan.sites[1].global == SnippetImage::Slot(kPlayerGlobal) * kSnippetSpacing);
    const size_t index = StubIndex(OSL_CHEAT_INFINITE_HEALTH, 0);
    const uint8_t* stub = all.base() + kStubsAt + index * OSL_CHEAT_STUB_BYTES;
    const Bytes region = Hex("49 8b 84 24 50 03 00 00 45 01 f5");
    CHECK(std::equal(region.begin(), region.end(), stub + kStubOriginalAt));
    const uint8_t* changed = stub + kStubOriginalAt + site.region_size + kStubChangedGap;
    for (size_t k = 0; k < site.changed_size; ++k) {
        if (int(k) < site.changed_player_at || int(k) >= site.changed_player_at + 4) CHECK(changed[k] == site.changed[k]);
    }
    int32_t disp;
    std::memcpy(&disp, changed + site.changed_player_at, 4);
    CHECK(changed + site.changed_player_at + 4 + disp == all.Global());
    // After the changed block, the jump back to the instruction after the region.
    int32_t back;
    std::memcpy(&back, changed + site.changed_size + 1, 4);
    CHECK(changed[site.changed_size] == 0xe9 &&
          changed + site.changed_size + 5 + back == all.base() + scan.sites[0].patch + site.region_size);
}

constexpr uint32_t kGoldCap = 999999;

uint32_t Gold(const SnippetImage& image, bool cheat, const Snippet& s, uint32_t gold, int32_t amount, bool* clean) {
    Object player, other, sum;
    player.U32(0x3ac, gold);
    Registers r;
    if (&s == &kGoldPickup) {
        r.rbx = static_cast<uint32_t>(amount);
        r.r15 = player.Address();
    } else if (&s == &kShopSale) {
        other.U32(0x328, static_cast<uint32_t>(amount));
        r.rax = player.Address();
        r.r12 = other.Address();
    } else {
        sum.U32(0x328, static_cast<uint32_t>(amount));
        other.Ptr(0x288, sum.bytes);
        r.rbx = player.Address();
        r.r12 = other.Address();
    }
    const Object before = player;
    CHECK(image.Run(cheat, s, r));
    *clean = OnlyChanged(before, player, 0x3ac, 4);
    return player.U32(0x3ac);
}

bool SemanticGold(const SnippetImage& image) {
    const int before = g_failures;
    bool clean = false;
    for (const Snippet* gain : {&kGoldPickup, &kShopSale}) {
        CHECK(Gold(image, false, *gain, 100, 50, &clean) == 150 && clean);
        CHECK(Gold(image, true, *gain, 100, 50, &clean) == kGoldCap && clean);
        CHECK(Gold(image, false, *gain, 999990, 50, &clean) == kGoldCap && clean);
        CHECK(Gold(image, true, *gain, 999990, 50, &clean) == kGoldCap && clean);
        CHECK(Gold(image, false, *gain, 100, -200, &clean) == 0 && clean);
        CHECK(Gold(image, true, *gain, 100, -200, &clean) == 0 && clean);
    }
    // The sale also clears its pending total, with or without the cheat.
    for (const bool cheat : {false, true}) {
        Object player, sum;
        player.U32(0x3ac, 10);
        sum.U32(0x328, 7);
        Registers r;
        r.rax = player.Address();
        r.r12 = sum.Address();
        CHECK(image.Run(cheat, kShopSale, r) && sum.U32(0x328) == 0);
    }
    // Buying still costs gold.
    CHECK(image.Same(kGoldSpend));
    CHECK(Gold(image, true, kGoldSpend, 100, 30, &clean) == 70 && clean);
    CHECK(Gold(image, true, kGoldSpend, 20, 30, &clean) == 0 && clean);
    // Either site alone is not the cheat: nothing is hooked.
    for (const Snippet* alone : {&kGoldPickup, &kShopSale}) {
        Bytes one = Hex(alone->bytes);
        one.resize(0x100, 0xcc);
        const HookedImage hooked(one);
        CHECK(!(hooked.installed & osl_cheat_bit(OSL_CHEAT_MAX_GOLD_GAIN)) && hooked.Code() == one &&
              hooked.scans[OSL_CHEAT_MAX_GOLD_GAIN].state == OSL_CHEAT_PARTIAL);
    }
    return g_failures == before;
}

uint32_t Phozons(const SnippetImage& image, bool cheat, uint32_t stock, uint32_t cap, bool* clean) {
    Object player;
    player.U32(0x2144, stock);
    player.U32(0x2148, cap);
    const Object before = player;
    Registers r;
    r.rbx = player.Address();
    CHECK(image.Run(cheat, kPhozonAbsorbed, r));
    *clean = OnlyChanged(before, player, 0x2144, 4);
    return player.U32(0x2144);
}

bool SemanticPhozon(const SnippetImage& image) {
    const int before = g_failures;
    bool clean = false;
    CHECK(Phozons(image, false, 10, 99999, &clean) == 11 && clean);
    CHECK(Phozons(image, true, 10, 99999, &clean) == 99999 && clean);
    CHECK(Phozons(image, false, 99999, 99999, &clean) == 99999 && clean);
    CHECK(Phozons(image, true, 99999, 99999, &clean) == 99999 && clean);
    CHECK(Phozons(image, true, 0, 1234, &clean) == 1234 && clean);
    // A 32-bit field: the cheat writes the cap and never wraps; the original wraps a full field to 0.
    CHECK(Phozons(image, true, 0xffffffffu, 99999, &clean) == 99999 && clean);
    CHECK(Phozons(image, false, 0xffffffffu, 99999, &clean) == 0 && clean);
    // Spending phozons on the Ability screen is untouched.
    CHECK(image.Same(kPhozonSpend));
    for (const bool cheat : {false, true}) {
        Object screen, player, entry;
        player.U32(0x2144, 50);
        player.U32(0x2148, 99999);
        entry.U32(0x20, 20);
        screen.Ptr(0x388, player.bytes);
        const Object kept = player;
        Registers r;
        r.r15 = screen.Address();
        r.rax = entry.Address();
        CHECK(image.Run(cheat, kPhozonSpend, r));
        CHECK(player.U32(0x2144) == 30 && OnlyChanged(kept, player, 0x2144, 4));
    }
    return g_failures == before;
}

uint32_t AbilityAp(const SnippetImage& image, bool cheat, bool* clean) {
    Object player, entry;
    player.U32(0x214c, 7);
    player.U32(0x2148, 99999);
    player.U32(0x2150, 3);
    entry.U32(0x20, 3);
    const Object kept = player;
    Registers r;
    r.rax = entry.Address();
    r.rbx = player.Address();
    CHECK(image.Run(cheat, kAbilityPurchase, r));
    *clean = OnlyChanged(kept, player, 0x214c, 4);
    return player.U32(0x214c);
}

bool SemanticAp(const SnippetImage& image) {
    const int before = g_failures;
    bool clean = false;
    CHECK(AbilityAp(image, false, &clean) == 4 && clean);
    CHECK(AbilityAp(image, true, &clean) == 7 && clean);
    // AP earned at a level-up is untouched.
    CHECK(image.Same(kApGain));
    Object player;
    player.U32(0x214c, 7);
    Registers r;
    r.rax = 2;
    r.rbx = player.Address();
    CHECK(image.Run(true, kApGain, r) && player.U32(0x214c) == 9);
    return g_failures == before;
}

int g_max_calls = 0;
__attribute__((sysv_abi)) float ActorMaximum(const void* actor) {
    ++g_max_calls;
    float value;
    std::memcpy(&value, static_cast<const uint8_t*>(actor) + 0x368, 4);
    return value;
}

// Any actor: the setter is the base class's, in the vtables of the player characters and the enemies alike.
float SetPp(const SnippetImage& image, bool cheat, float pp, float value, bool* clean) {
    static void* vtable[0xaa8 / 8];
    vtable[0xaa0 / 8] = reinterpret_cast<void*>(&ActorMaximum);
    Object actor;
    actor.Ptr(0, vtable);
    actor.U32(0x35c, 321);
    actor.F32(0x364, pp);
    actor.F32(0x368, 100.0f);
    const Object before = actor;
    image.Select(kPpSetter, cheat);
    reinterpret_cast<void(__attribute__((sysv_abi)) *)(void*, float)>(image.Entry(kPpSetter))(actor.bytes, value);
    *clean = OnlyChanged(before, actor, 0x364, 4) && actor.U32(0x35c) == 321;
    return actor.F32(0x364);
}

bool SemanticPp(const SnippetImage& image) {
    const int before = g_failures;
    bool clean = false;
    CHECK(SetPp(image, false, 80, 40, &clean) == 40 && clean);
    CHECK(SetPp(image, true, 80, 40, &clean) == 100 && clean);
    CHECK(SetPp(image, false, 20, 60, &clean) == 60 && clean);
    CHECK(SetPp(image, true, 20, 60, &clean) == 100 && clean);
    CHECK(SetPp(image, false, 20, 150, &clean) == 100 && clean);
    CHECK(SetPp(image, true, 20, 150, &clean) == 100 && clean);
    // Below zero is 0 either way: that branch is not intercepted.
    CHECK(SetPp(image, false, 20, -5, &clean) == 0 && clean);
    CHECK(SetPp(image, true, 20, -5, &clean) == 0 && clean);
    return g_failures == before;
}

float Pow(const SnippetImage& image, bool cheat, const Snippet& s, float pow, float cap, float cost, bool* clean) {
    Object player;
    player.F32(0x1694, pow);
    player.F32(0x1698, cap);
    const Object before = player;
    Registers r;
    if (&s == &kSkillPowCost) {
        r.r12 = player.Address();
        r.r13 = static_cast<uint32_t>(static_cast<int32_t>(cost));
    } else {
        r.r14 = player.Address();
        r.has_xmm1 = true;
        r.xmm1 = cost;
    }
    CHECK(image.Run(cheat, s, r));
    *clean = OnlyChanged(before, player, 0x1694, 4);
    return player.F32(0x1694);
}

bool SemanticPow(const SnippetImage& image, const Snippet& s) {
    const int before = g_failures;
    bool clean = false;
    CHECK(Pow(image, false, s, 60, 100, 25, &clean) == 35 && clean);
    CHECK(Pow(image, true, s, 60, 100, 25, &clean) == 100 && clean);
    CHECK(Pow(image, true, s, 60, 80, 25, &clean) == 80 && clean);
    CHECK(Pow(image, false, s, 10, 100, 25, &clean) == 0 && clean);
    CHECK(Pow(image, true, s, 10, 100, 25, &clean) == 0 && clean);
    return g_failures == before;
}

bool SemanticArrows(const SnippetImage& image) {
    const bool shot = SemanticPow(image, kCrossbowShot);
    const int before = g_failures;
    // Mercedes's other two drains are not the cheat's: their bytes and their spending stay as they are.
    for (const Snippet* drain : {&kMercedesDrainA, &kMercedesDrainB}) {
        CHECK(image.Same(*drain));
        bool clean = false;
        CHECK(Pow(image, true, *drain, 60, 100, 25, &clean) == 35 && clean);
    }
    return shot && g_failures == before;
}

int g_max_hp = 115;
__attribute__((sysv_abi)) int StatMaximum(const void*) {
    return g_max_hp;
}

enum class Target { kPlayer, kEnemy, kNoPlayer, kNoManager };

// The HP change through the hook on an actor, with the game's player chain [[[global]+0xf8]+0x170] set for `target`.
int Hp(const SnippetImage& image, bool cheat, Target target, int hp, int delta, bool* clean, int max = 115) {
    static void* stat_vtable[4];
    stat_vtable[2] = reinterpret_cast<void*>(&StatMaximum);
    g_max_hp = max;
    Object actor, enemy, game, manager, scratch;
    actor.Ptr(0x350, stat_vtable);
    actor.U32(0x35c, static_cast<uint32_t>(hp));
    actor.F32(0x364, 40.0f);
    actor.F32(0x368, 80.0f);
    manager.Ptr(0x170, target == Target::kEnemy ? enemy.bytes : actor.bytes);
    game.Ptr(0xf8, target == Target::kNoManager ? nullptr : manager.bytes);
    const uint64_t game_address = target == Target::kNoPlayer ? 0 : game.Address();
    std::memcpy(image.Global(), &game_address, 8);
    uint64_t canary = 0x1234;
    const Object before = actor;
    Registers r;
    r.r12 = actor.Address();
    r.r14 = static_cast<uint32_t>(delta);
    r.r15 = 0; // no damage number
    r.rax = reinterpret_cast<uint64_t>(&canary);
    r.rbp = scratch.Address() + 0x100;
    CHECK(image.Run(cheat, kHpChange, r));
    *clean = OnlyChanged(before, actor, 0x35c, 4) && actor.F32(0x364) == 40.0f;
    const uint64_t none = 0;
    std::memcpy(image.Global(), &none, 8);
    return static_cast<int>(actor.U32(0x35c));
}

bool SemanticHealth(const SnippetImage& image) {
    const int before = g_failures;
    bool clean = false;
    // Damage: 55 of 115, a hit of 20.
    CHECK(Hp(image, false, Target::kPlayer, 55, -20, &clean) == 35 && clean);
    CHECK(Hp(image, true, Target::kPlayer, 55, -20, &clean) == 55 && clean);
    CHECK(Hp(image, false, Target::kPlayer, 55, -20, &clean) == 35 && clean);
    // Healing still raises HP, clamped to the unchanged maximum.
    CHECK(Hp(image, true, Target::kPlayer, 55, 20, &clean) == 75 && clean);
    CHECK(Hp(image, true, Target::kPlayer, 55, 100, &clean) == 115 && clean);
    CHECK(Hp(image, false, Target::kPlayer, 55, 100, &clean) == 115 && clean);
    CHECK(Hp(image, true, Target::kPlayer, 55, 0, &clean) == 55 && clean);
    // A killing hit: 0 without the cheat (the game's death checks read HP 0), HP kept with it.
    CHECK(Hp(image, false, Target::kPlayer, 10, -50, &clean) == 0 && clean);
    CHECK(Hp(image, true, Target::kPlayer, 10, -50, &clean) == 10 && clean);
    // Enemies and every actor that is not the controlled character keep the game's own HP behaviour.
    CHECK(Hp(image, false, Target::kEnemy, 100, -25, &clean) == 75 && clean);
    CHECK(Hp(image, true, Target::kEnemy, 100, -25, &clean) == 75 && clean);
    CHECK(Hp(image, true, Target::kEnemy, 10, -50, &clean) == 0 && clean);
    // Without a player (no game object, no manager yet) the hook does what the game does.
    CHECK(Hp(image, true, Target::kNoPlayer, 55, -20, &clean) == 35 && clean);
    CHECK(Hp(image, true, Target::kNoManager, 55, -20, &clean) == 35 && clean);
    // HP and PP are neighbours: Infinite PP's hook leaves HP alone, Infinite Health's leaves PP alone (clean above).
    SetCheat(OSL_CHEAT_INFINITE_HEALTH, true);
    CHECK(SetPp(image, true, 80, 40, &clean) == 100 && clean);
    CHECK(SetPp(image, false, 80, 40, &clean) == 40 && clean);
    SetCheat(OSL_CHEAT_INFINITE_PP, true);
    CHECK(Hp(image, false, Target::kPlayer, 55, -20, &clean) == 35 && clean);
    CHECK(Hp(image, true, Target::kPlayer, 55, -20, &clean) == 55 && clean);
    SetCheat(OSL_CHEAT_INFINITE_PP, false);
    SetCheat(OSL_CHEAT_INFINITE_HEALTH, false);
    return g_failures == before;
}

// One observable result per cheat, read through the hook with the runtime's current switch.
struct Probe {
    OslCheat cheat;
    float original, cheated;
    float (*run)(const SnippetImage&);
};

float ProbeGold(const SnippetImage& image) {
    bool clean;
    const float pickup = static_cast<float>(Gold(image, runtime_cheat_enabled(OSL_CHEAT_MAX_GOLD_GAIN) != 0, kGoldPickup, 100, 50, &clean));
    const float sale = static_cast<float>(Gold(image, runtime_cheat_enabled(OSL_CHEAT_MAX_GOLD_GAIN) != 0, kShopSale, 100, 50, &clean));
    return pickup == sale ? pickup : -1.0f; // both paths agree or the probe fails
}
float ProbePhozon(const SnippetImage& image) {
    bool clean;
    return static_cast<float>(Phozons(image, runtime_cheat_enabled(OSL_CHEAT_MAX_PHOZON_GAIN) != 0, 10, 99999, &clean));
}
float ProbeAp(const SnippetImage& image) {
    bool clean;
    return static_cast<float>(AbilityAp(image, runtime_cheat_enabled(OSL_CHEAT_NO_AP_CONSUMPTION) != 0, &clean));
}
float ProbePp(const SnippetImage& image) {
    bool clean;
    return SetPp(image, runtime_cheat_enabled(OSL_CHEAT_INFINITE_PP) != 0, 80, 40, &clean);
}
float ProbePow(const SnippetImage& image) {
    bool clean;
    return Pow(image, runtime_cheat_enabled(OSL_CHEAT_INFINITE_POW) != 0, kSkillPowCost, 60, 100, 25, &clean);
}
float ProbeHealth(const SnippetImage& image) {
    bool clean;
    return static_cast<float>(Hp(image, runtime_cheat_enabled(OSL_CHEAT_INFINITE_HEALTH) != 0, Target::kPlayer, 55, -20, &clean));
}
float ProbeArrows(const SnippetImage& image) {
    bool clean;
    return Pow(image, runtime_cheat_enabled(OSL_CHEAT_INFINITE_ARROWS) != 0, kCrossbowShot, 60, 100, 25, &clean);
}

const Probe kProbes[OSL_CHEAT_COUNT] = {
    {OSL_CHEAT_MAX_GOLD_GAIN, 150, float(kGoldCap), ProbeGold}, {OSL_CHEAT_MAX_PHOZON_GAIN, 11, 99999, ProbePhozon},
    {OSL_CHEAT_NO_AP_CONSUMPTION, 4, 7, ProbeAp},               {OSL_CHEAT_INFINITE_PP, 40, 100, ProbePp},
    {OSL_CHEAT_INFINITE_POW, 35, 100, ProbePow},                {OSL_CHEAT_INFINITE_HEALTH, 35, 55, ProbeHealth},
    {OSL_CHEAT_INFINITE_ARROWS, 35, 100, ProbeArrows},
};

// F10's effect, on the real hooks: the menu's toggle changes the next run at once, with no restart and no code write.
bool LiveToggles(SnippetImage& image) {
    const int before = g_failures;
    image.auto_select = false;
    runtime_cheats_start(0);
    runtime_cheats_set_available(image.installed(), image.base() + kFlagsAt);
    const auto expect = [&](unsigned on_mask) {
        for (const Probe& probe : kProbes) {
            const bool on = (on_mask & osl_cheat_bit(probe.cheat)) != 0;
            CHECK(runtime_cheat_enabled(probe.cheat) == (on ? 1 : 0));
            CHECK(probe.run(image) == (on ? probe.cheated : probe.original));
        }
    };
    const Bytes code_before(image.base(), image.base() + kFlagsAt);
    expect(0);
    // Each alone: off, on, off again, on again.
    for (const Probe& probe : kProbes) {
        for (int round = 0; round < 2; ++round) {
            CHECK(runtime_cheat_toggle(probe.cheat) == 1);
            expect(osl_cheat_bit(probe.cheat));
            CHECK(runtime_cheat_toggle(probe.cheat) == 0);
            expect(0);
        }
    }
    // Several at once, then all, then none.
    for (const OslCheat partner : {OSL_CHEAT_INFINITE_PP, OSL_CHEAT_INFINITE_POW, OSL_CHEAT_INFINITE_ARROWS}) {
        runtime_cheat_toggle(OSL_CHEAT_INFINITE_HEALTH);
        runtime_cheat_toggle(partner);
        expect(osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH) | osl_cheat_bit(partner));
        runtime_cheat_toggle(partner);
        expect(osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH));
        runtime_cheat_toggle(OSL_CHEAT_INFINITE_HEALTH);
        expect(0);
    }
    const unsigned mix = osl_cheat_bit(OSL_CHEAT_MAX_GOLD_GAIN) | osl_cheat_bit(OSL_CHEAT_INFINITE_POW) | osl_cheat_bit(OSL_CHEAT_INFINITE_ARROWS);
    for (const Probe& probe : kProbes) {
        if (mix & osl_cheat_bit(probe.cheat)) runtime_cheat_toggle(probe.cheat);
    }
    expect(mix);
    for (const Probe& probe : kProbes) {
        if (!(mix & osl_cheat_bit(probe.cheat))) runtime_cheat_toggle(probe.cheat);
    }
    expect(kAll);
    for (const Probe& probe : kProbes) runtime_cheat_toggle(probe.cheat);
    expect(0);
    // A saved On starts On, and the menu turns it Off and On again.
    runtime_cheats_start(osl_cheat_bit(OSL_CHEAT_INFINITE_PP));
    expect(osl_cheat_bit(OSL_CHEAT_INFINITE_PP));
    runtime_cheat_toggle(OSL_CHEAT_INFINITE_PP);
    expect(0);
    runtime_cheat_toggle(OSL_CHEAT_INFINITE_PP);
    expect(osl_cheat_bit(OSL_CHEAT_INFINITE_PP));
    runtime_cheats_start(osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH));
    expect(osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH));
    runtime_cheat_toggle(OSL_CHEAT_INFINITE_HEALTH);
    expect(0);
    runtime_cheat_toggle(OSL_CHEAT_INFINITE_HEALTH);
    expect(osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH));
    runtime_cheats_start(0);
    // Hundreds of toggles later the executable bytes, the stubs and the epilogue are exactly as installed.
    for (int i = 0; i < 700; ++i) runtime_cheat_toggle(i % OSL_CHEAT_COUNT);
    for (int i = 0; i < 300; ++i) runtime_cheat_toggle(OSL_CHEAT_INFINITE_HEALTH);
    runtime_cheats_start(0);
    CHECK(Bytes(image.base(), image.base() + kFlagsAt) == code_before);
    image.auto_select = true;
    return g_failures == before;
}

struct Validation {
    bool byte_patch = false, semantic = false, live = false;
};

LinkedImage LinkLocal(const GameBuild& build, bool* present) {
    const fs::path app0 = fs::path(OSL_SOURCE_DIR) / "game" / "prepared" / build.folder;
    std::error_code ec;
    *present = fs::is_regular_file(app0 / "eboot.bin", ec);
    if (!*present) return {};
    std::vector<Bytes> modules;
    for (const char* name : kBundledModules) modules.push_back(ReadAll(app0 / "sce_module" / name));
    return LinkExecutable(ReadAll(app0 / "eboot.bin"), modules);
}

std::vector<size_t> FindAll(const Bytes& hay, size_t end, const Bytes& needle) {
    std::vector<size_t> hits;
    const auto last = hay.begin() + static_cast<std::ptrdiff_t>(std::min(end, hay.size()));
    for (auto at = hay.begin(); (at = std::search(at, last, needle.begin(), needle.end())) != last; ++at) {
        hits.push_back(static_cast<size_t>(at - hay.begin()));
    }
    return hits;
}

// The fixtures are the game's bytes: each once in every local build's code, and hooked there exactly as here.
void CheckSnippetsInLocalBuilds(const SnippetImage& all) {
    std::printf("Cheat semantic fixtures in local builds (the game's own instructions):\n");
    for (const GameBuild& build : SupportedBuilds()) {
        bool present = false;
        const LinkedImage linked = LinkLocal(build, &present);
        if (!present) {
            std::printf("  %-26s DATA UNAVAILABLE\n", build.label);
            continue;
        }
        const int before = g_failures;
        CHECK(linked.eboot_error.empty());
        if (!linked.eboot_error.empty()) continue;
        const Bytes& code = linked.image.image;
        const HookedImage hooked(code, EbootCode(linked.image, linked.eboot_size));
        CHECK(hooked.installed == kAll);
        for (const Snippet* s : kSnippets) {
            if (s->relocated) continue;
            const Bytes fixture = Hex(s->bytes);
            const std::vector<size_t> hits = FindAll(code, linked.eboot_size, fixture);
            CHECK(hits.size() == 1);
        }
        CHECK(hooked.scans[OSL_CHEAT_INFINITE_HEALTH].sites[1].global != 0);
        // Every site's hook in this build carries the same instructions, original and changed, as the fixtures'.
        for (const OslCheatInfo& info : osl_cheats) {
            for (size_t s = 0; s < info.site_count; ++s) {
                const OslCheatSite& site = info.sites[s];
                if (site.kind != OSL_CHEAT_SITE_HOOK) continue;
                const size_t n = site.region_size, changed = site.changed ? site.changed_size : n;
                const uint8_t* real = hooked.Stub(StubIndex(info.cheat, s));
                const uint8_t* fixture = all.base() + kStubsAt + StubIndex(info.cheat, s) * OSL_CHEAT_STUB_BYTES;
                CHECK(std::equal(real + kStubOriginalAt, real + kStubOriginalAt + n, fixture + kStubOriginalAt));
                for (size_t k = 0; k < changed; ++k) {
                    const bool rel32 = site.changed && int(k) >= site.changed_player_at && int(k) < site.changed_player_at + 4;
                    if (!rel32) CHECK(real[kStubOriginalAt + n + kStubChangedGap + k] == fixture[kStubOriginalAt + n + kStubChangedGap + k]);
                }
            }
        }
        // The cost each drain reads: the last `mov edi, param` before it. The normal shot's is the drains' minus 3
        // (0x34a and 0x34d in the USA and Europe builds; Japan numbers its parameters differently).
        const auto param_before = [&](const Snippet& s) -> uint32_t {
            const std::vector<size_t> at = FindAll(code, linked.eboot_size, Hex(s.bytes));
            if (at.size() != 1 || at[0] < 0x100) return 0;
            for (size_t i = at[0] - 5; i > at[0] - 0x100; --i) {
                if (code[i] == 0xbf && code[i + 3] == 0 && code[i + 4] == 0) return code[i + 1] | code[i + 2] << 8;
            }
            return 0;
        };
        const uint32_t shot = param_before(kCrossbowShot);
        CHECK(shot != 0 && param_before(kMercedesDrainA) == shot + 3 && param_before(kMercedesDrainB) == shot + 3);
        // The PP setter is in the vtable of every actor class, player characters and enemies.
        size_t actor_vtables = 0;
        const std::vector<size_t> setter = FindAll(code, linked.eboot_size, Hex(kPpSetter.bytes));
        const Bytes boot = ReadAll(fs::path(OSL_SOURCE_DIR) / "build" / "image" / build.folder / "boot.bin");
        if (boot.size() > 128 && setter.size() == 1) {
            const uint64_t size = Le64(boot, 8), segments = Le64(boot, 24), relocs = Le64(boot, 32), names = Le64(boot, 40);
            size_t at = 96;
            at += 8 + Le64(boot, at) * 56;
            at += 8 + Le64(boot, at) * 24 + segments * 24 + names * 128;
            for (uint64_t i = 0; i < relocs && at + i * 32 + 32 <= boot.size() - size; ++i) {
                actor_vtables += Le64(boot, at + i * 32 + 8) == 0 && Le64(boot, at + i * 32 + 16) == setter[0];
            }
            CHECK(actor_vtables > 100);
        }
        std::printf("  %-26s %s (13 snippets once each, the 8 hooks carry the fixtures' instructions; PP setter in %zu vtables)\n",
                    build.label, g_failures == before ? "STATIC PASS" : "FAIL", actor_vtables);
    }
}

void TestSemantics() {
    if (!__builtin_cpu_supports("avx")) {
        std::printf("skipped: the semantic models run AVX code, which the game itself needs\n");
        return;
    }
    SnippetImage all;
    CHECK(all.installed() == kAll);
    for (size_t c = 0; c < OSL_CHEAT_COUNT; ++c) CHECK(all.scans()[c].state == OSL_CHEAT_COMPLETE);
    Validation results[OSL_CHEAT_COUNT];
    for (const OslCheatInfo& info : osl_cheats) {
        const int before = g_failures;
        for (size_t s = 0; s < info.site_count; ++s) {
            const OslCheatSite& site = info.sites[s];
            if (site.id[0] != 'P') continue;
            const Reference& ref = ReferenceOf(site.id);
            const std::vector<size_t> at = FindAll(all.pristine(), all.pristine().size(), Hex(ref.aob));
            CHECK(at.size() == 1);
            if (at.size() != 1) continue;
            const size_t region = static_cast<size_t>(static_cast<int64_t>(at[0]) + site.region_at);
            const Bytes original(all.pristine().begin() + static_cast<std::ptrdiff_t>(region),
                                 all.pristine().begin() + static_cast<std::ptrdiff_t>(region + site.region_size));
            Bytes changed = original;
            const Bytes patched = Hex(ref.patched);
            std::copy(patched.begin(), patched.end(), changed.begin() + static_cast<std::ptrdiff_t>(at[0] + ref.hit - region));
            const uint8_t* stub = all.base() + kStubsAt + StubIndex(info.cheat, s) * OSL_CHEAT_STUB_BYTES;
            CHECK(std::equal(original.begin(), original.end(), stub + kStubOriginalAt));
            CHECK(std::equal(changed.begin(), changed.end(), stub + kStubOriginalAt + site.region_size + kStubChangedGap));
        }
        if (info.cheat == OSL_CHEAT_INFINITE_HEALTH) CheckHealthStub(all);
        results[info.cheat].byte_patch = g_failures == before;
    }
    results[OSL_CHEAT_MAX_GOLD_GAIN].semantic = SemanticGold(all);
    results[OSL_CHEAT_MAX_PHOZON_GAIN].semantic = SemanticPhozon(all);
    results[OSL_CHEAT_NO_AP_CONSUMPTION].semantic = SemanticAp(all);
    results[OSL_CHEAT_INFINITE_PP].semantic = SemanticPp(all);
    results[OSL_CHEAT_INFINITE_POW].semantic = SemanticPow(all, kSkillPowCost);
    results[OSL_CHEAT_INFINITE_HEALTH].semantic = SemanticHealth(all);
    results[OSL_CHEAT_INFINITE_ARROWS].semantic = SemanticArrows(all);
    CHECK(g_max_calls > 0);
    const bool live = LiveToggles(all);
    for (Validation& v : results) v.live = live;
    CheckSnippetsInLocalBuilds(all);

    std::printf("Cheat validation levels: BYTE PATCH = the hooks carry exactly the Issue #20 bytes; HOOK = the hook carries the\n"
                "port's own code for the site, its player test resolved; SEMANTIC MODEL = the game's own instructions run natively\n"
                "through the hooks, original against cheat; LIVE TOGGLE = the runtime switch changes the next run with the\n"
                "executable bytes untouched; GAMEPLAY = manual only\n");
    for (const OslCheatInfo& info : osl_cheats) {
        const Validation& v = results[info.cheat];
        const bool ready = v.byte_patch && v.semantic && v.live;
        const bool own = info.cheat == OSL_CHEAT_INFINITE_HEALTH;
        std::printf("  %-18s %s  %s  %s  gameplay: %s\n", info.label,
                    v.byte_patch ? (own ? "HOOK VERIFIED      " : "BYTE PATCH VERIFIED") : (own ? "HOOK FAILED        " : "BYTE PATCH FAILED  "),
                    v.semantic ? "SEMANTIC MODEL VERIFIED" : "SEMANTIC MODEL FAILED  ",
                    v.live ? "LIVE TOGGLE VERIFIED" : "LIVE TOGGLE FAILED  ", ready ? "READY FOR MANUAL GAMEPLAY TEST" : "NOT READY");
    }
    std::printf("  Infinite PP: applies to every actor that reaches the shared setter, enemies included\n"
                "  Infinite Health: the controlled character only, through the game's shared HP change; enemies keep theirs\n"
                "  Infinite Arrows: Mercedes's normal shot only; her two param 0x34d drains still spend POW\n");
}

} // namespace

int main() {
    TestDefinitions();
    TestSettings();
    TestPage();
    TestLocator();
    TestHooks();
    TestOverlapCheck();
    TestLocalBuilds();
    TestDiagnostics();
    TestSemantics();
    cheat_test::RunMenuTests();
    cheat_test::RunExperienceTests();
    cheat_test::RunClassicTests();
    std::printf("%s (%d checks, %d failures)\n", g_failures ? "FAILED" : "OK", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
