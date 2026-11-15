// Unsupported-build diagnostics: the read-only sources, SELF inspection and linking, the scanner's feature states,
// classification, the report, prompt and title asset checks, the issue #19 regression and the install paths, on
// synthetic data. Local game folders and packages under game\ add the known-build self-tests when present.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "build_diagnostics.h"
#include "build_scan.h"
#include "game_files.h"
#include "game_install.h"
#include "game_profile.h"
#include "game_source.h"
#include "synthetic_package.h"
#include "test_support.h"

#ifndef OSL_SOURCE_DIR
#define OSL_SOURCE_DIR "."
#endif

namespace fs = std::filesystem;
using namespace osl;
using namespace osl_test;
using Bytes = std::vector<uint8_t>;

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        ++g_checks;                                                                            \
        if (!(condition)) {                                                                    \
            ++g_failures;                                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                  \
        }                                                                                      \
    } while (0)

template <typename T> void Put(Bytes& out, size_t at, T value) {
    if (out.size() < at + sizeof(T)) {
        out.resize(at + sizeof(T));
    }
    std::memcpy(out.data() + at, &value, sizeof(T));
}

struct Segment {
    uint32_t type = 1, flags = 5;
    uint64_t vaddr = 0, memsz = 0;
    Bytes data;
};

// A plaintext PS4 SELF holding `segments`, each stored whole, as the boot image reads them.
Bytes MakeSelf(const std::vector<Segment>& segments, uint64_t entry = 0x40) {
    const uint16_t count = static_cast<uint16_t>(segments.size());
    const uint64_t base = 32 + uint64_t{count} * 32;
    const uint64_t headers = base + 64;
    uint64_t data_at = (headers + 56 * segments.size() + 15) & ~uint64_t{15};
    Bytes file(data_at, 0);
    const uint8_t magic[4] = {'O', 0x15, '=', 0x1d};
    std::memcpy(file.data(), magic, 4);
    file[4] = 0;
    file[5] = 1;
    file[6] = 1;
    file[7] = 0x12;
    Put<uint16_t>(file, 12, static_cast<uint16_t>(base));
    Put<uint16_t>(file, 24, count);
    const uint8_t ident[7] = {0x7f, 'E', 'L', 'F', 2, 1, 1};
    std::memcpy(file.data() + base, ident, sizeof(ident));
    Put<uint16_t>(file, base + 16, 0xfe10);
    Put<uint16_t>(file, base + 18, 62);
    Put<uint64_t>(file, base + 24, entry);
    Put<uint64_t>(file, base + 32, 64);
    Put<uint16_t>(file, base + 54, 56);
    Put<uint16_t>(file, base + 56, count);
    for (size_t i = 0; i < segments.size(); ++i) {
        const Segment& s = segments[i];
        const uint64_t ph = headers + i * 56;
        Put<uint32_t>(file, ph, s.type);
        Put<uint32_t>(file, ph + 4, s.flags);
        Put<uint64_t>(file, ph + 8, data_at);
        Put<uint64_t>(file, ph + 16, s.vaddr);
        Put<uint64_t>(file, ph + 32, s.data.size());
        Put<uint64_t>(file, ph + 40, s.memsz);
        Put<uint64_t>(file, ph + 48, 0x4000);
        const uint64_t entry_at = 32 + i * 32;
        Put<uint64_t>(file, entry_at, 0x800 | (uint64_t{i} << 20));
        Put<uint64_t>(file, entry_at + 8, data_at);
        Put<uint64_t>(file, entry_at + 16, s.data.size());
        Put<uint64_t>(file, entry_at + 24, s.data.size());
        file.resize(data_at + s.data.size());
        std::memcpy(file.data() + data_at, s.data.data(), s.data.size());
        data_at = (data_at + s.data.size() + 15) & ~uint64_t{15};
        file.resize(data_at, 0);
    }
    Put<uint64_t>(file, 16, file.size());
    return file;
}

// The SELF whose segments map to `image`'s bytes in [from, to), at addresses relative to `from`.
Bytes SelfOf(const MappedExecutable& image, uint64_t from, uint64_t to) {
    std::vector<Segment> segments;
    for (const MappedSegment& m : image.segments) {
        const uint64_t begin = std::max(m.vaddr, from), end = std::min(m.vaddr + m.memsz, to);
        if (begin >= end) {
            continue;
        }
        Segment s;
        s.flags = m.flags;
        s.vaddr = begin - from;
        s.memsz = end - begin;
        s.data.assign(image.image.begin() + static_cast<std::ptrdiff_t>(begin), image.image.begin() + static_cast<std::ptrdiff_t>(end));
        segments.push_back(std::move(s));
    }
    return MakeSelf(segments, image.entry - std::min(image.entry, from));
}

Bytes SmallModule(uint8_t fill) {
    Segment s;
    s.memsz = 0x2000;
    s.data.assign(0x1800, fill);
    return MakeSelf({s}, 0);
}

Bytes AsBytes(const std::string& text) {
    return Bytes(text.begin(), text.end());
}

// A source held in memory, reporting itself as a package, for what a synthetic PKG cannot provide.
class MemorySource final : public GameSource {
public:
    explicit MemorySource(std::map<std::string, Bytes> files, std::string kind = "package")
        : files_(std::move(files)), kind_(std::move(kind)) {}

    std::string Kind() const override { return kind_; }
    std::vector<SourceEntry> Files() const override {
        std::vector<SourceEntry> out;
        for (const auto& [path, bytes] : files_) {
            out.push_back({path, bytes.size()});
        }
        return out;
    }
    const std::map<std::string, Bytes>& files() const { return files_; }

protected:
    bool ReadWhole(const std::string& path, uint64_t max_bytes, Bytes& out) const override {
        const auto it = files_.find(path);
        if (it == files_.end() || it->second.size() > max_bytes) {
            return false;
        }
        out = it->second;
        return true;
    }
    bool ReadPart(const std::string& path, uint64_t offset, size_t length, Bytes& out) const override {
        const auto it = files_.find(path);
        if (it == files_.end() || offset > it->second.size() || length > it->second.size() - offset) {
            return false;
        }
        out.assign(it->second.begin() + static_cast<std::ptrdiff_t>(offset),
                   it->second.begin() + static_cast<std::ptrdiff_t>(offset + length));
        return true;
    }

private:
    std::map<std::string, Bytes> files_;
    std::string kind_;
};

const char* const kResources[] = {
    "ArchiveViewer/ArchiveList.txt", "ArchiveViewer/ArchiveListBookshelf.txt", "GUI/HD_bookshelf.ftx", "GUI/HD_kitchin.ftx",
    "GUI/REHD_kitchin.mbs",          "GUI/REHD_title.mbs",                     "GUI/HD_chrselect.ftx", "msgsheet/dramamsg_gwe1.fms",
    "msgsheet/dramamsg_cor1.fms",    "msgsheet/dramamsg_mer1.fms",             "msgsheet/dramamsg_osw1.fms",
    "msgsheet/dramamsg_vel1.fms",
};

// A Refined archive: `resources` of the characteristic entries, then `extra`.
Bytes RefinedArchive(size_t resources, const std::vector<cpk_test::Entry>& extra = {}) {
    std::vector<cpk_test::Entry> entries;
    for (size_t i = 0; i < resources && i < std::size(kResources); ++i) {
        entries.push_back({kResources[i], std::string(64, char('a' + i)), false});
    }
    entries.insert(entries.end(), extra.begin(), extra.end());
    return AsBytes(cpk_test::BuildCpk(entries, true));
}

Bytes Sfo(const std::vector<std::pair<std::string, std::string>>& values) {
    const fs::path dir = TempDir("diag-sfo");
    WriteSfo(dir / "param.sfo", values);
    return AsBytes(ReadFile(dir / "param.sfo"));
}

std::vector<std::pair<std::string, std::string>> AsiaSfo() {
    return {{"TITLE", "Odin Sphere Leifthrasir"},
            {"TITLE_ID", "CUSA03873"},
            {"CONTENT_ID", "HP0177-CUSA03873_00-ODINSPHERELE0000"},
            {"CATEGORY", "gd"},
            {"APP_VER", "01.00"},
            {"VERSION", "01.00"}};
}

std::string SfoString(const Bytes& sfo) {
    return std::string(sfo.begin(), sfo.end());
}

void WriteTree(const fs::path& root, const std::map<std::string, Bytes>& files) {
    for (const auto& [path, bytes] : files) {
        const fs::path file = root / fs::path(path);
        fs::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                     static_cast<std::streamsize>(bytes.size()));
    }
}

// The synthetic image as a game: eboot.bin, both modules and `extra` files.
std::map<std::string, Bytes> SyntheticGame(const MappedExecutable& image, std::map<std::string, Bytes> extra) {
    extra["eboot.bin"] = SelfOf(image, 0, image.image.size());
    extra["sce_module/libc.prx"] = SmallModule(0x11);
    extra["sce_module/libSceFios2.prx"] = SmallModule(0x22);
    extra["sce_discmap.plt"] = Bytes(16, 1);
    return extra;
}

const FeatureScan* Feature(const ScanResult& scan, const char* name) {
    for (const FeatureScan& feature : scan.features) {
        if (feature.name == name) {
            return &feature;
        }
    }
    return nullptr;
}

FeatureState StateOf(const ScanResult& scan, const char* name) {
    const FeatureScan* feature = Feature(scan, name);
    return feature ? feature->state : FeatureState::kMissing;
}

