// The Experience Multiplier: its choices and saved value, the session and runtime state, the locator, the hook run
// natively on the game's own instructions, the level-up model on the game's own threshold table, every local build,
// and its row in the in-game menu and the launcher page. Headless; game files are only read.
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "build_scan.h"
#include "cheats_page.h"
#include "cheats_test_support.h"
#include "game_files.h"
#include "game_profile.h"
#include "osl_cheat_menu.h"
#include "osl_cheat_session.h"
#include "osl_cheats.h"
#include "osl_launcher.h"
#include "runtime_cheats.h"
#include "settings.h"
#include "test_support.h"

namespace cheat_test {
namespace {

namespace fs = std::filesystem;
using namespace osl;
using osl_test::Contains;
using osl_test::HexText;
using osl_test::ReadFile;
using osl_test::SyntheticOdinImage;
using osl_test::TempDir;

// The award function's start and its end, written out here so the production table is compared, not shared.
constexpr char kAwardStart[] = "55 48 89 e5 41 56 53 48 83 ec 10 48 89 fb 89 f0 83 bb 5c 03 00 00 00 0f 84 15 02 00 00 "
                               "c4 e1 fa 2a c0 85 f6 0f 84 65 01 00 00 be 1b 00 00 00";
constexpr char kAwardEnd[] = "44 89 b3 40 21 00 00 48 83 c4 10 5b 41 5e 5d c3";
constexpr char kRegion[] = "48 89 fb 89 f0";
constexpr uint64_t kRegionAt = 11;
constexpr uint64_t kEndAt = 0x22b;
constexpr uint32_t kCap = 2500000;
constexpr int kChoices[] = {1, 2, 5, 10, 100};

// The award function in each local build's linked image.
const std::map<std::string, uint64_t> kLocalAward = {
    {"CUSA05083-1.00", 0x475810}, {"CUSA01290-1.00", 0x471340}, {"CUSA01290-1.02", 0x473de0},
    {"CUSA05262-1.00", 0x476280}, {"CUSA05262-1.01", 0x476360},
};

Bytes ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

Bytes Noise(size_t size, uint32_t seed) {
    Bytes out(size);
    uint32_t x = seed * 2654435761u + 1;
    for (uint8_t& b : out) {
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        b = static_cast<uint8_t>(x);
    }
    return out;
}

void Plant(Bytes& image, uint64_t at, const char* hex) {
    const Bytes code = Hex(hex);
    std::copy(code.begin(), code.end(), image.begin() + static_cast<std::ptrdiff_t>(at));
}

void PlantAward(Bytes& image, uint64_t at) {
    Plant(image, at, kAwardStart);
    Plant(image, at + kEndAt, kAwardEnd);
}

OslCheatScan ScanOf(const Bytes& image) {
    const OslCodeRange range{0, image.size()};
    OslCheatScan scan;
    osl_experience_scan(image.data(), image.size(), &range, 1, &scan);
    return scan;
}

// What the hook gives the rest of the function: the award, multiplied and capped unless Off (0).
uint64_t Expected(uint32_t award, int flag) {
    if (flag == 0) return award;
    const uint64_t multiplied = uint64_t{award} * uint64_t(flag);
    return std::min<uint64_t>(multiplied, kCap);
}

void TestChoices() {
    CHECK(OSL_EXPERIENCE_CHOICE_COUNT == 5 && OSL_EXPERIENCE_OFF == 1 && OSL_EXPERIENCE_CAP == kCap);
    const char* const saved[] = {"1", "2", "5", "10", "100"};
    const char* const labels[] = {"Off", "2x", "5x", "10x", "100x"};
    for (int i = 0; i < OSL_EXPERIENCE_CHOICE_COUNT; ++i) {
        const OslChoice& choice = osl_experience_choices[i];
        CHECK(choice.value == kChoices[i] && std::string(choice.saved) == saved[i] && std::string(choice.label) == labels[i]);
        CHECK(osl_experience_from_saved(saved[i]) == kChoices[i] && osl_experience_listed(kChoices[i]) == kChoices[i]);
        CHECK(std::string(osl_experience_label(kChoices[i])) == labels[i]);
        CHECK(osl_experience_next(kChoices[i]) == kChoices[(i + 1) % OSL_EXPERIENCE_CHOICE_COUNT]);
    }
    for (const char* bad : {"0", "3", "1000", "-1", "", " 2", "2 ", "2x", "x2", "on", "Off", "10.0", "0x2"}) {
        CHECK(osl_experience_from_saved(bad) == 0);
    }
    CHECK(osl_experience_from_saved(nullptr) == 0);
    for (const int bad : {0, -1, 3, 4, 50, 99, 101, 255, 1000}) {
        CHECK(osl_experience_listed(bad) == OSL_EXPERIENCE_OFF && std::string(osl_experience_label(bad)) == "Off" &&
              osl_experience_next(bad) == OSL_EXPERIENCE_OFF);
    }
    // Off, 2x, 5x, 10x, 100x, then Off again.
    int value = OSL_EXPERIENCE_OFF;
    std::string cycle;
    for (int i = 0; i < 6; ++i) {
        cycle += std::string(osl_experience_label(value)) + " ";
        value = osl_experience_next(value);
    }
    CHECK(cycle == "Off 2x 5x 10x 100x Off ");
    CHECK(std::string(osl_experience_multiplier.key) == "cheat_experience_multiplier" &&
          std::string(osl_experience_multiplier.label) == "Experience Multiplier" &&
          std::string(osl_experience_multiplier.description) == "Multiplies the experience each award adds");
    // One hooked site (each award is multiplied once, at the function every award goes through) and its end.
    CHECK(osl_experience_multiplier.site_count == 2 && osl_experience_multiplier.sites[0].kind == OSL_CHEAT_SITE_HOOK &&
          osl_experience_multiplier.sites[1].kind == OSL_CHEAT_SITE_SAME_FUNCTION);
    CHECK(Bytes(osl_experience_multiplier.sites[0].signature,
                osl_experience_multiplier.sites[0].signature + osl_experience_multiplier.sites[0].signature_size) ==
          Hex(kAwardStart));
    CHECK(Bytes(osl_experience_multiplier.sites[1].signature,
                osl_experience_multiplier.sites[1].signature + osl_experience_multiplier.sites[1].signature_size) ==
          Hex(kAwardEnd));
    // A setting, not an eighth on/off cheat.
    CHECK(OSL_CHEAT_COUNT == 7);
    for (const OslCheatInfo& cheat : osl_cheats) CHECK(std::string(cheat.key) != osl_experience_multiplier.key);
}

void TestSettings() {
    const fs::path file = TempDir("experience-settings") / "osl.ini";
    const std::string path = file.string();
    const auto write = [&](const std::string& text) {
        std::ofstream out(file, std::ios::trunc | std::ios::binary);
        out << text;
    };
    write("show_launcher = 1\n");
    CHECK(osl_experience_multiplier_saved(path.c_str()) == OSL_EXPERIENCE_OFF);
    CHECK(osl_experience_multiplier_saved(nullptr) == OSL_EXPERIENCE_OFF);
    for (const int value : kChoices) {
        write("cheat_experience_multiplier = " + std::to_string(value) + "\n");
        CHECK(osl_experience_multiplier_saved(path.c_str()) == value);
    }
    for (const char* bad : {"0", "3", "2x", "on", "", "-5", "1000"}) {
        write(std::string("cheat_experience_multiplier = ") + bad + "\n");
        CHECK(osl_experience_multiplier_saved(path.c_str()) == OSL_EXPERIENCE_OFF);
    }
    for (const char* good : {"1", "2", "5", "10", "100"}) CHECK(IsValidSetting("cheat_experience_multiplier", good));
    for (const char* bad : {"0", "3", "2x", "Off", "true"}) CHECK(!IsValidSetting("cheat_experience_multiplier", bad));

    const auto managed = ManagedSettings();
    CHECK(std::count_if(managed.begin(), managed.end(), [](const ManagedSetting& m) {
              return m.key == "cheat_experience_multiplier" && m.default_value == "1";
          }) == 1);

    // Saved by the launcher, independent of the seven cheats, and Off after Reset all settings.
    write("cheat_experience_multiplier = 10\ncheat_infinite_pow = 1\n");
    {
        LauncherSettings settings(file);
        CHECK(settings.Get("cheat_experience_multiplier", "1") == "10");
        settings.Set("cheat_experience_multiplier", "100");
        CHECK(settings.Save());
    }
    CHECK(osl_experience_multiplier_saved(path.c_str()) == 100 &&
          osl_cheats_enabled(path.c_str()) == osl_cheat_bit(OSL_CHEAT_INFINITE_POW));
    {
        LauncherSettings settings(file);
        settings.ResetToDefaults();
        CHECK(settings.Get("cheat_experience_multiplier") == "1");
        CHECK(settings.Save());
    }
    CHECK(osl_experience_multiplier_saved(path.c_str()) == OSL_EXPERIENCE_OFF && osl_cheats_enabled(path.c_str()) == 0);
    CHECK(Contains(ReadFile(file), "cheat_experience_multiplier"));
}

void TestSession() {
    cheats::SessionExperience session;
    CHECK(session.Effective() == OSL_EXPERIENCE_OFF && session.FollowsSaved());
    // Saved Off: the cycle visits every choice and follows the saved value again at Off.
    session.Start(OSL_EXPERIENCE_OFF);
    const int from_off[] = {2, 5, 10, 100, 1, 2};
    for (const int expected : from_off) {
        CHECK(session.Cycle() == expected && session.Effective() == expected);
        CHECK(session.FollowsSaved() == (expected == OSL_EXPERIENCE_OFF) && session.Saved() == OSL_EXPERIENCE_OFF);
    }
    // Saved 5x: starts at 5x; reaching 5x again follows the saved value.
    session.Start(5);
    CHECK(session.Effective() == 5 && session.FollowsSaved());
    const int from_five[] = {10, 100, 1, 2, 5};
    for (const int expected : from_five) {
        CHECK(session.Cycle() == expected && session.Effective() == expected && session.Saved() == 5);
        CHECK(session.FollowsSaved() == (expected == 5));
    }
    // A new session removes the override; an unlisted saved value is Off.
    session.Cycle();
    session.Start(5);
    CHECK(session.Effective() == 5 && session.FollowsSaved());
    session.Start(7);
    CHECK(session.Saved() == OSL_EXPERIENCE_OFF && session.Effective() == OSL_EXPERIENCE_OFF);
}

void TestRuntimeState() {
    // The cheats' bytes and the multiplier's, side by side as the loader lays them out.
    alignas(8) volatile uint8_t flags[OSL_CHEAT_COUNT + 1] = {};
    volatile uint8_t* const flag = flags + OSL_CHEAT_COUNT;
    runtime_cheats_start(0);
    runtime_cheats_set_available(OSL_CHEAT_ALL, flags);
    runtime_experience_start(OSL_EXPERIENCE_OFF);

    // Unavailable: Off, cannot cycle, the byte stays 0; the seven cheats still switch.
    runtime_experience_set_available(0, flag);
    CHECK(!runtime_experience_available() && runtime_experience_multiplier() == OSL_EXPERIENCE_OFF);
    CHECK(runtime_experience_cycle() == -1 && *flag == 0);
    for (int c = 0; c < OSL_CHEAT_COUNT; ++c) {
        CHECK(runtime_cheat_toggle(c) == 1 && flags[c] == 1 && *flag == 0);
        CHECK(runtime_cheat_toggle(c) == 0 && flags[c] == 0);
    }
    runtime_experience_start(10);
    CHECK(runtime_experience_multiplier() == OSL_EXPERIENCE_OFF && *flag == 0);

    // Available: the byte holds the multiplier, 0 for Off.
    runtime_experience_start(OSL_EXPERIENCE_OFF);
    runtime_experience_set_available(1, flag);
    CHECK(runtime_experience_available() && runtime_experience_multiplier() == OSL_EXPERIENCE_OFF && *flag == 0);
    const int cycle[] = {2, 5, 10, 100, 1};
    for (const int expected : cycle) {
        CHECK(runtime_experience_cycle() == expected && runtime_experience_multiplier() == expected);
        CHECK(*flag == (expected == OSL_EXPERIENCE_OFF ? 0 : expected));
        for (int c = 0; c < OSL_CHEAT_COUNT; ++c) CHECK(flags[c] == 0);
    }
    // Saved 5x starts at 5x; a cheat toggled meanwhile leaves the multiplier alone.
    runtime_experience_start(5);
    CHECK(runtime_experience_multiplier() == 5 && *flag == 5);
    CHECK(runtime_cheat_toggle(OSL_CHEAT_INFINITE_HEALTH) == 1 && *flag == 5);
    CHECK(runtime_cheat_toggle(OSL_CHEAT_INFINITE_HEALTH) == 0);
    CHECK(runtime_experience_cycle() == 10 && *flag == 10);
    runtime_experience_start(5);
    CHECK(runtime_experience_multiplier() == 5 && *flag == 5);
    // Losing the hook turns the byte off.
    runtime_experience_set_available(0, flag);
    CHECK(*flag == 0 && runtime_experience_multiplier() == OSL_EXPERIENCE_OFF && runtime_experience_cycle() == -1);
    // The cheats unavailable do not make it unavailable.
    runtime_cheats_set_available(0, flags);
    runtime_experience_set_available(1, flag);
    CHECK(runtime_cheat_toggle(OSL_CHEAT_INFINITE_POW) == -1 && runtime_experience_cycle() == 10 && *flag == 10);

    runtime_experience_set_available(0, nullptr);
    runtime_experience_start(OSL_EXPERIENCE_OFF);
    runtime_cheats_set_available(0, nullptr);
    runtime_cheats_start(0);
}

void TestSessionNeverWritesSettings() {
    const fs::path file = TempDir("experience-session") / "osl.ini";
    {
        std::ofstream out(file, std::ios::trunc | std::ios::binary);
        out << "# comment\ncheat_experience_multiplier = 2\n";
    }
    const std::string before = ReadFile(file);
    const auto time = fs::last_write_time(file);
    alignas(8) volatile uint8_t flag = 0;
    runtime_experience_start(osl_experience_multiplier_saved(file.string().c_str()));
    runtime_experience_set_available(1, &flag);
    CHECK(flag == 2);
    for (int i = 0; i < 7; ++i) runtime_experience_cycle();
    CHECK(runtime_experience_multiplier() == 10 && flag == 10);
    CHECK(ReadFile(file) == before && fs::last_write_time(file) == time);
    runtime_experience_start(osl_experience_multiplier_saved(file.string().c_str()));
    CHECK(flag == 2 && ReadFile(file) == before);
    runtime_experience_set_available(0, nullptr);
    runtime_experience_start(OSL_EXPERIENCE_OFF);
}

void TestLocator() {
    Bytes image = Noise(0x20000, 11);
    PlantAward(image, 0x4000);
    OslCheatScan scan = ScanOf(image);
    CHECK(scan.state == OSL_CHEAT_COMPLETE && scan.related && scan.reason[0] == 0);
    CHECK(scan.sites[0].state == OSL_CHEAT_SITE_UNIQUE && scan.sites[0].at[0] == 0x4000 &&
          scan.sites[0].patch == 0x4000 + kRegionAt);
    CHECK(scan.sites[1].state == OSL_CHEAT_SITE_UNIQUE && scan.sites[1].at[0] == 0x4000 + kEndAt);

    // Shifted.
    Bytes shifted = Noise(0x20000, 12);
    PlantAward(shifted, 0x9003);
    scan = ScanOf(shifted);
    CHECK(scan.state == OSL_CHEAT_COMPLETE && scan.sites[0].at[0] == 0x9003);

    // Nothing there.
    scan = ScanOf(Noise(0x20000, 13));
    CHECK(scan.state == OSL_CHEAT_MISSING && Contains(scan.reason, "E1 experience award: not found"));

    // Its end gone, or somewhere else: refused.
    Bytes no_end = image;
    std::memset(no_end.data() + 0x4000 + kEndAt, 0xcc, 16);
    scan = ScanOf(no_end);
    CHECK(scan.state == OSL_CHEAT_PARTIAL && Contains(scan.reason, "E2 experience stored: not found"));
    Bytes far_end = no_end;
    Plant(far_end, 0x4000 + 0x800, kAwardEnd);
    scan = ScanOf(far_end);
    CHECK(scan.state == OSL_CHEAT_INVALID && Contains(scan.reason, "E2 experience stored: not in the hooked function"));
    Bytes end_before = no_end;
    Plant(end_before, 0x3000, kAwardEnd);
    CHECK(ScanOf(end_before).state == OSL_CHEAT_INVALID);

    // Twice: ambiguous.
    Bytes twice = image;
    Plant(twice, 0x10000, kAwardStart);
    scan = ScanOf(twice);
    CHECK(scan.state == OSL_CHEAT_AMBIGUOUS && scan.sites[0].hits == 2 && Contains(scan.reason, "E1 experience award: 2 matches"));

    // The region changed by something else: found, refused, nothing written.
    Bytes modified = image;
    modified[0x4000 + kRegionAt] = 0xe9;
    scan = ScanOf(modified);
    CHECK(scan.state == OSL_CHEAT_INVALID && scan.sites[0].state == OSL_CHEAT_SITE_MODIFIED &&
          Contains(scan.reason, "E1 experience award: other bytes than the original instruction"));
    // Any other byte of the signature: not this function.
    Bytes other = image;
    other[0x4000 + 20] ^= 0xff;
    CHECK(ScanOf(other).state == OSL_CHEAT_PARTIAL);

    // Installing: refused cases write nothing.
    const OslCodeRange range{0, 0x20000};
    for (const Bytes* refused : {&no_end, &far_end, &twice, &modified}) {
        Bytes copy = *refused;
        copy.resize(0x20000 + 0x1000, 0);
        const Bytes before = copy;
        CHECK(osl_install_experience(copy.data(), 0x20000, &range, 1, copy.data() + 0x20000, OSL_CHEAT_STUB_BYTES,
                                     copy.data() + 0x20800, nullptr) == 0);
        CHECK(copy == before);
    }
    // No room for the stub.
    {
        Bytes copy = image;
        copy.resize(0x21000, 0);
        const Bytes before = copy;
        OslCheatScan result;
        CHECK(osl_install_experience(copy.data(), 0x20000, &range, 1, copy.data() + 0x20000, OSL_CHEAT_STUB_BYTES - 1,
                                     copy.data() + 0x20800, &result) == 0);
        CHECK(copy == before && result.state == OSL_CHEAT_INVALID && Contains(result.reason, "no room"));
    }
    // Installed: only the region changes, into a jump to the stub; the stub's test and the changed block read the
    // multiplier's own byte.
    Bytes hooked = image;
    hooked.resize(0x21000, 0);
    const Bytes before = hooked;
    OslCheatScan result;
    CHECK(osl_install_experience(hooked.data(), 0x20000, &range, 1, hooked.data() + 0x20000, OSL_CHEAT_STUB_BYTES,
                                 hooked.data() + 0x20800, &result) == 1);
    CHECK(result.state == OSL_CHEAT_COMPLETE);
    for (size_t at = 0; at < 0x20000; ++at) {
        if (hooked[at] != before[at]) CHECK(at >= 0x4000 + kRegionAt && at < 0x4000 + kRegionAt + 5);
    }
    int32_t jump;
    std::memcpy(&jump, hooked.data() + 0x4000 + kRegionAt + 1, 4);
    CHECK(hooked[0x4000 + kRegionAt] == 0xe9 && int64_t(0x4000 + kRegionAt + 5) + jump == 0x20000);
    const uint8_t* stub = hooked.data() + 0x20000;
    int32_t test;
    std::memcpy(&test, stub + 8, 4);
    CHECK(Bytes(stub, stub + 8) == Hex("48 8d 64 24 80 9c 80 3d") && 0x20000 + 13 + test == 0x20800);
    CHECK(Bytes(stub + 24, stub + 29) == Hex(kRegion));
    const size_t changed_at = 24 + 5 + 14;
    const OslCheatSite& site = osl_experience_multiplier.sites[0];
    for (size_t k = 0; k < site.changed_size; ++k) {
        if (k < size_t(site.changed_flag_at) || k >= size_t(site.changed_flag_at) + 4) CHECK(stub[changed_at + k] == site.changed[k]);
    }
    int32_t read;
    std::memcpy(&read, stub + changed_at + site.changed_flag_at, 4);
    CHECK(int64_t(0x20000 + changed_at + site.changed_flag_at + 4) + read == 0x20800);
    // The original block returns after the region, so does the changed one.
    int32_t back;
    std::memcpy(&back, stub + changed_at + site.changed_size + 1, 4);
    CHECK(stub[changed_at + site.changed_size] == 0xe9 &&
          int64_t(0x20000 + changed_at + site.changed_size + 5) + back == int64_t(0x4000 + kRegionAt + 5));
    CHECK(hooked[0x20800] == 0);
    // Scanned again, the hooked site is reported, not hooked twice.
    Bytes again = hooked;
    CHECK(osl_install_experience(again.data(), 0x20000, &range, 1, again.data() + 0x20000, OSL_CHEAT_STUB_BYTES,
                                 again.data() + 0x20800, &result) == 0);
    CHECK(again == hooked && result.sites[0].state == OSL_CHEAT_SITE_MODIFIED);

    // The seven cheats are judged against its sites too, and it against theirs: a cheat whose code shares bytes with
    // it is refused, and so is it. Max Phozon Gain's signature starts with the 00 that ends the award's.
    Bytes shared = image;
    const OslCheatSite& p4 = osl_cheats[OSL_CHEAT_MAX_PHOZON_GAIN].sites[0];
    CHECK(p4.signature[0] == Hex(kAwardStart).back());
    std::copy(p4.signature, p4.signature + p4.signature_size, shared.begin() + 0x4000 + 46);
    OslCheatScan cheats[OSL_CHEAT_COUNT];
    const OslCodeRange whole{0, shared.size()};
    osl_cheat_scan(shared.data(), shared.size(), &whole, 1, cheats);
    CHECK(cheats[OSL_CHEAT_MAX_PHOZON_GAIN].state == OSL_CHEAT_INVALID &&
          Contains(cheats[OSL_CHEAT_MAX_PHOZON_GAIN].reason, "overlaps E1 experience award"));
    scan = ScanOf(shared);
    CHECK(scan.state == OSL_CHEAT_INVALID && Contains(scan.reason, "overlaps P4 phozon absorbed"));
}

// An executable block: the award function's start (the game's instructions, through the hooked region), then a
// capture of what the rest of the function receives, and the function's own epilogue.
class NativeAward {
public:
    static constexpr size_t kCodeAt = 0x100, kImageSize = 0x2000, kStubAt = 0x2000, kFlagAt = 0x3000, kSize = 0x4000;

