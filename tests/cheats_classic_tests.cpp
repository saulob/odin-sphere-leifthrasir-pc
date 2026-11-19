// The cheats in the Classic engine: its sites in every local build (fingerprint, original bytes, hooked bytes, branch
// targets, overlap), the game's own Classic instructions run natively through the production hooks (original against
// cheat, Off, On, Off again), coexistence, failure paths, and the build x mode x cheat matrix. Headless; game files are
// only read.
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
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "build_scan.h"
#include "cheats_test_support.h"
#include "game_profile.h"
#include "osl_cheats.h"
#include "runtime_cheats.h"
#include "test_support.h"

namespace cheat_test {
namespace {

namespace fs = std::filesystem;
using namespace osl;
using osl_test::Contains;
using osl_test::HexText;

enum Site { kH1, kH2, kH3, kE1, kE2, kW1, kW2, kW3, kW4, kW5, kW6, kW7, kK1, kK2, kP1, kP2, kP3, kG1, kG2, kA1, kA2, kSiteCount };

// What each Classic site must be, written out here so the production table is compared, not shared.
struct Expected {
    const char* id;
    OslCheat cheat; // OSL_CHEAT_COUNT for the Experience Multiplier
    const char* original; // the hooked region; empty for a site that is only located
    const char* changed;  // ?? = a rel32 the installer links
};

// The controlled character's POW stored as its maximum, at every drain the character's pointer is in rbx.
const char kPowFull[] =
    "48 8d 64 24 80 50 48 8b 05 ?? ?? ?? ?? 48 85 c0 74 11 48 39 98 58 01 00 00 75 08 c5 fa 10 83 80 0d 00 00 58 48 8d a4 24 80 00 00 "
    "00 c5 f0 57 c9 c5 fa 11 83 7c 0d 00 00";

const Expected kExpected[kSiteCount] = {
    {"C-H1", OSL_CHEAT_INFINITE_HEALTH, "89 c1 44 29 f9",
     "89 c1 45 85 ff 7e 15 48 8b 15 ?? ?? ?? ?? 48 85 d2 74 09 48 39 9a 58 01 00 00 74 03 44 29 f9"},
    {"C-H2", OSL_CHEAT_INFINITE_HEALTH, "", ""},
    {"C-H3", OSL_CHEAT_INFINITE_HEALTH, "41 89 c6 45 29 b5 c0 07 00 00",
     "41 89 c6 45 85 f6 7e 15 48 8b 05 ?? ?? ?? ?? 48 85 c0 74 09 4c 39 a8 58 01 00 00 74 07 45 29 b5 c0 07 00 00"},
    {"C-E1", OSL_CHEAT_COUNT, "48 89 fb 41 89 f6",
     "48 89 fb 41 89 f6 0f b6 0d ?? ?? ?? ?? 83 f9 01 83 d1 00 4c 0f af f1 b9 ff e0 f5 05 49 39 ce 4c 0f 47 f1"},
    {"C-E2", OSL_CHEAT_COUNT, "", ""},
    {"C-W1", OSL_CHEAT_INFINITE_POW, "c5 f0 57 c9 c5 fa 11 83 7c 0d 00 00", kPowFull},
    {"C-W2", OSL_CHEAT_INFINITE_POW, "", ""},
    {"C-W3", OSL_CHEAT_INFINITE_POW, "c5 f0 57 c9 c4 c1 7a 11 84 24 7c 0d 00 00",
     "48 8d 64 24 80 50 48 8b 05 ?? ?? ?? ?? 48 85 c0 74 13 4c 39 a0 58 01 00 00 75 0a c4 c1 7a 10 84 24 80 0d 00 00 58 48 8d a4 24 80 "
     "00 00 00 c5 f0 57 c9 c4 c1 7a 11 84 24 7c 0d 00 00"},
    {"C-W4", OSL_CHEAT_INFINITE_POW, "c5 f0 57 c9 c5 fa 11 83 7c 0d 00 00", kPowFull},
    {"C-W5", OSL_CHEAT_INFINITE_POW, "c5 f0 57 c9 c5 fa 11 83 7c 0d 00 00", kPowFull},
    {"C-W6", OSL_CHEAT_INFINITE_POW, "c5 f0 57 c9 c5 fa 11 83 7c 0d 00 00", kPowFull},
    {"C-W7", OSL_CHEAT_INFINITE_POW, "c5 fa 10 97 80 0d 00 00 c5 f0 57 c9",
     "c5 fa 10 97 80 0d 00 00 48 8d 64 24 80 50 48 8b 05 ?? ?? ?? ?? 48 85 c0 74 0d 48 39 b8 58 01 00 00 75 04 c5 f8 28 c2 58 48 8d a4 "
     "24 80 00 00 00 c5 f0 57 c9"},
    {"C-K1", OSL_CHEAT_INFINITE_PP, "0f b7 87 c8 07 00 00",
     "48 8b 05 ?? ?? ?? ?? 48 85 c0 74 14 48 39 b8 58 01 00 00 75 0b 0f b7 87 ca 07 00 00 31 f6 eb 07 0f b7 87 c8 07 00 00"},
    {"C-K2", OSL_CHEAT_INFINITE_PP, "", ""},
    {"C-P1", OSL_CHEAT_MAX_PHOZON_GAIN, "0f bf 83 ca 07 00 00 0f bf ce",
     "0f bf 83 ca 07 00 00 0f bf ce 48 8b 15 ?? ?? ?? ?? 48 85 d2 74 10 48 39 9a 58 01 00 00 75 07 66 89 83 c8 07 00 00"},
    {"C-P2", OSL_CHEAT_MAX_PHOZON_GAIN, "48 89 fb 0f b6 83 05 08 00 00",
     "48 89 fb b9 fe e0 f5 05 2b 8b d4 07 00 00 39 f1 0f 4f f1 0f b6 83 05 08 00 00"},
    {"C-P3", OSL_CHEAT_MAX_PHOZON_GAIN, "", ""},
    {"C-G1", OSL_CHEAT_MAX_GOLD_GAIN, "bf 5a 01 00 00 31 f6",
     "31 db 4c 89 ef 89 de ba e7 03 00 00 e8 ?? ?? ?? ?? ff c3 83 fb 05 75 ea bf 5a 01 00 00 31 f6"},
    {"C-G2", OSL_CHEAT_MAX_GOLD_GAIN, "be 23 01 00 00 4c 89 f7",
     "31 db 49 8d be 00 0e 00 00 89 de ba e7 03 00 00 e8 ?? ?? ?? ?? ff c3 83 fb 05 75 e6 be 23 01 00 00 4c 89 f7"},
    {"C-A1", OSL_CHEAT_INFINITE_ARROWS, "03 8b bc 0e 00 00 89 8b bc 0e 00 00",
     "48 8d 64 24 80 52 48 8b 15 ?? ?? ?? ?? 48 85 d2 74 10 48 39 9a 58 01 00 00 75 07 b9 19 00 00 00 eb 06 03 8b bc 0e 00 00 5a "
     "48 8d a4 24 80 00 00 00 89 8b bc 0e 00 00 85 c9"},
    {"C-A2", OSL_CHEAT_INFINITE_ARROWS, "", ""},
};

constexpr bool IsGlobal(Site site) { return site == kH2 || site == kW2 || site == kK2 || site == kP3 || site == kA2; }

// Where each site lies in each local build's linked image: the hooked region's start, the disp32 of a root global's lea,
// the anchor for C-E2. Then the game's set_stack the gold hooks call.
const std::map<std::string, std::array<uint64_t, kSiteCount>> kLocal = {
    {"CUSA05083-1.00", {0x15e5e8, 0x15e636, 0x139623, 0x15e777, 0x15e8a4, 0xf943f, 0xf93ed, 0xf8fe3, 0xf9f80, 0xfa236, 0xfca04, 0xfd620,
                        0x15e750, 0x15e701, 0x15e729, 0x15e8d7, 0x15e918, 0x1b8da3, 0x15ef75, 0xe2bd0, 0xe2930}},
    {"CUSA01290-1.00", {0x15d518, 0x15d566, 0x138683, 0x15d6a7, 0x15d7d4, 0xf83ff, 0xf83ad, 0xf7fa3, 0xf8f40, 0xf91f6, 0xfb9c4, 0xfc5e0,
                        0x15d680, 0x15d631, 0x15d659, 0x15d807, 0x15d848, 0x1b7eb3, 0x15dea5, 0xe1b90, 0xe18f0}},
    {"CUSA01290-1.02", {0x15d828, 0x15d876, 0x138893, 0x15d9b7, 0x15dae4, 0xf85af, 0xf855d, 0xf8153, 0xf90f0, 0xf93a6, 0xfbb74, 0xfc790,
                        0x15d990, 0x15d941, 0x15d969, 0x15db17, 0x15db58, 0x1b84b3, 0x15e1b5, 0xe1d40, 0xe1aa0}},
    {"CUSA05262-1.00", {0x15ea68, 0x15eab6, 0x1397d3, 0x15ebf7, 0x15ed24, 0xf951f, 0xf94cd, 0xf90c3, 0xfa060, 0xfa316, 0xfcae4, 0xfd700,
                        0x15ebd0, 0x15eb81, 0x15eba9, 0x15ed57, 0x15ed98, 0x1b96a3, 0x15f3f5, 0xe2cb0, 0xe2a10}},
    {"CUSA05262-1.01", {0x15eb48, 0x15eb96, 0x1397f3, 0x15ecd7, 0x15ee04, 0xf953f, 0xf94ed, 0xf90e3, 0xfa080, 0xfa336, 0xfcb04, 0xfd720,
                        0x15ecb0, 0x15ec61, 0x15ec89, 0x15ee37, 0x15ee78, 0x1b9783, 0x15f4d5, 0xe2cd0, 0xe2a30}},
};
const std::map<std::string, uint64_t> kLocalSetStack = {
    {"CUSA05083-1.00", 0x13c4a0}, {"CUSA01290-1.00", 0x13b500}, {"CUSA01290-1.02", 0x13b710},
    {"CUSA05262-1.00", 0x13c650}, {"CUSA05262-1.01", 0x13c670},
};
// set_stack's own code: it gives a stack it creates the 999 maximum (mov word [rbx+0xda6],999).
constexpr size_t kSetStackMaximumAt = 0x8e;
const char kSetStackMaximum[] = "66 c7 83 a6 0d 00 00 e7 03";

// The cheat the Classic engine already behaves as, nothing to hook: it has no AP.
constexpr OslCheat kInherent[] = {OSL_CHEAT_NO_AP_CONSUMPTION};
constexpr unsigned kClassicMask = (1u << OSL_CHEAT_MAX_GOLD_GAIN) | (1u << OSL_CHEAT_MAX_PHOZON_GAIN) | (1u << OSL_CHEAT_INFINITE_PP) |
                                  (1u << OSL_CHEAT_INFINITE_POW) | (1u << OSL_CHEAT_INFINITE_HEALTH) | (1u << OSL_CHEAT_INFINITE_ARROWS);
bool IsInherent(OslCheat cheat) { return std::find(std::begin(kInherent), std::end(kInherent), cheat) != std::end(kInherent); }

// Where a found site lies, as kLocal lists it.
uint64_t Located(const OslCheatSite& def, const OslCheatSiteScan& found) {
    return def.kind == OSL_CHEAT_SITE_HOOK ? static_cast<uint64_t>(static_cast<int64_t>(found.at[0]) + def.region_at) : found.patch;
}
constexpr size_t kStubOriginalAt = 24, kStubChangedGap = 14;

Bytes ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

uint64_t Le64(const Bytes& b, size_t at) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | b[at + i];
    return v;
}

// build\image\<build>\boot.bin: the memory image the loader patches.
bool ReadBootImage(const fs::path& path, Bytes& image) {
    const Bytes file = ReadAll(path);
    if (file.size() < 128 || std::memcmp(file.data(), "OSLBOOT1", 8) != 0) return false;
    const uint64_t size = Le64(file, 8), segments = Le64(file, 24), relocs = Le64(file, 32), names = Le64(file, 40);
    size_t at = 96;
    at += 8 + Le64(file, at) * 56;
    at += 8 + Le64(file, at) * 24 + segments * 24 + names * 128;
    if (size > file.size() || at + relocs * 32 > file.size() - size) return false;
    image.assign(file.end() - static_cast<std::ptrdiff_t>(size), file.end());
    return true;
}

// Hex with ?? for bytes that differ from build to build.
struct Pattern {
    Bytes bytes;
    std::vector<bool> any;
};

Pattern Parse(const char* text) {
    Pattern p;
    for (const char* c = text; *c;) {
        if (*c == ' ') {
            ++c;
        } else if (c[0] == '?') {
            p.bytes.push_back(0);
            p.any.push_back(true);
            c += 2;
        } else {
            p.bytes.push_back(Hex(std::string(c, 2).c_str())[0]);
            p.any.push_back(false);
            c += 2;
        }
    }
    return p;
}

bool Matches(const uint8_t* at, const Pattern& p) {
    for (size_t k = 0; k < p.bytes.size(); ++k) {
        if (!p.any[k] && at[k] != p.bytes[k]) return false;
    }
    return true;
}

std::vector<size_t> FindAll(const Bytes& hay, size_t begin, size_t end, const Pattern& p) {
    std::vector<size_t> hits;
    for (size_t at = begin; at + p.bytes.size() <= std::min(end, hay.size()); ++at) {
        if (hay[at] == p.bytes[0] && Matches(hay.data() + at, p)) hits.push_back(at);
    }
    return hits;
}

bool ChangedMatches(const uint8_t* got, const char* expected) {
    return Matches(got, Parse(expected));
}

// The signature as the matcher compares it: masked bytes and a global's disp32 are any byte.
Pattern SignaturePattern(const OslCheatSite& def) {
    Pattern p;
    p.bytes.assign(def.signature, def.signature + def.signature_size);
    p.any.assign(def.signature_size, false);
    for (size_t k = 0; k < def.signature_size; ++k) {
        p.any[k] = (def.mask && def.mask[k] != 'x') ||
                   (def.kind == OSL_CHEAT_SITE_GLOBAL && k >= def.patch_at && k < def.patch_at + def.patch_size);
    }
    return p;
}

std::vector<OslCodeRange> EbootCode(const MappedExecutable& image, uint64_t eboot_size) {
    std::vector<OslCodeRange> ranges;
    for (const MappedSegment& s : image.segments) {
        if ((s.flags & 1) && s.vaddr < eboot_size) ranges.push_back({s.vaddr, std::min(s.vaddr + s.memsz, eboot_size)});
    }
    return ranges;
}

LinkedImage LinkLocal(const GameBuild& build, bool* present) {
    const fs::path app0 = fs::path(OSL_SOURCE_DIR) / "game" / "prepared" / build.folder;
    std::error_code ec;
    *present = fs::is_regular_file(app0 / "eboot.bin", ec);
    if (!*present) return {};
    std::vector<Bytes> modules;
    for (const char* name : kBundledModules) modules.push_back(ReadAll(app0 / "sce_module" / name));
    return LinkExecutable(ReadAll(app0 / "eboot.bin"), modules);
}

size_t HookSites(const OslCheatSite* sites, size_t count) {
    size_t hooks = 0;
    for (size_t s = 0; s < count; ++s) hooks += sites[s].kind == OSL_CHEAT_SITE_HOOK;
    return hooks;
}

// The stub of a cheat's hook in the loader's layout: the Refined engine's cheats, then the Classic engine's.
size_t StubIndex(OslEngine engine, OslCheat cheat, size_t site) {
    size_t index = 0;
    for (int e = 0; e <= static_cast<int>(engine); ++e) {
        for (int c = 0; c < OSL_CHEAT_COUNT; ++c) {
            if (e == static_cast<int>(engine) && c > static_cast<int>(cheat)) return index;
            size_t count = 0;
            const OslCheatSite* sites = osl_cheat_engine_sites(&osl_cheats[c], static_cast<OslEngine>(e), &count);
            const size_t upto = e == static_cast<int>(engine) && c == static_cast<int>(cheat) ? site : count;
            index += HookSites(sites, upto);
        }
    }
    return index;
}

struct Span {
    uint64_t begin, end;
    std::string id;
};

bool Overlap(const Span& a, const Span& b) { return a.begin < b.end && b.begin < a.end; }

// Every located site's code (prefix and signature) in both engines, cheats and Experience Multiplier.
std::vector<Span> AllSpans(const OslCheatScan refined[OSL_CHEAT_COUNT], const OslCheatScan classic[OSL_CHEAT_COUNT],
                           const OslCheatScan& refined_experience, const OslCheatScan& classic_experience) {
    std::vector<Span> spans;
    const auto add = [&](const OslCheatSite* sites, size_t count, const OslCheatScan& scan) {
        for (size_t s = 0; s < count; ++s) {
            if (scan.sites[s].hits != 1) continue;
            const uint64_t at = scan.sites[s].at[0] - sites[s].prefix_size;
            spans.push_back({at, at + sites[s].prefix_size + sites[s].signature_size, sites[s].id});
        }
    };
    for (int c = 0; c < OSL_CHEAT_COUNT; ++c) {
        add(osl_cheats[c].sites, osl_cheats[c].site_count, refined[c]);
        add(osl_cheats[c].classic_sites, osl_cheats[c].classic_site_count, classic[c]);
    }
    add(osl_experience_multiplier.sites, osl_experience_multiplier.site_count, refined_experience);
    add(osl_experience_multiplier.classic_sites, osl_experience_multiplier.classic_site_count, classic_experience);
    return spans;
}

// The hooked regions of every site found once.
std::vector<Span> HookRegions(const OslCheatScan refined[OSL_CHEAT_COUNT], const OslCheatScan classic[OSL_CHEAT_COUNT],
                              const OslCheatScan& refined_experience, const OslCheatScan& classic_experience) {
    std::vector<Span> regions;
    const auto add = [&](const OslCheatSite* sites, size_t count, const OslCheatScan& scan) {
        for (size_t s = 0; s < count; ++s) {
            if (sites[s].kind != OSL_CHEAT_SITE_HOOK || scan.sites[s].hits != 1) continue;
            const uint64_t at = static_cast<uint64_t>(static_cast<int64_t>(scan.sites[s].at[0]) + sites[s].region_at);
            regions.push_back({at, at + sites[s].region_size, sites[s].id});
        }
    };
    for (int c = 0; c < OSL_CHEAT_COUNT; ++c) {
        add(osl_cheats[c].sites, osl_cheats[c].site_count, refined[c]);
        add(osl_cheats[c].classic_sites, osl_cheats[c].classic_site_count, classic[c]);
    }
    add(osl_experience_multiplier.sites, osl_experience_multiplier.site_count, refined_experience);
    add(osl_experience_multiplier.classic_sites, osl_experience_multiplier.classic_site_count, classic_experience);
    return regions;
}

size_t CountOverlaps(const std::vector<Span>& spans) {
    size_t overlaps = 0;
    for (size_t a = 0; a < spans.size(); ++a) {
        for (size_t b = a + 1; b < spans.size(); ++b) overlaps += Overlap(spans[a], spans[b]);
    }
    return overlaps;
}

// Each site's place in its group: H, W, K, P, G and A in their cheat's Classic sites, E in the multiplier's.
constexpr size_t kIndexInGroup[kSiteCount] = {0, 1, 2, 0, 1, 0, 1, 2, 3, 4, 5, 6, 0, 1, 0, 1, 2, 0, 1, 0, 1};

const OslCheatSiteScan& SiteScan(Site site, const OslCheatScan classic[OSL_CHEAT_COUNT], const OslCheatScan& experience) {
    const OslCheat cheat = kExpected[site].cheat;
    return (cheat == OSL_CHEAT_COUNT ? experience : classic[cheat]).sites[kIndexInGroup[site]];
}

const OslCheatSite& SiteDef(Site site) {
    const OslCheat cheat = kExpected[site].cheat;
    return (cheat == OSL_CHEAT_COUNT ? osl_experience_multiplier.classic_sites : osl_cheats[cheat].classic_sites)[kIndexInGroup[site]];
}

bool InRel32(int at, size_t k) { return at >= 0 && k >= static_cast<size_t>(at) && k < static_cast<size_t>(at) + 4; }

// The Classic table: what it holds, per cheat, against the independent expectations above.
void TestDefinitions() {
    CHECK(OSL_ENGINE_COUNT == 2 && OSL_ENGINE_REFINED == 0 && OSL_ENGINE_CLASSIC == 1);
    CHECK(std::string(osl_engine_name(OSL_ENGINE_REFINED)) == "Refined" && std::string(osl_engine_name(OSL_ENGINE_CLASSIC)) == "Classic");
    std::set<std::string> ids;
    for (const OslCheatInfo& info : osl_cheats) {
        for (size_t s = 0; s < info.site_count; ++s) ids.insert(info.sites[s].id);
    }
    const size_t refined_ids = ids.size();
    size_t total = 0;
    for (const OslCheatInfo& info : osl_cheats) {
        size_t count = 0;
        CHECK(osl_cheat_engine_sites(&info, OSL_ENGINE_REFINED, &count) == info.sites && count == info.site_count);
        CHECK(osl_cheat_engine_sites(&info, OSL_ENGINE_CLASSIC, &count) == info.classic_sites && count == info.classic_site_count);
        CHECK(info.classic_site_count <= OSL_CHEAT_MAX_SITES);
        const bool inherent = IsInherent(info.cheat);
        // A cheat has Classic sites or a reason it needs none, never both and never neither.
        CHECK(inherent == (info.classic_site_count == 0) && inherent == (info.classic_inherent != nullptr));
        CHECK(!inherent || std::strlen(info.classic_inherent) > 10);
        for (size_t s = 0; s < info.classic_site_count; ++s) {
            ++total;
            CHECK(ids.insert(info.classic_sites[s].id).second);
            // A mask has one character per signature byte, and the bytes it leaves out are not in a hooked region.
            const OslCheatSite& site = info.classic_sites[s];
            CHECK(!site.mask || std::strlen(site.mask) == site.signature_size);
            for (size_t k = 0; site.mask && k < site.signature_size; ++k) {
                CHECK(site.mask[k] == 'x' || site.mask[k] == '?');
                const bool in_region = static_cast<int64_t>(k) >= site.region_at &&
                                       static_cast<int64_t>(k) < site.region_at + static_cast<int64_t>(site.region_size);
                CHECK(site.mask[k] == 'x' || (!in_region && !(k >= site.patch_at && k < site.patch_at + site.patch_size)));
            }
        }
    }
    total += osl_experience_multiplier.classic_site_count;
    for (size_t s = 0; s < osl_experience_multiplier.classic_site_count; ++s) {
        CHECK(ids.insert(osl_experience_multiplier.classic_sites[s].id).second);
    }
    CHECK(total == kSiteCount && ids.size() == refined_ids + kSiteCount);
    CHECK(osl_experience_multiplier.classic_site_count == 2);

    for (int i = 0; i < kSiteCount; ++i) {
        const Site site = static_cast<Site>(i);
        const OslCheatSite& def = SiteDef(site);
        const Expected& expect = kExpected[i];
        CHECK(std::string(def.id) == expect.id);
        if (IsGlobal(site)) {
            // Only located: a rip-relative lea of the root pointer's global; its disp32 is the patch.
            CHECK(def.kind == OSL_CHEAT_SITE_GLOBAL && def.patch_at == 3 && def.patch_size == 4 && (def.signature[0] & 0xfb) == 0x48 &&
                  def.signature[1] == 0x8d && (def.signature[2] & 0xc7) == 0x05 && !def.replacement && !def.changed &&
                  def.region_size == 0 && def.call_from < 0);
            continue;
        }
        if (site == kE2) {
            CHECK(def.kind == OSL_CHEAT_SITE_SAME_FUNCTION && !def.changed && def.region_size == 0);
            continue;
        }
        const Pattern original = Parse(expect.original), changed = Parse(expect.changed);
        CHECK(def.kind == OSL_CHEAT_SITE_HOOK && !def.replacement && def.changed);
        CHECK(def.region_size == original.bytes.size() && def.region_size >= 5 && def.region_size <= OSL_CHEAT_REGION_MAX);
        CHECK(def.changed_size == changed.bytes.size() && def.changed_size > def.region_size && def.changed_size <= OSL_CHEAT_CHANGED_MAX);
        // The hook replaces the region and nothing else: it is the patch, inside the signature that was verified.
        CHECK(def.region_at >= 0 && def.patch_at >= static_cast<size_t>(def.region_at) &&
              def.patch_at + def.patch_size <= static_cast<size_t>(def.region_at) + def.region_size && def.patch_size >= 1);
        CHECK(def.patch_at + def.patch_size <= def.signature_size && def.prefix_size == 0 && !def.prefix);
        CHECK(Bytes(def.signature + def.region_at, def.signature + def.region_at + def.region_size) == original.bytes);
        for (size_t k = 0; k < def.changed_size; ++k) {
            const bool linked = InRel32(def.changed_player_at, k) || InRel32(def.changed_flag_at, k) || InRel32(def.changed_call_at, k);
            CHECK(linked == changed.any[k]);
            if (!linked) CHECK(def.changed[k] == changed.bytes[k]);
        }
    }
    // What each changed block reads: the controlled character through the root global (Health, POW, PP, the phozon's PP,
    // Arrows), its own switch (the Experience Multiplier), or the game's set_stack (gold), whose call the sale's signature holds.
    const std::pair<Site, int> player_at[] = {{kH1, 10}, {kH3, 11}, {kW1, 9}, {kW3, 9}, {kW4, 9}, {kW5, 9}, {kW6, 9},
                                              {kW7, 17}, {kK1, 3},  {kP1, 13}, {kA1, 9}};
    for (const auto& [site, at] : player_at) CHECK(SiteDef(site).changed_player_at == at && SiteDef(site).changed_flag_at == -1);
    CHECK(SiteDef(kE1).changed_player_at == -1 && SiteDef(kE1).changed_flag_at == 9);
    CHECK(SiteDef(kP2).changed_player_at == -1 && SiteDef(kP2).changed_flag_at == -1 && SiteDef(kP2).changed_call_at == -1);
    CHECK(SiteDef(kG1).call_from == 29 && SiteDef(kG1).signature[29] == 0xe8 && SiteDef(kG1).changed_call_at == 13 &&
          SiteDef(kG2).call_from == -1 && SiteDef(kG2).changed_call_at == 17);
    for (int i = 0; i < kSiteCount; ++i) {
        const Site site = static_cast<Site>(i);
        if (site != kG1 && site != kG2) CHECK(SiteDef(site).changed_call_at == -1 && SiteDef(site).call_from == -1);
    }
    CHECK(OSL_EXPERIENCE_STUBS_BYTES == 2 * OSL_CHEAT_STUB_BYTES && OSL_ENGINE_COUNT == 2);
    std::printf("Classic definitions: %d sites in 7 groups (Max Gold Gain 2, Max Phozon Gain 3, Infinite PP 2, Infinite POW 7, Infinite "
                "Health 3, Infinite Arrows 2, Experience Multiplier 2), 1 cheat inherently satisfied\n", kSiteCount);
}

struct Hooked {
    Bytes memory;
    size_t image_size = 0, exp_stubs_at = 0, flags_at = 0;
    OslCheatScan scans[OSL_ENGINE_COUNT][OSL_CHEAT_COUNT];
    OslCheatScan experience_scans[OSL_ENGINE_COUNT];
    unsigned hooked[OSL_ENGINE_COUNT] = {0, 0};
    unsigned installed = 0, experience_hooked = 0;