void TestSources() {
    const fs::path root = TempDir("diag-folder-source");
    const Bytes payload = AsBytes(cpk_test::Pattern(5000, 7));
    WriteTree(root, {{"eboot.bin", payload}, {"sce_sys/param.sfo", Bytes(8, 2)}, {"Odin2_RE_US.CPK", Bytes(3, 3)}});
    const std::unique_ptr<GameSource> folder = OpenFolderSource(root);
    CHECK(folder->Kind() == "game folder");
    const std::vector<SourceEntry> files = folder->Files();
    CHECK(files.size() == 3 && files[0].path == "Odin2_RE_US.CPK" && files[1].path == "eboot.bin" &&
          files[2].path == "sce_sys/param.sfo" && files[1].size == payload.size());
    Bytes out;
    CHECK(folder->Read("eboot.bin", payload.size(), out) && out == payload);
    CHECK(!folder->Read("eboot.bin", payload.size() - 1, out) && out.empty());
    CHECK(folder->ReadRange("eboot.bin", 100, 50, out) && out == Bytes(payload.begin() + 100, payload.begin() + 150));
    CHECK(folder->ReadRange("eboot.bin", payload.size() - 10, 10, out) && out.size() == 10);
    CHECK(!folder->ReadRange("eboot.bin", payload.size() - 10, 11, out) && out.empty());
    CHECK(!folder->ReadRange("eboot.bin", payload.size() + 1, 0, out));
    CHECK(!folder->ReadRange("eboot.bin", 0, kMaxRangeRead + 1, out));
    CHECK(!folder->Read("missing.bin", 1 << 20, out) && !folder->ReadRange("missing.bin", 0, 1, out));
    for (const char* escape : {"../eboot.bin", "/eboot.bin", "sce_sys/../eboot.bin", "C:/Windows/win.ini", "a\\b", ""}) {
        CHECK(!folder->Read(escape, 1 << 20, out));
    }
    CHECK(!folder->Read("sce_sys", 1 << 20, out));
    CHECK(folder->reads() == 3 && folder->bytes_read() == payload.size() + 60);

    const MemorySource memory({{"a.bin", Bytes(100, 9)}});
    CHECK(memory.Kind() == "package" && memory.ReadRange("a.bin", 90, 10, out) && out == Bytes(10, 9));
    CHECK(!memory.ReadRange("a.bin", 95, 10, out) && !memory.Read("a.bin", 99, out));
    CHECK(memory.bytes_read() == 10 && memory.reads() == 1);

    // A CPK's tables through range reads: a few chunks, never the whole archive.
    std::vector<cpk_test::Entry> entries = {{"GUI/HD_title.ftx", cpk_test::Pattern(0x40000, 3), false}};
    const MemorySource archive({{"Odin2_RE_US.CPK", AsBytes(cpk_test::BuildCpk(entries, true))}});
    CpkArchive cpk;
    std::string error;
    CHECK(OpenSourceCpk(archive, {"Odin2_RE_US.CPK", archive.files().at("Odin2_RE_US.CPK").size()}, cpk, &error, 4096));
    CHECK(cpk.entries().size() == 1 && cpk.Find("gui/hd_title.ftx") && archive.bytes_read() < 0x6000);
    const uint64_t before = archive.bytes_read();
    CHECK(ReadSourceCpkEntry(archive, "Odin2_RE_US.CPK", *cpk.Find("GUI/HD_title.ftx"), 1 << 20, out, &error) &&
          out == AsBytes(entries[0].data));
    CHECK(!ReadSourceCpkEntry(archive, "Odin2_RE_US.CPK", *cpk.Find("GUI/HD_title.ftx"), 0x1000, out, &error) &&
          archive.bytes_read() == before + 0x40000);
    entries[0].compressed = true;
    const MemorySource packed({{"x.cpk", AsBytes(cpk_test::BuildCpk(entries, false))}});
    CHECK(OpenSourceCpk(packed, {"x.cpk", packed.files().at("x.cpk").size()}, cpk) &&
          ReadSourceCpkEntry(packed, "x.cpk", cpk.entries()[0], 1 << 20, out) && out == AsBytes(entries[0].data));
    CHECK(!OpenSourceCpk(packed, {"missing.cpk", 100}, cpk, &error) && !error.empty());

    // An entry larger than one range read comes back whole, in bounded reads.
    const std::string large = cpk_test::Pattern(kMaxRangeRead + 4096, 9);
    const MemorySource big({{"big.cpk", AsBytes(cpk_test::BuildCpk({{"GUI/HD_title.ftx", large, false}}, false))}});
    CHECK(OpenSourceCpk(big, {"big.cpk", big.files().at("big.cpk").size()}, cpk) &&
          ReadSourceCpkEntry(big, "big.cpk", cpk.entries()[0], 32u << 20, out) && out == AsBytes(large));
}

void TestExecutableInspection() {
    Segment code, data;
    code.memsz = 0x3000;
    code.data.assign(0x2800, 0xcc);
    data.flags = 6;
    data.vaddr = 0x4000;
    data.memsz = 0x2000;
    data.data.assign(0x100, 0x55);
    const Bytes self = MakeSelf({code, data}, 0x1234);
    const ExecutableInfo info = InspectExecutable(self);
    CHECK(info.self && info.elf && info.error.empty());
    CHECK(info.machine == 62 && info.elf_type == 0xfe10 && info.entry == 0x1234 && info.attributes == 0x12);
    CHECK(info.segment_count == 2 && info.segments.size() == 2 && info.headers.size() == 2);
    CHECK(info.segments[1].blocked() && info.segments[1].header() == 1 && info.headers[1].vaddr == 0x4000 &&
          info.headers[1].filesz == 0x100 && info.headers[1].memsz == 0x2000 && info.headers[1].flags == 6);
    CHECK(info.segments[0].offset == info.headers[0].offset && info.declared_size == self.size());
    MappedExecutable mapped;
    CHECK(MapExecutableBytes(self, mapped).empty() && mapped.image.size() == 0x6000 && mapped.entry == 0x1234);
    CHECK(mapped.image[0x27ff] == 0xcc && mapped.image[0x2800] == 0 && mapped.image[0x4000] == 0x55);

    CHECK(InspectExecutable({}).error == "empty file" && !InspectExecutable({}).self);
    const Bytes elf_only = {0x7f, 'E', 'L', 'F'};
    CHECK(!InspectExecutable(elf_only).self && !InspectExecutable(elf_only).error.empty());
    Bytes truncated(self.begin(), self.begin() + 200);
    const ExecutableInfo cut = InspectExecutable(truncated);
    CHECK(cut.self && !cut.error.empty() && !MapExecutableBytes(truncated, mapped).empty());
    Bytes bad_machine = self;
    Put<uint16_t>(bad_machine, 32 + 2 * 32 + 18, 3);
    CHECK(!InspectExecutable(bad_machine).error.empty());
    Bytes wild = self;
    Put<uint16_t>(wild, 24, 0xffff);
    CHECK(!InspectExecutable(wild).error.empty());

    // Modules are linked after the eboot at 64 KiB boundaries; a missing or broken one is named, the rest still links.
    const Bytes libc = SmallModule(0x11), fios = SmallModule(0x22);
    LinkedImage linked = LinkExecutable(self, std::vector<Bytes>{libc, fios});
    CHECK(linked.eboot_error.empty() && linked.eboot_size == 0x6000);
    CHECK(linked.modules.size() == 2 && linked.modules[0].name == "libc.prx" && linked.modules[0].base == 0x10000 &&
          linked.modules[0].size == 0x2000 && linked.modules[1].base == 0x20000 && linked.modules[1].error.empty());
    CHECK(linked.image.image.size() == 0x22000 && linked.image.image[0x10000] == 0x11 && linked.image.image[0x20000] == 0x22);
    linked = LinkExecutable(self, std::vector<Bytes>{Bytes{}, AsBytes("broken")});
    CHECK(linked.modules[0].error == "missing or unreadable" && !linked.modules[1].error.empty() &&
          linked.image.image.size() == 0x6000);
    linked = LinkExecutable(AsBytes("not a self"), std::vector<Bytes>{libc, fios});
    CHECK(!linked.eboot_error.empty() && linked.modules[0].error == "the executable did not map");
}

