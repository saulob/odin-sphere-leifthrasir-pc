#include "build_diagnostics.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string_view>

#include "cpk_patch.h"
#include "game_files.h"
#include "lzcnt_compat.h"
#include "osl_bookshelf.h"
#include "osl_resolution.h"
#include "osl_skip_intro.h"

namespace osl {
namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint64_t kMaxExecutable = 128ull << 20;
constexpr uint64_t kMaxModule = 16ull << 20;
constexpr uint64_t kMaxSfo = 64u << 10;
constexpr size_t kMaxProbedArchives = 40;
constexpr size_t kMaxListedFiles = 40;
constexpr size_t kMaxListedHeaders = 24;
constexpr size_t kMaxListedAssets = 48;
constexpr uint64_t kMaxAssetEntry = 48ull << 20;
constexpr uint64_t kMaxAssetBytes = 768ull << 20;
constexpr size_t kMaxOtherFields = 360;
constexpr size_t kMaxAnalysis = 240;
constexpr size_t kMaxReport = 160u << 10;
constexpr size_t kScannedFeatures = 5;

// Entries every Refined (RE) archive of the known builds holds, whatever its language: the Bookshelf viewer lists, the
// kitchen and title menus and the five heroes' drama sheets. An archive with most of them is the game's.
constexpr const char* kOdinResources[] = {
    "archiveviewer/archivelist.txt", "archiveviewer/archivelistbookshelf.txt", "gui/hd_bookshelf.ftx",
    "gui/hd_kitchin.ftx",            "gui/rehd_kitchin.mbs",                   "gui/rehd_title.mbs",
    "gui/hd_chrselect.ftx",          "msgsheet/dramamsg_gwe1.fms",             "msgsheet/dramamsg_cor1.fms",
    "msgsheet/dramamsg_mer1.fms",    "msgsheet/dramamsg_osw1.fms",             "msgsheet/dramamsg_vel1.fms",
};
constexpr size_t kMinOdinResources = 8;

// The bundled modules every supported build ships.
struct KnownModule {
    const char* name;
    uint64_t size;
    const char* sha256;
};
constexpr KnownModule kKnownModules[] = {
    {"libc.prx", 923954, "e436eb5bd6bc6dd706edea917da1e52460ce760971b8f067149f479fd3410bfe"},
    {"libSceFios2.prx", 437289, "cf4f3e57d05df4cd22a54de08ff790d5aca979f02bd5ed5c70c10ac7f5f69410"},
};

// Builds reported by players that are not supported yet; the log names them, nothing else treats them as known.
struct ReportedBuild {
    const char* label;
    const char* eboot_sha256;
};
constexpr std::array<ReportedBuild, 0> kReportedBuilds{};

// The sites a scanned supported build's executable was found to have, from the log it was mapped with, so an unknown
// executable can be related to it like to a build with tables.
struct RecordedScan {
    const char* eboot_sha256;
    uint64_t entry, image_size;
    uint32_t resolution[17];
    OslSkipIntroSites skip_intro;
    OslBookshelfSites bookshelf;
    uint32_t snapshot;
    uint32_t lzcnt[LZCNT_RANGE_COUNT];
};
constexpr RecordedScan kRecordedScans[] = {
    {"85ea2e4396b94a7b228d11e35a267f401cb2957bd116b3a5d23f5b3cb15a2fb2",
     0x67d610,
     0x1004570,
     {0x6a9bb7, 0x6aa6d8, 0x6aa6dd, 0x6aa809, 0x6aa80e, 0x6aa9c8, 0x6aa9cd, 0x6aa9e5, 0x6aa9ea, 0x6aa9fe, 0x6aaa03, 0x6ab2f0,
      0x6ab2f5, 0x6acca2, 0x6acca7, 0x6b2392, 0x6b7f6d},
     {0x53d, 0x66aa9b, 0x1791dd, 0x146980, 0xa8ace0},
     {0x5c1910,
      0x5759b7,
      {0x5e849e, 0x5e92f6, 0x5e9343, 0x5e9d9e, 0x5e9df6, 0x5e9ee6, 0x5e9f36, 0x5ea2c4, 0x5ea334, 0x5ea4e8, 0x5ea53c, 0x5ea64c,
       0x5ea796, 0x5eacd2},
      {0x5eab81, 0x5eae36},
      0},
     0x542f87,
     {0x682bd0, 0x682ca0, 0x68405c, 0x684073, 0x684118, 0x6841a9, 0x68423a, 0x684283, 0x6844c0, 0x6844cf,
      0x68458c, 0x684626, 0x6846be, 0x6846e8, 0x684830, 0xde361c, 0xde3649, 0xde36df, 0xde3854, 0xde3873,
      0xde3901, 0xde3af6, 0xde3b7f, 0xde458c, 0xde45aa, 0xde4c76, 0xde4c90, 0xde5268, 0xde5b23, 0xde5b7e,
      0xde6008, 0xde6048, 0xde6919, 0xde6937, 0xde7075, 0xde7090, 0xde7f62, 0xde7fdf, 0xde7fe4, 0xde801b,
      0xde8020, 0xde82ba, 0xde82bf, 0xde8352, 0xde8357, 0xde8398, 0xde839d, 0xde862a, 0xde862f}},
};

class Text {
public:
    __attribute__((format(printf, 2, 3))) void Add(const char* format, ...) {
        va_list args;
        va_start(args, format);
        va_list copy;
        va_copy(copy, args);
        const int length = std::vsnprintf(nullptr, 0, format, copy);
        va_end(copy);
        std::string line(length > 0 ? static_cast<size_t>(length) : 0, '\0');
        if (length > 0) {
            std::vsnprintf(line.data(), line.size() + 1, format, args);
        }
        va_end(args);
        Add(line);
    }
    void Add(const std::string& line) { text_ += line + "\n"; }
    void Raw(const std::string& text) { text_ += text; }
    std::string& str() { return text_; }

private:
    std::string text_;
};

std::string Hex(uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
}

std::string Delta(int64_t delta) {
    return (delta < 0 ? "-" : "+") + Hex(static_cast<uint64_t>(delta < 0 ? -delta : delta));
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string Upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

std::string Shown(const std::string& value) {
    return value.empty() ? "(missing)" : value;
}

bool IsRoot(const std::string& path) {
    return path.find('/') == std::string::npos;
}

bool IsRootCpk(const std::string& path) {
    return IsRoot(path) && Lower(path).ends_with(".cpk");
}

bool IsOdinArchiveName(const std::string& path) {
    const std::string name = Lower(path);
    return IsRoot(path) && name.starts_with("odin2_") && name.ends_with(".cpk");
}

std::string RegionOf(const std::string& content_id) {
    static const std::pair<const char*, const char*> kPrefixes[] = {
        {"UP", "USA"}, {"EP", "Europe"}, {"JP", "Japan"}, {"HP", "Asia/Hong Kong"}, {"KP", "Korea"}};
    for (const auto& [prefix, region] : kPrefixes) {
        if (content_id.size() >= 2 && content_id.compare(0, 2, prefix) == 0) {
            return region;
        }
    }
    return "unknown";
}

// The archive's family and language from its name: Odin2_RE_CN.CPK is family RE, language CN.
struct ArchiveName {
    std::string family, language;
};

ArchiveName ParseArchiveName(const std::string& path) {
    static const std::set<std::string> kLanguages = {"US", "UK", "DE", "ES", "FR", "IT", "CN", "KO", "JP", "TW"};
    ArchiveName name;
    std::string stem = Upper(path);
    if (!stem.starts_with("ODIN2_") || !stem.ends_with(".CPK")) {
        return name;
    }
    stem = stem.substr(6, stem.size() - 10);
    std::vector<std::string> parts;
    for (size_t start = 0; start <= stem.size();) {
        const size_t end = std::min(stem.find('_', start), stem.size());
        parts.push_back(stem.substr(start, end - start));
        start = end + 1;
    }
    if (parts.size() > 1 && kLanguages.contains(parts.back())) {
        name.language = parts.back();
        parts.pop_back();
    }
    for (const std::string& part : parts) {
        name.family += (name.family.empty() ? "" : "_") + part;
    }
    return name;
}

bool TitleNamesGame(const std::vector<SfoParameter>& fields) {
    static const char* const kLocalTitles[] = {
        "\xE3\x82\xAA\xE3\x83\xBC\xE3\x83\x87\xE3\x82\xA3\xE3\x83\xB3\xE3\x82\xB9\xE3\x83\x95\xE3\x82\xA3\xE3\x82\xA2",
        "\xE5\xA5\xA7\xE4\xB8\x81\xE9\xA0\x98\xE5\x9F\x9F",
        "\xEC\x98\xA4\xEB\x94\x98 \xEC\x8A\xA4\xED\x94\xBC\xEC\x96\xB4",
    };
    for (const SfoParameter& field : fields) {
        if (!field.key.starts_with("TITLE") || field.key == "TITLE_ID") {
            continue;
        }
        if (Lower(field.value).find("odin sphere") != std::string::npos ||
            std::any_of(std::begin(kLocalTitles), std::end(kLocalTitles),
                        [&](const char* title) { return field.value.find(title) != std::string::npos; })) {
            return true;
        }
    }
    return false;
}

struct ArchiveProbe {
    SourceEntry file;
    bool valid = false;
    std::string error;
    std::vector<CpkEntry> entries;
    size_t resources = 0;
    bool Strong() const { return valid && resources >= kMinOdinResources; }
};

ArchiveProbe ProbeArchive(const GameSource& source, const SourceEntry& file) {
    ArchiveProbe probe;
    probe.file = file;
    CpkArchive archive;
    if (file.size == 0 || !OpenSourceCpk(source, file, archive, &probe.error)) {
        if (probe.error.empty()) {
            probe.error = "empty file";
        }
        return probe;
    }
    probe.valid = true;
    probe.entries = archive.entries();
    std::set<std::string> names;
    for (const CpkEntry& entry : probe.entries) {
        names.insert(Lower(entry.path));
    }
    probe.resources = static_cast<size_t>(std::count_if(std::begin(kOdinResources), std::end(kOdinResources),
                                                        [&](const char* path) { return names.contains(path); }));
    return probe;
}

struct Classification {
    bool title_names = false, content_names = false, known_title = false, known_publisher = false;
    bool eboot = false, modules = false, discmap = false, update_executable = false;
    size_t named_archives = 0;
    bool strong_archive = false;
    std::vector<ArchiveProbe> archives;
    std::vector<std::string> evidence;

    bool Metadata() const { return title_names || content_names || known_title; }
    bool Structure() const { return named_archives > 0 || update_executable || (modules && discmap); }
    bool Identified() const { return strong_archive || (Metadata() && Structure()); }
};

Classification Classify(const GameSource& source, const std::string& sfo, const std::vector<SourceEntry>& files) {
    Classification cls;
    const std::string title_id = SfoString(sfo, "TITLE_ID"), content_id = SfoString(sfo, "CONTENT_ID");
    std::set<std::string> known_publishers;
    for (const GameBuild& build : SupportedBuilds()) {
        cls.known_title = cls.known_title || title_id == build.title_id;
        known_publishers.insert(std::string(build.content_id).substr(0, 6));
    }
    cls.content_names = content_id.find("ODINSPHERE") != std::string::npos;
    cls.known_publisher = content_id.size() >= 6 && known_publishers.contains(content_id.substr(0, 6));
    cls.title_names = TitleNamesGame(SfoParameters(sfo));
    if (cls.known_title) {
        cls.evidence.push_back("TITLE_ID " + title_id + " is a known Odin Sphere Leifthrasir title");
    }
    if (cls.content_names) {
        cls.evidence.push_back(std::string("CONTENT_ID names ODINSPHERE") +
                               (cls.known_publisher ? ", publisher " + content_id.substr(0, 6) + " of a known release" : ""));
    }
    if (cls.title_names) {
        cls.evidence.push_back("TITLE names Odin Sphere");
    }
    if (!cls.Metadata()) {
        cls.evidence.push_back(sfo.empty() ? "no param.sfo" : "param.sfo does not name Odin Sphere Leifthrasir");
    }

    std::set<std::string> present;
    for (const SourceEntry& file : files) {
        present.insert(file.path);
        cls.named_archives += IsOdinArchiveName(file.path) ? 1 : 0;
    }
    cls.eboot = present.contains("eboot.bin");
    cls.discmap = present.contains("sce_discmap.plt");
    cls.modules = std::all_of(std::begin(kBundledModules), std::end(kBundledModules),
                              [&](const char* name) { return present.contains(std::string("sce_module/") + name); });
    cls.update_executable = SfoString(sfo, "CATEGORY") == "gp" && cls.eboot && cls.Metadata();
    cls.evidence.push_back(cls.eboot ? "eboot.bin is present" : "no eboot.bin");
    if (cls.modules) {
        cls.evidence.push_back(std::string("the bundled modules are present") + (cls.discmap ? ", with sce_discmap.plt" : ""));
    }
    cls.evidence.push_back(cls.named_archives ? std::to_string(cls.named_archives) + " Odin2 archive(s) by name"
                                              : "no Odin2 archive by name");
    if (cls.update_executable) {
        cls.evidence.push_back("an update (CATEGORY gp) carries eboot.bin");
    }

    std::vector<SourceEntry> candidates;
    for (const SourceEntry& file : files) {
        if (IsRootCpk(file.path)) {
            candidates.push_back(file);
        }
    }
    const auto refined = [](const SourceEntry& file) { return Lower(file.path).find("_re") != std::string::npos; };
    std::stable_partition(candidates.begin(), candidates.end(), refined);
    for (const SourceEntry& file : candidates) {
        if (cls.archives.size() >= kMaxProbedArchives) {
            cls.evidence.push_back("archive probe limit reached (" + std::to_string(kMaxProbedArchives) + ")");
            break;
        }
        cls.archives.push_back(ProbeArchive(source, file));
        cls.strong_archive = cls.strong_archive || cls.archives.back().Strong();
    }
    size_t valid = 0;
    const ArchiveProbe* strongest = nullptr;
    for (const ArchiveProbe& probe : cls.archives) {
        valid += probe.valid ? 1 : 0;
        if (probe.valid && (!strongest || probe.resources > strongest->resources)) {
            strongest = &probe;
        }
    }
    if (!cls.archives.empty()) {
        cls.evidence.push_back(std::to_string(valid) + " of " + std::to_string(cls.archives.size()) +
                               " root archive(s) are valid CPKs");
    }
    if (strongest) {
        cls.evidence.push_back(strongest->file.path + " holds " + std::to_string(strongest->resources) + "/" +
                               std::to_string(std::size(kOdinResources)) + " characteristic Odin resources" +
                               (strongest->Strong() ? " (strong archive evidence)" : ""));
    }
    return cls;
}

const char* PermissionName(uint32_t flags) {
    static const char* const kNames[8] = {"---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx"};
    return kNames[flags & 7];
}

const GameBuild* BuildWithSha(const std::string& sha) {
    for (const GameBuild& build : SupportedBuilds()) {
        if (sha == build.eboot_sha256) {
            return &build;
        }
    }
    return nullptr;
}

struct Executables {
    Bytes eboot;
    bool eboot_present = false;
    std::string eboot_sha;
    ExecutableInfo info;
    std::vector<Bytes> modules;
    std::vector<bool> modules_present;
    LinkedImage linked;
};

Executables LoadExecutables(const GameSource& source) {
    Executables exe;
    exe.eboot_present = source.Read("eboot.bin", kMaxExecutable, exe.eboot);
    exe.eboot_sha = exe.eboot_present ? Sha256Hex(exe.eboot) : std::string();
    exe.info = InspectExecutable(exe.eboot);
    for (const char* name : kBundledModules) {
        Bytes bytes;
        exe.modules_present.push_back(source.Read(std::string("sce_module/") + name, kMaxModule, bytes));
        exe.modules.push_back(std::move(bytes));
    }
    exe.linked = LinkExecutable(exe.eboot, exe.modules);
    return exe;
}

void DescribeMetadata(Text& out, const std::string& sfo) {
    const std::string title_id = SfoString(sfo, "TITLE_ID"), content_id = SfoString(sfo, "CONTENT_ID");
    out.Add("Package metadata (param.sfo):");
    out.Add("  TITLE: %s", Shown(SfoString(sfo, "TITLE")).c_str());
    out.Add("  TITLE_ID: %s", Shown(title_id).c_str());
    out.Add("  CONTENT_ID: %s", Shown(content_id).c_str());
    out.Add("  region (inferred from the CONTENT_ID prefix): %s", RegionOf(content_id).c_str());
    out.Add("  APP_VER: %s", Shown(SfoString(sfo, "APP_VER")).c_str());
    out.Add("  VERSION: %s", Shown(SfoString(sfo, "VERSION")).c_str());
    out.Add("  CATEGORY: %s", Shown(SfoString(sfo, "CATEGORY")).c_str());
    std::string others;
    for (const SfoParameter& field : SfoParameters(sfo)) {
        static const std::set<std::string> kShown = {"TITLE", "TITLE_ID", "CONTENT_ID", "APP_VER", "VERSION", "CATEGORY"};
        if (!kShown.contains(field.key) && !field.value.empty()) {
            others += (others.empty() ? "" : ", ") + field.key + "=" + field.value;
        }
    }
    if (others.size() > kMaxOtherFields) {
        others = others.substr(0, kMaxOtherFields) + "...";
    }
    out.Add("  other fields: %s", others.empty() ? "(none)" : others.c_str());
    for (const GameBuild& build : SupportedBuilds()) {
        if (title_id == build.title_id) {
            const PackageLayer& layer = FinishedLayer(build);
            out.Add("  supported build of this TITLE_ID: %s (APP_VER %s, VERSION %s, eboot SHA-256 %s)", build.label,
                    layer.app_ver, layer.sfo_version, build.eboot_sha256);
        }
    }
    const GameCheck identity = CheckPackageIdentity(sfo);
    if (identity.state == GameFileState::kValid && identity.build) {
        const std::vector<const GameBuild*> same = SamePackageBuilds(*identity.build);
        std::string names;
        for (const GameBuild* build : same) {
            names += (names.empty() ? "" : ", ") + std::string(build->label);
        }
        if (same.size() > 1) {
            out.Add("  package identity shared by: %s; only eboot.bin tells them apart", names.c_str());
        } else {
            out.Add("  package identity: the same as %s's", names.c_str());
        }
    }
}

void DescribeExecutable(Text& out, const Executables& exe) {
    const ExecutableInfo& info = exe.info;
    out.Add("Executable (eboot.bin):");
    if (!exe.eboot_present) {
        out.Add("  eboot.bin: not in the source or larger than %s", Hex(kMaxExecutable).c_str());
        out.Add("  Executable: not readable (no eboot.bin)");
        return;
    }
    out.Add("  eboot.bin: %zu bytes", exe.eboot.size());
    out.Add("  SHA-256: %s", Shown(exe.eboot_sha).c_str());
    std::string known = "none (still unsupported)";
    if (const GameBuild* build = BuildWithSha(exe.eboot_sha)) {
        known = std::string("the executable of supported build ") + build->label;
    }
    for (const ReportedBuild& build : kReportedBuilds) {
        if (exe.eboot_sha == build.eboot_sha256) {
            known = std::string(build.label) + " (reported, still unsupported)";
        }
    }
    out.Add("  Known build match: %s", known.c_str());
    if (!info.self) {
        out.Add("  Executable: not readable (%s)", info.error.c_str());
        return;
    }
    out.Add("  SELF: version %u, mode 0x%x, endian %u, attributes 0x%x, key type 0x%x, header 0x%x, meta 0x%x, "
            "declared size %s, %u segments, flags 0x%x",
            info.version, info.mode, info.endian, info.attributes, info.key_type, info.header_size, info.meta_size,
            Hex(info.declared_size).c_str(), info.segment_count, info.self_flags);
    if (info.elf) {
        out.Add("  ELF: type 0x%x, machine %u, entry %s, %zu program headers", info.elf_type, info.machine,
                Hex(info.entry).c_str(), info.headers.size());
    }
    size_t executable = 0;
    for (size_t i = 0; i < info.headers.size(); ++i) {
        const ProgramHeaderInfo& h = info.headers[i];
        executable += h.type == 1 && (h.flags & 1) ? 1 : 0;
        if (i < kMaxListedHeaders) {
            out.Add("  ph[%zu]: type 0x%x %s vaddr %s memsz %s filesz %s elf-offset %s align %s", i, h.type,
                    PermissionName(h.flags), Hex(h.vaddr).c_str(), Hex(h.memsz).c_str(), Hex(h.filesz).c_str(),
                    Hex(h.offset).c_str(), Hex(h.align).c_str());
        }
    }
    out.Add("  executable load segments: %zu", executable);
    size_t listed = 0;
    for (size_t i = 0; i < info.segments.size(); ++i) {
        const SelfSegmentInfo& s = info.segments[i];
        if (s.blocked() && listed++ < kMaxListedHeaders) {
            out.Add("  self segment[%zu]: flags %s file-offset %s size %s -> ph[%llu]", i, Hex(s.flags).c_str(),
                    Hex(s.offset).c_str(), Hex(s.size).c_str(), static_cast<unsigned long long>(s.header()));
        }
    }
    for (size_t i = 0; i < info.headers.size(); ++i) {
        const ProgramHeaderInfo& h = info.headers[i];
        if ((h.type != 1 && h.type != 0x61000010) || !h.filesz) {
            continue;
        }
        for (const SelfSegmentInfo& s : info.segments) {
            if (s.blocked() && s.header() == i) {
                out.Add("  rva %s..%s = file offset %s", Hex(h.vaddr).c_str(), Hex(h.vaddr + h.filesz).c_str(),
                        Hex(s.offset).c_str());
            }
        }
    }
    if (!exe.linked.eboot_error.empty()) {
        out.Add("  Executable: not readable (%s)", exe.linked.eboot_error.c_str());
        return;
    }
    out.Add("  Entry point: %s, mapped image: %s (an image offset is the segment vaddr)", Hex(info.entry).c_str(),
            Hex(exe.linked.eboot_size).c_str());
}

void DescribeModules(Text& out, const Executables& exe) {
    out.Add("Bundled modules (linked after the executable at %s boundaries):", Hex(kModuleAlignment).c_str());
    for (size_t i = 0; i < exe.linked.modules.size(); ++i) {
        const LinkedModule& module = exe.linked.modules[i];
        const std::string path = "sce_module/" + module.name;
        if (!exe.modules_present[i]) {
            out.Add("  %s: missing or unreadable", path.c_str());
            continue;
        }
        const Bytes& bytes = exe.modules[i];
        const std::string sha = Sha256Hex(bytes);
        const ExecutableInfo info = InspectExecutable(bytes);
        const bool known = std::any_of(std::begin(kKnownModules), std::end(kKnownModules), [&](const KnownModule& k) {
            return module.name == k.name && bytes.size() == k.size && sha == k.sha256;
        });
        out.Add("  %s: %zu bytes, SHA-256 %s, %s, %s", path.c_str(), bytes.size(), Shown(sha).c_str(),
                info.error.empty() ? "valid SELF" : info.error.c_str(),
                known ? "the copy the supported builds ship" : "UNKNOWN copy");
        out.Add("    image: base %s size %s %s", Hex(module.base).c_str(), Hex(module.size).c_str(),
                module.error.empty() ? "linked" : ("not linked: " + module.error).c_str());
    }
    out.Add("  linked image: %s, entry %s", Hex(exe.linked.image.image.size()).c_str(), Hex(exe.linked.image.entry).c_str());
}

void DescribeFiles(Text& out, const std::vector<SourceEntry>& files) {
    out.Add("Files: %zu%s", files.size(), files.size() >= kMaxSourceFiles ? " (listing limit reached)" : "");
    std::map<std::string, size_t> folders;
    size_t listed = 0, unlisted = 0;
    std::set<std::string> present;
    for (const SourceEntry& file : files) {
        present.insert(file.path);
        if (!IsRoot(file.path)) {
            ++folders[file.path.substr(0, file.path.find('/'))];
        } else if (listed < kMaxListedFiles) {
            out.Add("  %s: %llu bytes", file.path.c_str(), static_cast<unsigned long long>(file.size));
            ++listed;
        } else {
            ++unlisted;
        }
    }
    if (unlisted) {
        out.Add("  (+%zu more root files)", unlisted);
    }
    for (const auto& [folder, count] : folders) {
        out.Add("  %s/: %zu files", folder.c_str(), count);
    }
    for (const char* name : {"sce_discmap.plt", "sce_discmap_patch.plt"}) {
        if (!present.contains(name)) {
            out.Add("  %s: absent", name);
        }
    }
}

// What a package without every shared game file cannot show.
void DescribeLimits(Text& out, const std::string& sfo, const std::vector<SourceEntry>& files) {
    std::set<std::string> present;
    for (const SourceEntry& file : files) {
        present.insert(file.path);
    }
    std::vector<std::string> absent;
    for (const char* name : kSharedGameFiles) {
        if (std::string_view(name) != "sce_sys/param.sfo" && !present.contains(name)) {
            absent.push_back(name);
        }
    }
    if (absent.empty()) {
        return;
    }
    out.Add("Package limits:");
    for (const std::string& name : absent) {
        out.Add("  %s is not in this source", name.c_str());
    }
    out.Add(SfoString(sfo, "CATEGORY") == "gp"
                ? "  this is an update (CATEGORY gp): the files it does not carry belong to its base package, so what lives in "
                  "them (the bundled modules' LZCNT sites, art in base archives) is reported as missing here"
                : "  the game is incomplete: what lives in the missing files is reported as missing");
}

std::string Joined(const std::set<std::string>& values, const char* none = "none") {
    std::string text;
    for (const std::string& value : values) {
        text += (text.empty() ? "" : ", ") + value;
    }
    return text.empty() ? std::string(none) : text;
}

void DescribeArchives(Text& out, const Classification& cls, const std::vector<SourceEntry>& files, const std::string& title_id) {
    out.Add("Archives:");
    std::set<std::string> families, languages, names;
    for (const ArchiveProbe& probe : cls.archives) {
        const ArchiveName name = ParseArchiveName(probe.file.path);
        std::string line = "  " + probe.file.path + ": " + std::to_string(probe.file.size) + " bytes";
        if (probe.valid) {
            line += ", CPK valid, " + std::to_string(probe.entries.size()) + " entries, Odin resources " +
                    std::to_string(probe.resources) + "/" + std::to_string(std::size(kOdinResources)) +
                    (probe.Strong() ? " (strong)" : "");
        } else {
            line += ", CPK NOT READABLE (" + probe.error + ")";
        }
        if (!name.family.empty()) {
            line += ", family " + name.family + (name.language.empty() ? "" : ", language " + name.language);
            families.insert(name.family);
        }
        if (!name.language.empty()) {
            languages.insert(name.language);
        }
        out.Add(line);
    }
    for (const SourceEntry& file : files) {
        if (IsOdinArchiveName(file.path)) {
            names.insert(Upper(file.path));
        }
    }
    out.Add("  archive families present: %s", Joined(families).c_str());
    out.Add("  language archives present: %s", Joined(languages).c_str());
    out.Add("  runtime language routing: not proven (an archive's presence does not show that the executable selects it)");
    for (const GameBuild& build : SupportedBuilds()) {
        std::set<std::string> expected;
        for (const char* file : build.data_files) {
            expected.insert(Upper(file));
        }
        if (expected == names) {
            out.Add("  archive set equals %s's", build.label);
        } else if (title_id == build.title_id) {
            std::set<std::string> missing, extra;
            std::set_difference(expected.begin(), expected.end(), names.begin(), names.end(), std::inserter(missing, missing.end()));
            std::set_difference(names.begin(), names.end(), expected.begin(), expected.end(), std::inserter(extra, extra.end()));
            out.Add("  compared with %s: extra %s; missing %s", build.label, Joined(extra).c_str(), Joined(missing).c_str());
        }
    }
}

struct Reference {
    std::string label;
    std::vector<uint32_t> resolution;
    std::optional<OslSkipIntroSites> skip;
    std::optional<OslBookshelfSites> shelf;
    uint32_t snapshot = 0;
    std::vector<uint32_t> lzcnt;
    uint64_t entry = 0, image_size = 0;
    bool HasSites() const { return !resolution.empty() || skip || shelf || snapshot || !lzcnt.empty(); }
};

std::vector<Reference> References() {
    std::vector<Reference> refs;
    for (const GameBuild& build : SupportedBuilds()) {
        Reference ref;
        ref.label = build.label;
        if (build.mapping == ExecutableMapping::kTable) {
            for (const OslResolutionSite& site : build.resolution_sites) {
                ref.resolution.push_back(site.offset);
            }
            if (build.skip_intro) {
                ref.skip = *build.skip_intro;
            }
            if (build.bookshelf) {
                ref.shelf = *build.bookshelf;
            }
            ref.snapshot = build.snapshot_scale_site;
            for (const LzcntBuild& lzcnt : lzcnt_builds) {
                if (std::string_view(lzcnt.build) == build.label) {
                    ref.lzcnt.assign(std::begin(lzcnt.offsets), std::end(lzcnt.offsets));
                    ref.entry = lzcnt.entry;
                    ref.image_size = lzcnt.size;
                }
            }
        }
        for (const RecordedScan& recorded : kRecordedScans) {
            if (std::string_view(recorded.eboot_sha256) == build.eboot_sha256) {
                ref.resolution.assign(std::begin(recorded.resolution), std::end(recorded.resolution));
                ref.skip = recorded.skip_intro;
                ref.shelf = recorded.bookshelf;
                ref.snapshot = recorded.snapshot;
                ref.lzcnt.assign(std::begin(recorded.lzcnt), std::end(recorded.lzcnt));
                ref.entry = recorded.entry;
                ref.image_size = recorded.image_size;
            }
        }
        refs.push_back(std::move(ref));
    }
    return refs;
}

// The delta most of the pairs share, and how many share it.
std::pair<int64_t, size_t> CommonDelta(const std::vector<int64_t>& deltas) {
    std::map<int64_t, size_t> counts;
    for (const int64_t delta : deltas) {
        ++counts[delta];
    }
    std::pair<int64_t, size_t> best{0, 0};
    for (const auto& [delta, count] : counts) {
        if (count > best.second) {
            best = {delta, count};
        }
    }
    return best;
}

int64_t Diff(uint64_t a, uint64_t b) {
    return static_cast<int64_t>(a) - static_cast<int64_t>(b);
}

struct Relation {
    size_t whole = 0;    // features whose every site moved by one delta
    size_t compared = 0; // features compared
    int64_t resolution = INT64_MAX;
    std::vector<std::string> lines;
};

Relation Relate(const Reference& ref, const ScannedMapping& m, const LinkedImage& linked) {
    Relation r;
    const auto add = [&](const std::string& line) { r.lines.push_back("    " + line); };
    if (!m.resolution_offsets.empty() && m.resolution_offsets.size() == ref.resolution.size()) {
        std::vector<int64_t> deltas;
        for (size_t i = 0; i < ref.resolution.size(); ++i) {
            deltas.push_back(Diff(m.resolution_offsets[i], ref.resolution[i]));
        }
        const auto [delta, count] = CommonDelta(deltas);
        ++r.compared;
        r.whole += count == deltas.size() ? 1 : 0;
        r.resolution = delta;
        add(count == deltas.size() ? "Resolution: all " + std::to_string(count) + " sites at delta " + Delta(delta)
                                   : "Resolution: " + std::to_string(count) + " of " + std::to_string(deltas.size()) +
                                         " sites at delta " + Delta(delta) + ", the rest moved differently");
    }
    if (m.skip_intro && ref.skip) {
        ++r.compared;
        const std::vector<int64_t> deltas = {Diff(m.skip_intro->notice_scene, ref.skip->notice_scene),
                                             Diff(m.skip_intro->hd_title_check, ref.skip->hd_title_check),
                                             Diff(m.skip_intro->classic_title_check, ref.skip->classic_title_check)};
        r.whole += CommonDelta(deltas).second == deltas.size() ? 1 : 0;
        add("Skip intro: the same signatures, notice scene " + Delta(deltas[0]) + ", HD title " + Delta(deltas[1]) +
            ", Classic title " + Delta(deltas[2]));
    }
    if (m.bookshelf && ref.shelf) {
        ++r.compared;
        const int64_t getter = Diff(m.bookshelf->accessor, ref.shelf->accessor);
        size_t calls = 0, same = 0;
        for (int i = 0; i < OSL_BOOKSHELF_READS; ++i, ++calls) {
            same += Diff(m.bookshelf->reads[i], ref.shelf->reads[i]) == getter ? 1 : 0;
        }
        for (int i = 0; i < OSL_BOOKSHELF_MARKS; ++i, ++calls) {
            same += Diff(m.bookshelf->marks[i], ref.shelf->marks[i]) == getter ? 1 : 0;
        }
        r.whole += same == calls ? 1 : 0;
        add("Unlock Bookshelf: getter and " + std::to_string(same) + " of " + std::to_string(calls) + " calls at delta " +
            Delta(getter) + ", special test " + Delta(Diff(m.bookshelf->total, ref.shelf->total)) +
            (m.bookshelf->in_place_page_read == ref.shelf->in_place_page_read ? ", same page read" : ", OTHER page read"));
    }
    if (m.snapshot_scale_site && ref.snapshot) {
        ++r.compared;
        ++r.whole;
        add("Snapshot scale: delta " + Delta(Diff(m.snapshot_scale_site, ref.snapshot)));
    }
    if (m.lzcnt_offsets.size() == LZCNT_RANGE_COUNT && ref.lzcnt.size() == LZCNT_RANGE_COUNT && ref.image_size) {
        std::vector<int64_t> eboot;
        size_t modules = 0, same_modules = 0;
        for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i) {
            if (m.lzcnt_offsets[i] < linked.eboot_size) {
                eboot.push_back(Diff(m.lzcnt_offsets[i], ref.lzcnt[i]));
            } else {
                ++modules;
                same_modules += linked.image.image.size() - m.lzcnt_offsets[i] == ref.image_size - ref.lzcnt[i] ? 1 : 0;
            }
        }
        const auto [delta, count] = CommonDelta(eboot);
        ++r.compared;
        r.whole += count == eboot.size() && same_modules == modules ? 1 : 0;
        add("Legacy CPU LZCNT: " + std::to_string(count) + " of " + std::to_string(eboot.size()) + " eboot ranges at delta " +
            Delta(delta) + ", " + std::to_string(same_modules) + " of " + std::to_string(modules) +
            " module ranges at the same distance from the image end");
    }
    return r;
}

void DescribeRelations(Text& out, const ScanResult& scan, const LinkedImage& linked) {
    out.Add("Structural relation to the supported builds (offsets compared feature by feature; not a compatibility claim):");
    const Reference* closest = nullptr;
    Relation best;
    const std::vector<Reference> refs = References();
    for (const Reference& ref : refs) {
        if (!ref.HasSites()) {
            out.Add("  %s: no recorded sites to compare (mapped by the scan at installation)", ref.label.c_str());
            continue;
        }
        Relation r = Relate(ref, scan.mapping, linked);
        std::string head = "  " + ref.label;
        if (ref.entry) {
            head += ": entry " + Delta(Diff(linked.image.entry, ref.entry)) + ", linked image " +
                    Delta(Diff(linked.image.image.size(), ref.image_size));
        }
        out.Add(head);
        for (const std::string& line : r.lines) {
            out.Add(line);
        }
        if (r.compared == 0) {
            out.Add("    no mapped feature to compare");
        }
        const auto magnitude = [](int64_t value) { return value == INT64_MAX ? INT64_MAX : (value < 0 ? -value : value); };
        if (r.compared && (!closest || r.whole > best.whole ||
                           (r.whole == best.whole && magnitude(r.resolution) < magnitude(best.resolution)))) {
            closest = &ref;
            best = std::move(r);
        }
    }
    if (closest) {
        out.Add("  closest structural relation: %s (%zu of %zu compared features moved as one block)", closest->label.c_str(),
                best.whole, best.compared);
    } else {
        out.Add("  closest structural relation: none (no feature mapped)");
    }
}

// On a supported build's own executable the scan must reproduce what its profile records.
void DescribeSelfCheck(Text& out, const ScanResult& scan, const std::string& sha) {
    const GameBuild* build = BuildWithSha(sha);
    if (!build) {
        return;
    }
    for (const Reference& ref : References()) {
        if (ref.label != build->label) {
            continue;
        }
        out.Add("Supported-build self-check (%s):", build->label);
        if (!ref.HasSites()) {
            out.Add("  DATA UNAVAILABLE: no recorded sites for this build");
            return;
        }
        const ScannedMapping& m = scan.mapping;
        const auto verdict = [&](const char* feature, bool recorded, bool same) {
            out.Add("  %s: %s", feature, !recorded ? "DATA UNAVAILABLE" : same ? "matches the recorded sites" : "DIFFERS");
        };
        verdict("Resolution", !ref.resolution.empty(),
                std::vector<uint32_t>(m.resolution_offsets.begin(), m.resolution_offsets.end()) == ref.resolution);
        verdict("Skip intro", ref.skip.has_value(),
                m.skip_intro && ref.skip && std::memcmp(&*m.skip_intro, &*ref.skip, sizeof(OslSkipIntroSites)) == 0);
        verdict("Unlock Bookshelf", ref.shelf.has_value(),
                m.bookshelf && ref.shelf && std::memcmp(&*m.bookshelf, &*ref.shelf, sizeof(OslBookshelfSites)) == 0);
        verdict("Snapshot scale", ref.snapshot != 0, m.snapshot_scale_site == ref.snapshot);
        verdict("Legacy CPU LZCNT", !ref.lzcnt.empty(), m.lzcnt_offsets == ref.lzcnt);
        return;
    }
}

struct AssetSummary {
    size_t found = 0, known = 0, unknown = 0, patched = 0, unread = 0;
    std::vector<std::string> unknown_assets;
    std::set<std::string> missing_targets;
};

std::string ProfileLabel(std::span<const KnownEntryProfile> profiles, uint64_t size, uint32_t crc) {
    for (const KnownEntryProfile& profile : profiles) {
        if (profile.size == size && profile.crc == crc) {
            return profile.label;
        }
    }
    return "?";
}

AssetSummary DescribeAssets(Text& out, const GameSource& source, const Classification& cls, const BlockPatch& patch,
                            std::span<const KnownEntryProfile> profiles, uint64_t& budget) {
    AssetSummary summary;
    std::vector<std::string> targets;
    for (const BlockPatchTarget& target : patch.targets) {
        if (std::find(targets.begin(), targets.end(), Lower(target.name)) == targets.end()) {
            targets.push_back(Lower(target.name));
        }
    }
    std::set<std::string> seen;
    size_t listed = 0;
    for (const ArchiveProbe& probe : cls.archives) {
        if (!probe.valid) {
            continue;
        }
        for (const CpkEntry& entry : probe.entries) {
            const std::string name = Lower(entry.path);
            if (std::find(targets.begin(), targets.end(), name) == targets.end()) {
                continue;
            }
            seen.insert(name);
            ++summary.found;
            std::string line = "  " + probe.file.path + "/" + entry.path + ": " + std::to_string(entry.extract_size) + " bytes";
            Bytes data;
            std::string error;
            if (entry.extract_size > kMaxAssetEntry || entry.size > budget ||
                !ReadSourceCpkEntry(source, probe.file.path, entry, kMaxAssetEntry, data, &error)) {
                ++summary.unread;
                line += ", NOT CHECKED (" + (error.empty() ? std::string("diagnostic read limit") : error) + ")";
            } else {
                budget -= std::min<uint64_t>(budget, entry.size);
                const uint32_t crc = Crc32(data);
                char hex[16];
                std::snprintf(hex, sizeof(hex), "%08x", crc);
                line += std::string(", CRC32 ") + hex + ": ";
                const EntryMatch match = MatchEntryWithProfiles(patch, profiles, entry.path, data, crc);
                if (match.state == EntryState::kPatched) {
                    ++summary.patched;
                    line += "already a patched copy";
                } else if (match.state == EntryState::kOriginal) {
                    ++summary.known;
                    line += match.by_profile ? "KNOWN (profile " + ProfileLabel(profiles, data.size(), crc) + ")"
                            : match.exact    ? "KNOWN (exact copy)"
                                             : "KNOWN (by source blocks)";
                } else {
                    ++summary.unknown;
                    summary.unknown_assets.push_back(probe.file.path + "/" + entry.path);
                    std::string analysis = match.analysis;
                    if (analysis.size() > kMaxAnalysis) {
                        analysis = analysis.substr(0, kMaxAnalysis) + "...";
                    }
                    line += std::string(match.ambiguous ? "AMBIGUOUS" : "UNKNOWN") + (analysis.empty() ? "" : " (" + analysis + ")");
                }
            }
            if (listed++ < kMaxListedAssets) {
                out.Add(line);
            }
        }
    }
    if (listed > kMaxListedAssets) {
        out.Add("  (+%zu more assets not listed)", listed - kMaxListedAssets);
    }
    for (const std::string& target : targets) {
        if (!seen.contains(target)) {
            summary.missing_targets.insert(target);
        }
    }
    out.Add("  targets of the patch absent from every archive: %s", Joined(summary.missing_targets).c_str());
    return summary;
}

std::vector<KnownEntryProfile> AllPromptProfiles() {
    std::vector<KnownEntryProfile> profiles;
    for (const GameBuild& build : SupportedBuilds()) {
        for (const KnownEntryProfile& profile : build.prompt_profiles) {
            const bool known = std::any_of(profiles.begin(), profiles.end(), [&](const KnownEntryProfile& p) {
                return p.name == profile.name && p.size == profile.size && p.crc == profile.crc;
            });
            if (!known) {
                profiles.push_back(profile);
            }
        }
    }
    return profiles;
}

std::string DescribePrompts(Text& out, const GameSource& source, const Classification& cls, std::span<const uint8_t> patch_bytes,
                            uint64_t& budget, bool update) {
    out.Add("Controller prompts (read-only check of the Xbox prompts patch; nothing is patched):");
    const std::optional<BlockPatch> patch = ParseBlockPatch(patch_bytes);
    if (!patch) {
        out.Add("  not checked: no prompts patch");
        return "not checked";
    }
    const std::vector<KnownEntryProfile> profiles = AllPromptProfiles();
    const AssetSummary summary = DescribeAssets(out, source, cls, *patch, profiles, budget);
    std::string verdict;
    if (summary.found == 0) {
        verdict = update ? "not in this update package; its base package carries the prompt art"
                         : "DATA UNAVAILABLE: no prompt art found in the archives";
    } else if (summary.unknown) {
        verdict = "DATA UNAVAILABLE: " + std::to_string(summary.unknown) +
                  " unknown prompt asset(s); the Xbox prompts patch would refuse this build";
    } else if (summary.unread) {
        verdict = "undetermined: " + std::to_string(summary.unread) + " prompt asset(s) beyond the diagnostic read limit";
    } else {
        verdict = "compatible: every one of the " + std::to_string(summary.found) + " prompt asset(s) is a known copy";
    }
    for (size_t i = 0; i < summary.unknown_assets.size() && i < 8; ++i) {
        out.Add("  unknown: %s", summary.unknown_assets[i].c_str());
    }
    out.Add("  verdict: %s (the base executable does not depend on prompt art)", verdict.c_str());
    return verdict;
}

std::string DescribeTitle(Text& out, const GameSource& source, const Classification& cls, std::span<const uint8_t> patch_bytes,
                          uint64_t& budget, bool update) {
    out.Add("Exit Game title art (read-only check of the title menu patch; nothing is patched or enabled):");
    const std::optional<BlockPatch> patch = ParseBlockPatch(patch_bytes);
    if (!patch) {
        out.Add("  not checked: no title menu patch");
        return "not checked";
    }
    const AssetSummary summary = DescribeAssets(out, source, cls, *patch, {}, budget);
    std::string verdict;
    if (summary.found == 0) {
        verdict = update ? "not in this update package; its base package carries the title menu files"
                         : "would stay off: no title menu file found";
    } else if (summary.unknown) {
        verdict = "would stay off: " + summary.unknown_assets.front() + " is not a copy the patch was made for" +
                  (summary.unknown > 1 ? " (+" + std::to_string(summary.unknown - 1) + " more)" : "");
    } else if (summary.unread) {
        verdict = "undetermined: " + std::to_string(summary.unread) + " title file(s) beyond the diagnostic read limit";
    } else {
        verdict = "could be enabled: every one of the " + std::to_string(summary.found) + " title file(s) is a known copy";
    }
    out.Add("  verdict: %s", verdict.c_str());
    return verdict;
}

std::string ComponentText(const FeatureScan& feature) {
    std::string text;
    for (const ScanComponent& c : feature.components) {
        text += (text.empty() ? "" : "; ") + c.name + " " + std::to_string(c.unique) + "/" + std::to_string(c.expected) +
                " unique";
        if (c.ambiguous) {
            text += ", " + std::to_string(c.ambiguous) + " ambiguous";
        }
        if (c.missing()) {
            text += ", " + std::to_string(c.missing()) + " missing";
        }
    }
    return text;
}

void DescribeFeatureStates(Text& out, const ScanResult& scan) {
    out.Add("Feature states:");
    FeatureState resolution = FeatureState::kMissing, snapshot = FeatureState::kMissing;
    for (const FeatureScan& feature : scan.features) {
        out.Add("  %s: %s (%s; validator %s)%s", feature.name.c_str(), FeatureStateName(feature.state), ComponentText(feature).c_str(),
                feature.validator.empty() ? "not run" : feature.validator.c_str(),
                feature.state == FeatureState::kInvalid ? " located, but not accepted" : "");
        if (feature.name == "Resolution") {
            resolution = feature.state;
        } else if (feature.name == "Snapshot scale") {
            snapshot = feature.state;
        }
    }
    const bool render = resolution == FeatureState::kComplete && snapshot == FeatureState::kComplete;
    out.Add("  Render resolution and Aspect Ratio: %s (they use the Resolution and Snapshot scale sites; no sites of their own)",
            render ? "available" : "unavailable");
}

void Bound(std::string& text) {
    if (text.size() > kMaxReport) {
        text.resize(kMaxReport);
        text.resize(text.rfind('\n') + 1);
        text += "(report truncated at " + std::to_string(kMaxReport) + " bytes)\n";
    }
}

struct FeatureFlag {
    const char* name;
    bool complete;
    std::string detail;
};

std::string FeatureMappingText(const ScannedMapping& mapping, std::vector<std::string>* unmapped = nullptr) {
    size_t expected_sites = 0;
    for (const GameBuild& build : SupportedBuilds()) {
        if (!build.resolution_sites.empty()) {
            expected_sites = build.resolution_sites.size();
            break;
        }
    }
    const FeatureFlag flags[] = {
        {"Resolution", !mapping.resolution_offsets.empty(),
         std::to_string(mapping.resolution_offsets.size()) + "/" + std::to_string(expected_sites)},
        {"Skip intro", mapping.skip_intro.has_value(), {}},
        {"Unlock Bookshelf", mapping.bookshelf.has_value(), {}},
        {"Snapshot scale", mapping.snapshot_scale_site != 0, {}},
        {"Legacy CPU LZCNT", !mapping.lzcnt_offsets.empty(), std::to_string(mapping.lzcnt_offsets.size()) + " ranges"},
    };
    std::string text = "  Feature mapping:\n";
    for (const FeatureFlag& feature : flags) {
        text += std::string("    ") + feature.name + ": " + (feature.complete ? "complete" : "incomplete") +
                (feature.detail.empty() ? "" : " (" + feature.detail + ")") + "\n";
        if (!feature.complete && unmapped) {
            unmapped->push_back(feature.name);
        }
    }
    return text;
}

std::string SfoLine(const std::string& sfo) {
    return "  param.sfo: TITLE_ID " + SfoString(sfo, "TITLE_ID") + ", CONTENT_ID " + SfoString(sfo, "CONTENT_ID") + ", APP_VER " +
           SfoString(sfo, "APP_VER") + ", VERSION " + SfoString(sfo, "VERSION") + ", CATEGORY " + SfoString(sfo, "CATEGORY");
}

std::string ReadSfo(const GameSource& source) {
    Bytes bytes;
    return source.Read("sce_sys/param.sfo", kMaxSfo, bytes) ? std::string(bytes.begin(), bytes.end()) : std::string();
}

ScanResult ScanLinked(const Executables& exe) {
    if (!exe.linked.eboot_error.empty()) {
        ScanResult scan;
        scan.unresolved.push_back("executable");
        return scan;
    }
    return ScanExecutable(exe.linked.image, exe.linked.eboot_size);
}

constexpr char kMappingBegin[] = "Build mapping diagnostics begin\n";
constexpr char kMappingEnd[] = "Build mapping diagnostics end\n";
constexpr char kLzcntNotice[] =
    "Legacy CPU LZCNT mapping incomplete:\n"
    "  installation is not affected on CPUs with native LZCNT\n"
    "  a CPU without LZCNT cannot run this build until the mapping exists\n"
    "  attach the complete log when reporting this mapping issue; do not upload game files\n";

} // namespace