    Hooked(const Bytes& image, const std::vector<OslCodeRange>& ranges) : memory(image), image_size(image.size()) {
        exp_stubs_at = image_size + OSL_CHEAT_STUBS_BYTES;
        flags_at = exp_stubs_at + OSL_EXPERIENCE_STUBS_BYTES;
        memory.resize(flags_at + OSL_CHEAT_COUNT + 1, 0);
        installed = osl_install_cheats_engines(memory.data(), image_size, ranges.data(), ranges.size(), memory.data() + image_size,
                                               OSL_CHEAT_STUBS_BYTES, memory.data() + flags_at, scans[OSL_ENGINE_REFINED],
                                               scans[OSL_ENGINE_CLASSIC], hooked);
        experience_hooked = osl_install_experience_engines(memory.data(), image_size, ranges.data(), ranges.size(),
                                                           memory.data() + exp_stubs_at, OSL_EXPERIENCE_STUBS_BYTES,
                                                           memory.data() + flags_at + OSL_CHEAT_COUNT,
                                                           &experience_scans[OSL_ENGINE_REFINED], &experience_scans[OSL_ENGINE_CLASSIC]);
    }
    const uint8_t* Stub(size_t index) const { return memory.data() + image_size + index * OSL_CHEAT_STUB_BYTES; }
    const uint8_t* ExperienceStub(OslEngine engine) const { return memory.data() + exp_stubs_at + static_cast<size_t>(engine) * OSL_CHEAT_STUB_BYTES; }
};

// Bytes inside instructions that only look like a short branch into a region: the disp8 0x7c of the POW drains' vmovss and
// AddPOW's imul constant before the clamp, from the signature's start; each checked against a disassembly of every build.
const std::map<Site, std::vector<int64_t>> kNotBranches = {{kW1, {0xe}}, {kW3, {2, 6}}, {kW4, {4}}, {kW5, {4}}, {kW6, {4}}, {kP1, {-0x4b}}};

bool HasBranchInto(const Bytes& image, const std::vector<OslCodeRange>& ranges, uint64_t start, uint64_t end,
                   const std::set<uint64_t>& not_branches = {}) {
    for (const OslCodeRange& r : ranges) {
        for (uint64_t at = r.begin; at + 6 <= std::min<uint64_t>(r.end, image.size()); ++at) {
            if (not_branches.count(at)) continue;
            const uint8_t op = image[at];
            int64_t target = -1;
            int32_t rel = 0;
            if (op == 0xe8 || op == 0xe9) {
                std::memcpy(&rel, image.data() + at + 1, 4);
                target = static_cast<int64_t>(at) + 5 + rel;
            } else if (op == 0x0f && (image[at + 1] & 0xf0) == 0x80) {
                std::memcpy(&rel, image.data() + at + 2, 4);
                target = static_cast<int64_t>(at) + 6 + rel;
            } else if (op == 0xeb || (op & 0xf0) == 0x70) {
                target = static_cast<int64_t>(at) + 2 + static_cast<int8_t>(image[at + 1]);
            }
            // The region's first byte may be a branch target (the jump now reaches the hook); the rest may not. A byte that
            // only looks like a branch because it lies in the displacement of a call or jump before it is not one.
            bool in_displacement = false;
            for (uint64_t back = 1; back <= 4 && back <= at; ++back) {
                in_displacement = in_displacement || ((image[at - back] == 0xe8 || image[at - back] == 0xe9) && op != 0xe8 && op != 0xe9 && at - back + 5 > at);
            }
            if (!in_displacement && target > static_cast<int64_t>(start) && target < static_cast<int64_t>(end)) return true;
        }
    }
    return false;
}

struct Totals {
    size_t builds = 0;
};

// Every local build: each Classic site once in the eboot's code, at the expected place with the expected bytes; the
// hooks the production installer lays down carry those bytes and the right switches; nothing overlaps.
void TestLocalBuilds(Totals* totals) {
    std::printf("Classic cheat build parity (real files = game\\prepared eboot and build\\image boot.bin; DATA UNAVAILABLE is never PASS):\n");
    for (const GameBuild& build : SupportedBuilds()) {
        bool present = false;
        const LinkedImage linked = LinkLocal(build, &present);
        if (!present) {
            std::printf("  %-26s DATA UNAVAILABLE (no prepared eboot.bin; the signature scan is generic)\n", build.label);
            continue;
        }
        const int before = g_failures;
        CHECK(linked.eboot_error.empty());
        if (!linked.eboot_error.empty()) continue;
        const Bytes& image = linked.image.image;
        const std::vector<OslCodeRange> code = EbootCode(linked.image, linked.eboot_size);
        const auto expected = kLocal.find(build.folder);
        CHECK(expected != kLocal.end());
        if (expected == kLocal.end()) continue;

        Hooked hooked(image, code);
        CHECK(hooked.installed == OSL_CHEAT_ALL && hooked.hooked[OSL_ENGINE_REFINED] == OSL_CHEAT_ALL &&
              hooked.hooked[OSL_ENGINE_CLASSIC] == kClassicMask);
        CHECK(hooked.experience_hooked == 3);
        OslCheatScan refined[OSL_CHEAT_COUNT], classic[OSL_CHEAT_COUNT];
        osl_cheat_scan_engine(image.data(), image.size(), code.data(), code.size(), OSL_ENGINE_REFINED, refined);
        osl_cheat_scan_engine(image.data(), image.size(), code.data(), code.size(), OSL_ENGINE_CLASSIC, classic);
        OslCheatScan refined_experience, classic_experience;
        osl_experience_scan_engine(image.data(), image.size(), code.data(), code.size(), OSL_ENGINE_REFINED, &refined_experience);
        osl_experience_scan_engine(image.data(), image.size(), code.data(), code.size(), OSL_ENGINE_CLASSIC, &classic_experience);

        for (int c = 0; c < OSL_CHEAT_COUNT; ++c) {
            CHECK(refined[c].state == OSL_CHEAT_COMPLETE);
            const bool inherent = IsInherent(static_cast<OslCheat>(c));
            CHECK(classic[c].state == (inherent ? OSL_CHEAT_INHERENT : OSL_CHEAT_COMPLETE));
            CHECK(!inherent || std::string(classic[c].reason) == osl_cheats[c].classic_inherent);
            CHECK(classic[c].reason[0] == 0 || inherent);
        }
        CHECK(refined_experience.state == OSL_CHEAT_COMPLETE && classic_experience.state == OSL_CHEAT_COMPLETE);

        for (int i = 0; i < kSiteCount; ++i) {
            const Site site = static_cast<Site>(i);
            const OslCheatSiteScan& found = SiteScan(site, classic, classic_experience);
            const OslCheatSite& def = SiteDef(site);
            CHECK(found.state == OSL_CHEAT_SITE_UNIQUE && found.hits == 1 && Located(def, found) == expected->second[static_cast<size_t>(i)]);
            // The fingerprint as the game's bytes: once in the whole eboot, nowhere else.
            const std::vector<size_t> hits = FindAll(image, 0, linked.eboot_size, SignaturePattern(def));
            CHECK(hits.size() == 1 && hits[0] == found.at[0]);
            // The original instructions, written independently, and nothing branching into their middle.
            if (def.kind == OSL_CHEAT_SITE_HOOK) {
                const uint64_t region = Located(def, found);
                const Pattern original = Parse(kExpected[i].original);
                CHECK(std::equal(original.bytes.begin(), original.bytes.end(), image.begin() + static_cast<std::ptrdiff_t>(region)));
                std::set<uint64_t> not_branches;
                const auto listed = kNotBranches.find(site);
                for (const int64_t at : listed == kNotBranches.end() ? std::vector<int64_t>{} : listed->second) {
                    not_branches.insert(static_cast<uint64_t>(static_cast<int64_t>(found.at[0]) + at));
                }
                CHECK(!HasBranchInto(image, code, region, region + original.bytes.size(), not_branches));
            }
        }
        // The five root globals the hooks read are one, in writable data of the eboot.
        const uint64_t global = SiteScan(kH2, classic, classic_experience).global;
        for (const Site site : {kW2, kK2, kP3, kA2}) CHECK(SiteScan(site, classic, classic_experience).global == global);
        bool writable = false;
        for (const MappedSegment& s : linked.image.segments) {
            writable = writable || ((s.flags & 2) && global >= s.vaddr && global + 8 <= s.vaddr + s.memsz && global < linked.eboot_size);
        }
        CHECK(global != 0 && writable);
        // The function the gold hooks call is the game's set_stack, by its address and by its own code.
        const uint64_t set_stack = classic[OSL_CHEAT_MAX_GOLD_GAIN].sites[0].callee;
        const auto expected_set_stack = kLocalSetStack.find(build.folder);
        CHECK(expected_set_stack != kLocalSetStack.end() && set_stack == expected_set_stack->second);
        const Bytes maximum = Hex(kSetStackMaximum);
        CHECK(set_stack != 0 && std::equal(maximum.begin(), maximum.end(), image.begin() + static_cast<std::ptrdiff_t>(set_stack + kSetStackMaximumAt)));

        const std::vector<Span> spans = AllSpans(refined, classic, refined_experience, classic_experience);
        const std::vector<Span> regions = HookRegions(refined, classic, refined_experience, classic_experience);
        CHECK(spans.size() == 9 + 2 + kSiteCount && regions.size() == 24);
        CHECK(CountOverlaps(spans) == 0 && CountOverlaps(regions) == 0);

        // The hooks as installed: a jump per region, the stub holding the original then the changed instructions, the
        // switch of the stub's own cheat, and nothing else in the code changed.
        for (int i = 0; i < kSiteCount; ++i) {
            const Site site = static_cast<Site>(i);
            const OslCheatSite& def = SiteDef(site);
            if (def.kind != OSL_CHEAT_SITE_HOOK) continue;
            const OslCheat cheat = kExpected[i].cheat;
            const uint8_t* stub;
            size_t flag_index;
            if (cheat == OSL_CHEAT_COUNT) {
                stub = hooked.ExperienceStub(OSL_ENGINE_CLASSIC);
                flag_index = OSL_CHEAT_COUNT;
            } else {
                size_t site_in_cheat = 0;
                for (size_t s = 0; s < osl_cheats[cheat].classic_site_count; ++s) {
                    if (std::string(osl_cheats[cheat].classic_sites[s].id) == kExpected[i].id) site_in_cheat = s;
                }
                stub = hooked.Stub(StubIndex(OSL_ENGINE_CLASSIC, cheat, site_in_cheat));
                flag_index = static_cast<size_t>(cheat);
            }
            const uint64_t region = expected->second[static_cast<size_t>(i)];
            const Pattern original = Parse(kExpected[i].original);
            const uint8_t* at = hooked.memory.data() + region;
            int32_t jump;
            std::memcpy(&jump, at + 1, 4);
            CHECK(at[0] == 0xe9 && at + 5 + jump == stub);
            for (size_t k = 5; k < original.bytes.size(); ++k) CHECK(at[k] == 0x90);
            CHECK(std::equal(original.bytes.begin(), original.bytes.end(), stub + kStubOriginalAt));
            const uint8_t* changed = stub + kStubOriginalAt + original.bytes.size() + kStubChangedGap;
            CHECK(ChangedMatches(changed, kExpected[i].changed));
            // The stub tests this cheat's switch: lea rsp; pushfq; cmp byte [rip+disp],0.
            int32_t flag;
            std::memcpy(&flag, stub + 8, 4);
            CHECK(Bytes(stub, stub + 8) == Hex("48 8d 64 24 80 9c 80 3d") && stub + 13 + flag == hooked.memory.data() + hooked.flags_at + flag_index);
            // The block returns to the instruction after the region.
            const size_t changed_size = Parse(kExpected[i].changed).bytes.size();
            int32_t back;
            std::memcpy(&back, changed + changed_size + 1, 4);
            CHECK(changed[changed_size] == 0xe9 && changed + changed_size + 5 + back == at + original.bytes.size());
        }
        // The root global linked into every block that tells the controlled character from the others; set_stack into the
        // gold blocks.
        const auto linked_to = [&](OslCheat cheat, size_t site, int at) {
            const OslCheatSite& def = osl_cheats[cheat].classic_sites[site];
            const uint8_t* changed = hooked.Stub(StubIndex(OSL_ENGINE_CLASSIC, cheat, site)) + kStubOriginalAt + def.region_size + kStubChangedGap;
            int32_t disp;
            std::memcpy(&disp, changed + at, 4);
            return static_cast<uint64_t>(changed + at + 4 + disp - hooked.memory.data());
        };
        for (const Site site : {kH1, kH3, kW1, kW3, kW4, kW5, kW6, kW7, kK1, kP1, kA1}) {
            CHECK(linked_to(kExpected[site].cheat, kIndexInGroup[site], SiteDef(site).changed_player_at) == global);
        }
        CHECK(linked_to(OSL_CHEAT_MAX_GOLD_GAIN, 0, SiteDef(kG1).changed_call_at) == set_stack);
        CHECK(linked_to(OSL_CHEAT_MAX_GOLD_GAIN, 1, SiteDef(kG2).changed_call_at) == set_stack);
        std::vector<Span> allowed = regions;
        for (size_t at = 0; at < image.size(); ++at) {
            if (image[at] == hooked.memory[at]) continue;
            CHECK(std::any_of(allowed.begin(), allowed.end(), [&](const Span& r) { return at >= r.begin && at < r.end; }));
        }
        // Every byte of the original image outside the hooked regions is as the game has it, the Classic buy and every
        // other spend included.
        CHECK(Bytes(hooked.memory.begin(), hooked.memory.begin() + static_cast<std::ptrdiff_t>(image.size())) != image);

        // The same image as the loader maps it: boot.bin, byte for byte in every site.
        Bytes boot;
        if (ReadBootImage(fs::path(OSL_SOURCE_DIR) / "build" / "image" / build.folder / "boot.bin", boot)) {
            OslCheatScan boot_classic[OSL_CHEAT_COUNT];
            osl_cheat_scan_engine(boot.data(), boot.size(), code.data(), code.size(), OSL_ENGINE_CLASSIC, boot_classic);
            OslCheatScan boot_experience;
            osl_experience_scan_engine(boot.data(), boot.size(), code.data(), code.size(), OSL_ENGINE_CLASSIC, &boot_experience);
            for (int i = 0; i < kSiteCount; ++i) {
                const Site site = static_cast<Site>(i);
                CHECK(Located(SiteDef(site), SiteScan(site, boot_classic, boot_experience)) == expected->second[static_cast<size_t>(i)]);
            }
        }

        const bool pass = g_failures == before;
        totals->builds += pass;
        std::printf("  %-26s %s (Classic: 6 cheats and the Experience Multiplier, %d sites once each, 1 cheat inherently satisfied; "
                    "no overlap in %zu sites)\n", build.label, pass ? "PASS" : "FAIL", kSiteCount, spans.size());
    }
    std::printf("Classic cheat build parity: %zu build(s) verified on real files\n", totals->builds);
}

// The game's instructions, hooked by the production installer and run natively with synthetic objects.
constexpr size_t kSlot = 0x200, kSlotCount = 14, kGlobalAt = kSlot * kSlotCount, kInsertResultAt = kGlobalAt + 8,
                 kShotCostsAt = kGlobalAt + 0x10, kGuardCostAt = kGlobalAt + 0x20, kMockAt = kGlobalAt + 0x40, kMockSize = 0x20,
                 kMockCount = 5, kImageSize = kMockAt + kMockCount * kMockSize, kStubsAt = 0x2000,
                 kExperienceStubsAt = kStubsAt + OSL_CHEAT_STUBS_BYTES, kFlagsAt = kExperienceStubsAt + OSL_EXPERIENCE_STUBS_BYTES + 0x100,
                 kThunkAt = kFlagsAt + 0x100, kBlockSize = 0x5000;
static_assert(kImageSize <= kStubsAt && kThunkAt + 0x200 <= kBlockSize);

enum Slot { kDamage, kHit, kExp, kPoints, kAddPow, kSale, kPickup, kShot, kDrain, kAttack, kGuard, kGuardHeld, kSpend, kSet };
// Stand-ins for the game functions the fixtures call: a plain return, the purse's count getter and set_stack (one dword per
// coin at purse+0x10, clamped to 999 as the game's setCount does), the purse insert (adds a 1 G coin when it accepts) and the
// item's category (a coin).
enum Mock { kReturn, kGetCount, kSetStack, kInsert, kCategory };
const char* const kMockCode[kMockCount] = {"c3", "8b 44 b7 10 c3", "81 fa e7 03 00 00 76 05 ba e7 03 00 00 89 54 b7 10 c3",
                                           "8a 05 ?? ?? ?? ?? 84 c0 74 03 ff 47 20 c3", "b8 0a 00 00 00 c3"};
constexpr size_t MockAt(Mock m) { return kMockAt + static_cast<size_t>(m) * kMockSize; }

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
    void Flush() const {
#ifdef _WIN32
        FlushInstructionCache(GetCurrentProcess(), memory_, kBlockSize);
#endif
    }

private:
    uint8_t* memory_ = nullptr;
};