void TestFeatureStates() {
    const MappedExecutable image = SyntheticOdinImage();
    const ScanResult scan = ScanExecutable(image, image.image.size());
    CHECK(scan.features.size() == 5 && scan.complete());
    for (const FeatureScan& feature : scan.features) {
        CHECK(feature.state == FeatureState::kComplete && feature.validator == "accepted" && feature.invariant.empty());
    }
    const FeatureScan* resolution = Feature(scan, "Resolution");
    CHECK(resolution && resolution->components.size() == 1 && resolution->components[0].expected == 17 &&
          resolution->components[0].unique == 17);
    size_t complete = 0;
    for (size_t at = scan.text.find("status: COMPLETE"); at != std::string::npos; at = scan.text.find("status: COMPLETE", at + 1)) {
        ++complete;
    }
    CHECK(complete == 5 && Contains(scan.text, "  runtime validator: accepted\n"));
    CHECK(Contains(scan.text, "notice scene: unique " + HexText(kPlantedNotice + 19)));

    MappedExecutable duplicated = image;
    PlantPattern(duplicated, 0x10000, Shorts(osl_skip_intro_notice_code.bytes, osl_skip_intro_notice_code.count));
    const ScanResult ambiguous = ScanExecutable(duplicated, duplicated.image.size());
    CHECK(StateOf(ambiguous, "Skip intro") == FeatureState::kAmbiguous && Unresolved(ambiguous, "Skip intro notice scene"));
    CHECK(StateOf(ambiguous, "Resolution") == FeatureState::kComplete);

    MappedExecutable no_mark = image;
    std::fill_n(no_mark.image.begin() + kPlantedMarks + 0x20, 16, uint8_t{0x90});
    const ScanResult partial = ScanExecutable(no_mark, no_mark.image.size());
    const FeatureScan* shelf = Feature(partial, "Unlock Bookshelf");
    CHECK(shelf && shelf->state == FeatureState::kPartial && shelf->components.size() == 4);
    CHECK(shelf && shelf->components[3].name == "marks" && shelf->components[3].unique == 1 && shelf->components[3].missing() == 1);
    CHECK(shelf && shelf->components[2].unique == 14 && shelf->validator.empty());

    MappedExecutable two_sites = image;
    const std::vector<OslResolutionSite>& sites = SyntheticLayout(0).resolution;
    std::fill_n(two_sites.image.begin() + sites[15].offset, 10, uint8_t{0x90});
    const ScanResult fewer = ScanExecutable(two_sites, two_sites.image.size());
    const FeatureScan* res = Feature(fewer, "Resolution");
    CHECK(res && res->state == FeatureState::kPartial && res->components[0].unique == 16 && res->components[0].missing() == 1);

    MappedExecutable wrong_scale = image;
    PlantFloat(wrong_scale, kPlantedData, 2.0f);
    const ScanResult invalid = ScanExecutable(wrong_scale, wrong_scale.image.size());
    const FeatureScan* snapshot = Feature(invalid, "Snapshot scale");
    CHECK(snapshot && snapshot->state == FeatureState::kInvalid && Contains(snapshot->invariant, "not 1.5"));
    CHECK(Contains(invalid.text, "scan invariant failed: the native scale is not 1.5"));

    // Located by the scan, refused by the loader's own check: the stubs' page reaches past the image.
    MappedExecutable short_image = image;
    short_image.image.resize(0x2f900);
    short_image.segments = {{0, 0x2f800, 5}};
    const ScanResult refused = ScanExecutable(short_image, short_image.image.size());
    const FeatureScan* skip = Feature(refused, "Skip intro");
    CHECK(skip && skip->state == FeatureState::kInvalid && skip->invariant.empty() &&
          Contains(skip->validator, "refused (the stub space is not inside a code segment)"));
    CHECK(Unresolved(refused, "Skip intro patch check") && !refused.mapping.skip_intro);
    CHECK(Contains(refused.text, "stub space: free") && Contains(refused.text, "status: INVALID"));

    // A signature that is not found leaves hints where its literal runs still match, never a mapping.
    MappedExecutable shifted = image;
    shifted.image[kPlantedHd + 2] ^= 0xff;
    const ScanResult hinted = ScanExecutable(shifted, shifted.image.size());
    CHECK(StateOf(hinted, "Skip intro") == FeatureState::kPartial && Contains(hinted.text, "    partial: bytes "));
    CHECK(!hinted.mapping.skip_intro);

    MappedExecutable empty;
    empty.image.assign(0x1000, 0);
    empty.segments = {{0, 0x1000, 5}};
    const ScanResult nothing = ScanExecutable(empty, empty.image.size());
    for (const FeatureScan& feature : nothing.features) {
        CHECK(feature.state == FeatureState::kMissing);
    }
    CHECK(!nothing.anything_found && Contains(nothing.text, "status: MISSING"));

    // Bounded even when every signature is hinted.
    MappedExecutable noisy = image;
    for (size_t at = 0x20000; at + 64 < 0x2f000; at += 64) {
        std::memcpy(&noisy.image[at], osl_bookshelf_total_code, 8);
    }
    std::fill_n(noisy.image.begin() + kPlantedTotal, OSL_BOOKSHELF_TOTAL_BYTES, uint8_t{0x90});
    CHECK(ScanExecutable(noisy, noisy.image.size()).text.size() < 32 * 1024);
}

struct Expect {
    bool odin;
    const char* why;
};

void TestClassification() {
    const MappedExecutable image = SyntheticOdinImage();
    const Bytes odin_sfo = Sfo({{"TITLE_ID", "CUSA05083"}, {"CONTENT_ID", "UP0177-CUSA05083_00-ODINSPHERELE0000"},
                                {"APP_VER", "01.09"}, {"VERSION", "01.09"}, {"CATEGORY", "gd"}});
    const Bytes unknown_odin = Sfo({{"TITLE", "Odin Sphere Leifthrasir"}, {"TITLE_ID", "CUSA99998"},
                                    {"CONTENT_ID", "XP9999-CUSA99998_00-ODINSPHERELE0000"}, {"APP_VER", "01.00"}, {"VERSION", "01.00"}});
    const Bytes foreign = Sfo({{"TITLE", "Another Game"}, {"TITLE_ID", "CUSA99999"},
                               {"CONTENT_ID", "UP0000-CUSA99999_00-OTHERGAME0000000"}, {"APP_VER", "01.00"}});
    const Bytes generic_cpk = AsBytes(cpk_test::BuildCpk({{"data/level1.bin", std::string(64, 'x'), false}}, false));

    const auto diagnose = [&](const Bytes& sfo, std::map<std::string, Bytes> files) {
        files["sce_sys/param.sfo"] = sfo;
        const MemorySource source(std::move(files));
        return DiagnoseUnsupportedBuild(source, {}, "test");
    };

    // Known TITLE_ID with a game layout.
    UnsupportedBuildReport known = diagnose(odin_sfo, SyntheticGame(image, {{"Odin2_BT_US.CPK", Bytes(9, 'x')}}));
    CHECK(known.odin && Contains(known.text, "evidence: TITLE_ID CUSA05083 is a known Odin Sphere Leifthrasir title"));
    CHECK(Contains(known.text, "identified by: metadata and game structure"));

    // Unknown TITLE_ID: the title and CONTENT_ID name the game, and the structure is the game's.
    UnsupportedBuildReport by_metadata = diagnose(unknown_odin, SyntheticGame(image, {}));
    CHECK(by_metadata.odin && Contains(by_metadata.text, "evidence: CONTENT_ID names ODINSPHERE") &&
          Contains(by_metadata.text, "evidence: TITLE names Odin Sphere") &&
          Contains(by_metadata.text, "evidence: the bundled modules are present, with sce_discmap.plt"));
    CHECK(!Contains(by_metadata.text, "is a known Odin Sphere Leifthrasir title"));

    // Unknown TITLE_ID and foreign metadata, but an archive that is the game's: identified by the archive alone.
    UnsupportedBuildReport by_archive = diagnose(foreign, SyntheticGame(image, {{"Data_RE.CPK", RefinedArchive(12)}}));
    CHECK(by_archive.odin && Contains(by_archive.text, "identified by: strong archive evidence") &&
          Contains(by_archive.text, "Data_RE.CPK holds 12/12 characteristic Odin resources (strong archive evidence)"));
    CHECK(Contains(by_archive.text, "evidence: param.sfo does not name Odin Sphere Leifthrasir"));

    const Expect refused[] = {{false, "a random game with an eboot.bin"}, {false, "a renamed random file"},
                              {false, "a valid generic CPK named like the game's"}, {false, "too few resources"}};
    const std::map<std::string, Bytes> cases[] = {
        SyntheticGame(image, {{"game.cpk", generic_cpk}}),
        SyntheticGame(image, {{"Odin2_FAKE.CPK", Bytes(4096, 'r')}}),
        SyntheticGame(image, {{"Odin2_RE_XX.CPK", generic_cpk}}),
        SyntheticGame(image, {{"Odin2_RE_XX.CPK", RefinedArchive(7)}}),
    };
    for (size_t i = 0; i < std::size(cases); ++i) {
        const UnsupportedBuildReport report = diagnose(foreign, cases[i]);
        if (report.odin != refused[i].odin) {
            std::printf("unexpected classification: %s\n", refused[i].why);
        }
        CHECK(report.odin == refused[i].odin && report.text.empty() && !report.evidence.empty());
    }
    CHECK(Contains(diagnose(foreign, cases[3]).evidence.back(), "holds 7/12 characteristic Odin resources"));

    // An incomplete Odin folder: metadata alone is not a game; with its archives it is, and the missing executable is named.
    CHECK(!diagnose(odin_sfo, {}).odin);
    const UnsupportedBuildReport incomplete = diagnose(odin_sfo, {{"Odin2_RE_US.CPK", RefinedArchive(12)}});
    CHECK(incomplete.odin && Contains(incomplete.text, "evidence: no eboot.bin") &&
          Contains(incomplete.text, "eboot.bin: not in the source") && Contains(incomplete.text, "Mapping incomplete"));

    // Malformed inputs are reported, never trusted.
    const UnsupportedBuildReport bad_sfo =
        diagnose(Bytes(64, 0xee), SyntheticGame(image, {{"Odin2_RE_US.CPK", RefinedArchive(12)}}));
    CHECK(bad_sfo.odin && Contains(bad_sfo.text, "TITLE_ID: (missing)") && Contains(bad_sfo.text, "Identity: incomplete"));
    std::map<std::string, Bytes> bad_self = SyntheticGame(image, {{"Odin2_BT_US.CPK", Bytes(9, 'x')}});
    bad_self["eboot.bin"].resize(300);
    const UnsupportedBuildReport broken = diagnose(odin_sfo, bad_self);
    CHECK(broken.odin && Contains(broken.text, "Executable: not readable") && Contains(broken.text, "not run: the executable did not map"));
    CHECK(Contains(broken.text, "Features: 0/5 COMPLETE") && Contains(broken.text, "Mapping incomplete"));
    Bytes bad_cpk = RefinedArchive(12);
    bad_cpk.resize(0x900);
    const UnsupportedBuildReport torn = diagnose(odin_sfo, SyntheticGame(image, {{"Odin2_RE_US.CPK", bad_cpk}}));
    CHECK(torn.odin && Contains(torn.text, "Odin2_RE_US.CPK: 2304 bytes, CPK NOT READABLE ("));

    CHECK(Contains(diagnose(odin_sfo, {}).evidence.front(), "TITLE_ID CUSA05083"));
    const UnsupportedBuildReport usa =
        diagnose(Sfo(UsaSfo("gd", "01.00", "01.01")), SyntheticGame(image, {{"Odin2_BT_US.CPK", Bytes(9, 'x')}}));
    CHECK(Contains(usa.text, "  package identity: the same as CUSA05083 1.00 USA's"));
}

std::map<std::string, Bytes> AsiaArchives(const std::string& ko_title) {
    using cpk_test::Entry;
    const std::string iface = cpk_test::Pattern(20480, 7), atlas = cpk_test::Pattern(24576, 5);
    return {{"Odin2_BT_CN.CPK", AsBytes(cpk_test::BuildCpk({{"msgsheet/commonmsg.fms", std::string(32, 'c'), false}}, false))},
            {"Odin2_OR_CN.CPK", AsBytes(cpk_test::BuildCpk({{"Interface.ftx", iface, true}}, true))},
            {"Odin2_RE_CN.CPK", RefinedArchive(12, {{"GUI/HD_Interface.ftx", iface, false}, {"GUI/HD_title.ftx", atlas, false}})},
            {"Odin2_BT_KO.CPK", AsBytes(cpk_test::BuildCpk({{"msgsheet/commonmsg.fms", std::string(32, 'k'), false}}, false))},
            {"Odin2_OR_KO.CPK", AsBytes(cpk_test::BuildCpk({{"Interface.ftx", cpk_test::Painted(iface, {0x340}, 'K'), true}}, true))},
            {"Odin2_RE_KO.CPK", RefinedArchive(12, {{"GUI/HD_title.ftx", ko_title, false}})}};
}