    explicit NativeAward(const Bytes& start) {
#ifdef _WIN32
        memory_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, kSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
#else
        void* m = mmap(nullptr, kSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        memory_ = m == MAP_FAILED ? nullptr : static_cast<uint8_t*>(m);
#endif
        if (!memory_) return;
        std::memset(memory_, 0xcc, kSize);
        std::copy(start.begin(), start.end(), memory_ + kCodeAt);
        const Bytes end = Hex(kAwardEnd);
        std::copy(end.begin(), end.end(), memory_ + kCodeAt + kEndAt);
        memory_[kFlagAt] = 0;
        const OslCodeRange range{0, kImageSize};
        installed_ = osl_install_experience(memory_, kImageSize, &range, 1, memory_ + kStubAt, OSL_CHEAT_STUB_BYTES,
                                            memory_ + kFlagAt, nullptr) == 1;
        // After the region: mov [rdx],rax; mov [rdx+8],rsi; mov [rdx+16],rbx, then the function's epilogue.
        const Bytes capture = Hex("48 89 02 48 89 72 08 48 89 5a 10 48 83 c4 10 5b 41 5e 5d c3");
        std::copy(capture.begin(), capture.end(), memory_ + kCodeAt + kRegionAt + 5);
    }
    ~NativeAward() {
#ifdef _WIN32
        if (memory_) VirtualFree(memory_, 0, MEM_RELEASE);
#else
        if (memory_) munmap(memory_, kSize);
#endif
    }
    NativeAward(const NativeAward&) = delete;
    NativeAward& operator=(const NativeAward&) = delete;

    bool ready() const { return memory_ && installed_; }
    // The stub's test always taken: the changed block runs whatever the byte, as when it turns Off mid-award.
    void ForceChanged() { memory_[kStubAt + 13] = 0xeb; }
    volatile uint8_t* flag() { return memory_ + kFlagAt; }
    Bytes Code() const { return Bytes(memory_, memory_ + kStubAt + OSL_CHEAT_STUB_BYTES); }

    struct Result {
        uint64_t rax, rsi, rbx;
    };

    // The award as the rest of the function sees it: rax (the value made a float next) and esi (the zero test).
    Result Run(void* character, uint32_t award) {
        uint64_t out[3] = {~uint64_t{0}, ~uint64_t{0}, 0};
        using Fn = void(__attribute__((sysv_abi)) *)(void*, uint64_t, uint64_t*);
        // Garbage above the award's 32 bits: the game passes an int, so they are undefined.
        reinterpret_cast<Fn>(memory_ + kCodeAt)(character, 0xdead000000000000ull | award, out);
        return {out[0], out[1], out[2]};
    }

private:
    uint8_t* memory_ = nullptr;
    bool installed_ = false;
};

const uint32_t kAwards[] = {0, 1, 7, 30, 1234, 24999, 25000, 25001, 100000, 250000, 1250000, 2500000, 2500001,
                            0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu};

bool RunAll(NativeAward& native, int flag) {
    bool ok = true;
    char character[16] = {};
    for (const uint32_t award : kAwards) {
        const NativeAward::Result r = native.Run(character, award);
        const uint64_t expected = Expected(award, flag);
        ok = ok && r.rax == expected && uint32_t(r.rsi) == uint32_t(expected) &&
             r.rbx == reinterpret_cast<uintptr_t>(character);
    }
    return ok;
}

// The real award function's first bytes from the USA 1.00 build when it is prepared, else the written-out ones (which
// TestLocalBuilds checks against every build).
Bytes AwardStart(std::string& source) {
    const fs::path app0 = fs::path(OSL_SOURCE_DIR) / "game" / "prepared" / "CUSA05083-1.00";
    std::error_code ec;
    if (fs::is_regular_file(app0 / "eboot.bin", ec)) {
        std::vector<Bytes> modules;
        for (const char* name : kBundledModules) modules.push_back(ReadAll(app0 / "sce_module" / name));
        const LinkedImage linked = LinkExecutable(ReadAll(app0 / "eboot.bin"), modules);
        if (linked.eboot_error.empty() && linked.image.image.size() > 0x475810 + 47) {
            source = "CUSA05083-1.00 bytes";
            const auto at = linked.image.image.begin() + 0x475810;
            return Bytes(at, at + 47);
        }
    }
    source = "written-out bytes";
    return Hex(kAwardStart);
}

void TestNative() {
    std::string source;
    const Bytes start = AwardStart(source);
    CHECK(start == Hex(kAwardStart));
    NativeAward native(start);
    CHECK(native.ready());
    if (!native.ready()) return;
    const Bytes code = native.Code();

    // The byte alone, every value the runtime writes.
    bool direct = true;
    for (const int flag : {0, 2, 5, 10, 100}) {
        *native.flag() = uint8_t(flag);
        direct = RunAll(native, flag) && direct;
    }
    CHECK(direct);

    // Through the runtime: Off, 2x, 5x, 10x, 100x, then Off again.
    runtime_experience_start(OSL_EXPERIENCE_OFF);
    runtime_experience_set_available(1, native.flag());
    bool cycled = RunAll(native, 0);
    std::string seen = osl_experience_label(runtime_experience_multiplier());
    for (int i = 0; i < 5; ++i) {
        const int multiplier = runtime_experience_cycle();
        seen += std::string(" ") + osl_experience_label(multiplier);
        cycled = cycled && *native.flag() == (multiplier == OSL_EXPERIENCE_OFF ? 0 : multiplier) &&
                 RunAll(native, *native.flag());
    }
    CHECK(cycled && seen == "Off 2x 5x 10x 100x Off");

    // Zero stays zero, the cap is never passed and nothing wraps, at every multiplier.
    char character[16] = {};
    for (const int flag : {2, 5, 10, 100}) {
        *native.flag() = uint8_t(flag);
        CHECK(native.Run(character, 0).rax == 0 && native.Run(character, 0).rsi == 0);
        CHECK(native.Run(character, 0xffffffffu).rax == kCap && native.Run(character, 0x80000000u).rax == kCap);
        CHECK(native.Run(character, kCap / uint32_t(flag)).rax == kCap - kCap % uint32_t(flag));
        CHECK(native.Run(character, kCap / uint32_t(flag) + 1).rax == kCap);
        // The same award twice is multiplied once each time: nothing accumulates.
        CHECK(native.Run(character, 1234).rax == native.Run(character, 1234).rax);
    }
    *native.flag() = 100;
    CHECK(native.Run(character, 30).rax == 3000 && native.Run(character, 25000).rax == 2500000 &&
          native.Run(character, 25001).rax == 2500000);
    // Off is the game's own behaviour, unclamped.
    *native.flag() = 0;
    CHECK(native.Run(character, 0xffffffffu).rax == 0xffffffffu && native.Run(character, 3000000).rax == 3000000);

    // Turned Off between the stub's test and the changed block's read: a 0 counts as 1, never as a zeroed award.
    {
        NativeAward raced(start);
        CHECK(raced.ready());
        if (raced.ready()) {
            raced.ForceChanged();
            *raced.flag() = 0;
            CHECK(raced.Run(character, 1234).rax == 1234 && raced.Run(character, 0).rax == 0 &&
                  raced.Run(character, 0xffffffffu).rax == kCap);
            *raced.flag() = 5;
            CHECK(raced.Run(character, 1234).rax == 6170);
        }
    }

    // Hundreds of switches while it runs: the code never changes, only the byte.
    bool steady = true;
    runtime_experience_start(OSL_EXPERIENCE_OFF);
    for (int i = 0; i < 600; ++i) {
        const int multiplier = runtime_experience_cycle();
        steady = steady && RunAll(native, multiplier == OSL_EXPERIENCE_OFF ? 0 : multiplier) && native.Code() == code;
    }
    CHECK(steady && native.Code() == code);
    runtime_experience_set_available(0, nullptr);
    runtime_experience_start(OSL_EXPERIENCE_OFF);
    std::printf("  Experience Multiplier   HOOK VERIFIED (%s, %zu awards x Off/2x/5x/10x/100x, 600 live switches, code unchanged)\n",
                source.c_str(), std::size(kAwards));
}

// After the award: total = experience + award, then one level-up per threshold the total reaches, while below the
// last level (the level field counts from 0; 98 is level 99). Mirrors the game's loop; it is not hooked.
struct Character {
    uint32_t experience = 0;
    int level = 0;
};

int GameLevelUps(Character& c, uint32_t award, const std::vector<uint32_t>& table) {
    const uint32_t total = c.experience + award;
    int ups = 0;
    while (c.level != 98 && total >= table[size_t(std::min(c.level + 1, 98))]) {
        ++c.level;
        ++ups;
    }
    c.experience = total;
    return ups;
}

// The level of a total, counted from the table: the highest level whose threshold it reaches.
int LevelOf(uint32_t total, const std::vector<uint32_t>& table) {
    int level = 0;
    for (int i = 1; i <= 98; ++i)
        if (total >= table[size_t(i)]) level = i;
    return level;
}

// The threshold table the loop reads, resolved from the award function's own call to the threshold lookup.
std::vector<uint32_t> ThresholdTable(const Bytes& image, uint64_t award_at) {
    const uint64_t call = award_at + kEndAt - 10;
    if (image[call] != 0xe8) return {};
    int32_t rel;
    std::memcpy(&rel, image.data() + call + 1, 4);
    const uint64_t lookup = call + 5 + int64_t(rel);
    const Bytes expected = Hex("31 c0 85 ff 78 0f 83 ff 62 48 63 cf b8 62 00 00 00 48 0f 4e c1 48 8d 0d");
    if (lookup + 32 > image.size() || !std::equal(expected.begin(), expected.end(), image.begin() + int64_t(lookup))) return {};
    std::memcpy(&rel, image.data() + lookup + 24, 4);
    const uint64_t table = lookup + 28 + int64_t(rel);
    if (table + 99 * 4 > image.size()) return {};
    std::vector<uint32_t> out(99);
    std::memcpy(out.data(), image.data() + table, 99 * 4);
    return out;
}

void TestLevelModel(const std::vector<uint32_t>& table, const char* source) {
    CHECK(table.size() == 99);
    if (table.size() != 99) return;
    CHECK(table[0] == 0 && table[1] == 30 && table[2] == 80 && table[49] == 396820 && table[98] == kCap);
    for (size_t i = 1; i < table.size(); ++i) CHECK(table[i] > table[i - 1]);

    std::string start_source;
    NativeAward native(AwardStart(start_source));
    CHECK(native.ready());
    if (!native.ready()) return;
    char object[16] = {};
    const auto award = [&](uint32_t base, int flag) {
        *native.flag() = uint8_t(flag);
        return uint32_t(native.Run(object, base).rax);
    };
    // Off: 100 experience from level 1 reaches level 3 (80); 2x reaches level 4 (150 <= 200 < 250).
    Character off;
    CHECK(GameLevelUps(off, award(100, 0), table) == 2 && off.level == 2 && off.experience == 100);
    Character twice;
    CHECK(GameLevelUps(twice, award(100, 2), table) == 3 && twice.level == 3 && twice.experience == 200);
    // One award several levels at once, each multiplier: the loop and the table agree.
    bool multi = true;
    for (const int flag : {0, 2, 5, 10, 100}) {
        for (const uint32_t base : {29u, 30u, 500u, 4000u, 12345u, 25000u}) {
            Character c;
            const uint32_t gained = award(base, flag);
            const int ups = GameLevelUps(c, gained, table);
            multi = multi && c.experience == gained && c.level == LevelOf(gained, table) && ups == c.level;
        }
    }
    CHECK(multi);
    Character big;
    CHECK(GameLevelUps(big, award(4000, 100), table) > 40 && big.experience == 400000 && big.level == LevelOf(400000, table));
    // The last level: the capped award ends at level 99 and the loop stops there.
    Character near_top{table[97], 97};
    CHECK(GameLevelUps(near_top, award(0xffffffffu, 100), table) == 1 && near_top.level == 98 &&
          near_top.experience == table[97] + kCap);
    Character top{table[98], 98};
    CHECK(GameLevelUps(top, award(50000, 10), table) == 0 && top.level == 98);
    // Experience already part-way to the next level counts as before.
    Character partial{70, 1};
    CHECK(GameLevelUps(partial, award(5, 2), table) == 1 && partial.level == 2 && partial.experience == 80);
    std::printf("  Experience Multiplier   LEVEL MODEL VERIFIED (threshold table from %s, multi-level awards, level 99 stop)\n",
                source);
}

// Every prepared build: the sites, the table, nothing branching into the region, and the build scan's report.
void TestLocalBuilds() {
    size_t verified = 0;
    bool modelled = false;
    for (const GameBuild& build : SupportedBuilds()) {
        const fs::path app0 = fs::path(OSL_SOURCE_DIR) / "game" / "prepared" / build.folder;
        std::error_code ec;
        if (!fs::is_regular_file(app0 / "eboot.bin", ec)) {
            std::printf("  %-26s DATA UNAVAILABLE (Experience Multiplier)\n", build.label);
            continue;
        }
        const int failures = g_failures;
        std::vector<Bytes> modules;
        for (const char* name : kBundledModules) modules.push_back(ReadAll(app0 / "sce_module" / name));
        const LinkedImage linked = LinkExecutable(ReadAll(app0 / "eboot.bin"), modules);
        CHECK(linked.eboot_error.empty());
        if (!linked.eboot_error.empty()) continue;
        const MappedExecutable& mapped = linked.image;
        std::vector<OslCodeRange> code;
        for (const MappedSegment& s : mapped.segments) {
            if ((s.flags & 1) && s.vaddr < linked.eboot_size)
                code.push_back({s.vaddr, std::min(s.vaddr + s.memsz, linked.eboot_size)});
        }
        OslCheatScan scan;
        osl_experience_scan(mapped.image.data(), mapped.image.size(), code.data(), code.size(), &scan);
        CHECK(scan.state == OSL_CHEAT_COMPLETE);
        const auto expected = kLocalAward.find(build.folder);
        CHECK(expected != kLocalAward.end());
        const uint64_t at = scan.sites[0].at[0];
        if (expected != kLocalAward.end()) CHECK(at == expected->second);
        CHECK(scan.sites[1].at[0] == at + kEndAt);
        const Bytes start = Hex(kAwardStart);
        CHECK(std::equal(start.begin(), start.end(), mapped.image.begin() + int64_t(at)));
        // Also unique in the whole image, bundled modules included.
        const OslCodeRange whole{0, mapped.image.size()};
        OslCheatScan everywhere;
        osl_experience_scan(mapped.image.data(), mapped.image.size(), &whole, 1, &everywhere);
        CHECK(everywhere.state == OSL_CHEAT_COMPLETE && everywhere.sites[0].at[0] == at);
        // No rel32 call or jump lands inside the region (after its first byte); the byte scan over-approximates.
        const uint64_t region = at + kRegionAt;
        size_t jumps_in = 0;
        for (const OslCodeRange& r : code) {
            for (uint64_t p = r.begin; p + 6 <= r.end; ++p) {
                const uint8_t op = mapped.image[p];
                int64_t target = -1;
                int32_t rel;
                if (op == 0xe8 || op == 0xe9) {
                    std::memcpy(&rel, mapped.image.data() + p + 1, 4);
                    target = int64_t(p) + 5 + rel;
                } else if (op == 0x0f && (mapped.image[p + 1] & 0xf0) == 0x80) {
                    std::memcpy(&rel, mapped.image.data() + p + 2, 4);
                    target = int64_t(p) + 6 + rel;
                }
                jumps_in += target > int64_t(region) && target < int64_t(region + 5);
            }
        }
        CHECK(jumps_in == 0);
        const std::vector<uint32_t> table = ThresholdTable(mapped.image, at);
        CHECK(table.size() == 99 && table[98] == kCap && table[1] == 30);
        if (!modelled && table.size() == 99) {
            TestLevelModel(table, build.label);
            modelled = true;
        }
        // The build scan: separate from the cheats' summary.
        const ScanResult result = ScanExecutable(mapped, linked.eboot_size);
        CHECK(result.experience && result.experience->state == FeatureState::kComplete &&
              result.experience->validator == "accepted");
        CHECK(ExperienceSummary(result) == "Experience Multiplier: COMPLETE" && CheatSummary(result) == "Cheats: 7/7 COMPLETE");
        CHECK(Contains(result.text, "  Experience Multiplier:\n    E1 experience award: unique at " + HexText(region) +
                                        ", original bytes 48 89 fb 89 f0 valid\n    E2 experience stored: unique at " +
                                        HexText(at + kEndAt) + "\n"));
        CHECK(Contains(result.text, "  Cheats: 7/7 COMPLETE\n  Experience Multiplier: COMPLETE\n"));
        const bool pass = g_failures == failures;
        verified += pass;
        std::printf("  %-26s %s (Experience Multiplier COMPLETE at %s, end +0x22b, table ok)\n", build.label,
                    pass ? "PASS" : "FAIL", HexText(at).c_str());
    }
    if (!modelled) {
        // No prepared build: the model runs on the table's values written out.
        std::printf("  Experience Multiplier   LEVEL MODEL DATA UNAVAILABLE (no prepared build)\n");
    }
    std::printf("Experience build parity: %zu build(s) verified on real files\n", verified);
}

void TestDiagnostics() {
    const MappedExecutable synthetic = SyntheticOdinImage();
    const ScanResult none = ScanExecutable(synthetic, synthetic.image.size());
    CHECK(none.experience && none.experience->state == FeatureState::kMissing);
    CHECK(ExperienceSummary(none) == "Experience Multiplier: MISSING" && Contains(none.text, "    E1 experience award: not found"));
    CHECK(none.complete() && none.cheats.size() == OSL_CHEAT_COUNT);

    // Room in the synthetic image's code that no other pattern uses.
    uint64_t spot = 0;
    for (uint64_t at = 0x1000; !spot && at + 0x1400 <= 0x2f000; at += 0x100) {
        if (std::all_of(synthetic.image.begin() + int64_t(at), synthetic.image.begin() + int64_t(at + 0x1400),
                        [](uint8_t b) { return b == 0; }))
            spot = at;
    }
    CHECK(spot != 0);
    MappedExecutable planted = synthetic;
    PlantAward(planted.image, spot);
    const Bytes before = planted.image;
    const ScanResult found = ScanExecutable(planted, planted.image.size());
    CHECK(planted.image == before);
    CHECK(ExperienceSummary(found) == "Experience Multiplier: COMPLETE" && found.experience->validator == "accepted");
    CHECK(Contains(found.text, "    relationship: accepted (2 distinct sites, apart from every other cheat's)"));

    MappedExecutable twice = planted;
    Plant(twice.image, spot + 0xa00, kAwardStart);
    CHECK(ExperienceSummary(ScanExecutable(twice, twice.image.size())) == "Experience Multiplier: AMBIGUOUS");
    MappedExecutable far_end = planted;
    std::memset(far_end.image.data() + spot + kEndAt, 0xcc, 16);
    Plant(far_end.image, spot + 0x900, kAwardEnd);
    const ScanResult invalid = ScanExecutable(far_end, far_end.image.size());
    CHECK(ExperienceSummary(invalid) == "Experience Multiplier: INVALID" &&
          Contains(invalid.text, "rejected (E2 experience stored: not in the hooked function)"));
    MappedExecutable modified = planted;
    modified.image[spot + kRegionAt + 2] = 0x90;
    const ScanResult changed = ScanExecutable(modified, modified.image.size());
    CHECK(ExperienceSummary(changed) == "Experience Multiplier: INVALID" &&
          Contains(changed.text, "original bytes 48 89 fb 89 f0 invalid: found 48 89 90 89 f0"));
    // Optional like the cheats: never part of the executable mapping.
    CHECK(changed.complete() && invalid.complete());

    // Both diagnostic reports print it apart from the cheats' line.
    const std::string diagnostics = ReadFile(fs::path(OSL_SOURCE_DIR) / "launcher" / "build_diagnostics.cpp");
    size_t count = 0;
    for (size_t at = diagnostics.find("ExperienceSummary(scan)"); at != std::string::npos;
         at = diagnostics.find("ExperienceSummary(scan)", at + 1))
        ++count;
    CHECK(count == 2);
}

void TestMenuRow() {
    const auto rows = cheats::MenuRows();
    CHECK(cheats::kMenuRowCount == 8 && rows.size() == 8);
    const char* const labels[] = {"Max Gold Gain",   "Max Phozon Gain", "No AP Consumption", "Infinite PP",
                                  "Infinite POW",    "Infinite Health", "Infinite Arrows",   "Experience Multiplier"};
    for (size_t i = 0; i < rows.size(); ++i) {
        CHECK(std::string(rows[i].label) == labels[i]);
        CHECK((rows[i].kind == cheats::RowKind::Experience) == (i == 7));
        if (i < 7) CHECK(rows[i].cheat == osl_cheats[i].cheat);
    }
    CHECK(std::string(cheats::ExperienceText(true, 1)) == "Off" && std::string(cheats::ExperienceText(true, 2)) == "2x" &&
          std::string(cheats::ExperienceText(true, 100)) == "100x" &&
          std::string(cheats::ExperienceText(false, 10)) == "Unavailable");
    CHECK(!cheats::ExperienceLit(true, 1) && cheats::ExperienceLit(true, 5) && !cheats::ExperienceLit(false, 5));
    CHECK(cheats::ExperienceLogLine(2) == "Cheats: Experience Multiplier 2x (this session)" &&
          cheats::ExperienceLogLine(100) == "Cheats: Experience Multiplier 100x (this session)" &&
          cheats::ExperienceLogLine(1) == "Cheats: Experience Multiplier Off (this session)");

    // The last row, reached by stepping down (the list stops there) and by a click, cycles the multiplier only.
    alignas(8) volatile uint8_t flags[OSL_CHEAT_COUNT + 1] = {};
    runtime_cheats_start(0);
    runtime_cheats_set_available(OSL_CHEAT_ALL, flags);
    runtime_experience_start(OSL_EXPERIENCE_OFF);
    runtime_experience_set_available(1, flags + OSL_CHEAT_COUNT);
    const int count = int(cheats::kMenuRowCount);
    cheats::MenuList list;
    list.Open();
    for (int i = 0; i < 20; ++i) list.Step(1, count);
    CHECK(list.selected == 7 && rows[size_t(list.selected)].kind == cheats::RowKind::Experience);
    CHECK(runtime_experience_cycle() == 2 && flags[OSL_CHEAT_COUNT] == 2);
    for (int c = 0; c < OSL_CHEAT_COUNT; ++c) CHECK(flags[c] == 0);
    list.Click(7, count);
    CHECK(list.selected == 7);
    // Unavailable: shown so, cannot cycle; the seven toggles still work.
    runtime_experience_set_available(0, flags + OSL_CHEAT_COUNT);
    CHECK(std::string(cheats::ExperienceText(runtime_experience_available() != 0, runtime_experience_multiplier())) ==
          "Unavailable");
    CHECK(runtime_experience_cycle() == -1 && flags[OSL_CHEAT_COUNT] == 0);
    CHECK(runtime_cheat_toggle(OSL_CHEAT_INFINITE_ARROWS) == 1 && flags[OSL_CHEAT_INFINITE_ARROWS] == 1);
    runtime_experience_set_available(0, nullptr);
    runtime_experience_start(OSL_EXPERIENCE_OFF);
    runtime_cheats_set_available(0, nullptr);
    runtime_cheats_start(0);

    // 100%, 150% and 225% display scale: eight rows fit without a scrollbar, and the last row can be scrolled to.
    const float widths[] = {1280, 1920, 2560, 3840};
    const float heights[] = {720, 1080, 1440, 2160};
    for (const float display : {1.0f, 1.5f, 2.25f}) {
        for (size_t i = 0; i < 4; ++i) {
            const float em_pixels = 18.0f * std::max(heights[i] / 1080.0f, 0.75f);
            const float scale = cheats::MenuScale(display, widths[i], em_pixels);
            const float em = em_pixels * scale;
            const float row = em + 2.0f * 4.0f * scale + 4.0f * scale;
            const float spacing = 4.0f * scale;
            const float body = cheats::MenuBodyHeight(cheats::kMenuRowCount, row, spacing);
            const float available = heights[i] - 2.0f * em * 0.8f - em * 4.0f;
            CHECK(!cheats::MenuNeedsScrollbar(body, std::min(body, available)));
            // In a half-height viewport the last row still comes into view.
            const float viewport = body / 2.0f;
            const float last_top = 7 * (row + spacing);
            const float scroll = cheats::MenuScrollToShowRow(last_top, last_top + row, 0, viewport);
            CHECK(scroll >= last_top + row - viewport - 0.01f && scroll <= last_top + 0.01f);
        }
    }

    // The overlay draws the row from the runtime and logs the session change.
    const std::string overlay = ReadFile(fs::path(OSL_SOURCE_DIR) / "gpu" / "shim" / "osl_overlay.cpp");
    for (const char* part : {"rows[index].kind == osl::cheats::RowKind::Experience", "runtime_experience_cycle()",
                             "osl::cheats::ExperienceLogLine(multiplier)", "runtime_experience_available()",
                             "osl::cheats::ExperienceText(available, multiplier)"}) {
        CHECK(Contains(overlay, part));
    }
    const std::string loader = ReadFile(fs::path(OSL_SOURCE_DIR) / "src" / "loader.c");
    CHECK(Contains(loader, "printf(\"Cheats: %s %s\\n\", osl_experience_multiplier.label, osl_experience_label(experience));"));
    CHECK(Contains(loader, "osl_install_experience(") && Contains(loader, "round_page(OSL_CHEAT_COUNT + 1)") &&
          Contains(loader, "round_page(OSL_CHEAT_STUBS_BYTES + OSL_CHEAT_STUB_BYTES)"));
}

void TestPageRow() {
    const std::vector<CheatPageRow> rows = CheatPageRows();
    CHECK(!rows.empty() && rows.back().setting == &osl_experience_multiplier && !rows.back().cheat && !rows.back().heading);
    size_t settings = 0;
    for (const CheatPageRow& row : rows) settings += row.setting != nullptr;
    CHECK(settings == 1);
    const std::string launcher = ReadFile(fs::path(OSL_SOURCE_DIR) / "launcher" / "launcher.cpp");
    CHECK(Contains(launcher, "for (const OslChoice& choice : osl_experience_choices)") &&
          Contains(launcher, "ui.Combo(row.setting->label, row.setting->description, row.setting->key, "
                             "osl_experience_choices[0].saved,"));
}

} // namespace

void RunExperienceTests() {
    std::printf("Experience Multiplier:\n");
    TestChoices();
    TestSettings();
    TestSession();
    TestRuntimeState();
    TestSessionNeverWritesSettings();
    TestLocator();
    TestNative();
    TestLocalBuilds();
    TestDiagnostics();
    TestMenuRow();
    TestPageRow();
}

} // namespace cheat_test