struct Object {
    alignas(16) uint8_t bytes[0x1200]; // the character fields reach +0x1129
    Object() { std::memset(bytes, 0xa5, sizeof(bytes)); }
    uint64_t Address() const { return reinterpret_cast<uint64_t>(bytes); }
    void U32(size_t at, uint32_t v) { std::memcpy(bytes + at, &v, 4); }
    void U16(size_t at, uint16_t v) { std::memcpy(bytes + at, &v, 2); }
    void U8(size_t at, uint8_t v) { bytes[at] = v; }
    void Ptr(size_t at, const void* p) {
        const uint64_t v = reinterpret_cast<uint64_t>(p);
        std::memcpy(bytes + at, &v, 8);
    }
    uint32_t U32(size_t at) const {
        uint32_t v;
        std::memcpy(&v, bytes + at, 4);
        return v;
    }
    uint16_t U16(size_t at) const {
        uint16_t v;
        std::memcpy(&v, bytes + at, 2);
        return v;
    }
};

bool OnlyChanged(const Object& before, const Object& after, std::initializer_list<std::pair<size_t, size_t>> fields) {
    for (size_t i = 0; i < sizeof(before.bytes); ++i) {
        bool allowed = false;
        for (const auto& [at, size] : fields) allowed = allowed || (i >= at && i < at + size);
        if (!allowed && before.bytes[i] != after.bytes[i]) return false;
    }
    return true;
}

struct Registers {
    uint64_t rax = 0, rbx = 0, rcx = 0, rdx = 0, rsi = 0, rdi = 0, rbp = 0, r12 = 0, r13 = 0, r14 = 0, r15 = 0;
    float xmm0 = 0, xmm1 = 0;
};

// The Classic game's own instructions, as in every local build (found below with the rel32 fields masked).
const char kDamageCode[] =
    "8b 83 c0 07 00 00 89 c1 44 29 f9 89 8b c0 07 00 00 0f b7 93 8c 07 00 00 83 fa 3b 77 1a 66 c7 83 8c 07 00 00 00 00 0f b7 93 8e "
    "07 00 00 44 01 fa 66 89 93 8e 07 00 00 ba 13 01 00 00 c4 e2 68 f7 93 a8 0b 00 00 39 d1 7d 08 89 d1 89 93 c0 07 00 00 48 8d 15 "
    "?? ?? ?? ?? 48 8b 12 48 39 9a 58 01 00 00 75 11 29 c8 7e 0d 48 8b 92 d8 00 00 00 01 82 54 01 00 00";
const char kHitCode[] = "48 8b 4d b0 41 89 c6 45 29 b5 c0 07 00 00 f6 41 25 01";
const char kExperienceStartCode[] =
    "55 48 89 e5 41 56 53 48 89 fb 41 89 f6 0f b7 83 4e 0c 00 00 83 f8 02 75 2e be 16 00 00 00 48 89 df e8 ?? ?? ?? ?? 84 c0 74 1d "
    "44 89 f0 c4 e1 fa 2a c0 c5 fa 59 05 ?? ?? ?? ?? c5 fa 58 05 ?? ?? ?? ?? c4 61 fa 2c f0 0f b6 83 04 08 00 00 83 f8 62 75 11 8b "
    "83 d0 07 00 00 89 83 cc 07 00 00 e9 da 00 00 00 8b 8b cc 07 00 00 ba ff e0 f5 05 29 ca 44 39 f2 0f 86 ba 00 00 00 44 01 f1 89 "
    "8b cc 07 00 00";
const char kExperienceEndCode[] = "c7 83 cc 07 00 00 ff e0 f5 05 5b 41 5e 5d c3";
// AddPP up to its read of the root global; the fixture stops right after the store of the total.
const char kPointsStartCode[] =
    "55 48 89 e5 41 56 53 48 89 fb 0f b6 83 05 08 00 00 83 f8 62 75 0e 8b 83 d8 07 00 00 89 83 d4 07 00 00 eb 70 8b 8b d4 07 00 00 "
    "ba ff e0 f5 05 29 ca 39 f2 76 55 01 f1 89 8b d4 07 00 00 3b 8b d8 07 00 00 72 4f 4c 8d 35 ?? ?? ?? ?? eb 08 66 90 8a 83 05 08 00 "
    "00 0f b6 c0 83 f8 61 77 36";