struct Patches {
    std::string prompts, title;
    DiagnosticPatches Spans() const { return {cpk_test::Span(prompts), cpk_test::Span(title)}; }
};

Patches AsiaPatches() {
    const std::string iface = cpk_test::Pattern(20480, 7), atlas = cpk_test::Pattern(24576, 5);
    return {cpk_test::MakePatch({{"Interface.ftx", {iface, cpk_test::Painted(iface, {0x340}, 'X')}},
                                 {"GUI/HD_Interface.ftx", {iface, cpk_test::Painted(iface, {0x340}, 'X')}},
                                 {"OnMemory.ftx", {std::string(4096, 'o'), std::string(4096, 'O')}}}),
            cpk_test::MakePatch({{"GUI/HD_title.ftx", {atlas, cpk_test::Painted(atlas, {0x1230}, 'T')}}})};
}

// An update carries only what changed: the base package's files are named as limits, not as faults.
void TestUpdatePackage() {
    const MappedExecutable image = SyntheticOdinImage();
    const Bytes sfo = Sfo(UsaSfo("gp", "01.02", "01.00"));
    const MemorySource update({{"eboot.bin", SelfOf(image, 0, image.image.size())},
                               {"Odin2_US_PATCH2_RE_US.CPK", RefinedArchive(3)},
                               {"sce_discmap_patch.plt", Bytes(8, 1)}});
    const Patches patches = AsiaPatches();
    const UnsupportedBuildReport report = DiagnoseUnsupportedBuild(update, SfoString(sfo), "x", patches.Spans());
    const std::string& text = report.text;
    CHECK(report.odin && Contains(text, "evidence: an update (CATEGORY gp) carries eboot.bin"));
    CHECK(Contains(text, "Package limits:\n  sce_discmap.plt is not in this source\n"));
    CHECK(Contains(text, "  sce_module/libc.prx is not in this source") && Contains(text, "this is an update (CATEGORY gp)"));
    CHECK(Contains(text, "  sce_module/libc.prx: missing or unreadable") && Contains(text, "Executable mapping: incomplete"));
    CHECK(Contains(text, "Controller prompts: not in this update package") &&
          Contains(text, "Exit Game title art: not in this update package"));
}

void TestReport() {
    const MappedExecutable image = SyntheticOdinImage();
    const std::string atlas = cpk_test::Pattern(24576, 5);
    std::map<std::string, Bytes> files = SyntheticGame(image, AsiaArchives(atlas));
    const Bytes sfo = Sfo(AsiaSfo());
    const Patches patches = AsiaPatches();
    const MemorySource source(files);
    const UnsupportedBuildReport report = DiagnoseUnsupportedBuild(source, SfoString(sfo), "eboot.bin is not the supported build",
                                                                   patches.Spans());
    const std::string& text = report.text;
    CHECK(report.odin && text.starts_with(kDiagnosticsBegin) && text.ends_with(kDiagnosticsEnd));
    for (const std::string line : {
             "Read-only: nothing was extracted, patched or written.", "Supported-build check: eboot.bin is not the supported build",
             "  source: package", "  identified by: strong archive evidence", "  TITLE: Odin Sphere Leifthrasir",
             "  region (inferred from the CONTENT_ID prefix): Asia/Hong Kong", "  CATEGORY: gd",
             "  package identity shared by: CUSA03873 1.00 HK, CUSA03873 1.00 Asia; only eboot.bin tells them apart",
             "  Known build match: none (still unsupported)", "  ELF: type 0xfe10, machine 62, entry 0x1234",
             "  executable load segments: 1", "  rva 0x0..0x2f800 = file offset ",
             "UNKNOWN copy", "    image: base 0x40000 size 0x2000 linked",
             "  linked image: 0x52000, entry 0x1234", "  language archives present: CN, KO", "  archive families present: BT, OR, RE",
             "  runtime language routing: not proven", "  archive set equals CUSA03873 1.00 Asia's",
             "  compared with CUSA03873 1.00 HK: extra ODIN2_BT_KO.CPK, ODIN2_OR_KO.CPK, ODIN2_RE_KO.CPK; missing none",
             "  Odin2_RE_CN.CPK: ", "CPK valid, 14 entries, Odin resources 12/12 (strong), family RE, language CN",
             "  Resolution: COMPLETE (render size instructions 17/17 unique; validator accepted)",
             "  Render resolution and Aspect Ratio: available", "  CUSA03873 1.00 HK: no recorded sites to compare",
             "  Odin2_OR_CN.CPK/Interface.ftx: 20480 bytes, CRC32 ", "KNOWN (exact copy)",
             "  Odin2_OR_KO.CPK/Interface.ftx: 20480 bytes", "UNKNOWN", "  unknown: Odin2_OR_KO.CPK/Interface.ftx",
             "  targets of the patch absent from every archive: onmemory.ftx",
             "  verdict: DATA UNAVAILABLE: 1 unknown prompt asset(s); the Xbox prompts patch would refuse this build",
             "  verdict: could be enabled: every one of the 2 title file(s) is a known copy", "  Features: 5/5 COMPLETE",
             "  Mapping appears complete", "attach the complete log, never game files"}) {
        if (!Contains(text, line)) {
            std::printf("missing from the report: %s\n", line.c_str());
        }
        CHECK(Contains(text, line));
    }
    CHECK(Contains(text, "SHA-256: " + Sha256Hex(source.files().at("eboot.bin"))));
    CHECK(Contains(text, "  sce_module/libc.prx: " + std::to_string(files.at("sce_module/libc.prx").size()) + " bytes, SHA-256 " +
                             Sha256Hex(files.at("sce_module/libc.prx")) + ", valid SELF, UNKNOWN copy"));
    CHECK(Contains(text, "supported build of this TITLE_ID: CUSA03873 1.00 HK (APP_VER 01.00, VERSION 01.00, eboot SHA-256 "
                         "8c483dc2c06d958ccd84bdbf726df6d23e31592c7c1bb39eb5425718b5bc1eb3)"));
    CHECK(Contains(text, "supported build of this TITLE_ID: CUSA03873 1.00 Asia (APP_VER 01.00, VERSION 01.00, eboot SHA-256 "
                         "85ea2e4396b94a7b228d11e35a267f401cb2957bd116b3a5d23f5b3cb15a2fb2)"));
    CHECK(Contains(text, "Diagnostic reads: 10 files listed, 6 archives probed, ") && text.size() < 64 * 1024);

    // Deterministic, read-only, no payloads and no local paths.
    CHECK(DiagnoseUnsupportedBuild(source, SfoString(sfo), "eboot.bin is not the supported build", patches.Spans()).text == text);
    CHECK(source.files() == files);
    CHECK(!Contains(text, atlas.substr(0x100, 32)) && !Contains(text, fs::temp_directory_path().generic_string()));
    CHECK(!Contains(text, "status: incomplete") && !Contains(text, "DEBUG_SAULO"));

    // A title file the patch was not made for keeps Exit Game off, and the report names it.
    const UnsupportedBuildReport other = DiagnoseUnsupportedBuild(
        MemorySource(SyntheticGame(image, AsiaArchives(cpk_test::Painted(atlas, {0x1230}, 'Q')))), SfoString(sfo), "x", patches.Spans());
    CHECK(Contains(other.text, "  verdict: would stay off: Odin2_RE_KO.CPK/GUI/HD_title.ftx is not a copy the patch was made for"));
    CHECK(Contains(other.text, "  Exit Game title art: would stay off"));

    // Without patches the asset checks are skipped, not passed.
    const UnsupportedBuildReport bare = DiagnoseUnsupportedBuild(source, SfoString(sfo), "x");
    CHECK(Contains(bare.text, "Controller prompts: not checked") && Contains(bare.text, "Exit Game title art: not checked"));
}

// The issue #19 executable and its modules with the Asia archives, as a game's files.
std::map<std::string, Bytes> Issue19Files(size_t bulk = 0) {
    const MappedExecutable image = Issue19Image();
    std::map<std::string, Bytes> files = AsiaArchives(cpk_test::Pattern(24576, 5));
    if (bulk) {
        files["Odin2_BT_CN.CPK"] = AsBytes(cpk_test::BuildCpk(
            {{"msgsheet/commonmsg.fms", std::string(32, 'c'), false}, {"movie/bulk.bin", fakepkg::Noise(bulk, 3), false}}, false));
    }
    files["eboot.bin"] = SelfOf(image, 0, kIssue19EbootImage);
    files["sce_module/libc.prx"] = SelfOf(image, 0xda0000, 0xdb0000);
    files["sce_module/libSceFios2.prx"] = SelfOf(image, 0xdb0000, kIssue19LinkedImage);
    files["sce_discmap.plt"] = Bytes(16, 1);
    return files;
}