bool ExecutableDiffers(const GameSource& source, const GameBuild& build) {
    Bytes eboot;
    return source.Read("eboot.bin", kMaxExecutable, eboot) && Sha256Hex(eboot) != build.eboot_sha256;
}

UnsupportedBuildReport DiagnoseUnsupportedBuild(const GameSource& source, const std::string& given_sfo, const std::string& detail,
                                                const DiagnosticPatches& patches) {
    UnsupportedBuildReport report;
    const uint64_t bytes_before = source.bytes_read();
    const size_t reads_before = source.reads();
    const std::string sfo = given_sfo.empty() ? ReadSfo(source) : given_sfo;
    const std::vector<SourceEntry> files = source.Files();
    const Classification cls = Classify(source, sfo, files);
    report.evidence = cls.evidence;
    if (!cls.Identified()) {
        return report;
    }

    const Executables exe = LoadExecutables(source);
    const ScanResult scan = ScanLinked(exe);

    Text out;
    out.Raw(kDiagnosticsBegin);
    out.Add("Odin Sphere Leifthrasir was identified, but this exact build is not supported; nothing was installed.");
    out.Add("Read-only: nothing was extracted, patched or written.");
    out.Add("Supported-build check: %s", detail.c_str());
    out.Add("Game identification:");
    out.Add("  source: %s", source.Kind().c_str());
    for (const std::string& line : cls.evidence) {
        out.Add("  evidence: %s", line.c_str());
    }
    out.Add("  identified by: %s", cls.strong_archive ? "strong archive evidence" : "metadata and game structure");
    DescribeMetadata(out, sfo);
    DescribeExecutable(out, exe);
    DescribeModules(out, exe);
    DescribeFiles(out, files);
    DescribeLimits(out, sfo, files);
    DescribeArchives(out, cls, files, SfoString(sfo, "TITLE_ID"));

    out.Add("Executable feature mapping (the scan the installer uses for scanned builds; read-only):");
    if (!exe.linked.eboot_error.empty()) {
        out.Add("  not run: the executable did not map");
    }
    out.Raw(scan.text);
    DescribeFeatureStates(out, scan);
    DescribeSelfCheck(out, scan, exe.eboot_sha);
    DescribeRelations(out, scan, exe.linked);

    const bool update = SfoString(sfo, "CATEGORY") == "gp";
    uint64_t budget = kMaxAssetBytes;
    const std::string prompts = DescribePrompts(out, source, cls, patches.prompts, budget, update);
    const std::string title = DescribeTitle(out, source, cls, patches.title, budget, update);

    out.Add("Diagnostic reads: %zu files listed, %zu archives probed, %llu bytes read in %zu reads", files.size(),
            cls.archives.size(), static_cast<unsigned long long>(source.bytes_read() - bytes_before),
            source.reads() - reads_before);

    size_t complete = 0;
    for (const FeatureScan& feature : scan.features) {
        complete += feature.state == FeatureState::kComplete ? 1 : 0;
    }
    const bool identity = !SfoString(sfo, "TITLE_ID").empty() && !SfoString(sfo, "CONTENT_ID").empty() &&
                          !SfoString(sfo, "APP_VER").empty() && !SfoString(sfo, "VERSION").empty();
    const bool modules = std::all_of(exe.linked.modules.begin(), exe.linked.modules.end(),
                                     [](const LinkedModule& m) { return m.error.empty(); });
    const bool executable = exe.linked.eboot_error.empty() && modules;
    out.Add("Overall diagnostic result:");
    out.Add("  Identity: %s", identity ? "complete" : "incomplete");
    out.Add("  Executable mapping: %s", executable ? "complete" : "incomplete");
    out.Add("  Features: %zu/%zu COMPLETE", complete, kScannedFeatures);
    out.Add("  Controller prompts: %s", prompts.c_str());
    out.Add("  Exit Game title art: %s", title.c_str());
    out.Add("  %s (identification only: cheats stay off while the build is unsupported)", CheatSummary(scan).c_str());
    out.Add("  %s", ExperienceSummary(scan).c_str());
    const bool all = executable && complete == kScannedFeatures && scan.complete();
    if (all) {
        out.Add("  Mapping appears complete");
    } else {
        std::string unresolved;
        for (size_t i = 0; i < scan.unresolved.size(); ++i) {
            unresolved += (i ? ", " : " ") + scan.unresolved[i];
        }
        out.Add("  Mapping incomplete");
        out.Add("  unresolved:%s", unresolved.empty() ? " (executable modules)" : unresolved.c_str());
    }
    out.Add("This build stays unsupported until it is added to the supported builds; attach the complete log, never game files.");
    Bound(out.str());
    out.Raw(kDiagnosticsEnd);
    report.odin = true;
    report.text = std::move(out.str());
    return report;
}