constexpr size_t kPointsStoredAt = 0x3d, kPointsRootAt = 0x48;
const char kPointsEndCode[] = "c7 83 d4 07 00 00 ff e0 f5 05 5b 41 5e 5d c3";
// AddPOW (PP plus the phozon's amount, a sound for the controlled character, the clamp to the maximum; the sound id at 0x69
// differs on Japan 1.00), its padding, then SubPOW.
const char kAddPowCode[] =
    "55 48 89 e5 53 50 48 89 fb 0f b7 83 c8 07 00 00 48 0f bf c8 01 c6 0f bf c6 48 69 c9 1f 85 eb 51 66 89 b3 c8 07 00 00 48 98 48 69 "
    "c0 1f 85 eb 51 48 89 ca 48 c1 f9 25 48 c1 ea 3f 01 d1 48 89 c2 48 c1 f8 25 48 c1 ea 3f 01 d0 39 c1 7d 2b 48 8d 05 ?? ?? ?? ?? 48 "
    "8b 00 48 39 98 58 01 00 00 75 18 48 8b b8 b8 00 00 00 be 45 07 00 00 e8 ?? ?? ?? ?? 0f b7 b3 c8 07 00 00 0f bf 83 ca 07 00 00 0f "
    "bf ce 39 c1 7e 07 66 89 83 c8 07 00 00 48 83 c4 08 5b 5d c3 66 66 2e 0f 1f 84 00 00 00 00 00 0f b7 87 c8 07 00 00 31 c9 29 f0 "
    "66 85 c0 66 0f 49 c8 66 89 8f c8 07 00 00 c3";
constexpr size_t kAddPowRootAt = 0x51, kAddPowSoundAt = 0x6e, kAbsorbedAt = 0x79, kSubPowAt = 0xa0;
const char kPowSpentCode[] = "0f b7 87 c8 07 00 00 31 c9 29 f0 66 85 c0 66 0f 49 c8 66 89 8f c8 07 00 00 c3";
// POW (the stamina gauge): a whole drain function (character in rdi, the cost in xmm0), the attack's drain (character in
// r12, cost in xmm1), the two guard drains (character in rbx, -0.1 a frame), another drain (rbx, cost in xmm1) and the setter.
const char kDrainCode[] =
    "55 48 89 e5 41 56 53 48 83 ec 10 48 89 fb 41 89 f6 f6 83 b8 0b 00 00 20 75 78 48 8d 05 ?? ?? ?? ?? 48 8b 00 48 8b b8 c8 00 00 00 "
    "48 85 ff 74 33 48 83 c7 10 be aa 01 00 00 c5 fa 11 45 ec e8 ?? ?? ?? ?? c5 fa 10 45 ec 85 c0 74 17 45 84 f6 75 12 c5 fa 59 05 ?? "
    "?? ?? ?? c5 fa 58 83 7c 0d 00 00 eb 0c c5 fa 10 8b 7c 0d 00 00 c5 f2 5c c0 c5 f0 57 c9 c5 fa 11 83 7c 0d 00 00 c5 f8 2e c8 72 11 "
    "80 8b b8 0b 00 00 20 c7 83 7c 0d 00 00 00 00 00 00 48 83 c4 10 5b 41 5e 5d c3";
constexpr size_t kDrainRootAt = 0x1d, kDrainCallAt = 0x3f, kDrainFactorAt = 0x55, kDrainRegionAt = 0x6f;
const char kAttackCode[] =
    "c4 c1 7a 10 84 24 7c 0d 00 00 c5 fa 5c c1 c5 f0 57 c9 c4 c1 7a 11 84 24 7c 0d 00 00 c5 f8 2e c8 72 15 41 80 8c 24 b8 0b 00 00 20 "
    "41 c7 84 24 7c 0d 00 00 00 00 00 00";
const char kGuardCode[] =
    "c5 fa 10 83 7c 0d 00 00 c5 fa 58 05 ?? ?? ?? ?? c5 f0 57 c9 c5 fa 11 83 7c 0d 00 00 c5 f8 2e c8 72 11 80 8b b8 0b 00 00 20 c7 83 "
    "7c 0d 00 00 00 00 00 00 be 10 00 00 00 48 89 df e8 ?? ?? ?? ??";
const char kGuardHeldCode[] =
    "c5 fa 10 83 7c 0d 00 00 c5 fa 58 05 ?? ?? ?? ?? c5 f0 57 c9 c5 fa 11 83 7c 0d 00 00 c5 f8 2e c8 72 11 80 8b b8 0b 00 00 20 c7 83 "
    "7c 0d 00 00 00 00 00 00 44 8b 75 18 44 8b 7d 10";
constexpr size_t kGuardCostLinkAt = 12, kGuardDoneAt = 0x33;
const char kSpendCode[] =
    "c5 fa 10 83 7c 0d 00 00 c5 fa 5c c1 c5 f0 57 c9 c5 fa 11 83 7c 0d 00 00 c5 f8 2e c8 72 11 80 8b b8 0b 00 00 20 c7 83 7c 0d 00 00 "
    "00 00 00 00 41 f6 47 24 20";
constexpr size_t kSpendDoneAt = 0x2f;
const char kSetCode[] = "c5 fa 10 97 80 0d 00 00 c5 f0 57 c9 c5 f2 5f c0 c5 ea 5d c0 c5 fa 11 87 7c 0d 00 00 c3";
constexpr float kGuardCost = -0.1f;
// A sale's ones credit (count in ebx, purse in r13), then the code both credits join.
const char kSaleCode[] =
    "85 db 74 1e be 04 00 00 00 4c 89 ef e8 ?? ?? ?? ?? 01 c3 be 04 00 00 00 4c 89 ef 89 da e8 ?? ?? ?? ?? bf 5a 01 00 00 31 f6 ba ff "
    "ff ff ff 31 c9 e8 ?? ?? ?? ??";
// A pickup (character in r14, item in rbx): a coin, by the controlled character, taken by the purse; the vtable slot
// at 8 differs on Japan 1.00.
const char kPickupCode[] =
    "48 8b 03 48 89 df ff 90 a8 03 00 00 83 f8 0a 75 38 48 8d 05 ?? ?? ?? ?? 45 31 ff 48 8b 00 4c 39 b0 58 01 00 00 75 69 49 8d be 00 "
    "0e 00 00 48 89 de e8 ?? ?? ?? ?? 84 c0 74 56 be 23 01 00 00 4c 89 f7 e8 ?? ?? ?? ?? eb 47";
constexpr size_t kPickupOtherAt = 0x49, kPickupDoneAt = 0x90;
// Mercedes's fire function: its read of the root global, and 0x282 bytes later the shot's cost (character in rbx, shot
// kind in r12d, the cost table through rdx).
const char kShotRootCode[] = "4c 8d 2d ?? ?? ?? ?? be 02 00 00 00 ba 01 00 00 00 49 8b 45 00 48 8b b8 f0 00 00 00 e8";
const char kShotCode[] =
    "83 bb b8 0e 00 00 5a b9 f6 ff ff ff 0f 94 c0 74 0d 49 63 cc 48 8d 15 ?? ?? ?? ?? 8b 0c 8a 03 8b bc 0e 00 00 89 8b bc 0e 00 00 7e "
    "15 83 f9 1a 7c 21 c7 83 bc 0e 00 00 19 00 00 00 eb 15 31 c0 eb 11 c6 83 29 11 00 00 00 c7 83 bc 0e 00 00 00 00 00 00 c7 83 b8 0e "
    "00 00 00 00 00 00";
constexpr size_t kShotAt = 0x100;
constexpr int32_t kShotCosts[] = {-1, -3, -10, -3};

// A fixture chunk and where, from its start, each located site lies in it.
struct Chunk {
    const char* name;
    const char* code;
    std::vector<std::pair<Site, size_t>> sites;
    std::vector<size_t> differs; // bytes that differ in the Japan builds and belong to no site
};

const Chunk kChunks[] = {
    {"damage", kDamageCode, {{kH1, 6}, {kH2, 0x54}}, {0x71}},
    {"hit", kHitCode, {{kH3, 4}}, {}},
    {"experience start", kExperienceStartCode, {{kE1, 7}}, {}},
    {"experience end", kExperienceEndCode, {{kE2, 0}}, {}},
    {"phozon points", kPointsStartCode, {{kP2, 7}, {kP3, kPointsRootAt}}, {}},
    {"phozon points end", kPointsEndCode, {}, {}},
    {"AddPOW and SubPOW", kAddPowCode, {{kK2, kAddPowRootAt}, {kP1, kAbsorbedAt}, {kK1, kSubPowAt}}, {0x69, 0x6a}},
    {"POW drain", kDrainCode, {{kW2, kDrainRootAt}, {kW1, kDrainRegionAt}}, {}},
    {"POW attack", kAttackCode, {{kW3, 14}}, {}},
    {"POW guard", kGuardCode, {{kW4, 16}}, {}},
    {"POW guard held", kGuardHeldCode, {{kW5, 16}}, {}},
    {"POW spend", kSpendCode, {{kW6, 12}}, {}},
    {"POW set", kSetCode, {{kW7, 0}}, {}},
    {"sale", kSaleCode, {{kG1, 34}}, {}},
    {"coin pickup", kPickupCode, {{kG2, 58}}, {8}},
    {"shot root", kShotRootCode, {{kA2, 3}}, {}},
    {"shot", kShotCode, {{kA1, 30}}, {}},
};

Bytes Concrete(const char* masked) { return Parse(masked).bytes; }

void Rel32(uint8_t* base, size_t at, size_t target) {
    const int32_t rel = static_cast<int32_t>(static_cast<int64_t>(target) - static_cast<int64_t>(at + 4));
    std::memcpy(base + at, &rel, 4);
}

void Jump(uint8_t* base, size_t from, size_t target) {
    base[from] = 0xe9;
    Rel32(base, from + 1, target);
}

constexpr size_t SlotAt(Slot s) { return static_cast<size_t>(s) * kSlot; }

class ClassicImage {
public:
    ClassicImage() {
        uint8_t* base = block_.data();
        const auto put = [&](size_t at, const char* masked) {
            const Bytes code = Concrete(masked);
            std::copy(code.begin(), code.end(), base + at);
            return code.size();
        };
        ends_[kDamage] = put(SlotAt(kDamage), kDamageCode);
        Rel32(base, SlotAt(kDamage) + 0x54, kGlobalAt);
        ends_[kHit] = put(SlotAt(kHit), kHitCode);
        ends_[kExp] = put(SlotAt(kExp), kExperienceStartCode);
        put(SlotAt(kExp) + 0x134, kExperienceEndCode);
        put(SlotAt(kPoints), kPointsStartCode);
        ends_[kPoints] = kPointsStoredAt;
        Rel32(base, SlotAt(kPoints) + kPointsRootAt, kGlobalAt);
        put(SlotAt(kPoints) + 0x8a, kPointsEndCode);
        ends_[kAddPow] = put(SlotAt(kAddPow), kAddPowCode);
        Rel32(base, SlotAt(kAddPow) + kAddPowRootAt, kGlobalAt);
        Rel32(base, SlotAt(kAddPow) + kAddPowSoundAt, MockAt(kReturn));
        ends_[kDrain] = put(SlotAt(kDrain), kDrainCode);
        Rel32(base, SlotAt(kDrain) + kDrainRootAt, kGlobalAt);
        Rel32(base, SlotAt(kDrain) + kDrainCallAt, MockAt(kReturn));
        Rel32(base, SlotAt(kDrain) + kDrainFactorAt, kGuardCostAt);
        ends_[kAttack] = put(SlotAt(kAttack), kAttackCode);
        put(SlotAt(kGuard), kGuardCode);
        put(SlotAt(kGuardHeld), kGuardHeldCode);
        for (const Slot s : {kGuard, kGuardHeld}) {
            ends_[s] = kGuardDoneAt;
            Rel32(base, SlotAt(s) + kGuardCostLinkAt, kGuardCostAt);
        }
        put(SlotAt(kSpend), kSpendCode);
        ends_[kSpend] = kSpendDoneAt;
        ends_[kSet] = put(SlotAt(kSet), kSetCode);
        std::memcpy(base + kGuardCostAt, &kGuardCost, sizeof(kGuardCost));
        ends_[kSale] = put(SlotAt(kSale), kSaleCode);
        Rel32(base, SlotAt(kSale) + 13, MockAt(kGetCount));
        Rel32(base, SlotAt(kSale) + 30, MockAt(kSetStack));
        Rel32(base, SlotAt(kSale) + 49, MockAt(kReturn));
        ends_[kPickup] = put(SlotAt(kPickup), kPickupCode);
        Rel32(base, SlotAt(kPickup) + 20, kGlobalAt);
        Rel32(base, SlotAt(kPickup) + 50, MockAt(kInsert));
        Rel32(base, SlotAt(kPickup) + 67, MockAt(kReturn));
        put(SlotAt(kShot), kShotRootCode);
        Rel32(base, SlotAt(kShot) + 3, kGlobalAt);
        ends_[kShot] = kShotAt + put(SlotAt(kShot) + kShotAt, kShotCode);
        entries_[kShot] = kShotAt;
        Rel32(base, SlotAt(kShot) + kShotAt + 23, kShotCostsAt);
        std::memcpy(base + kShotCostsAt, kShotCosts, sizeof(kShotCosts));
        for (size_t m = 0; m < kMockCount; ++m) put(kMockAt + m * kMockSize, kMockCode[m]);
        Rel32(base, MockAt(kInsert) + 2, kInsertResultAt);
        InsertAccepts(true);
        const OslCodeRange range{0, kImageSize};
        installed_ = osl_install_cheats_engines(base, kImageSize, &range, 1, base + kStubsAt, OSL_CHEAT_STUBS_BYTES, base + kFlagsAt,
                                                scans_[OSL_ENGINE_REFINED], scans_[OSL_ENGINE_CLASSIC], hooked_);
        experience_hooked_ = osl_install_experience_engines(base, kImageSize, &range, 1, base + kExperienceStubsAt, OSL_EXPERIENCE_STUBS_BYTES,
                                                            base + kFlagsAt + OSL_CHEAT_COUNT, &experience_scans_[0], &experience_scans_[1]);
        // What ends each fixture, written once the hooks are in.
        for (const Slot s : {kDamage, kHit, kSale, kShot, kAttack, kGuard, kGuardHeld, kSpend}) base[SlotAt(s) + ends_[s]] = 0xc3;
        Jump(base, SlotAt(kExp) + ends_[kExp], SlotAt(kExp) + 0x13e);
        Jump(base, SlotAt(kPoints) + ends_[kPoints], SlotAt(kPoints) + 0x94);
        base[SlotAt(kPickup) + kPickupOtherAt] = 0xc3;
        base[SlotAt(kPickup) + kPickupDoneAt] = 0xc3;
        Root(nullptr);
        block_.Flush();
        runtime_cheats_start(0);
        runtime_cheats_set_available(installed_, base + kFlagsAt);
        runtime_experience_start(OSL_EXPERIENCE_OFF);
        runtime_experience_set_available(experience_hooked_ != 0, base + kFlagsAt + OSL_CHEAT_COUNT);
    }
    ~ClassicImage() {
        runtime_cheats_set_available(0, nullptr);
        runtime_cheats_start(0);
        runtime_experience_set_available(0, nullptr);
        runtime_experience_start(OSL_EXPERIENCE_OFF);
    }

    unsigned installed() const { return installed_; }
    unsigned hooked(OslEngine e) const { return hooked_[e]; }
    unsigned experience_hooked() const { return experience_hooked_; }
    const OslCheatScan& Scan(OslEngine e, int c) const { return scans_[e][c]; }
    const OslCheatScan& ExperienceScan(OslEngine e) const { return experience_scans_[e]; }
    void Root(const Object* root) const {
        const uint64_t value = root ? root->Address() : 0;
        std::memcpy(block_.data() + kGlobalAt, &value, 8);
    }
    void InsertAccepts(bool accepts) const { block_.data()[kInsertResultAt] = accepts ? 1 : 0; }
    const void* StandIn(Mock m) const { return block_.data() + MockAt(m); }
    // The executable bytes as installed: the fixtures, the stand-ins, the stubs and the switches (not the data the runs set).
    Bytes Installed() const {
        Bytes code(block_.data(), block_.data() + kGlobalAt);
        code.insert(code.end(), block_.data() + kMockAt, block_.data() + kFlagsAt + OSL_CHEAT_COUNT + 1);
        return code;
    }