// Issue #19: the Asia executable's sites, a third executable of the same package identity built from them, its
// relation to the recorded Asia scan and to the USA tables, and the folder install path refusing it read-only.
void TestIssue19Regression() {
    std::map<std::string, Bytes> files = Issue19Files();
    const Bytes sfo = Sfo(AsiaSfo());
    files["sce_sys/param.sfo"] = sfo;
    const MemorySource source(files);
    const UnsupportedBuildReport report = DiagnoseUnsupportedBuild(source, {}, "eboot.bin is not the supported build");
    const std::string& text = report.text;
    CHECK(report.odin);
    for (const char* line : {
             "  TITLE_ID: CUSA03873", "  CONTENT_ID: HP0177-CUSA03873_00-ODINSPHERELE0000", "  APP_VER: 01.00", "  VERSION: 01.00",
             "  Known build match: none (still unsupported)", "  Entry point: 0x67d610, mapped image: 0xd9d4e0",
             "    image: base 0xda0000 size 0x10000 linked", "    image: base 0xdb0000 size 0x254570 linked",
             "  linked image: 0x1004570, entry 0x67d610", "notice scene: unique 0x53d", "getter: unique 0x5c1910",
             "site: unique 0x542f87", "sites: 17/17", "ranges: unique (49 shapes placed in order)", "  Features: 5/5 COMPLETE",
             "  Mapping appears complete", "  CUSA05083 1.00 USA: entry ", "    Resolution: all 17 sites at delta -0x19e0",
             "    Unlock Bookshelf: getter and 16 of 16 calls at delta -0x1740, special test -0x1230, same page read",
             "    Snapshot scale: delta -0x1230", "  CUSA03873 1.00 Asia: entry +0x0, linked image +0x0",
             "    Resolution: all 17 sites at delta +0x0",
             "    Legacy CPU LZCNT: 15 of 15 eboot ranges at delta +0x0, 34 of 34 module ranges at the same distance from the image end",
             "  closest structural relation: CUSA03873 1.00 Asia (5 of 5 compared features moved as one block)",
             "  CUSA03873 1.00 HK: no recorded sites to compare", "  archive set equals CUSA03873 1.00 Asia's",
             "  language archives present: CN, KO"}) {
        if (!Contains(text, line)) {
            std::printf("missing from the issue #19 report: %s\n", line);
        }
        CHECK(Contains(text, line));
    }
    CHECK(!Contains(text, "Supported-build self-check"));
    // The synthetic issue #19 executable holds none of the Issue #20 cheat code: Asia's real cheat mapping is DATA
    // UNAVAILABLE, and the report must say MISSING rather than borrow HK's.
    CHECK(Contains(text, "Cheats (identified only; applied only for a supported build whose sites validate):") &&
          Contains(text, "    P7 PP setter: not found") && Contains(text, "    relationship: not checked") &&
          Contains(text, "  Cheats: 0/7 COMPLETE, Max Gold Gain MISSING, Max Phozon Gain MISSING, No AP Consumption MISSING, "
                         "Infinite PP MISSING, Infinite POW MISSING, Infinite Health MISSING, Infinite Arrows MISSING "
                         "(identification only: cheats stay off while the build is unsupported)\n"
                         "  Experience Multiplier: MISSING\n"));
    CHECK(Contains(text, "    E1 experience award: not found") && Contains(text, "  Cheats: 0/7 COMPLETE, Max Gold Gain MISSING") &&
          Contains(text, "Infinite Arrows MISSING\n  Experience Multiplier: MISSING\n"));

    // The folder path: CheckGameTree keeps HK for an unknown hash and the diagnostics refuse it without writing.
    const fs::path root = TempDir("diag-issue19");
    const fs::path game = root / "game";
    WriteTree(game, files);
    const GameRoots roots{root / "prepared", root / "image"};
    const auto stamp = fs::last_write_time(game / "eboot.bin");
    const InstallResult result = InstallFromFolder(game, roots);
    CHECK(result.status == InstallStatus::kUnsupportedBuild && result.report.starts_with(kDiagnosticsBegin));
    CHECK(Contains(result.report, "  source: game folder") && Contains(result.report, "Supported-build check: eboot.bin is not the supported build"));
    CHECK(Contains(result.report, "closest structural relation: CUSA03873 1.00 Asia") && !Contains(result.report, root.generic_string()));
    CHECK(!fs::exists(roots.prepared) && !fs::exists(roots.image) && fs::last_write_time(game / "eboot.bin") == stamp);
    size_t count = 0;
    for (fs::recursive_directory_iterator it(game), end; it != end; ++it) {
        count += it->is_regular_file() ? 1 : 0;
    }
    CHECK(count == files.size());
}

void TestInstallPaths() {
    const fs::path root = TempDir("diag-install");
    const fs::path game = root / "game";
    const GameRoots roots{root / "prepared", root / "image"};
    WriteTree(game, {{"eboot.bin", Bytes(2048, 'x')}, {"Odin2_BT_US.CPK", AsBytes("not a cpk")}, {"sce_discmap.plt", Bytes(3, 'p')},
                     {"sce_sys/param.sfo", Bytes(4, 0)}});
    WriteSfo(game / "sce_sys" / "param.sfo", UsaSfo("gd", "01.02", "01.01"));
    const InstallResult future = InstallFromFolder(game, roots);
    CHECK(future.status == InstallStatus::kUnsupportedBuild && future.report.starts_with(kDiagnosticsBegin) &&
          future.report.ends_with(kDiagnosticsEnd));
    CHECK(Contains(future.report, "  APP_VER: 01.02") && Contains(future.report, "Odin2_BT_US.CPK: 9 bytes, CPK NOT READABLE"));
    CHECK(Contains(future.report, "  sce_discmap_patch.plt: absent") && Contains(future.report, "Mapping incomplete"));
    CHECK(Contains(future.report, "SHA-256: " + Sha256Hex(game / "eboot.bin")));
    CHECK(!fs::exists(roots.prepared) && !fs::exists(roots.image));

    WriteSfo(game / "sce_sys" / "param.sfo", UsaSfo("gd", "01.01", "01.01"));
    const InstallResult supported = InstallFromFolder(game, roots);
    CHECK(supported.status == InstallStatus::kIncomplete && supported.report.empty());

    fs::remove(game / "Odin2_BT_US.CPK");
    WriteSfo(game / "sce_sys" / "param.sfo", UsaSfo("gd", "01.02", "01.01"));
    const InstallResult no_archive = InstallFromFolder(game, roots);
    CHECK(no_archive.status == InstallStatus::kWrongVersion && no_archive.report.empty());

    WriteSfo(game / "sce_sys" / "param.sfo", {{"TITLE_ID", "CUSA99999"}, {"CONTENT_ID", "UP0000-CUSA99999_00-OTHERGAME000000"}});
    std::ofstream(game / "Odin2_BT_US.CPK", std::ios::binary) << "not a cpk";
    CHECK(InstallFromFolder(game, roots).report.empty());

    const MemorySource eboot({{"eboot.bin", Bytes(16, 'e')}});
    const GameBuild& usa = SupportedBuilds()[0];
    CHECK(ExecutableDiffers(eboot, usa) && !ExecutableDiffers(MemorySource({}), usa));

    const std::string installer = ReadFile(fs::path(OSL_SOURCE_DIR) / "launcher" / "game_install.cpp");
    CHECK(!Contains(installer, ".unsupported-build.install-temp") && !Contains(installer, ".identify-build.install-temp"));
    CHECK(Contains(installer, "OpenPackageSource(package)") && Contains(installer, "package.ReadFile(\"eboot.bin\""));
}

// The scan the report runs reproduces every local supported build's recorded sites; a build without local data is
// DATA UNAVAILABLE, never a pass.
void TestKnownBuilds() {
    const fs::path base = fs::path(OSL_SOURCE_DIR) / "game" / "prepared";
    for (const GameBuild& build : SupportedBuilds()) {
        const fs::path app0 = base / build.folder;
        std::error_code ec;
        if (!fs::is_regular_file(app0 / "eboot.bin", ec) || Sha256Hex(app0 / "eboot.bin") != build.eboot_sha256) {
            std::printf("not covered: DATA UNAVAILABLE: %s (no local prepared folder with its eboot.bin)\n", build.label);
            continue;
        }
        const auto stamp = fs::last_write_time(app0 / "eboot.bin", ec);
        const std::unique_ptr<GameSource> source = OpenFolderSource(app0);
        const UnsupportedBuildReport report = DiagnoseUnsupportedBuild(*source, {}, "self-test");
        const std::string& text = report.text;
        CHECK(report.odin && Contains(text, std::string("Known build match: the executable of supported build ") + build.label));
        CHECK(Contains(text, "identified by: strong archive evidence") && Contains(text, "Mapping appears complete"));
        CHECK(Contains(text, std::string("Supported-build self-check (") + build.label + "):"));
        const bool table = build.mapping == ExecutableMapping::kTable;
        for (const char* feature : {"Resolution", "Skip intro", "Unlock Bookshelf", "Snapshot scale", "Legacy CPU LZCNT"}) {
            CHECK(!table || Contains(text, std::string("  ") + feature + ": matches the recorded sites"));
        }
        CHECK(Contains(text, std::string("closest structural relation: ") + build.label));
        CHECK(Contains(text, "  sce_module/libc.prx: 923954 bytes") && Contains(text, "the copy the supported builds ship"));
        CHECK(fs::last_write_time(app0 / "eboot.bin", ec) == stamp && source->bytes_read() < (96ull << 20));
        std::printf("%s: %llu bytes read\n", build.label, static_cast<unsigned long long>(source->bytes_read()));
    }
}

// Each local package is read in place: its eboot, modules and archive tables, never its archive contents.
void TestLocalPackages() {
    std::error_code ec;
    size_t packages = 0;
    for (fs::directory_iterator it(fs::path(OSL_SOURCE_DIR) / "game", ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".pkg" || !it->is_regular_file(ec)) {
            continue;
        }
        Package package;
        if (!package.Open(it->path()).ok() || !package.LoadFiles().ok()) {
            std::printf("note: %s could not be opened\n", it->path().filename().string().c_str());
            continue;
        }
        ++packages;
        const std::unique_ptr<GameSource> source = OpenPackageSource(package);
        const std::vector<SourceEntry> files = source->Files();
        CHECK(!files.empty() && source->Kind() == "package");
        Bytes whole, part;
        CHECK(source->Read("eboot.bin", 128ull << 20, whole) && whole.size() > 0x200000);
        CHECK(source->ReadRange("eboot.bin", 0x10000 - 8, 16, part) &&
              part == Bytes(whole.begin() + 0x10000 - 8, whole.begin() + 0x10000 + 8));
        CHECK(!source->ReadRange("eboot.bin", whole.size() - 4, 8, part) && !source->Read("eboot.bin", 1024, part));
        CHECK(!source->Read("no/such/file", 1024, part));
        const uint64_t size = fs::file_size(it->path(), ec);
        const UnsupportedBuildReport report = DiagnoseUnsupportedBuild(*source, package.sfo(), "self-test");
        CHECK(report.odin && Contains(report.text, "  source: package") && Contains(report.text, "SHA-256: " + Sha256Hex(whole)));
        CHECK(source->bytes_read() < (128ull << 20) && fs::file_size(it->path(), ec) == size);
        std::printf("%s: %llu of %llu bytes read\n", it->path().filename().string().c_str(),
                    static_cast<unsigned long long>(source->bytes_read()), static_cast<unsigned long long>(size));
    }
    if (!packages) {
        std::printf("not covered: DATA UNAVAILABLE: no local package for the direct package reads\n");
    }
}