std::string DiagnoseBuildMapping(const std::filesystem::path& app0, const GameBuild& build, std::span<const uint8_t> prompt_patch,
                                 std::vector<std::string>* unmapped) {
    std::error_code ec;
    const std::uintmax_t eboot_size = std::filesystem::file_size(app0 / "eboot.bin", ec);
    if (build.mapping != ExecutableMapping::kScanned || ec || eboot_size > kMaxExecutable) {
        return {};
    }
    const std::unique_ptr<GameSource> source = OpenFolderSource(app0);
    const std::string sfo = ReadSfo(*source);
    const Executables exe = LoadExecutables(*source);
    const ScanResult scan = ScanLinked(exe);

    Text out;
    out.Raw(kMappingBegin);
    out.Add("Identity:");
    out.Add("  Build: %s", build.label);
    out.Add(SfoLine(sfo));
    DescribeExecutable(out, exe);
    DescribeModules(out, exe);
    out.Raw(FeatureMappingText(scan.mapping, unmapped));
    const std::vector<std::string> missing = UnresolvedFeatures(scan.mapping);
    std::string names;
    for (size_t i = 0; i < missing.size(); ++i) {
        names += (i ? ", " : " ") + missing[i];
    }
    out.Add("  Features not resolved by the scan:%s", missing.empty() ? " none" : names.c_str());
    out.Raw(scan.text);
    DescribeFeatureStates(out, scan);
    if (!prompt_patch.empty()) {
        const std::vector<SourceEntry> files = source->Files();
        const Classification cls = Classify(*source, sfo, files);
        uint64_t budget = kMaxAssetBytes;
        DescribePrompts(out, *source, cls, prompt_patch, budget, false);
    }
    if (scan.mapping.lzcnt_offsets.empty()) {
        out.Raw(kLzcntNotice);
    }
    out.Add("Overall diagnostic result:");
    out.Add("  %s", CheatSummary(scan).c_str());
    out.Add("  %s", ExperienceSummary(scan).c_str());
    if (scan.complete()) {
        out.Add("  Mapping appears complete");
    } else {
        std::string unresolved;
        for (size_t i = 0; i < scan.unresolved.size(); ++i) {
            unresolved += (i ? ", " : " ") + scan.unresolved[i];
        }
        out.Add("  Mapping incomplete");
        out.Add("  unresolved:%s", unresolved.c_str());
    }
    Bound(out.str());
    out.Raw(kMappingEnd);
    return std::move(out.str());
}

} // namespace osl