    // Runs the fixture from its entry, or from `at` in its slot.
    bool Run(Slot slot, const Registers& r, size_t at = SIZE_MAX) const {
        Bytes thunk = {0x53, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x08};
        const auto put = [&](uint64_t value, int bytes) {
            for (int i = 0; i < bytes; ++i) thunk.push_back(static_cast<uint8_t>(value >> (8 * i)));
        };
        // mov eax,imm32; vmovd xmm0,eax; mov eax,imm32; vmovd xmm1,eax
        for (const auto& [value, modrm] : {std::pair<float, uint8_t>{r.xmm0, 0xc0}, {r.xmm1, 0xc8}}) {
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            thunk.push_back(0xb8);
            put(bits, 4);
            for (const uint8_t b : {uint8_t{0xc5}, uint8_t{0xf9}, uint8_t{0x6e}, modrm}) thunk.push_back(b);
        }
        const std::pair<uint16_t, uint64_t> moves[] = {{0x48b8, r.rax}, {0x48bb, r.rbx}, {0x48b9, r.rcx}, {0x48ba, r.rdx},
                                                       {0x48be, r.rsi}, {0x48bf, r.rdi}, {0x48bd, r.rbp}, {0x49bc, r.r12},
                                                       {0x49bd, r.r13}, {0x49be, r.r14}, {0x49bf, r.r15}};
        for (const auto& [opcode, value] : moves) {
            thunk.push_back(static_cast<uint8_t>(opcode >> 8));
            thunk.push_back(static_cast<uint8_t>(opcode));
            put(value, 8);
        }
        const size_t entry = SlotAt(slot) + (at == SIZE_MAX ? entries_[slot] : at);
        const int32_t rel = static_cast<int32_t>(static_cast<int64_t>(entry) - static_cast<int64_t>(kThunkAt + thunk.size()) - 5);
        thunk.push_back(0xe8);
        put(static_cast<uint32_t>(rel), 4);
        for (const uint8_t b : {0x48, 0x83, 0xc4, 0x08, 0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5d, 0x41, 0x5c, 0x5d, 0x5b, 0xc3}) thunk.push_back(b);
        if (kThunkAt + thunk.size() > kBlockSize) return false;
        std::copy(thunk.begin(), thunk.end(), block_.data() + kThunkAt);
        block_.Flush();
        reinterpret_cast<void(__attribute__((sysv_abi)) *)()>(block_.data() + kThunkAt)();
        return true;
    }

private:
    ExecutableBlock block_;
    size_t ends_[kSlotCount] = {}, entries_[kSlotCount] = {};
    unsigned installed_ = 0, hooked_[OSL_ENGINE_COUNT] = {0, 0}, experience_hooked_ = 0;
    OslCheatScan scans_[OSL_ENGINE_COUNT][OSL_CHEAT_COUNT];
    OslCheatScan experience_scans_[OSL_ENGINE_COUNT];
};

void SetCheat(OslCheat cheat, bool on) {
    if ((runtime_cheat_enabled(cheat) != 0) != on) runtime_cheat_toggle(cheat);
}

void SetExperience(int value) { runtime_experience_start(value); }

// Who the root's controlled character ([[root]+0x158]) is: the object the code works on, another one, nobody, or no root.
enum class Who { kPlayer, kEnemy, kNoPlayer, kNoRoot };

// The root as `who` says, for the object `self`; `other` stands for the controlled character when `self` is not it.
void SetRoot(const ClassicImage& image, Object& root, Who who, const Object& self, const Object& other) {
    root.Ptr(0x158, who == Who::kPlayer ? self.bytes : who == Who::kEnemy ? other.bytes : nullptr);
    image.Root(who == Who::kNoRoot ? nullptr : &root);
}

// The damage routine through the hook; `*taken` receives the damage the game counts for the player.
int32_t Damage(const ClassicImage& image, Who who, int32_t hp, int32_t damage, bool* clean, uint32_t* taken = nullptr) {
    Object character, other, root, stats;
    character.U32(0x7c0, static_cast<uint32_t>(hp));
    character.U16(0x78c, 0x100);
    character.U16(0x78e, 0);
    character.U32(0xba8, 0);
    stats.U32(0x154, 0);
    root.Ptr(0xd8, stats.bytes);
    SetRoot(image, root, who == Who::kNoRoot ? Who::kNoPlayer : who, character, other);
    const Object before = character;
    Registers r;
    r.rbx = character.Address();
    r.r15 = static_cast<uint32_t>(damage);
    CHECK(image.Run(kDamage, r));
    image.Root(nullptr);
    *clean = OnlyChanged(before, character, {{0x7c0, 4}});
    if (taken) *taken = stats.U32(0x154);
    return static_cast<int32_t>(character.U32(0x7c0));
}

int32_t ModelHp(bool protect, int32_t hp, int32_t damage) {
    if (protect && damage > 0) return hp;
    return std::max<int32_t>(hp - damage, 0);
}

// An attack's hit on `target` (HP subtracted in place, no floor) with `controlled` as the root's controlled character.
int32_t Hit(const ClassicImage& image, Object& target, const Object* controlled, bool root_present, int32_t hp, int32_t damage, bool* clean) {
    Object root, attack, frame;
    target.U32(0x7c0, static_cast<uint32_t>(hp));
    attack.U8(0x25, 1);
    frame.Ptr(0x80 - 0x50, attack.bytes);
    root.Ptr(0x158, controlled ? controlled->bytes : nullptr);
    image.Root(root_present ? &root : nullptr);
    const Object before = target;
    Registers r;
    r.rax = static_cast<uint32_t>(damage);
    r.r13 = target.Address();
    r.rbp = frame.Address() + 0x80;
    CHECK(image.Run(kHit, r));
    image.Root(nullptr);
    *clean = OnlyChanged(before, target, {{0x7c0, 4}});
    return static_cast<int32_t>(target.U32(0x7c0));
}

// A Psypher skill's PP cost through SubPOW.
uint32_t PpSpend(const ClassicImage& image, Who who, uint16_t pp, uint16_t maximum, uint32_t cost, bool* clean) {
    Object character, other, root;
    character.U16(0x7c8, pp);
    character.U16(0x7ca, maximum);
    SetRoot(image, root, who, character, other);
    const Object before = character;
    Registers r;
    r.rdi = character.Address();
    r.rsi = cost;
    CHECK(image.Run(kAddPow, r, kSubPowAt));
    image.Root(nullptr);
    *clean = OnlyChanged(before, character, {{0x7c8, 2}});
    return character.U16(0x7c8);
}

uint16_t ModelPpSpend(bool refill, uint16_t pp, uint16_t maximum, uint32_t cost) {
    if (refill) return maximum;
    const int32_t left = static_cast<int16_t>(static_cast<uint16_t>(pp - cost));
    return left < 0 ? 0 : static_cast<uint16_t>(left);
}

// A phozon's amount added to PP through AddPOW (the absorb gives it to whoever absorbs).
uint16_t Absorb(const ClassicImage& image, Who who, uint16_t pp, uint16_t maximum, uint16_t amount, bool* clean) {
    Object character, other, root;
    character.U16(0x7c8, pp);
    character.U16(0x7ca, maximum);
    SetRoot(image, root, who, character, other);
    const Object before = character;
    Registers r;
    r.rdi = character.Address();
    r.rsi = amount;
    CHECK(image.Run(kAddPow, r, 0));
    image.Root(nullptr);
    *clean = OnlyChanged(before, character, {{0x7c8, 2}});
    return character.U16(0x7c8);
}

uint16_t ModelAbsorb(bool full, uint16_t pp, uint16_t maximum, uint16_t amount) {
    if (full) return maximum;
    return static_cast<uint16_t>(std::min<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(pp + amount)), static_cast<int16_t>(maximum)));
}

struct Stamina {
    float value;
    bool exhausted; // the bit the game sets when POW runs out
    bool operator==(const Stamina& o) const { return value == o.value && exhausted == o.exhausted; }
};

constexpr float kStaminaMax = 100.0f;

// One drain of POW at `slot` (the cost is ignored by the guard drains, which take 0.1).
Stamina Drain(const ClassicImage& image, Slot slot, Who who, float stamina, float cost, bool* clean) {
    Object character, other, root;
    character.U8(0xbb8, 0);
    character.U32(0xd7c, 0);
    std::memcpy(character.bytes + 0xd7c, &stamina, 4);
    std::memcpy(character.bytes + 0xd80, &kStaminaMax, 4);
    root.Ptr(0xc8, nullptr); // no game state to ask: the whole drain function takes its plain subtraction
    SetRoot(image, root, who, character, other);
    const Object before = character;
    Registers r;
    r.rdi = r.rbx = r.r12 = character.Address();
    (slot == kDrain ? r.xmm0 : r.xmm1) = cost;
    CHECK(image.Run(slot, r));
    image.Root(nullptr);
    *clean = OnlyChanged(before, character, {{0xd7c, 4}, {0xbb8, 1}}) && (character.bytes[0xbb8] & ~0x20) == 0;
    float value;
    std::memcpy(&value, character.bytes + 0xd7c, 4);
    return {value, (character.bytes[0xbb8] & 0x20) != 0};
}

Stamina ModelDrain(bool full, Slot slot, float stamina, float cost) {
    if (full) return {kStaminaMax, false};
    const float left = slot == kGuard || slot == kGuardHeld ? stamina + kGuardCost : stamina - cost;
    return 0.0f < left ? Stamina{left, false} : Stamina{0.0f, true};
}

// The POW setter, which status effects call with 0.
float SetPow(const ClassicImage& image, Who who, float stamina, float value, bool* clean) {
    Object character, other, root;
    std::memcpy(character.bytes + 0xd7c, &stamina, 4);
    std::memcpy(character.bytes + 0xd80, &kStaminaMax, 4);
    SetRoot(image, root, who, character, other);
    const Object before = character;
    Registers r;
    r.rdi = character.Address();
    r.xmm0 = value;
    CHECK(image.Run(kSet, r));
    image.Root(nullptr);
    *clean = OnlyChanged(before, character, {{0xd7c, 4}});
    float got;
    std::memcpy(&got, character.bytes + 0xd7c, 4);
    return got;
}

float ModelSetPow(bool full, float value) { return full ? kStaminaMax : std::min(kStaminaMax, std::max(0.0f, value)); }

struct Ammo {
    uint32_t count;
    uint8_t loaded; // the flag the game clears when the ammunition runs out
};

// One shot of `kind` (charged: the -10 cost) with `count` arrows.
Ammo Shot(const ClassicImage& image, Who who, uint32_t count, int kind, bool charged, bool* clean) {
    Object character, other, root;
    character.U32(0xebc, count);
    character.U32(0xeb8, charged ? 0x5a : 0);
    character.U8(0x1129, 1);
    SetRoot(image, root, who, character, other);
    const Object before = character;
    Registers r;
    r.rbx = character.Address();
    r.r12 = static_cast<uint32_t>(kind);
    CHECK(image.Run(kShot, r));
    image.Root(nullptr);
    *clean = OnlyChanged(before, character, {{0xebc, 4}, {0xeb8, 4}, {0x1129, 1}}) && character.U32(0xeb8) == 0;
    return {character.U32(0xebc), character.bytes[0x1129]};
}

Ammo ModelShot(bool full, uint32_t count, int kind, bool charged) {
    if (full) return {25, 1};
    const int32_t left = static_cast<int32_t>(count) + (charged ? -10 : kShotCosts[kind]);
    if (left <= 0) return {0, 0};
    return {static_cast<uint32_t>(std::min(left, 25)), 1};
}

uint32_t Experience(const ClassicImage& image, uint32_t total, uint32_t award, bool* clean) {
    Object character;
    character.U16(0xc4e, 0);
    character.U8(0x804, 5);
    character.U32(0x7cc, total);
    character.U32(0x7d0, 5000000);
    const Object before = character;
    Registers r;
    r.rdi = character.Address();
    r.rsi = award;
    CHECK(image.Run(kExp, r));
    *clean = OnlyChanged(before, character, {{0x7cc, 4}});
    return character.U32(0x7cc);
}

constexpr uint32_t kExperienceMax = 99999999, kPointsMax = 99999999;

uint32_t ModelExperience(uint32_t total, uint32_t award, int multiplier) {
    uint64_t gained = award;
    if (multiplier > 1) gained = std::min<uint64_t>(gained * static_cast<uint64_t>(multiplier), kExperienceMax);
    return kExperienceMax - total <= gained ? kExperienceMax : static_cast<uint32_t>(total + gained);
}

// AddPP up to its store, at PP level `level` (98 = the last, where the game pins the total to the level's).
uint32_t Points(const ClassicImage& image, uint32_t points, uint32_t next, uint32_t amount, bool* clean, uint8_t level = 3) {
    Object character;
    character.U8(0x805, level);
    character.U32(0x7d4, points);
    character.U32(0x7d8, next);
    const Object before = character;
    Registers r;
    r.rdi = character.Address();
    r.rsi = amount;
    CHECK(image.Run(kPoints, r));
    *clean = OnlyChanged(before, character, {{0x7d4, 4}});
    return character.U32(0x7d4);
}

// The total AddPP stores: the cheat raises the amount to one below the cap (the cap path stores without levelling up);
// the game's level-up loop that follows then takes the character to level 98.
uint32_t ModelPoints(bool full, uint32_t points, uint32_t next, uint32_t amount, uint8_t level = 3) {
    if (level == 0x62) return next;
    if (full) amount = static_cast<uint32_t>(std::max<int32_t>(static_cast<int32_t>(amount), static_cast<int32_t>(kPointsMax - 1 - points)));
    return kPointsMax - points <= amount ? kPointsMax : points + amount;
}

// The purse's five coins, worth 10, 5, 20, 10 and 1 G, 999 at most each.
constexpr uint32_t kCoinValue[5] = {10, 5, 20, 10, 1};
constexpr uint32_t kCoinMax = 999, kGoldMax = 999 * (10 + 5 + 20 + 10 + 1);

using Purse = std::array<uint32_t, 5>;

uint32_t Gold(const Purse& purse) {
    uint32_t total = 0;
    for (size_t d = 0; d < purse.size(); ++d) total += purse[d] * kCoinValue[d];
    return total;
}

void PutPurse(Object& object, size_t at, const Purse& purse) {
    for (size_t d = 0; d < purse.size(); ++d) object.U32(at + 0x10 + d * 4, purse[d]);
}

Purse GetPurse(const Object& object, size_t at) {
    Purse purse;
    for (size_t d = 0; d < purse.size(); ++d) purse[d] = object.U32(at + 0x10 + d * 4);
    return purse;
}

// A sale's ones credit (`ones` coins of 1 G) into the purse.
Purse Sale(const ClassicImage& image, const Purse& start, uint32_t ones, bool* clean) {
    Object purse;
    PutPurse(purse, 0, start);
    const Object before = purse;
    Registers r;
    r.rbx = ones;
    r.r13 = purse.Address();
    CHECK(image.Run(kSale, r));
    *clean = OnlyChanged(before, purse, {{0x10, 5 * 4}});
    return GetPurse(purse, 0);
}

Purse ModelSale(bool full, const Purse& start, uint32_t ones) {
    if (full) return {kCoinMax, kCoinMax, kCoinMax, kCoinMax, kCoinMax};
    Purse purse = start;
    if (ones) purse[4] = std::min(purse[4] + ones, kCoinMax);
    return purse;
}

// A coin picked up by the character whom `who` names; `accepted`: the purse takes it (a 1 G coin here).
Purse Pickup(const ClassicImage& image, Who who, const Purse& start, bool accepted, bool* clean) {
    Object character, other, root, item, vtable;
    PutPurse(character, 0xe00, start);
    vtable.Ptr(0x3a8, image.StandIn(kCategory));
    item.Ptr(0, vtable.bytes);
    SetRoot(image, root, who, character, other);
    image.InsertAccepts(accepted);
    const Object before = character;
    Registers r;
    r.rbx = item.Address();
    r.r14 = character.Address();
    CHECK(image.Run(kPickup, r));
    image.Root(nullptr);
    image.InsertAccepts(true);
    *clean = OnlyChanged(before, character, {{0xe10, 5 * 4}});
    return GetPurse(character, 0xe00);
}

Purse ModelPickup(bool full, Who who, const Purse& start, bool accepted) {
    if (who != Who::kPlayer || !accepted) return start;
    if (full) return {kCoinMax, kCoinMax, kCoinMax, kCoinMax, kCoinMax};
    Purse purse = start;
    ++purse[4];
    return purse;
}

const Purse kPurses[] = {{0, 0, 0, 0, 0}, {1, 1, 1, 1, 1}, {998, 998, 998, 998, 998}, {999, 999, 999, 999, 999},
                         {0, 999, 1, 998, 0}, {999, 0, 999, 0, 998}};