// ---- InstallFromPackage on synthetic packages: the launcher's own entry point, the real Package reader ----

const char kHk[] = "CUSA03873 1.00 HK";
const char kAsia[] = "CUSA03873 1.00 Asia";
const char kHkSha[] = "8c483dc2c06d958ccd84bdbf726df6d23e31592c7c1bb39eb5425718b5bc1eb3";
const char kAsiaSha[] = "85ea2e4396b94a7b228d11e35a267f401cb2957bd116b3a5d23f5b3cb15a2fb2";
const char kAsiaContentId[] = "HP0177-CUSA03873_00-ODINSPHERELE0000";
const char kFutureContentId[] = "XP9999-CUSA99998_00-ODINSPHERELE0000";

using Tree = std::map<std::string, std::string>;

Tree Snapshot(const fs::path& root) {
    Tree tree;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = fs::relative(it->path(), root).generic_string();
        if (it->is_directory()) {
            tree[name] = "<dir>";
            continue;
        }
        const std::string data = ReadFile(it->path());
        tree[name] = std::to_string(data.size()) + ":" + std::to_string(std::hash<std::string>{}(data)) + ":" +
                     std::to_string(static_cast<long long>(fs::last_write_time(it->path()).time_since_epoch().count()));
    }
    return tree;
}

bool ExtractedAnything(const fs::path& root) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = it->path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (name == "eboot.bin" || name == "libc.prx" || name == "libscifios2.prx" || name == "sce_discmap.plt" ||
            name.ends_with(".cpk") || name.find("install-temp") != std::string::npos ||
            name.find("identify-build") != std::string::npos || name.find("unsupported-build") != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string DiagnosticsOf(const std::string& log) {
    const size_t begin = log.find(kDiagnosticsBegin);
    if (begin == std::string::npos) {
        return {};
    }
    const size_t end = log.find(kDiagnosticsEnd, begin);
    return end == std::string::npos ? log.substr(begin) : log.substr(begin, end + std::strlen(kDiagnosticsEnd) - begin);
}

struct ReadVolume {
    bool found = false;
    size_t files = 0, archives = 0, reads = 0;
    unsigned long long bytes = 0;
};

ReadVolume VolumeOf(const std::string& text) {
    ReadVolume volume;
    const size_t at = text.find("Diagnostic reads: ");
    volume.found = at != std::string::npos &&
                   std::sscanf(text.c_str() + at, "Diagnostic reads: %zu files listed, %zu archives probed, %llu bytes read in %zu reads",
                               &volume.files, &volume.archives, &volume.bytes, &volume.reads) == 4;
    return volume;
}

std::vector<std::string> LinesOf(const std::string& text) {
    std::vector<std::string> lines;
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        end = end == std::string::npos ? text.size() : end;
        lines.push_back(text.substr(at, end - at));
        at = end + 1;
    }
    return lines;
}

std::string LineStarting(const std::string& text, const std::string& prefix) {
    for (const std::string& line : LinesOf(text)) {
        const size_t first = line.find_first_not_of(' ');
        if (first != std::string::npos && line.compare(first, prefix.size(), prefix) == 0) {
            return line.substr(first);
        }
    }
    return {};
}

size_t LongestHexRun(const std::string& text) {
    size_t best = 0, run = 0;
    for (const char c : text) {
        run = std::isxdigit(static_cast<unsigned char>(c)) ? run + 1 : 0;
        best = std::max(best, run);
    }
    return best;
}

uint64_t TotalSize(const std::map<std::string, Bytes>& files) {
    uint64_t total = 0;
    for (const auto& entry : files) {
        total += entry.second.size();
    }
    return total;
}

struct SyntheticPackage {
    fs::path root, file;
    GameRoots roots;
    std::map<std::string, Bytes> files;
};

// The package's inner file system holds `files` and sce_sys/param.sfo, as a real package does.
SyntheticPackage MakePackage(const char* name, const Bytes& sfo, const std::string& content_id,
                             std::map<std::string, Bytes> files, uint32_t image_key_index = 3) {
    SyntheticPackage made;
    made.root = TempDir(name);
    made.roots = {made.root / "prepared", made.root / "image"};
    files["sce_sys/param.sfo"] = sfo;
    made.files = files;
    made.file = made.root / "source" / "game.pkg";
    fakepkg::Spec spec;
    spec.content_id = content_id;
    spec.sfo = sfo;
    spec.files = std::move(files);
    spec.image_key_index = image_key_index;
    fakepkg::Write(made.file, fakepkg::Build(spec));
    return made;
}

void PlantInstalled(const GameRoots& roots, const char* label) {
    const GamePaths paths = BuildPaths(roots, *BuildWithLabel(label));
    fs::create_directories(paths.app0);
    fs::create_directories(paths.image_dir);
    std::ofstream(paths.app0 / "keep.txt") << label;
    std::ofstream(paths.image_dir / "boot.bin") << label;
}

InstallResult InstallWith(const fs::path& source, const GameRoots& roots, const Patches& patches,
                          std::span<const GameBuild> builds = SupportedBuilds()) {
    InstallHooks hooks;
    hooks.prompt_patch = cpk_test::Span(patches.prompts);
    hooks.title_patch = cpk_test::Span(patches.title);
    return InstallGame(source, roots, hooks, builds);
}

Bytes FutureSfo() {
    return Sfo({{"TITLE", "Odin Sphere Leifthrasir"}, {"TITLE_ID", "CUSA99998"}, {"CONTENT_ID", kFutureContentId},
                {"CATEGORY", "gd"}, {"APP_VER", "01.00"}, {"VERSION", "01.00"}});
}

// Package contents are read back through the real Package reader: block boundaries, stored and deflated blocks.
void TestPackageRangeReads() {
    const std::string noise = fakepkg::Noise(300000, 1);
    const std::string mixed = fakepkg::Noise(0x10000, 5) + std::string(0x10000, '\0') + fakepkg::Noise(5000, 6);
    const std::string large(kMaxRangeRead + 4096, '\0');
    const std::string cpk = cpk_test::BuildCpk({{"GUI/HD_title.ftx", fakepkg::Noise(200000, 9), false}}, true);
    const Bytes sfo = Sfo(AsiaSfo());
    const SyntheticPackage made = MakePackage("diag-pkg-ranges", sfo, kAsiaContentId,
                                              {{"noise.bin", AsBytes(noise)}, {"mixed.bin", AsBytes(mixed)},
                                               {"large.bin", AsBytes(large)}, {"Odin2_RE_CN.CPK", AsBytes(cpk)},
                                               {"a/b/c/deep.txt", AsBytes("deep")}, {"empty.bin", Bytes{}}});
    Package package;
    CHECK(package.Open(made.file).ok() && package.sfo() == SfoString(sfo) && package.content_id() == kAsiaContentId);
    CHECK(package.LoadFiles().ok() && package.file_count() == 7 && package.total_bytes() == TotalSize(made.files));
    std::map<std::string, uint64_t> sizes;
    for (const PackageFile& file : package.Files()) {
        sizes[file.path] = file.size;
    }
    CHECK(sizes.at("a/b/c/deep.txt") == 4 && sizes.at("large.bin") == large.size() && sizes.at("empty.bin") == 0);

    Bytes out;
    const auto same = [&](const std::string& path, const std::string& whole, uint64_t offset, size_t length) {
        return package.ReadFileRange(path, offset, length, out).ok() &&
               std::string(out.begin(), out.end()) == whole.substr(size_t(offset), length);
    };
    CHECK(same("noise.bin", noise, 0, 16) && same("noise.bin", noise, 0xFFF0, 0x40) && same("noise.bin", noise, 12345, 1));
    CHECK(same("noise.bin", noise, 0xFF00, 0x20200) && same("noise.bin", noise, noise.size() - 16, 16));
    CHECK(same("noise.bin", noise, 0, noise.size()));
    CHECK(same("mixed.bin", mixed, 0xFFF0, 0x20) && same("mixed.bin", mixed, 0x1FFF0, 0x20) && same("mixed.bin", mixed, 0, mixed.size()));
    CHECK(same("noise.bin", noise, noise.size(), 0) && out.empty());

    CHECK(!package.ReadFileRange("noise.bin", noise.size() - 8, 16, out).ok() && out.empty());
    CHECK(!package.ReadFileRange("noise.bin", noise.size() + 1, 0, out).ok());
    CHECK(!package.ReadFileRange("noise.bin", 0, noise.size() + 1, out).ok());
    CHECK(!package.ReadFileRange("missing.bin", 0, 1, out).ok() && !package.ReadFileRange("a/b", 0, 1, out).ok());
    CHECK(package.ReadFile("noise.bin", noise.size(), out).ok() && out.size() == noise.size());
    CHECK(!package.ReadFile("noise.bin", noise.size() - 1, out).ok() && out.empty());
    CHECK(package.ReadFile("a/b/c/deep.txt", 64, out).ok() && std::string(out.begin(), out.end()) == "deep");

    // The source in front of the package: the CPK's tables through range reads, and the range limit.
    const std::unique_ptr<GameSource> source = OpenPackageSource(package);
    CHECK(source->Kind() == "package" && source->Files().size() == 7);
    CpkArchive archive;
    std::string error;
    CHECK(OpenSourceCpk(*source, {"Odin2_RE_CN.CPK", cpk.size()}, archive, &error, 4096) && archive.entries().size() == 1);
    CHECK(archive.Find("gui/hd_title.ftx") && source->bytes_read() < cpk.size() / 2 && source->reads() >= 1);
    CHECK(ReadSourceCpkEntry(*source, "Odin2_RE_CN.CPK", archive.entries()[0], 1 << 20, out, &error) &&
          out.size() == 200000 && source->bytes_read() > cpk.size() / 2);
    CHECK(source->ReadRange("large.bin", 0, kMaxRangeRead, out) && out.size() == kMaxRangeRead &&
          std::all_of(out.begin(), out.end(), [](uint8_t b) { return b == 0; }));
    CHECK(!source->ReadRange("large.bin", 0, kMaxRangeRead + 1, out) && out.empty());
    CHECK(source->ReadRange("large.bin", large.size() - 4, 4, out) && !source->ReadRange("large.bin", large.size() - 4, 5, out));
    CHECK(!source->Read("large.bin", kMaxRangeRead, out) && source->Read("large.bin", large.size(), out) && out.size() == large.size());
    CHECK(!source->ReadRange("../noise.bin", 0, 1, out) && !source->ReadRange("a\\b", 0, 1, out));
}

// An unknown TITLE_ID that is still the game: through InstallGame, refused read-only with a report.
void TestUnknownTitlePackage() {
    const MappedExecutable image = SyntheticOdinImage();
    const Patches patches = AsiaPatches();

    // Metadata that names the game plus the game's structure.
    const SyntheticPackage structural = MakePackage("diag-pkg-unknown-title", FutureSfo(), kFutureContentId,
                                                    SyntheticGame(image, {{"Odin2_BT_XX.CPK", Bytes(9, 'x')}}));
    const Tree before = Snapshot(structural.root);
    InstallResult result = InstallWith(structural.file, structural.roots, patches);
    CHECK(result.status == InstallStatus::kUnsupportedBuild && result.status != InstallStatus::kWrongGame && !result.ok());
    CHECK(result.build == nullptr && result.bytes == 0 && result.files == 0);
    std::string report = DiagnosticsOf(result.report);
    CHECK(Contains(report, "  source: package") && Contains(report, "  TITLE_ID: CUSA99998"));
    CHECK(Contains(report, "  identified by: metadata and game structure") && Contains(report, "  Known build match: none (still unsupported)"));
    CHECK(Contains(result.report, "  Matched build: none (param.sfo TITLE_ID") && !Contains(report, "is a known Odin Sphere Leifthrasir title"));
    CHECK(Snapshot(structural.root) == before && !fs::exists(structural.roots.prepared) && !fs::exists(structural.roots.image));

    // Foreign metadata but a genuine Odin archive: the archive alone identifies it.
    const Bytes foreign = Sfo({{"TITLE", "Some Title"}, {"TITLE_ID", "CUSA99998"}, {"CONTENT_ID", "XP9999-CUSA99998_00-SOMETITLE0000000"}});
    const SyntheticPackage strong = MakePackage("diag-pkg-strong-evidence", foreign, "XP9999-CUSA99998_00-SOMETITLE0000000",
                                                SyntheticGame(image, {{"Odin2_RE_XX.CPK", RefinedArchive(12)}}));
    const Tree strong_before = Snapshot(strong.root);
    result = InstallWith(strong.file, strong.roots, patches);
    report = DiagnosticsOf(result.report);
    CHECK(result.status == InstallStatus::kUnsupportedBuild && !report.empty());
    CHECK(Contains(report, "  identified by: strong archive evidence") &&
          Contains(report, "  evidence: Odin2_RE_XX.CPK holds 12/12 characteristic Odin resources (strong archive evidence)") &&
          Contains(report, "  evidence: param.sfo does not name Odin Sphere Leifthrasir"));
    CHECK(Snapshot(strong.root) == strong_before && !fs::exists(strong.roots.prepared) && !fs::exists(strong.roots.image));
}

// A future CUSA03873 1.00 executable laid out like the issue #19 log: the whole path, no writes, bounded reads.
void TestUnknownHashPackage() {
    constexpr size_t kBulk = 6000000;
    const Bytes sfo = Sfo(AsiaSfo());
    std::map<std::string, Bytes> files = Issue19Files(kBulk);
    const std::string eboot_sha = Sha256Hex(files.at("eboot.bin"));
    CHECK(eboot_sha != kHkSha && eboot_sha != kAsiaSha);
    const SyntheticPackage made = MakePackage("diag-pkg-unknown-hash", sfo, kAsiaContentId, files);
    PlantInstalled(made.roots, kHk);
    PlantInstalled(made.roots, kAsia);
    const Patches patches = AsiaPatches();

    const Tree before = Snapshot(made.root);
    const InstallResult result = InstallWith(made.file, made.roots, patches);
    const Tree after = Snapshot(made.root);
    const std::string report = DiagnosticsOf(result.report);

    CHECK(result.status == InstallStatus::kUnsupportedBuild && result.build == nullptr && !result.ok());
    CHECK(Contains(result.report, "  Matched build: none (eboot.bin is not the supported build)"));
    CHECK(report.starts_with(kDiagnosticsBegin) && report.ends_with(kDiagnosticsEnd));

    // Nothing was written, unpacked or touched, and the installed HK and Asia folders are as they were.
    CHECK(after == before && !ExtractedAnything(made.root));
    CHECK(!fs::exists(made.roots.prepared / ".unsupported-build.install-temp") && !fs::exists(made.roots.prepared / ".identify-build.install-temp"));
    CHECK(before.count("source/game.pkg") == 1 && std::ifstream(made.file, std::ios::binary).good());
    const GamePaths hk = BuildPaths(made.roots, *BuildWithLabel(kHk)), asia = BuildPaths(made.roots, *BuildWithLabel(kAsia));
    CHECK(ReadFile(hk.app0 / "keep.txt") == kHk && ReadFile(hk.image_dir / "boot.bin") == kHk);
    CHECK(ReadFile(asia.app0 / "keep.txt") == kAsia && ReadFile(asia.image_dir / "boot.bin") == kAsia);
    for (const auto& entry : after) {
        CHECK(entry.first.find("prompts") == std::string::npos && entry.first.find("title") == std::string::npos);
    }

    // The report contract.
    for (const std::string& line : std::initializer_list<std::string>{
             "Unsupported build diagnostics begin", "Read-only: nothing was extracted, patched or written.", "Game identification:",
             "  source: package", "  TITLE_ID: CUSA03873", "  CONTENT_ID: HP0177-CUSA03873_00-ODINSPHERELE0000",
             "  SHA-256: " + eboot_sha, "  Known build match: none (still unsupported)",
             "  package identity shared by: CUSA03873 1.00 HK, CUSA03873 1.00 Asia; only eboot.bin tells them apart",
             "  SELF: version ", "  ELF: type 0xfe10, machine 62, entry 0x67d610, 3 program headers",
             "  Entry point: 0x67d610, mapped image: 0xd9d4e0", "Bundled modules (", "  language archives present: CN, KO",
             "  archive families present: BT, OR, RE", "Feature states:", "  Skip intro: COMPLETE (", "  Unlock Bookshelf: COMPLETE (",
             "  Snapshot scale: COMPLETE (", "  Resolution: COMPLETE (", "  Legacy CPU (LZCNT): COMPLETE (", "  Features: 5/5 COMPLETE",
             "  closest structural relation: CUSA03873 1.00 Asia (5 of 5 compared features moved as one block)",
             "Controller prompts (", "Exit Game title art (", "Diagnostic reads: ", "  Mapping appears complete",
             "attach the complete log, never game files", "Unsupported build diagnostics end"}) {
        if (!Contains(report, line)) {
            std::printf("missing from the package report: %s\n", line.c_str());
        }
        CHECK(Contains(report, line));
    }
    CHECK(Contains(report, "  sce_module/libc.prx: " + std::to_string(files.at("sce_module/libc.prx").size()) + " bytes, SHA-256 " +
                               Sha256Hex(files.at("sce_module/libc.prx"))));
    CHECK(Contains(report, "  CUSA03873 1.00 HK: no recorded sites to compare"));
    CHECK(!Contains(report, "Known build match: the executable"));

    // No payload, no long hex dumps, no local paths.
    CHECK(LongestHexRun(report) <= 64);
    CHECK(!Contains(report, fakepkg::Noise(kBulk, 3).substr(0x1000, 24)) && !Contains(report, cpk_test::Pattern(24576, 5).substr(0x100, 32)));
    CHECK(!Contains(report, made.root.generic_string()) && !Contains(report, fs::temp_directory_path().generic_string()));

    // Bounded, direct reads: every figure comes from the package source of this run.
    const ReadVolume volume = VolumeOf(report);
    const uint64_t total = TotalSize(made.files), eboot = files.at("eboot.bin").size(),
                   modules = files.at("sce_module/libc.prx").size() + files.at("sce_module/libSceFios2.prx").size();
    CHECK(volume.found && volume.files <= made.files.size() && volume.archives <= 6 && volume.reads > 0 && volume.reads < 400);
    CHECK(volume.bytes >= eboot + modules && volume.bytes < eboot + modules + (3ull << 20) && total - volume.bytes > kBulk / 2);
    std::printf("synthetic package: %llu of %llu file bytes read in %zu reads\n", volume.bytes,
                static_cast<unsigned long long>(total), volume.reads);

    // The same analysis through the folder source: one engine, the same findings.
    const fs::path folder = made.root / "folder-copy";
    WriteTree(folder, made.files);
    const GameRoots folder_roots{made.root / "folder-prepared", made.root / "folder-image"};
    const InstallResult from_folder = InstallWith(folder, folder_roots, patches);
    const std::string folder_report = DiagnosticsOf(from_folder.report);
    CHECK(from_folder.status == InstallStatus::kUnsupportedBuild && Contains(folder_report, "  source: game folder"));
    const auto comparable = [](const std::string& text) {
        std::vector<std::string> kept;
        for (const std::string& line : LinesOf(text)) {
            if (!line.starts_with("  source:") && !line.starts_with("Diagnostic reads:")) {
                kept.push_back(line);
            }
        }
        return kept;
    };
    CHECK(comparable(report) == comparable(folder_report));
    for (const char* prefix :
         {"identified by:", "TITLE_ID:", "CONTENT_ID:", "SHA-256:", "Known build match:", "SELF:", "ELF:", "Entry point:",
          "language archives present:", "archive families present:", "Features:", "closest structural relation:", "verdict:",
          "Controller prompts:", "Exit Game title art:", "Mapping appears complete"}) {
        CHECK(!LineStarting(report, prefix).empty() && LineStarting(report, prefix) == LineStarting(folder_report, prefix));
    }
    CHECK(!fs::exists(folder_roots.prepared) && !fs::exists(folder_roots.image));
    const ReadVolume folder_volume = VolumeOf(folder_report);
    CHECK(folder_volume.found && folder_volume.archives == volume.archives && folder_volume.files == volume.files);
}