bool SemanticGold(const ClassicImage& image) {
    const int before = g_failures;
    bool clean = false;
    CHECK(kGoldMax == 45954 && Gold({kCoinMax, kCoinMax, kCoinMax, kCoinMax, kCoinMax}) == kGoldMax);
    for (const bool cheat : {false, true, false, true, false}) {
        SetCheat(OSL_CHEAT_MAX_GOLD_GAIN, cheat);
        for (const Purse& start : kPurses) {
            // Every coin from empty, one, one below and at its 999: a sale (with or without ones to credit) and a pickup.
            for (const uint32_t ones : {0u, 1u, 7u, 998u, 5000u}) {
                const Purse got = Sale(image, start, ones, &clean);
                CHECK(got == ModelSale(cheat, start, ones) && clean);
                CHECK(Gold(got) <= kGoldMax && (!cheat || Gold(got) == kGoldMax));
                for (const uint32_t coin : got) CHECK(coin <= kCoinMax);
            }
            const bool full_ones = start[4] >= kCoinMax; // the game refuses a coin its stack has no room for
            CHECK(Pickup(image, Who::kPlayer, start, !full_ones, &clean) == ModelPickup(cheat, Who::kPlayer, start, !full_ones) && clean);
            // Another character's pickup, a coin the purse refuses, no controlled character: nothing is credited.
            CHECK(Pickup(image, Who::kEnemy, start, true, &clean) == start && clean);
            CHECK(Pickup(image, Who::kPlayer, start, false, &clean) == start && clean);
            CHECK(Pickup(image, Who::kNoPlayer, start, true, &clean) == start && clean);
        }
    }
    return g_failures == before;
}

bool SemanticPhozon(const ClassicImage& image) {
    const int before = g_failures;
    bool clean = false;
    struct Sample {
        uint32_t points, next, amount;
        uint8_t level;
    };
    static const Sample kSamples[] = {
        {0, 60, 10, 0},        {40, 100, 10, 3},     {40, 100, 80, 3},       {99, 100, 0, 3},
        {1578000, 1578290, 50, 97}, {99999990, 99999999, 5, 3}, {99999998, 99999999, 0, 3}, {99999999, 99999999, 5, 3},
        {1578290, 1578290, 40, 0x62}, {0, 1578290, 7, 0x62},  {10, 60, 0x7fffffff, 3},
    };
    for (const bool cheat : {false, true, false, true}) {
        SetCheat(OSL_CHEAT_MAX_PHOZON_GAIN, cheat);
        for (const Sample& s : kSamples) {
            const uint32_t got = Points(image, s.points, s.next, s.amount, &clean, s.level);
            CHECK(got == ModelPoints(cheat, s.points, s.next, s.amount, s.level) && clean);
            // Never past the cap, never lower than the original would leave it, never the cap path that skips levelling.
            CHECK(got <= kPointsMax && got >= ModelPoints(false, s.points, s.next, s.amount, s.level));
        }
        // One absorb, then more: the total stays where the first put it (the game's level 98 pins it afterwards).
        CHECK(Points(image, 40, 100, 10, &clean) == (cheat ? kPointsMax - 1 : 50u));
        CHECK(Points(image, kPointsMax - 1, 1578290, 10, &clean, 0x62) == 1578290);
        // The absorb's PP: the controlled character's fills to its maximum; anyone else's gets the amount, clamped.
        static const std::array<uint16_t, 3> kPp[] = {{0, 100, 7}, {350, 900, 50}, {880, 900, 50}, {900, 900, 9}, {50, 1000, 0}, {199, 200, 300}};
        for (const auto& [pp, maximum, amount] : kPp) {
            CHECK(Absorb(image, Who::kPlayer, pp, maximum, amount, &clean) == ModelAbsorb(cheat, pp, maximum, amount) && clean);
            CHECK(Absorb(image, Who::kEnemy, pp, maximum, amount, &clean) == ModelAbsorb(false, pp, maximum, amount) && clean);
            CHECK(Absorb(image, Who::kNoPlayer, pp, maximum, amount, &clean) == ModelAbsorb(false, pp, maximum, amount) && clean);
        }
    }
    SetCheat(OSL_CHEAT_MAX_PHOZON_GAIN, false);
    return g_failures == before;
}

bool SemanticPp(const ClassicImage& image) {
    const int before = g_failures;
    bool clean = false;
    struct Sample {
        uint16_t pp, maximum;
        uint32_t cost;
    };
    static const Sample kSamples[] = {{500, 900, 300}, {500, 900, 600}, {900, 900, 0}, {500, 700, 100}, {0, 1000, 100}, {9900, 9900, 9900}};
    for (const bool cheat : {false, true, false, true, false}) {
        SetCheat(OSL_CHEAT_INFINITE_PP, cheat);
        for (const Sample& s : kSamples) {
            // The controlled character's skill refills PP; any other character, or no controlled one, spends it.
            CHECK(PpSpend(image, Who::kPlayer, s.pp, s.maximum, s.cost, &clean) == ModelPpSpend(cheat, s.pp, s.maximum, s.cost) && clean);
            CHECK(PpSpend(image, Who::kEnemy, s.pp, s.maximum, s.cost, &clean) == ModelPpSpend(false, s.pp, s.maximum, s.cost) && clean);
            CHECK(PpSpend(image, Who::kNoPlayer, s.pp, s.maximum, s.cost, &clean) == ModelPpSpend(false, s.pp, s.maximum, s.cost) && clean);
            CHECK(PpSpend(image, Who::kNoRoot, s.pp, s.maximum, s.cost, &clean) == ModelPpSpend(false, s.pp, s.maximum, s.cost) && clean);
        }
    }
    return g_failures == before;
}

bool SemanticPow(const ClassicImage& image) {
    const int before = g_failures;
    bool clean = false;
    static const std::pair<float, float> kSamples[] = {{100.0f, 30.0f}, {100.0f, 0.25f}, {10.0f, 30.0f}, {30.0f, 30.0f},
                                                       {0.5f, 0.1f},    {0.05f, 1.0f},   {64.0f, 0.0f}, {99.9f, 100.0f}};
    for (const bool cheat : {false, true, false, true, false}) {
        SetCheat(OSL_CHEAT_INFINITE_POW, cheat);
        for (const Slot slot : {kDrain, kAttack, kGuard, kGuardHeld, kSpend}) {
            for (const auto& [stamina, cost] : kSamples) {
                // The controlled character's POW stays full and never exhausts it; other characters spend theirs.
                CHECK(Drain(image, slot, Who::kPlayer, stamina, cost, &clean) == ModelDrain(cheat, slot, stamina, cost) && clean);
                CHECK(Drain(image, slot, Who::kEnemy, stamina, cost, &clean) == ModelDrain(false, slot, stamina, cost) && clean);
                CHECK(Drain(image, slot, Who::kNoPlayer, stamina, cost, &clean) == ModelDrain(false, slot, stamina, cost) && clean);
                // The whole drain function reads the root itself, so it always has one.
                if (slot != kDrain) CHECK(Drain(image, slot, Who::kNoRoot, stamina, cost, &clean) == ModelDrain(false, slot, stamina, cost) && clean);
            }
        }
        for (const float value : {0.0f, 40.0f, 150.0f, -5.0f}) {
            CHECK(SetPow(image, Who::kPlayer, 60.0f, value, &clean) == ModelSetPow(cheat, value) && clean);
            CHECK(SetPow(image, Who::kEnemy, 60.0f, value, &clean) == ModelSetPow(false, value) && clean);
            CHECK(SetPow(image, Who::kNoRoot, 60.0f, value, &clean) == ModelSetPow(false, value) && clean);
        }
    }
    return g_failures == before;
}

bool SemanticHealth(const ClassicImage& image) {
    const int before = g_failures;
    bool clean = false;
    uint32_t taken = 0;
    for (const bool cheat : {false, true, false, true, false}) {
        SetCheat(OSL_CHEAT_INFINITE_HEALTH, cheat);
        // The damage routine: the controlled character keeps its HP (and the damage is not counted); enemies, and a root
        // with no controlled character yet, keep the game's own behaviour.
        CHECK(Damage(image, Who::kPlayer, 100, 30, &clean, &taken) == ModelHp(cheat, 100, 30) && clean && taken == (cheat ? 0u : 30u));
        CHECK(Damage(image, Who::kPlayer, 10, 50, &clean) == ModelHp(cheat, 10, 50) && clean);
        CHECK(Damage(image, Who::kPlayer, 100, 0, &clean) == 100 && clean);
        CHECK(Damage(image, Who::kPlayer, 100, -20, &clean) == 120 && clean);
        CHECK(Damage(image, Who::kEnemy, 100, 30, &clean, &taken) == 70 && clean && taken == 0);
        CHECK(Damage(image, Who::kEnemy, 10, 50, &clean) == 0 && clean);
        CHECK(Damage(image, Who::kNoPlayer, 100, 30, &clean) == 70 && clean);
        // The hit routine, with the player and two enemies in one fight: only the controlled character is spared.
        Object player, first, second;
        CHECK(Hit(image, player, &player, true, 100, 30, &clean) == (cheat ? 100 : 70) && clean);
        CHECK(Hit(image, first, &player, true, 100, 30, &clean) == 70 && clean);
        CHECK(Hit(image, second, &player, true, 250, 45, &clean) == 205 && clean);
        CHECK(Hit(image, first, &player, true, 20, 50, &clean) == -30 && clean);
        CHECK(Hit(image, player, &player, true, 100, -20, &clean) == 120 && clean);
        CHECK(Hit(image, player, &player, true, 100, 0, &clean) == 100 && clean);
        // When the controlled character is the enemy's target object no more (switched, none, no root): the game's own.
        CHECK(Hit(image, player, &first, true, 100, 30, &clean) == 70 && clean);
        CHECK(Hit(image, player, nullptr, true, 100, 30, &clean) == 70 && clean);
        CHECK(Hit(image, player, &player, false, 100, 30, &clean) == 70 && clean);
    }
    return g_failures == before;
}

bool SemanticArrows(const ClassicImage& image) {
    const int before = g_failures;
    bool clean = false;
    for (const bool cheat : {false, true, false, true, false}) {
        SetCheat(OSL_CHEAT_INFINITE_ARROWS, cheat);
        for (const uint32_t count : {1u, 2u, 3u, 10u, 24u, 25u}) {
            for (int kind = 0; kind < 4; ++kind) {
                for (const bool charged : {false, true}) {
                    // Mercedes as the controlled character keeps 25 arrows; another one (an enemy Mercedes) spends hers.
                    const Ammo player = Shot(image, Who::kPlayer, count, kind, charged, &clean);
                    const Ammo want = ModelShot(cheat, count, kind, charged);
                    CHECK(player.count == want.count && player.loaded == want.loaded && clean);
                    const Ammo enemy = Shot(image, Who::kEnemy, count, kind, charged, &clean);
                    const Ammo original = ModelShot(false, count, kind, charged);
                    CHECK(enemy.count == original.count && enemy.loaded == original.loaded && clean);
                    const Ammo nobody = Shot(image, Who::kNoRoot, count, kind, charged, &clean);
                    CHECK(nobody.count == original.count && clean);
                }
            }
        }
    }
    return g_failures == before;
}

bool SemanticExperience(const ClassicImage& image) {
    const int before = g_failures;
    bool clean = false;
    for (const int multiplier : {1, 2, 5, 10, 100, 1, 10}) {
        SetExperience(multiplier);
        static const std::pair<uint32_t, uint32_t> kSamples[] = {{1000, 50}, {0, 1}, {1000, 0}, {99999000, 500}, {99999999, 7},
                                                                 {99999998, 1}, {99999990, 100}, {5, 3000000000u}, {0, 400000},
                                                                 {0, 999999}, {0, 0xffffffffu}};
        for (const std::pair<uint32_t, uint32_t>& sample : kSamples) {
            const uint32_t got = Experience(image, sample.first, sample.second, &clean);
            CHECK(got == ModelExperience(sample.first, sample.second, multiplier) && clean && got <= kExperienceMax && got >= sample.first);
        }
        // Once: an award of 50 on 1000 is exactly 50 times the multiplier, never twice.
        CHECK(Experience(image, 1000, 50, &clean) == 1000 + 50u * static_cast<uint32_t>(multiplier));
    }
    SetExperience(OSL_EXPERIENCE_OFF);
    CHECK(Experience(image, 1000, 50, &clean) == 1050 && clean);
    return g_failures == before;
}

struct Probe {
    OslCheat cheat; // OSL_CHEAT_COUNT: the Experience Multiplier
    uint32_t original, cheated;
    uint32_t (*run)(const ClassicImage&);
};

uint32_t ProbeGold(const ClassicImage& image) {
    bool clean;
    return Gold(Sale(image, {0, 0, 0, 0, 0}, 7, &clean)) + Gold(Pickup(image, Who::kPlayer, {0, 0, 0, 0, 0}, true, &clean));
}
uint32_t ProbePhozon(const ClassicImage& image) {
    bool clean;
    return Points(image, 40, 100, 10, &clean) + Absorb(image, Who::kPlayer, 300, 900, 50, &clean);
}
uint32_t ProbePp(const ClassicImage& image) {
    bool clean;
    return PpSpend(image, Who::kPlayer, 500, 900, 300, &clean);
}
uint32_t ProbePow(const ClassicImage& image) {
    bool clean;
    const float attack = Drain(image, kAttack, Who::kPlayer, 100.0f, 30.0f, &clean).value;
    const float guard = Drain(image, kGuard, Who::kPlayer, 50.0f, 0, &clean).value;
    return static_cast<uint32_t>(attack) + (guard < 50.0f ? 0u : 1000u);
}
uint32_t ProbeHealth(const ClassicImage& image) {
    bool clean;
    Object player;
    return static_cast<uint32_t>(Damage(image, Who::kPlayer, 100, 30, &clean) + Hit(image, player, &player, true, 100, 30, &clean));
}
uint32_t ProbeArrows(const ClassicImage& image) {
    bool clean;
    return Shot(image, Who::kPlayer, 10, 0, false, &clean).count;
}
uint32_t ProbeExperience(const ClassicImage& image) {
    bool clean;
    return Experience(image, 1000, 50, &clean);
}

const Probe kProbes[] = {
    {OSL_CHEAT_MAX_GOLD_GAIN, 7 + 1, 2 * kGoldMax, ProbeGold}, {OSL_CHEAT_MAX_PHOZON_GAIN, 50 + 350, kPointsMax - 1 + 900, ProbePhozon},
    {OSL_CHEAT_INFINITE_PP, 200, 900, ProbePp},                {OSL_CHEAT_INFINITE_POW, 70, 100 + 1000, ProbePow},
    {OSL_CHEAT_INFINITE_HEALTH, 140, 200, ProbeHealth},        {OSL_CHEAT_INFINITE_ARROWS, 9, 25, ProbeArrows},
    {OSL_CHEAT_COUNT, 1050, 1500, ProbeExperience},
};

// The runtime's live toggles on the real hooks, every subset of the cheats with every multiplier: each probe shows its
// own cheat and no other; Off, On, Off, On again accumulates nothing; the code is as installed after hundreds of toggles.
bool LiveToggles(const ClassicImage& image) {
    const int before = g_failures;
    runtime_cheats_start(0);
    SetExperience(OSL_EXPERIENCE_OFF);
    const Bytes code_before = image.Installed();
    const auto probe_cell = [&](const Probe& probe, unsigned on_mask, int multiplier) {
        const bool experience = probe.cheat == OSL_CHEAT_COUNT;
        const bool on = experience ? multiplier > 1 : (on_mask & osl_cheat_bit(probe.cheat)) != 0;
        const uint32_t expected = !on ? probe.original : experience ? 1000 + 50u * static_cast<uint32_t>(multiplier) : probe.cheated;
        return probe.run(image) == expected;
    };
    for (unsigned mask = 0; mask < (1u << OSL_CHEAT_COUNT); ++mask) {
        if (mask & ~kClassicMask) continue;
        for (const int multiplier : {1, 2, 5, 10, 100}) {
            for (int c = 0; c < OSL_CHEAT_COUNT; ++c) SetCheat(static_cast<OslCheat>(c), (mask & osl_cheat_bit(static_cast<OslCheat>(c))) != 0);
            SetExperience(multiplier);
            for (const Probe& probe : kProbes) CHECK(probe_cell(probe, mask, multiplier));
        }
    }
    // Each alone, repeated: Off -> original, On -> patched, Off -> original, On -> patched.
    for (const Probe& probe : kProbes) {
        for (int round = 0; round < 3; ++round) {
            if (probe.cheat == OSL_CHEAT_COUNT) {
                SetExperience(OSL_EXPERIENCE_OFF);
                CHECK(probe.run(image) == probe.original);
                SetExperience(10);
                CHECK(probe.run(image) == probe.cheated);
                SetExperience(OSL_EXPERIENCE_OFF);
                CHECK(probe.run(image) == probe.original);
                SetExperience(10);
                CHECK(probe.run(image) == probe.cheated);
            } else {
                SetCheat(probe.cheat, false);
                CHECK(probe.run(image) == probe.original);
                CHECK(runtime_cheat_toggle(probe.cheat) == 1);
                CHECK(probe.run(image) == probe.cheated);
                CHECK(runtime_cheat_toggle(probe.cheat) == 0);
                CHECK(probe.run(image) == probe.original);
                CHECK(runtime_cheat_toggle(probe.cheat) == 1);
                CHECK(probe.run(image) == probe.cheated);
            }
        }
        if (probe.cheat != OSL_CHEAT_COUNT) SetCheat(probe.cheat, false);
        SetExperience(OSL_EXPERIENCE_OFF);
    }
    // The cheats the Classic engine already behaves as have no Classic hook to switch.
    for (const OslCheat cheat : kInherent) CHECK(!runtime_cheat_available(cheat) && runtime_cheat_toggle(cheat) == -1);
    for (int i = 0; i < 700; ++i) runtime_cheat_toggle(i % OSL_CHEAT_COUNT);
    for (int i = 0; i < 300; ++i) runtime_cheat_toggle(OSL_CHEAT_INFINITE_HEALTH);
    runtime_cheats_start(0);
    SetExperience(OSL_EXPERIENCE_OFF);
    CHECK(image.Installed() == code_before);
    for (const Probe& probe : kProbes) CHECK(probe.run(image) == probe.original);
    return g_failures == before;
}