// The same package family with both known eboot hashes: the supported-build path takes it, never the diagnostics.
void TestKnownEbootHashes() {
    const Bytes sfo = Sfo(AsiaSfo());
    std::map<std::string, Bytes> hk_files = Issue19Files(), asia_files = Issue19Files();
    asia_files["eboot.bin"][asia_files["eboot.bin"].size() - 1] ^= 0x5a;
    const std::string hk_sha = Sha256Hex(hk_files.at("eboot.bin")), asia_sha = Sha256Hex(asia_files.at("eboot.bin"));
    CHECK(hk_sha != asia_sha);

    std::vector<GameBuild> builds(SupportedBuilds().begin(), SupportedBuilds().end());
    GameBuild* hk = nullptr;
    GameBuild* asia = nullptr;
    for (GameBuild& build : builds) {
        hk = std::string_view(build.label) == kHk ? &build : hk;
        asia = std::string_view(build.label) == kAsia ? &build : asia;
    }
    CHECK(hk && asia && std::string_view(hk->eboot_sha256) == kHkSha && std::string_view(asia->eboot_sha256) == kAsiaSha);
    if (!hk || !asia) {
        return;
    }
    CHECK(BuildWithEboot(*hk, kHkSha) == BuildWithLabel(kHk) && BuildWithEboot(*hk, kAsiaSha) == BuildWithLabel(kAsia));
    CHECK(BuildWithEboot(*asia, kAsiaSha) == BuildWithLabel(kAsia) && BuildWithEboot(*asia, kHkSha) == BuildWithLabel(kHk));
    CHECK(BuildWithEboot(*hk, hk_sha) == nullptr && BuildWithEboot(*hk, "") == nullptr);
    CHECK(CheckPackageIdentity(SfoString(sfo)).state == GameFileState::kValid &&
          CheckPackageIdentity(SfoString(sfo)).build == BuildWithLabel(kHk));

    hk->eboot_sha256 = hk_sha.c_str();
    asia->eboot_sha256 = asia_sha.c_str();
    CHECK(BuildWithEboot(*hk, hk_sha, builds) == hk && BuildWithEboot(*hk, asia_sha, builds) == asia && BuildWithEboot(*hk, kHkSha, builds) == nullptr);

    const Patches patches = AsiaPatches();
    struct Case {
        const char* name;
        std::map<std::string, Bytes>* files;
        const char* label;
    };
    for (const Case& test : {Case{"diag-pkg-known-hk", &hk_files, kHk}, Case{"diag-pkg-known-asia", &asia_files, kAsia}}) {
        const SyntheticPackage made = MakePackage(test.name, sfo, kAsiaContentId, *test.files);
        const InstallResult result = InstallWith(made.file, made.roots, patches, builds);
        CHECK(result.status != InstallStatus::kUnsupportedBuild && result.status != InstallStatus::kWrongGame);
        CHECK(!Contains(result.report, kDiagnosticsBegin) && !Contains(result.report, "Matched build: none"));
        CHECK(Contains(result.report, std::string("  Matched build: ") + test.label + "\n"));
        CHECK(result.build == nullptr || std::string_view(result.build->label) == test.label);
        std::printf("%s: supported path, status %d\n", test.label, static_cast<int>(result.status));
    }
}

// Packages that are not the game, or cannot be read, never turn into diagnostics of the game.
void TestMalformedPackages() {
    const MappedExecutable image = SyntheticOdinImage();
    const Patches patches = AsiaPatches();
    const Bytes odin_sfo = FutureSfo();
    const std::string foreign_id = "UP0000-CUSA99999_00-OTHERGAME0000000";
    const Bytes foreign = Sfo({{"TITLE", "Another Game"}, {"TITLE_ID", "CUSA99999"}, {"CONTENT_ID", foreign_id}});
    const Bytes generic_cpk = AsBytes(cpk_test::BuildCpk({{"data/level1.bin", std::string(64, 'x'), false}}, false));
    Bytes torn = RefinedArchive(12);
    torn.resize(0x900);

    struct Case {
        const char* name;
        Bytes sfo;
        std::string content_id;
        std::map<std::string, Bytes> files;
        InstallStatus status;
    };
    std::map<std::string, Bytes> bad_self = SyntheticGame(image, {{"Odin2_RE_XX.CPK", RefinedArchive(12)}});
    bad_self["eboot.bin"].resize(300);
    const Case cases[] = {
        {"a random game", foreign, foreign_id, SyntheticGame(image, {{"game.cpk", generic_cpk}}), InstallStatus::kWrongGame},
        {"Odin metadata alone", odin_sfo, kFutureContentId, {{"eboot.bin", SelfOf(image, 0, image.image.size())}}, InstallStatus::kWrongGame},
        {"a renamed generic CPK", foreign, foreign_id, SyntheticGame(image, {{"Odin2_RE_XX.CPK", generic_cpk}}), InstallStatus::kWrongGame},
        {"too few Odin resources", foreign, foreign_id, SyntheticGame(image, {{"Odin2_RE_XX.CPK", RefinedArchive(7)}}), InstallStatus::kWrongGame},
        {"a torn Odin archive and foreign metadata", foreign, foreign_id, SyntheticGame(image, {{"Odin2_RE_XX.CPK", torn}}), InstallStatus::kWrongGame},
        {"a broken executable with strong evidence", foreign, foreign_id, bad_self, InstallStatus::kUnsupportedBuild},
    };
    for (const Case& test : cases) {
        const SyntheticPackage made = MakePackage("diag-pkg-malformed", test.sfo, test.content_id, test.files);
        const Tree before = Snapshot(made.root);
        const InstallResult result = InstallWith(made.file, made.roots, patches);
        if (result.status != test.status) {
            std::printf("unexpected status %d for %s\n", static_cast<int>(result.status), test.name);
        }
        CHECK(result.status == test.status && !result.ok());
        const std::string report = DiagnosticsOf(result.report);
        CHECK(report.empty() == (test.status != InstallStatus::kUnsupportedBuild));
        CHECK(Snapshot(made.root) == before && !fs::exists(made.roots.prepared) && !fs::exists(made.roots.image));
    }

    // The broken executable is still named as the fault.
    const SyntheticPackage broken = MakePackage("diag-pkg-broken-self", foreign, foreign_id, bad_self);
    const std::string broken_report = DiagnosticsOf(InstallWith(broken.file, broken.roots, patches).report);
    CHECK(Contains(broken_report, "  identified by: strong archive evidence") && Contains(broken_report, "Executable: not readable") &&
          Contains(broken_report, "not run: the executable did not map") && Contains(broken_report, "Features: 0/5 COMPLETE") &&
          Contains(broken_report, "Mapping incomplete"));

    // A package the reader cannot decrypt, cut short, or not a package at all.
    const Bytes usa = Sfo(UsaSfo("gd", "01.00", "01.01"));
    const SyntheticPackage console = MakePackage("diag-pkg-console", usa, "UP0177-CUSA05083_00-ODINSPHERELE0000", SyntheticGame(image, {}), 0);
    InstallResult result = InstallWith(console.file, console.roots, patches);
    CHECK(result.status == InstallStatus::kUnsupportedPackage && DiagnosticsOf(result.report).empty());
    CHECK(!fs::exists(console.roots.prepared) && !fs::exists(console.roots.image));
    const SyntheticPackage console_unknown = MakePackage("diag-pkg-console-unknown", odin_sfo, kFutureContentId, SyntheticGame(image, {}), 0);
    result = InstallWith(console_unknown.file, console_unknown.roots, patches);
    CHECK(result.status != InstallStatus::kUnsupportedBuild && !result.ok() && DiagnosticsOf(result.report).empty());
    std::printf("console-encrypted package with an unknown TITLE_ID: status %d\n", static_cast<int>(result.status));

    const SyntheticPackage cut = MakePackage("diag-pkg-cut", usa, "UP0177-CUSA05083_00-ODINSPHERELE0000", SyntheticGame(image, {}));
    const std::string whole = ReadFile(cut.file);
    cpk_test::WriteAll(cut.file, whole.substr(0, whole.size() / 2));
    result = InstallWith(cut.file, cut.roots, patches);
    CHECK(result.status == InstallStatus::kIncomplete && DiagnosticsOf(result.report).empty());
    cpk_test::WriteAll(cut.file, "not a package");
    CHECK(InstallWith(cut.file, cut.roots, patches).status == InstallStatus::kNotPackage);
    CHECK(InstallWith(cut.root / "missing.pkg", cut.roots, patches).status == InstallStatus::kNotPackage);
}

} // namespace

int main() {
    TestSources();
    TestExecutableInspection();
    TestFeatureStates();
    TestClassification();
    TestReport();
    TestUpdatePackage();
    TestIssue19Regression();
    TestInstallPaths();
    TestPackageRangeReads();
    TestUnknownTitlePackage();
    TestUnknownHashPackage();
    TestKnownEbootHashes();
    TestMalformedPackages();
    TestKnownBuilds();
    TestLocalPackages();
    std::printf("%s (%d checks, %d failures)\n", g_failures ? "FAILED" : "OK", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