// The cheats that meet in play: Infinite Health with Infinite POW, Max Phozon Gain with Infinite POW and with Infinite PP,
// the Experience Multiplier with Max Gold Gain, Infinite Arrows with POW, PP with POW, and all of them at once; enemies keep
// their own behaviour.
bool Coexistence(const ClassicImage& image) {
    const int before = g_failures;
    bool clean = false;
    constexpr OslCheat kExperience = OSL_CHEAT_COUNT;
    const std::vector<std::vector<OslCheat>> combos = {
        {OSL_CHEAT_INFINITE_HEALTH, OSL_CHEAT_INFINITE_POW},
        {OSL_CHEAT_MAX_PHOZON_GAIN, OSL_CHEAT_INFINITE_POW},
        {kExperience, OSL_CHEAT_MAX_GOLD_GAIN},
        {OSL_CHEAT_INFINITE_ARROWS, OSL_CHEAT_INFINITE_POW},
        {OSL_CHEAT_INFINITE_HEALTH, kExperience},
        {OSL_CHEAT_INFINITE_PP, OSL_CHEAT_INFINITE_POW},
        {OSL_CHEAT_MAX_PHOZON_GAIN, OSL_CHEAT_INFINITE_PP},
        {OSL_CHEAT_INFINITE_HEALTH, OSL_CHEAT_INFINITE_POW, kExperience, OSL_CHEAT_MAX_PHOZON_GAIN, OSL_CHEAT_MAX_GOLD_GAIN,
         OSL_CHEAT_INFINITE_ARROWS, OSL_CHEAT_INFINITE_PP},
    };
    const auto has = [](const std::vector<OslCheat>& combo, OslCheat cheat) { return std::find(combo.begin(), combo.end(), cheat) != combo.end(); };
    for (const std::vector<OslCheat>& combo : combos) {
        for (int c = 0; c < OSL_CHEAT_COUNT; ++c) {
            if (kClassicMask & osl_cheat_bit(static_cast<OslCheat>(c))) SetCheat(static_cast<OslCheat>(c), has(combo, static_cast<OslCheat>(c)));
        }
        const bool experience = has(combo, kExperience);
        SetExperience(experience ? 100 : OSL_EXPERIENCE_OFF);
        for (const Probe& probe : kProbes) {
            const bool on = has(combo, probe.cheat);
            const uint32_t expected = !on ? probe.original : probe.cheat == kExperience ? 1000 + 50u * 100u : probe.cheated;
            CHECK(probe.run(image) == expected);
        }
        // One round of play: the player hit, attacking and using a skill, an enemy doing the same, a shot, an absorb, an award,
        // a sale.
        const bool health = has(combo, OSL_CHEAT_INFINITE_HEALTH), power = has(combo, OSL_CHEAT_INFINITE_POW),
                   points = has(combo, OSL_CHEAT_INFINITE_PP), phozon = has(combo, OSL_CHEAT_MAX_PHOZON_GAIN);
        Object player, enemy;
        CHECK(Hit(image, player, &player, true, 100, 40, &clean) == (health ? 100 : 60) && clean);
        CHECK(Hit(image, enemy, &player, true, 100, 40, &clean) == 60 && clean);
        CHECK(Damage(image, Who::kPlayer, 100, 40, &clean) == ModelHp(health, 100, 40) && clean);
        CHECK(Damage(image, Who::kEnemy, 100, 40, &clean) == 60 && clean);
        CHECK(Drain(image, kAttack, Who::kPlayer, 80.0f, 20.0f, &clean) == ModelDrain(power, kAttack, 80.0f, 20.0f) && clean);
        CHECK(Drain(image, kAttack, Who::kEnemy, 80.0f, 20.0f, &clean) == ModelDrain(false, kAttack, 80.0f, 20.0f) && clean);
        CHECK(PpSpend(image, Who::kPlayer, 500, 900, 200, &clean) == (points ? 900u : 300u) && clean);
        CHECK(PpSpend(image, Who::kEnemy, 500, 900, 200, &clean) == 300u && clean);
        CHECK(Absorb(image, Who::kPlayer, 100, 900, 50, &clean) == (phozon ? 900u : 150u) && clean);
        CHECK(Absorb(image, Who::kEnemy, 100, 900, 50, &clean) == 150u && clean);
        CHECK(Shot(image, Who::kEnemy, 10, 2, false, &clean).count == 0 && clean);
        CHECK(Experience(image, 10, 10, &clean) == ModelExperience(10, 10, experience ? 100 : 1) && clean);
        CHECK(Gold(Sale(image, {5, 5, 5, 5, 5}, 3, &clean)) == (has(combo, OSL_CHEAT_MAX_GOLD_GAIN) ? kGoldMax : 230u + 3u) && clean);
        CHECK(Points(image, 40, 100, 10, &clean) == (has(combo, OSL_CHEAT_MAX_PHOZON_GAIN) ? kPointsMax - 1 : 50u) && clean);
    }
    for (int c = 0; c < OSL_CHEAT_COUNT; ++c) {
        if (kClassicMask & osl_cheat_bit(static_cast<OslCheat>(c))) SetCheat(static_cast<OslCheat>(c), false);
    }
    SetExperience(OSL_EXPERIENCE_OFF);
    return g_failures == before;
}

// The fixtures are the game's bytes: each once in every local build, with the sites at the place they hold in the build.
void TestFixturesInBuilds() {
    std::printf("Classic semantic fixtures in local builds (the game's own instructions):\n");
    for (const GameBuild& build : SupportedBuilds()) {
        bool present = false;
        const LinkedImage linked = LinkLocal(build, &present);
        if (!present) {
            std::printf("  %-26s DATA UNAVAILABLE\n", build.label);
            continue;
        }
        const int before = g_failures;
        const auto expected = kLocal.find(build.folder);
        CHECK(linked.eboot_error.empty() && expected != kLocal.end());
        if (!linked.eboot_error.empty() || expected == kLocal.end()) continue;
        const Bytes& image = linked.image.image;
        size_t found = 0;
        for (const Chunk& chunk : kChunks) {
            Pattern pattern = Parse(chunk.code);
            for (const size_t at : chunk.differs) pattern.any[at] = true;
            const std::vector<size_t> hits = FindAll(image, 0, linked.eboot_size, pattern);
            CHECK(hits.size() == 1);
            ++found;
            if (hits.size() != 1) continue;
            for (const auto& [site, at] : chunk.sites) CHECK(hits[0] + at == expected->second[static_cast<size_t>(site)]);
        }
        std::printf("  %-26s %s (%zu fixtures once each, every site where its fixture puts it)\n", build.label,
                    g_failures == before ? "STATIC PASS" : "FAIL", found);
    }
}

// Not enough doubt-free sites, no room, sites outside the code, a global too far, a call outside the code: the runtime
// keeps its safe behaviour and never takes a first match.
void TestFailurePaths() {
    const int before = g_failures;
    // The fixtures in noise, at slots apart: the hooks of the Classic engine only.
    constexpr size_t kAtDamage = 0x100, kAtExp = 0x300, kAtPow = 0x500, kAtPoints = 0x600, kAtSale = 0x700, kAtPickup = 0x900,
                     kAtShot = 0xb00, kAtHit = 0xd00, kAtDrain = 0x1100, kAtAttack = 0x1200, kAtGuard = 0x1280, kAtGuardHeld = 0x1300,
                     kAtSpend = 0x1380, kAtSet = 0x1400;
    const auto planted = [&](size_t size) {
        Bytes image(size, 0xcc);
        for (size_t i = 0; i < image.size(); ++i) image[i] = static_cast<uint8_t>(0x20 + (i * 7) % 5);
        const auto place = [&](size_t at, const char* code) {
            const Bytes bytes = Concrete(code);
            std::copy(bytes.begin(), bytes.end(), image.begin() + static_cast<std::ptrdiff_t>(at));
        };
        place(kAtDamage, kDamageCode);
        place(kAtExp, kExperienceStartCode);
        place(kAtExp + 0x134, kExperienceEndCode);
        place(kAtPow, kAddPowCode);
        place(kAtDrain, kDrainCode);
        place(kAtAttack, kAttackCode);
        place(kAtGuard, kGuardCode);
        place(kAtGuardHeld, kGuardHeldCode);
        place(kAtSpend, kSpendCode);
        place(kAtSet, kSetCode);
        place(kAtSale, kSaleCode);
        place(kAtPickup, kPickupCode);
        place(kAtShot, kShotRootCode);
        place(kAtShot + kShotAt, kShotCode);
        place(kAtHit, kHitCode);
        place(kAtPoints, kPointsStartCode);
        return image;
    };
    const auto run = [&](Bytes image, const std::vector<OslCodeRange>& ranges, size_t stubs_size, unsigned* hooked, OslCheatScan classic[OSL_CHEAT_COUNT],
                         OslCheatScan refined[OSL_CHEAT_COUNT], OslCheatScan* experience, unsigned* experience_hooked) {
        const size_t size = image.size();
        image.resize(size + OSL_CHEAT_STUBS_BYTES + OSL_EXPERIENCE_STUBS_BYTES + 0x100, 0);
        const Bytes original = image;
        const unsigned installed = osl_install_cheats_engines(image.data(), size, ranges.data(), ranges.size(), image.data() + size, stubs_size,
                                                              image.data() + size + OSL_CHEAT_STUBS_BYTES + OSL_EXPERIENCE_STUBS_BYTES, refined, classic, hooked);
        *experience_hooked = osl_install_experience_engines(image.data(), size, ranges.data(), ranges.size(), image.data() + size + OSL_CHEAT_STUBS_BYTES,
                                                            OSL_EXPERIENCE_STUBS_BYTES, image.data() + size + OSL_CHEAT_STUBS_BYTES + OSL_EXPERIENCE_STUBS_BYTES,
                                                            nullptr, experience);
        return std::make_pair(installed, std::make_pair(original, image));
    };
    const auto same = [](const Bytes& a, const Bytes& b, size_t from, size_t to) {
        return std::equal(a.begin() + static_cast<std::ptrdiff_t>(from), a.begin() + static_cast<std::ptrdiff_t>(to),
                          b.begin() + static_cast<std::ptrdiff_t>(from));
    };
    const size_t size = 0x2000;
    const std::vector<OslCodeRange> whole = {{0, size}};
    OslCheatScan classic[OSL_CHEAT_COUNT], refined[OSL_CHEAT_COUNT], experience;
    unsigned hooked[OSL_ENGINE_COUNT], experience_hooked;
    const unsigned all_but = kClassicMask;

    // Everything in place: the Classic engine hooks six cheats and the multiplier; the Refined engine has nothing here.
    {
        auto [installed, result] = run(planted(size), whole, OSL_CHEAT_STUBS_BYTES, hooked, classic, refined, &experience, &experience_hooked);
        CHECK(installed == kClassicMask && hooked[OSL_ENGINE_CLASSIC] == kClassicMask && hooked[OSL_ENGINE_REFINED] == 0);
        CHECK(experience_hooked == 2 && classic[OSL_CHEAT_MAX_GOLD_GAIN].state == OSL_CHEAT_COMPLETE &&
              classic[OSL_CHEAT_INFINITE_ARROWS].state == OSL_CHEAT_COMPLETE);
        for (int c = 0; c < OSL_CHEAT_COUNT; ++c) CHECK(refined[c].state == OSL_CHEAT_MISSING);
        for (const OslCheat cheat : kInherent) CHECK(classic[cheat].state == OSL_CHEAT_INHERENT);
        CHECK(result.first != result.second);
    }
    // A missing site (the damage routine's root global): Infinite Health is PARTIAL and nothing of it is written, neither
    // the damage routine nor the hit; the rest hooks.
    {
        Bytes image = planted(size);
        image[kAtDamage + 0x54 - 1] ^= 0xff;
        auto [installed, result] = run(image, whole, OSL_CHEAT_STUBS_BYTES, hooked, classic, refined, &experience, &experience_hooked);
        CHECK(classic[OSL_CHEAT_INFINITE_HEALTH].state == OSL_CHEAT_PARTIAL && Contains(classic[OSL_CHEAT_INFINITE_HEALTH].reason, "C-H2 root object global: not found"));
        CHECK(same(result.first, result.second, kAtDamage, kAtDamage + 0x80) && same(result.first, result.second, kAtHit, kAtHit + 0x20));
        CHECK(hooked[OSL_ENGINE_CLASSIC] == (all_but & ~osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH)) && experience_hooked == 2 && installed == hooked[OSL_ENGINE_CLASSIC]);
    }
    // Twice: ambiguous, so neither copy is hooked.
    {
        Bytes image = planted(size);
        const Bytes second = Concrete(kPowSpentCode);
        std::copy(second.begin(), second.end(), image.begin() + 0x1800);
        auto [installed, result] = run(image, whole, OSL_CHEAT_STUBS_BYTES, hooked, classic, refined, &experience, &experience_hooked);
        CHECK(classic[OSL_CHEAT_INFINITE_PP].state == OSL_CHEAT_AMBIGUOUS && Contains(classic[OSL_CHEAT_INFINITE_PP].reason, "C-K1 PP spent: 2 matches"));
        CHECK(same(result.first, result.second, 0x1800, 0x181a) && same(result.first, result.second, kAtPow + kSubPowAt, kAtPow + kSubPowAt + 0x1a));
        CHECK(installed == (all_but & ~osl_cheat_bit(OSL_CHEAT_INFINITE_PP)));
    }
    // Other bytes where the original instruction should be: found, refused, nothing written.
    {
        Bytes image = planted(size);
        image[kAtSale + 34] ^= 0xff;
        auto [installed, result] = run(image, whole, OSL_CHEAT_STUBS_BYTES, hooked, classic, refined, &experience, &experience_hooked);
        CHECK(classic[OSL_CHEAT_MAX_GOLD_GAIN].state == OSL_CHEAT_INVALID && Contains(classic[OSL_CHEAT_MAX_GOLD_GAIN].reason, "other bytes than the original instruction"));
        CHECK(installed == (all_but & ~osl_cheat_bit(OSL_CHEAT_MAX_GOLD_GAIN)));
        CHECK(same(result.first, result.second, kAtSale, kAtSale + 0x200) && same(result.first, result.second, kAtPickup, kAtPickup + 0x200));
    }
    // The bytes the masks leave out differ (another build's branch and vtable slot): still found and hooked.
    {
        Bytes image = planted(size);
        image[kAtSale + 3] ^= 0x01;
        image[kAtPickup + 8] = 0x98;
        auto [installed, result] = run(image, whole, OSL_CHEAT_STUBS_BYTES, hooked, classic, refined, &experience, &experience_hooked);
        CHECK(classic[OSL_CHEAT_MAX_GOLD_GAIN].state == OSL_CHEAT_COMPLETE && installed == all_but);
    }
    // The sale's call to set_stack does not reach the code: the gold hooks have no function to call and are refused.
    {
        Bytes image = planted(size);
        const int32_t outside = 0x10000000;
        std::memcpy(image.data() + kAtSale + 30, &outside, 4);
        auto [installed, result] = run(image, whole, OSL_CHEAT_STUBS_BYTES, hooked, classic, refined, &experience, &experience_hooked);
        CHECK(classic[OSL_CHEAT_MAX_GOLD_GAIN].state == OSL_CHEAT_INVALID && Contains(classic[OSL_CHEAT_MAX_GOLD_GAIN].reason, "a call outside the code"));
        CHECK(classic[OSL_CHEAT_MAX_GOLD_GAIN].sites[0].callee == 0 && installed == (all_but & ~osl_cheat_bit(OSL_CHEAT_MAX_GOLD_GAIN)));
        CHECK(same(result.first, result.second, kAtSale, kAtSale + 0x200) && same(result.first, result.second, kAtPickup, kAtPickup + 0x200));
    }
    // SubPOW away from the root global it reads (the copy in AddPOW's place spoiled): the global is not next to the hook.
    {
        Bytes image = planted(size);
        image[kAtPow + kSubPowAt + 9] ^= 0xff; // past the patch bytes, which the matcher skips
        const Bytes distant = Concrete(kPowSpentCode);
        std::copy(distant.begin(), distant.end(), image.begin() + 0x1800);
        auto [installed, result] = run(image, whole, OSL_CHEAT_STUBS_BYTES, hooked, classic, refined, &experience, &experience_hooked);
        CHECK(classic[OSL_CHEAT_INFINITE_PP].state == OSL_CHEAT_INVALID && Contains(classic[OSL_CHEAT_INFINITE_PP].reason, "C-K2 root object global: not next to the hooked code"));
        CHECK(installed == (all_but & ~osl_cheat_bit(OSL_CHEAT_INFINITE_PP)) && same(result.first, result.second, 0x1800, 0x181a));
    }
    // A POW drain whose root global is gone: the whole POW group, every drain included, stays unhooked.
    {
        Bytes image = planted(size);
        image[kAtDrain + kDrainRootAt - 2] ^= 0xff;
        auto [installed, result] = run(image, whole, OSL_CHEAT_STUBS_BYTES, hooked, classic, refined, &experience, &experience_hooked);
        CHECK(classic[OSL_CHEAT_INFINITE_POW].state == OSL_CHEAT_PARTIAL && Contains(classic[OSL_CHEAT_INFINITE_POW].reason, "C-W2 root object global: not found"));
        CHECK(installed == (all_but & ~osl_cheat_bit(OSL_CHEAT_INFINITE_POW)) && same(result.first, result.second, kAtDrain, kAtSet + 0x40));
    }
    // The multiplier's end of function in another place: the award is not in the hooked function, so not hooked.
    {
        Bytes image = planted(size);
        image[kAtExp + 0x134] ^= 0xff;
        auto [installed, result] = run(image, whole, OSL_CHEAT_STUBS_BYTES, hooked, classic, refined, &experience, &experience_hooked);
        CHECK(experience_hooked == 0 && experience.state == OSL_CHEAT_PARTIAL);
        CHECK(installed == all_but);
    }
    // Sites outside the code ranges are not looked at.
    {
        const std::vector<OslCodeRange> narrow = {{0, 0x300}, {kAtHit, kAtHit + 0x100}};
        auto [installed, result] = run(planted(size), narrow, OSL_CHEAT_STUBS_BYTES, hooked, classic, refined, &experience, &experience_hooked);
        CHECK(classic[OSL_CHEAT_INFINITE_POW].state == OSL_CHEAT_MISSING && classic[OSL_CHEAT_MAX_GOLD_GAIN].state == OSL_CHEAT_MISSING &&
              classic[OSL_CHEAT_INFINITE_ARROWS].state == OSL_CHEAT_MISSING);
        CHECK(installed == osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH));
        CHECK(same(result.first, result.second, 0x300, kAtHit) && same(result.first, result.second, kAtHit + 0x100, size));
    }
    // No room for a group's stubs: all or nothing, the others are not disturbed. Gold and phozon take the four.
    {
        auto [installed, result] = run(planted(size), whole, OSL_CHEAT_STUB_BYTES * 4, hooked, classic, refined, &experience, &experience_hooked);
        CHECK(classic[OSL_CHEAT_INFINITE_HEALTH].state == OSL_CHEAT_INVALID && Contains(classic[OSL_CHEAT_INFINITE_HEALTH].reason, "no room"));
        CHECK(classic[OSL_CHEAT_INFINITE_ARROWS].state == OSL_CHEAT_INVALID && classic[OSL_CHEAT_INFINITE_POW].state == OSL_CHEAT_INVALID &&
              classic[OSL_CHEAT_INFINITE_PP].state == OSL_CHEAT_INVALID);
        CHECK(installed == (osl_cheat_bit(OSL_CHEAT_MAX_GOLD_GAIN) | osl_cheat_bit(OSL_CHEAT_MAX_PHOZON_GAIN)));
        CHECK(same(result.first, result.second, kAtDamage, kAtDamage + 0x80) && same(result.first, result.second, kAtHit, kAtHit + 0x20) &&
              same(result.first, result.second, kAtShot, kAtShot + 0x200) && same(result.first, result.second, kAtDrain, kAtSet + 0x40));
    }
    // The Experience Multiplier with room for one engine's stub: the Classic group alone is refused.
    {
        Bytes image = planted(size);
        image.resize(size + OSL_CHEAT_STUBS_BYTES + OSL_EXPERIENCE_STUBS_BYTES + 0x100, 0);
        OslCheatScan one;
        const unsigned got = osl_install_experience_engines(image.data(), size, whole.data(), whole.size(), image.data() + size, OSL_CHEAT_STUB_BYTES - 1,
                                                            image.data() + size + 0x1000, nullptr, &one);
        CHECK(got == 0 && one.state == OSL_CHEAT_INVALID && Contains(one.reason, "no room"));
    }
    // The Refined scan is unchanged by the Classic sites: it still sees none of its own in a Classic-only image.
    {
        const Bytes image = planted(size);
        OslCheatScan scans[OSL_CHEAT_COUNT];
        osl_cheat_scan(image.data(), image.size(), whole.data(), whole.size(), scans);
        for (const OslCheatScan& scan : scans) CHECK(scan.state == OSL_CHEAT_MISSING);
        OslCheatScan engine[OSL_CHEAT_COUNT];
        osl_cheat_scan_engine(image.data(), image.size(), whole.data(), whole.size(), OSL_ENGINE_CLASSIC, engine);
        CHECK(engine[OSL_CHEAT_INFINITE_POW].state == OSL_CHEAT_COMPLETE);
    }
    std::printf("Classic failure paths: missing, ambiguous, modified, masked bytes, a call outside the code, a global too far, outside the "
                "code, out of room: nothing hooked on doubt, the other groups and the Refined engine unaffected%s\n",
                g_failures == before ? "" : " (FAILED)");
}

constexpr const char* kVerifiedPatched = "VERIFIED PATCHED";
constexpr const char* kInherentlySatisfied = "INHERENTLY SATISFIED";
constexpr const char* kDataUnavailable = "DATA UNAVAILABLE";

struct Cell {
    std::string build, mode, cheat, state;
};

// The build x mode x cheat matrix: every cell is verified patched on the real files of its build, inherently satisfied
// (proved in the Classic code, and the build's own Classic scan agrees), or DATA UNAVAILABLE; no cell is anything else.
void TestMatrix() {
    std::vector<Cell> cells;
    std::map<std::string, size_t> counts;
    std::vector<std::string> lines;
    for (const GameBuild& build : SupportedBuilds()) {
        bool present = false;
        const LinkedImage linked = LinkLocal(build, &present);
        if (!present) {
            for (const char* mode : {"Refined", "Classic"}) {
                for (int c = 0; c <= OSL_CHEAT_COUNT; ++c) {
                    cells.push_back({build.label, mode, c < OSL_CHEAT_COUNT ? osl_cheats[c].label : osl_experience_multiplier.label, kDataUnavailable});
                }
            }
            lines.push_back(std::string("  ") + build.label + ": both modes, all 8 cheats DATA UNAVAILABLE (no prepared eboot; the generic locator "
                            "refuses missing, ambiguous and modified sites)");
            continue;
        }
        const Bytes& image = linked.image.image;
        const std::vector<OslCodeRange> code = EbootCode(linked.image, linked.eboot_size);
        const Hooked hooked(image, code);
        std::string line = std::string("  ") + build.label + ":";
        for (int e = 0; e < OSL_ENGINE_COUNT; ++e) {
            const OslEngine engine = static_cast<OslEngine>(e);
            size_t patched = 0;
            std::string inherent;
            for (int c = 0; c <= OSL_CHEAT_COUNT; ++c) {
                const bool experience = c == OSL_CHEAT_COUNT;
                const OslCheatScan& scan = experience ? hooked.experience_scans[e] : hooked.scans[e][c];
                size_t site_count = 0;
                const OslCheatSite* sites = experience ? (e == OSL_ENGINE_CLASSIC ? osl_experience_multiplier.classic_sites : osl_experience_multiplier.sites)
                                                       : osl_cheat_engine_sites(&osl_cheats[c], engine, &site_count);
                site_count = experience ? (e == OSL_ENGINE_CLASSIC ? osl_experience_multiplier.classic_site_count : osl_experience_multiplier.site_count) : site_count;
                const char* label = experience ? osl_experience_multiplier.label : osl_cheats[c].label;
                if (scan.state == OSL_CHEAT_INHERENT) {
                    const bool proved = e == OSL_ENGINE_CLASSIC && !experience && site_count == 0 && IsInherent(static_cast<OslCheat>(c)) &&
                                        std::string(scan.reason) == osl_cheats[c].classic_inherent;
                    CHECK(proved);
                    cells.push_back({build.label, osl_engine_name(engine), label, proved ? kInherentlySatisfied : "UNKNOWN"});
                    inherent += std::string(inherent.empty() ? "" : "; ") + label + ": " + scan.reason;
                    continue;
                }
                // Verified patched: complete, every hook installed in the image's own code over the game's own bytes.
                bool ok = scan.state == OSL_CHEAT_COMPLETE && (experience ? (hooked.experience_hooked >> e) & 1 : (hooked.hooked[e] >> c) & 1);
                for (size_t s = 0; ok && s < site_count; ++s) {
                    ok = scan.sites[s].state == OSL_CHEAT_SITE_UNIQUE && scan.sites[s].hits == 1;
                    if (sites[s].kind != OSL_CHEAT_SITE_HOOK) continue;
                    const uint64_t region = static_cast<uint64_t>(static_cast<int64_t>(scan.sites[s].at[0]) + sites[s].region_at);
                    ok = ok && hooked.memory[region] == 0xe9;
                    ok = ok && (e == OSL_ENGINE_REFINED || std::equal(sites[s].signature + sites[s].region_at,
                                                                      sites[s].signature + sites[s].region_at + sites[s].region_size,
                                                                      image.begin() + static_cast<std::ptrdiff_t>(region)));
                }
                CHECK(ok);
                cells.push_back({build.label, osl_engine_name(engine), label, ok ? kVerifiedPatched : "UNKNOWN"});
                patched += ok;
            }
            line += std::string(" ") + osl_engine_name(engine) + " " + std::to_string(patched) + " " + kVerifiedPatched;
            if (!inherent.empty()) line += ", " + std::to_string(OSL_CHEAT_COUNT + 1 - patched) + " " + kInherentlySatisfied + " (" + inherent + ")";
            line += e + 1 < OSL_ENGINE_COUNT ? ";" : "";
        }
        lines.push_back(line);
    }
    for (const Cell& cell : cells) ++counts[cell.state];
    const size_t total = cells.size();
    CHECK(total == SupportedBuilds().size() * OSL_ENGINE_COUNT * (OSL_CHEAT_COUNT + 1));
    CHECK(counts[kVerifiedPatched] + counts[kInherentlySatisfied] + counts[kDataUnavailable] == total && counts.count("UNKNOWN") == 0);
    std::printf("Cheat matrix (build x mode x cheat): %zu builds x %d modes x %d cheats = %zu cells: %zu VERIFIED PATCHED on real files, "
                "%zu INHERENTLY SATISFIED, %zu DATA UNAVAILABLE, %zu UNKNOWN\n",
                SupportedBuilds().size(), OSL_ENGINE_COUNT, OSL_CHEAT_COUNT + 1, total, counts[kVerifiedPatched], counts[kInherentlySatisfied],
                counts[kDataUnavailable], counts.count("UNKNOWN") ? counts["UNKNOWN"] : size_t{0});
    for (const std::string& line : lines) std::printf("%s\n", line.c_str());
}

void TestSemantics() {
    if (!__builtin_cpu_supports("bmi") || !__builtin_cpu_supports("avx")) {
        std::printf("skipped: the semantic models run BMI1 and AVX code, which the game itself needs\n");
        return;
    }
    ClassicImage image;
    CHECK(image.installed() == kClassicMask && image.hooked(OSL_ENGINE_CLASSIC) == kClassicMask && image.hooked(OSL_ENGINE_REFINED) == 0);
    CHECK(image.experience_hooked() == 2);
    for (int c = 0; c < OSL_CHEAT_COUNT; ++c) {
        CHECK(image.Scan(OSL_ENGINE_CLASSIC, c).state == (IsInherent(static_cast<OslCheat>(c)) ? OSL_CHEAT_INHERENT : OSL_CHEAT_COMPLETE));
    }
    CHECK(image.ExperienceScan(OSL_ENGINE_CLASSIC).state == OSL_CHEAT_COMPLETE);
    const bool gold = SemanticGold(image), phozon = SemanticPhozon(image), pp = SemanticPp(image), pow = SemanticPow(image),
               health = SemanticHealth(image), arrows = SemanticArrows(image), experience = SemanticExperience(image);
    const bool coexist = Coexistence(image);
    const bool live = LiveToggles(image);
    std::printf("Classic validation levels: HOOK = the hook carries the port's own code for the Classic site; SEMANTIC MODEL = the Classic\n"
                "game's own instructions run natively through the hooks, original against cheat, controlled character against others; LIVE\n"
                "TOGGLE = the runtime switch changes the next run with the executable bytes untouched, Off -> On -> Off -> On; GAMEPLAY = manual only\n");
    const auto line = [&](const char* label, bool semantic) {
        std::printf("  Classic %-22s HOOK VERIFIED  %s  %s  gameplay: %s\n", label, semantic ? "SEMANTIC MODEL VERIFIED" : "SEMANTIC MODEL FAILED  ",
                    live ? "LIVE TOGGLE VERIFIED" : "LIVE TOGGLE FAILED  ", semantic && live ? "UNPROVEN until the manual test" : "NOT READY");
    };
    line("Max Gold Gain", gold);
    line("Max Phozon Gain", phozon);
    line("Infinite PP", pp);
    line("Infinite POW", pow);
    line("Infinite Health", health);
    line("Infinite Arrows", arrows);
    line("Experience Multiplier", experience);
    for (const OslCheat cheat : kInherent) {
        std::printf("  Classic %-22s INHERENTLY SATISFIED (%s)\n", osl_cheats[cheat].label, osl_cheats[cheat].classic_inherent);
    }
    std::printf("Classic coexistence: %s (Health with POW, phozon with POW and PP, the multiplier with gold, arrows with POW, PP with POW, "
                "all at once; enemies unaffected)\n", coexist ? "VERIFIED" : "FAILED");
    std::printf("  Max Gold Gain: Classic gold is five coin stacks of 10, 5, 20, 10 and 1 G, 999 at most each; a sale or a coin pickup fills\n"
                "  all five, 45,954 G, the most the purse holds. Max Phozon Gain: an absorb fills PP and raises the Psypher level's running\n"
                "  total so the game's own level-up takes it to 98, its last. Infinite POW: the stamina gauge attacks, guarding and skills\n"
                "  drain stays full. Infinite PP: skills refill PP. Infinite Health, POW, PP, the phozon's PP and Arrows: the controlled\n"
                "  character only. Experience Multiplier: Classic awards EXP for food and items only, never for a defeated enemy\n");
}

}  // namespace

void RunClassicTests() {
    TestDefinitions();
    Totals totals;
    TestLocalBuilds(&totals);
    TestFixturesInBuilds();
    TestSemantics();
    TestFailurePaths();
    TestMatrix();
}

}  // namespace cheat_test
