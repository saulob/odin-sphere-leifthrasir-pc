#include "build_diagnostics.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <system_error>

#include "cpk_patch.h"
#include "game_files.h"
#include "lzcnt_compat.h"
#include "osl_bookshelf.h"
#include "osl_resolution.h"
#include "osl_skip_intro.h"
#include "utf8.h"

namespace osl {
namespace {

using Bytes = std::vector<uint8_t>;

struct Range {
    uint64_t begin, end;
};

struct ReportedBuild {
    const char* label;
    uint64_t eboot_size;
    const char* eboot_sha256;
};

// Builds reported by players that are not supported yet; the log names them, nothing else treats them as known.
constexpr ReportedBuild kReportedBuilds[] = {
    {"CUSA05083 1.01 USA", 18591996, "2865b6cec33f2515def173c1ed0c03afc27a1cd5fad1b0d0f516b4527aba9a88"},
};

constexpr uint64_t kMaxFile = 256ull << 20;
constexpr uint64_t kClusterSpan = 0x4000;
constexpr uint64_t kStubPage = 0x1000;
constexpr uint32_t kNativeScale = 0x3fc00000;
constexpr size_t kMaxListed = 16;
constexpr std::array<const char*, 2> kPlt = {"sce_discmap.plt", "sce_discmap_patch.plt"};

__attribute__((format(printf, 1, 2))) std::string Fmt(const char* format, ...) {
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    const int length = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::string text(length > 0 ? static_cast<size_t>(length) : 0, '\0');
    if (length > 0) {
        std::vsnprintf(text.data(), text.size() + 1, format, args);
    }
    va_end(args);
    return text;
}

std::string Hex(uint64_t value) {
    return Fmt("0x%llx", static_cast<unsigned long long>(value));
}

std::string List(const std::vector<uint64_t>& values, size_t limit = kMaxListed) {
    std::string text;
    for (size_t i = 0; i < values.size() && i < limit; ++i) {
        text += (i ? " " : "") + Hex(values[i]);
    }
    if (values.size() > limit) {
        text += Fmt(" (+%zu more)", values.size() - limit);
    }
    return text;
}

std::string Verdict(size_t found, size_t expected) {
    if (found == 0) {
        return "not found";
    }
    if (found == expected) {
        return expected == 1 ? "unique" : Fmt("%zu/%zu", found, expected);
    }
    return found < expected ? Fmt("partial (%zu of %zu)", found, expected)
                            : Fmt("ambiguous (%zu matches, expected %zu)", found, expected);
}

uint32_t Read32(const Bytes& image, uint64_t at) {
    uint32_t value;
    std::memcpy(&value, image.data() + at, 4);
    return value;
}

bool InRanges(const std::vector<Range>& ranges, uint64_t position) {
    return std::any_of(ranges.begin(), ranges.end(),
                       [&](const Range& r) { return position >= r.begin && position < r.end; });
}

std::vector<Range> ExecutableRanges(const MappedExecutable& m, uint64_t limit) {
    std::vector<Range> ranges;
    for (const MappedSegment& s : m.segments) {
        if (!(s.flags & 1) || s.vaddr >= limit || s.vaddr >= m.image.size()) {
            continue;
        }
        ranges.push_back({s.vaddr, std::min({s.vaddr + s.memsz, limit, static_cast<uint64_t>(m.image.size())})});
    }
    return ranges;
}

std::vector<int16_t> PatternOf(const short* bytes, size_t count) {
    return std::vector<int16_t>(bytes, bytes + count);
}

std::vector<int16_t> PatternOf(const uint8_t* bytes, size_t count) {
    return std::vector<int16_t>(bytes, bytes + count);
}

// Every position inside `ranges` where the pattern matches; -1 matches any byte and the first element is a literal.
std::vector<uint64_t> Find(const Bytes& image, const std::vector<Range>& ranges, const std::vector<int16_t>& pattern) {
    std::vector<uint64_t> hits;
    const size_t length = pattern.size();
    if (length == 0 || pattern[0] < 0) {
        return hits;
    }
    const uint8_t first = static_cast<uint8_t>(pattern[0]);
    for (const Range& r : ranges) {
        if (r.end > image.size() || r.end - r.begin < length) {
            continue;
        }
        const uint64_t last = r.end - length;
        uint64_t position = r.begin;
        while (position <= last) {
            const void* found = std::memchr(image.data() + position, first, last - position + 1);
            if (!found) {
                break;
            }
            position = static_cast<uint64_t>(static_cast<const uint8_t*>(found) - image.data());
            bool match = true;
            for (size_t k = 1; k < length && match; ++k) {
                match = pattern[k] < 0 || image[position + k] == static_cast<uint8_t>(pattern[k]);
            }
            if (match) {
                hits.push_back(position);
            }
            ++position;
        }
    }
    return hits;
}

// The calls (E8 rel32) inside `ranges` that reach `target`.
std::vector<uint64_t> CallsTo(const Bytes& image, const std::vector<Range>& ranges, uint64_t target) {
    std::vector<uint64_t> calls;
    for (const Range& r : ranges) {
        if (r.end > image.size() || r.end - r.begin < 5) {
            continue;
        }
        for (uint64_t at = r.begin; at + 5 <= r.end; ++at) {
            if (image[at] == 0xe8 &&
                static_cast<int64_t>(at) + 5 + static_cast<int32_t>(Read32(image, at + 1)) == static_cast<int64_t>(target)) {
                calls.push_back(at);
            }
        }
    }
    return calls;
}

struct Scan {
    const MappedExecutable& mapped;
    uint64_t eboot_size;
    std::vector<Range> eboot_exec;
    ScanResult& result;

    const Bytes& image() const { return mapped.image; }
    void Line(const std::string& text) { result.text += text + "\n"; }
    void Unresolved(const std::string& what) { result.unresolved.push_back(what); }
    void Found() { result.anything_found = true; }
    std::vector<uint64_t> InEboot(const std::vector<int16_t>& pattern) const {
        return Find(mapped.image, eboot_exec, pattern);
    }
};

void ScanSkipIntro(Scan& s) {
    s.Line("Skip intro:");
    const struct {
        const char* name;
        const OslSkipIntroCode* code;
    } kinds[] = {{"notice scene", &osl_skip_intro_notice_code},
                 {"hd title check", &osl_skip_intro_hd_title_code},
                 {"classic title check", &osl_skip_intro_classic_title_code}};
    std::vector<uint64_t> classic;
    bool complete = true;
    for (const auto& kind : kinds) {
        std::vector<uint64_t> sites = s.InEboot(PatternOf(kind.code->bytes, kind.code->count));
        for (uint64_t& site : sites) {
            site += kind.code->site;
        }
        s.Line(Fmt("  %s: %s %s", kind.name, Verdict(sites.size(), 1).c_str(), List(sites).c_str()));
        if (!sites.empty()) {
            s.Found();
        }
        if (sites.size() != 1) {
            complete = false;
            s.Unresolved(std::string("Skip intro ") + kind.name);
        } else if (kind.code == &osl_skip_intro_classic_title_code) {
            classic = sites;
        }
    }
    if (classic.size() == 1) {
        const uint64_t site = classic[0];
        const int64_t target = static_cast<int64_t>(site) + 5 + static_cast<int32_t>(Read32(s.image(), site + 1));
        const bool consistent = target >= 0 && InRanges(s.eboot_exec, static_cast<uint64_t>(target));
        s.Line(Fmt("  classic button: %s %s", consistent ? "inside the code" : "OUTSIDE THE CODE",
                   Hex(static_cast<uint64_t>(target)).c_str()));
        if (!consistent) {
            complete = false;
            s.Unresolved("Skip intro classic button");
        }
    }
    uint64_t code_end = 0;
    for (const MappedSegment& seg : s.mapped.segments) {
        if ((seg.flags & 1) && seg.vaddr < s.eboot_size) {
            code_end = std::max(code_end, seg.vaddr + seg.memsz);
        }
    }
    const uint64_t page_end = std::min<uint64_t>((code_end + kStubPage - 1) / kStubPage * kStubPage, s.eboot_size);
    bool zeroed = code_end != 0 && page_end >= code_end && page_end - code_end >= OSL_SKIP_INTRO_STUB_BYTES;
    for (uint64_t i = 0; zeroed && i < OSL_SKIP_INTRO_STUB_BYTES; ++i) {
        zeroed = s.image()[code_end + i] == 0;
    }
    s.Line(Fmt("  stub space: %s %s (%llu bytes to the page end)", zeroed ? "free" : "NOT FREE", Hex(code_end).c_str(),
               static_cast<unsigned long long>(page_end > code_end ? page_end - code_end : 0)));
    if (!zeroed) {
        complete = false;
        s.Unresolved("Skip intro stub space");
    }
    s.Line(std::string("  status: ") + (complete ? "complete" : "incomplete"));
}

std::vector<uint64_t> Near(const std::vector<uint64_t>& hits, bool anchored, uint64_t anchor) {
    std::vector<uint64_t> near;
    for (uint64_t hit : hits) {
        if (!anchored || (hit > anchor ? hit - anchor : anchor - hit) <= kClusterSpan) {
            near.push_back(hit);
        }
    }
    return near;
}

void ScanBookshelf(Scan& s) {
    s.Line("Unlock Bookshelf:");
    const std::vector<uint64_t> getter =
        s.InEboot(PatternOf(osl_bookshelf_accessor_shape, OSL_BOOKSHELF_ACCESSOR_BYTES));
    s.Line(Fmt("  getter: %s %s", Verdict(getter.size(), 1).c_str(), List(getter).c_str()));
    const std::vector<uint64_t> special = s.InEboot(PatternOf(osl_bookshelf_total_code, OSL_BOOKSHELF_TOTAL_BYTES));
    s.Line(Fmt("  special test: %s %s", Verdict(special.size(), 1).c_str(), List(special).c_str()));
    s.Line("  stub space: allocated by the loader, not build specific");
    bool complete = true;
    if (!getter.empty() || !special.empty()) {
        s.Found();
    }
    if (special.size() != 1) {
        complete = false;
        s.Unresolved("Bookshelf special test");
    }
    if (getter.size() != 1) {
        complete = false;
        s.Unresolved("Bookshelf getter");
        s.Line(Fmt("  reads: not attempted (getter %s)", Verdict(getter.size(), 1).c_str()));
        s.Line("  status: incomplete");
        return;
    }
    const std::vector<uint64_t> calls = CallsTo(s.image(), s.eboot_exec, getter[0]);
    s.Line(Fmt("  calls to the getter: %zu", calls.size()));
    const auto match = [&](uint64_t call, const unsigned char* signature) {
        return call + 5 + OSL_BOOKSHELF_SIGNATURE_BYTES <= s.image().size() &&
               std::memcmp(s.image().data() + call + 5, signature, OSL_BOOKSHELF_SIGNATURE_BYTES) == 0;
    };
    const auto matching = [&](const unsigned char* signature) {
        std::vector<uint64_t> hits;
        for (uint64_t call : calls) {
            if (match(call, signature)) {
                hits.push_back(call);
            }
        }
        return hits;
    };
    const auto same_signature = [](const unsigned char* a, const unsigned char* b) {
        return std::memcmp(a, b, OSL_BOOKSHELF_SIGNATURE_BYTES) == 0;
    };

    std::vector<std::vector<uint64_t>> found(OSL_BOOKSHELF_READS);
    std::vector<uint64_t> alone;
    for (int i = 0; i < OSL_BOOKSHELF_READS; ++i) {
        found[i] = matching(osl_bookshelf_read_signatures[i]);
        int shared = 0;
        for (int j = 0; j < OSL_BOOKSHELF_READS; ++j) {
            shared += same_signature(osl_bookshelf_read_signatures[i], osl_bookshelf_read_signatures[j]);
        }
        if (shared == 1 && found[i].size() == 1) {
            alone.push_back(found[i][0]);
        }
    }
    std::sort(alone.begin(), alone.end());
    const bool anchored = !alone.empty();
    const uint64_t anchor = anchored ? alone[alone.size() / 2] : 0;

    std::vector<uint64_t> reads(OSL_BOOKSHELF_READS, 0);
    int resolved = 0;
    bool in_place = false;
    for (int i = 0; i < OSL_BOOKSHELF_READS; ++i) {
        std::vector<int> group;
        for (int j = 0; j < OSL_BOOKSHELF_READS; ++j) {
            if (same_signature(osl_bookshelf_read_signatures[i], osl_bookshelf_read_signatures[j])) {
                group.push_back(j);
            }
        }
        if (group[0] != i) {
            continue;
        }
        std::vector<uint64_t> hits = Near(found[i], anchored, anchor);
        if (hits.size() != group.size() && i == 7) {
            const std::vector<uint64_t> alternate = Near(matching(osl_bookshelf_in_place_page_signature), anchored, anchor);
            if (alternate.size() == group.size()) {
                hits = alternate;
                in_place = true;
            }
        }
        if (hits.size() == group.size()) {
            std::sort(hits.begin(), hits.end());
            for (size_t k = 0; k < group.size(); ++k) {
                reads[group[k]] = hits[k];
            }
            resolved += static_cast<int>(group.size());
        } else {
            s.Line(Fmt("  read %d: %s %s", i, Verdict(hits.size(), group.size()).c_str(), List(hits).c_str()));
            s.Unresolved(Fmt("Bookshelf read %d", i));
            complete = false;
        }
    }
    const bool ordered = resolved == OSL_BOOKSHELF_READS && std::adjacent_find(reads.begin(), reads.end(), [](uint64_t a, uint64_t b) {
                                                                return a >= b;
                                                            }) == reads.end();
    s.Line(Fmt("  reads: %d/%d%s", resolved, OSL_BOOKSHELF_READS, resolved == OSL_BOOKSHELF_READS ? "" : " (incomplete)"));
    if (resolved == OSL_BOOKSHELF_READS) {
        s.Line("    " + List(reads, OSL_BOOKSHELF_READS));
        s.Line(Fmt("  read order: %s, page read: %s", ordered ? "consistent" : "INCONSISTENT",
                   in_place ? "in place" : "through a helper"));
        if (!ordered) {
            complete = false;
            s.Unresolved("Bookshelf read order");
        }
    }
    std::vector<uint64_t> marks;
    for (int i = 0; i < OSL_BOOKSHELF_MARKS; ++i) {
        const std::vector<uint64_t> hits = Near(matching(osl_bookshelf_mark_signatures[i]), anchored, anchor);
        if (hits.size() == 1) {
            marks.push_back(hits[0]);
        } else {
            s.Line(Fmt("  mark %d: %s %s", i, Verdict(hits.size(), 1).c_str(), List(hits).c_str()));
            s.Unresolved(Fmt("Bookshelf mark %d", i));
            complete = false;
        }
    }
    s.Line(Fmt("  marks: %zu/%d %s", marks.size(), OSL_BOOKSHELF_MARKS, List(marks).c_str()));
    s.Line(std::string("  status: ") + (complete ? "complete" : "incomplete"));
}

float FloatOf(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

void ScanSnapshotScale(Scan& s) {
    s.Line("Snapshot scale:");
    const std::vector<uint64_t> hits = s.InEboot(PatternOf(osl_snapshot_window, OSL_SNAPSHOT_WINDOW));
    s.Line(Fmt("  site: %s %s", Verdict(hits.size(), 1).c_str(), List(hits).c_str()));
    if (!hits.empty()) {
        s.Found();
    }
    bool complete = hits.size() == 1;
    if (hits.size() == 1) {
        const uint64_t site = hits[0];
        const auto operand = [&](int at) {
            return static_cast<int64_t>(site) + at + 4 + static_cast<int32_t>(Read32(s.image(), site + at));
        };
        const int64_t factor = operand(OSL_SNAPSHOT_FACTOR), pair = operand(OSL_SNAPSHOT_PAIR);
        const bool factor_ok = factor >= 0 && static_cast<uint64_t>(factor) + 4 <= s.image().size();
        const bool pair_ok = pair >= 0 && static_cast<uint64_t>(pair) + 8 <= s.image().size();
        if (factor_ok) {
            const uint32_t bits = Read32(s.image(), static_cast<uint64_t>(factor));
            s.Line(Fmt("  native scale: %g at %s (%s)", FloatOf(bits), Hex(static_cast<uint64_t>(factor)).c_str(),
                       bits == kNativeScale ? "the expected 1.5" : "NOT 1.5"));
            complete = complete && bits == kNativeScale;
        } else {
            s.Line("  native scale: outside the image");
            complete = false;
        }
        if (pair_ok) {
            s.Line(Fmt("  layer scale base: %g %g at %s", FloatOf(Read32(s.image(), static_cast<uint64_t>(pair))),
                       FloatOf(Read32(s.image(), static_cast<uint64_t>(pair) + 4)), Hex(static_cast<uint64_t>(pair)).c_str()));
        } else {
            s.Line("  layer scale base: outside the image");
            complete = false;
        }
    }
    if (!complete) {
        s.Unresolved("Snapshot scale");
    }
    s.Line(std::string("  status: ") + (complete ? "complete" : "incomplete"));
}

struct SiteGroup {
    std::vector<uint8_t> bytes;
    std::vector<uint32_t> starts; // of each instruction, from the group's first byte
    size_t expected = 0;
    std::string label;
};

void AppendLe32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<uint8_t>(value >> shift));
    }
}

// The template's instructions, with adjacent ones (a width followed by its height) as one group.
std::vector<SiteGroup> GroupsOf(std::span<const OslResolutionSite> sites) {
    std::map<std::vector<uint8_t>, SiteGroup> unique;
    for (size_t i = 0; i < sites.size();) {
        SiteGroup group;
        uint32_t end = 0;
        do {
            const OslResolutionSite& site = sites[i];
            group.starts.push_back(static_cast<uint32_t>(group.bytes.size()));
            group.bytes.insert(group.bytes.end(), site.opcode, site.opcode + site.opcode_length);
            AppendLe32(group.bytes, site.value);
            if (site.packed_floats) {
                AppendLe32(group.bytes, site.value_high);
            }
            end = site.offset + site.opcode_length + (site.packed_floats ? 8 : 4);
            ++i;
        } while (i < sites.size() && sites[i].offset == end);
        SiteGroup& entry = unique[group.bytes];
        if (entry.expected == 0) {
            entry = group;
            const OslResolutionSite& first = sites[i - group.starts.size()];
            entry.label = Fmt("opcode %02x value 0x%x%s", first.opcode[0], first.value,
                              group.starts.size() > 1 ? Fmt(" + %zu more", group.starts.size() - 1).c_str() : "");
        }
        ++entry.expected;
    }
    std::vector<SiteGroup> groups;
    for (auto& [bytes, group] : unique) {
        groups.push_back(std::move(group));
    }
    return groups;
}

void ScanResolution(Scan& s, std::span<const GameBuild> templates) {
    s.Line("Resolution:");
    const auto source = std::find_if(templates.begin(), templates.end(),
                                     [](const GameBuild& build) { return !build.resolution_sites.empty(); });
    if (source == templates.end()) {
        s.Line("  no supported build to take the instruction shapes from");
        s.Unresolved("Resolution sites");
        return;
    }
    const std::span<const OslResolutionSite> expected_sites = source->resolution_sites;
    std::vector<uint64_t> mapped;
    bool complete = true;
    for (const SiteGroup& group : GroupsOf(expected_sites)) {
        std::vector<uint64_t> hits = s.InEboot(PatternOf(group.bytes.data(), group.bytes.size()));
        s.Line(Fmt("  %-34s %s %s", group.label.c_str(), Verdict(hits.size(), group.expected).c_str(), List(hits).c_str()));
        if (!hits.empty()) {
            s.Found();
        }
        if (hits.size() == group.expected) {
            for (uint64_t hit : hits) {
                for (uint32_t start : group.starts) {
                    mapped.push_back(hit + start);
                }
            }
        } else {
            complete = false;
            s.Unresolved("Resolution " + group.label);
        }
    }
    std::sort(mapped.begin(), mapped.end());
    s.Line(Fmt("  sites: %zu/%zu", mapped.size(), expected_sites.size()));
    if (complete) {
        s.Line("    " + List(mapped, expected_sites.size()));
    }
    s.Line(std::string("  status: ") + (complete ? "complete" : "incomplete"));
}

// The start of every LZCNT ([66] F3 [REX] 0F BD) inside `ranges`.
std::vector<uint64_t> LzcntStarts(const Bytes& image, const std::vector<Range>& ranges) {
    std::vector<uint64_t> starts;
    for (const Range& r : ranges) {
        if (r.end > image.size() || r.end - r.begin < 4) {
            continue;
        }
        for (uint64_t at = r.begin + 2; at + 3 <= r.end; ++at) {
            if (image[at] != 0x0f || image[at + 1] != 0xbd) {
                continue;
            }
            uint64_t prefix = at - 1;
            if ((image[prefix] & 0xf0) == 0x40) {
                --prefix;
            }
            if (prefix < r.begin || image[prefix] != 0xf3) {
                continue;
            }
            starts.push_back(prefix > r.begin && image[prefix - 1] == 0x66 ? prefix - 1 : prefix);
        }
    }
    return starts;
}

void ScanLzcnt(Scan& s) {
    s.Line("Legacy CPU (LZCNT):");
    const Bytes& image = s.image();
    const std::vector<Range> all = ExecutableRanges(s.mapped, image.size());
    const std::vector<uint64_t> starts = LzcntStarts(image, all);
    s.Line(Fmt("  linked image: %s, entry %s", Hex(image.size()).c_str(), Hex(s.mapped.entry).c_str()));
    s.Line(Fmt("  instructions found: %zu (the shapes hold %zu)", starts.size(), lzcnt_compat_instructions()));
    if (!starts.empty()) {
        s.Found();
    }

    std::vector<std::vector<uint64_t>> candidates(LZCNT_RANGE_COUNT);
    for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i) {
        const LzcntShape& shape = lzcnt_shapes[i];
        for (uint64_t start : starts) {
            if (start + shape.length <= image.size() && std::memcmp(image.data() + start, shape.bytes, shape.length) == 0) {
                candidates[i].push_back(start);
            }
        }
    }
    std::array<uint64_t, LZCNT_RANGE_COUNT> early{}, late{};
    uint64_t previous = 0;
    size_t failed = LZCNT_RANGE_COUNT;
    for (size_t i = 0; i < LZCNT_RANGE_COUNT && failed == LZCNT_RANGE_COUNT; ++i) {
        const auto pick = std::find_if(candidates[i].begin(), candidates[i].end(), [&](uint64_t p) { return p >= previous; });
        if (pick == candidates[i].end()) {
            failed = i;
        } else {
            early[i] = *pick;
            previous = *pick + lzcnt_shapes[i].length;
        }
    }
    uint64_t next = image.size();
    for (size_t i = LZCNT_RANGE_COUNT; i-- > 0 && failed == LZCNT_RANGE_COUNT;) {
        const auto pick = std::find_if(candidates[i].rbegin(), candidates[i].rend(),
                                       [&](uint64_t p) { return p + lzcnt_shapes[i].length <= next; });
        if (pick == candidates[i].rend()) {
            failed = i;
        } else {
            late[i] = *pick;
            next = *pick;
        }
    }
    if (failed != LZCNT_RANGE_COUNT) {
        s.Line(Fmt("  ranges: shape %zu has no candidate in order", failed));
        s.Line("  status: incomplete");
        s.Unresolved("LZCNT ranges");
        return;
    }
    const bool unique = early == late;
    s.Line(Fmt("  ranges: %s (%d shapes placed in order)", unique ? "unique" : "AMBIGUOUS", LZCNT_RANGE_COUNT));
    s.Line("    " + List(std::vector<uint64_t>(early.begin(), early.end()), LZCNT_RANGE_COUNT));
    if (!unique) {
        s.Line("    latest placement: " + List(std::vector<uint64_t>(late.begin(), late.end()), LZCNT_RANGE_COUNT));
        s.Unresolved("LZCNT ranges ambiguous");
    }
    std::vector<uint64_t> uncovered;
    for (uint64_t start : starts) {
        bool covered = false;
        for (size_t i = 0; i < LZCNT_RANGE_COUNT && !covered; ++i) {
            covered = start >= early[i] && start < early[i] + lzcnt_shapes[i].length;
        }
        if (!covered) {
            uncovered.push_back(start);
        }
    }
    s.Line(Fmt("  unexpected instructions: %zu %s", uncovered.size(), List(uncovered).c_str()));
    if (!uncovered.empty()) {
        s.Unresolved("LZCNT instructions outside the shapes");
    }
    bool rewritable = false;
    if (unique && uncovered.empty()) {
        const uint64_t stubs_at = (image.size() + kStubPage - 1) / kStubPage * kStubPage;
        const size_t stub_bytes = lzcnt_compat_stub_bytes();
        Bytes scratch(stubs_at + stub_bytes, 0);
        std::copy(image.begin(), image.end(), scratch.begin());
        std::array<uint32_t, LZCNT_RANGE_COUNT> offsets{};
        std::copy(early.begin(), early.end(), offsets.begin());
        const LzcntCompatStatus status = lzcnt_compat_apply(scratch.data(), image.size(), offsets.data(), lzcnt_shapes,
                                                            LZCNT_RANGE_COUNT, scratch.data() + stubs_at, stub_bytes);
        rewritable = status == LZCNT_COMPAT_OK;
        s.Line(Fmt("  rewrite check on a scratch copy: %s", lzcnt_compat_status_text(status)));
        if (!rewritable) {
            s.Unresolved("LZCNT rewrite check");
        }
    }
    s.Line(std::string("  status: ") + (unique && uncovered.empty() && rewritable ? "complete" : "incomplete"));
}

std::string ReadSmallText(const std::filesystem::path& path) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > (4u << 20)) {
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    std::string text(static_cast<size_t>(size), '\0');
    in.read(text.data(), static_cast<std::streamsize>(size));
    return static_cast<size_t>(in.gcount()) == text.size() ? text : std::string{};
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool IsOdinArchive(const std::filesystem::path& file) {
    const std::string name = Lower(Utf8(file.filename()));
    return name.starts_with("odin2_") && name.ends_with(".cpk");
}

// Appends the bundled module's mapping after the image, at the 64 KiB boundary the boot image uses.
std::string LinkModule(MappedExecutable& image, const MappedExecutable& module) {
    const uint64_t base = (image.image.size() + 65535) & ~uint64_t{65535};
    uint64_t size = 0;
    for (const MappedSegment& s : module.segments) {
        size = std::max(size, s.vaddr + s.memsz);
    }
    if (base + size > (512ull << 20) || size < module.image.size()) {
        return "linked image exceeds the 512 MiB limit";
    }
    image.image.resize(base + size, 0);
    std::copy(module.image.begin(), module.image.end(), image.image.begin() + static_cast<std::ptrdiff_t>(base));
    for (const MappedSegment& s : module.segments) {
        image.segments.push_back({base + s.vaddr, s.memsz, s.flags});
    }
    return {};
}

void DescribeArchives(const std::filesystem::path& app0, std::span<const uint8_t> prompt_patch, std::string& text) {
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(app0, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (it->is_regular_file(type_ec)) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    text += "Files:\n";
    std::vector<CpkArchive> archives;
    for (const std::filesystem::path& file : files) {
        std::error_code size_ec;
        const std::uintmax_t size = std::filesystem::file_size(file, size_ec);
        std::string line = Fmt("  %s: %llu bytes", Utf8(file.filename()).c_str(), static_cast<unsigned long long>(size));
        if (IsOdinArchive(file)) {
            CpkArchive archive;
            std::string error;
            if (archive.Open(file, &error)) {
                line += Fmt(", CPK valid, %zu entries", archive.entries().size());
                archives.push_back(std::move(archive));
            } else {
                line += ", CPK NOT READABLE (" + error + ")";
            }
        }
        text += line + "\n";
    }
    for (const char* name : kPlt) {
        std::error_code plt_ec;
        if (!std::filesystem::is_regular_file(app0 / name, plt_ec)) {
            text += Fmt("  %s: absent\n", name);
        }
    }
    if (prompt_patch.empty()) {
        return;
    }
    const std::optional<BlockPatch> patch = ParseBlockPatch(prompt_patch);
    if (!patch) {
        return;
    }
    size_t present = 0, same_size = 0;
    std::string detail;
    for (const BlockPatchTarget& target : patch->targets) {
        for (const CpkArchive& archive : archives) {
            if (const CpkEntry* entry = archive.Find(target.name)) {
                ++present;
                const bool equal = entry->extract_size == target.size;
                same_size += equal;
                detail += Fmt("    %s in %s: %s\n", target.name.c_str(), Utf8(archive.file().filename()).c_str(),
                              equal ? "size matches" : "SIZE DIFFERS");
                break;
            }
        }
    }
    text += Fmt("Controller prompt patch targets: %zu of %zu present, %zu with the expected size\n", present,
                patch->targets.size(), same_size);
    text += detail;
}

std::string SfoLine(const std::string& sfo) {
    return Fmt("  param.sfo: TITLE_ID %s, CONTENT_ID %s, APP_VER %s, VERSION %s, CATEGORY %s",
               SfoString(sfo, "TITLE_ID").c_str(), SfoString(sfo, "CONTENT_ID").c_str(), SfoString(sfo, "APP_VER").c_str(),
               SfoString(sfo, "VERSION").c_str(), SfoString(sfo, "CATEGORY").c_str());
}

constexpr char kBegin[] = "Unsupported build diagnostics begin\n";
constexpr char kEnd[] = "Unsupported build diagnostics end\n";

} // namespace

ScanResult ScanExecutable(const MappedExecutable& image, uint64_t eboot_size, std::span<const GameBuild> templates) {
    ScanResult result;
    Scan scan{image, eboot_size, ExecutableRanges(image, eboot_size), result};
    ScanSkipIntro(scan);
    ScanBookshelf(scan);
    ScanSnapshotScale(scan);
    ScanResolution(scan, templates);
    ScanLzcnt(scan);
    return result;
}

UnsupportedBuildReport DiagnoseUnsupportedBuild(const std::filesystem::path& app0, const std::string& detail,
                                                std::span<const uint8_t> prompt_patch) {
    UnsupportedBuildReport report;
    std::error_code ec;
    const std::filesystem::path eboot_path = app0 / "eboot.bin";
    const std::uintmax_t eboot_size = std::filesystem::file_size(eboot_path, ec);
    bool has_archive = false;
    for (std::filesystem::directory_iterator it(app0, ec), end; !ec && it != end && !has_archive; it.increment(ec)) {
        std::error_code type_ec;
        has_archive = it->is_regular_file(type_ec) && IsOdinArchive(it->path());
    }
    ec.clear();
    if (!has_archive || !std::filesystem::is_regular_file(eboot_path, ec) || eboot_size > kMaxFile) {
        return report;
    }

    const std::string sfo = ReadSmallText(app0 / "sce_sys" / "param.sfo");
    const bool odin_content = SfoString(sfo, "CONTENT_ID").find("ODINSPHERE") != std::string::npos;
    const std::string sha = Sha256Hex(eboot_path);

    std::string text = kBegin;
    text += "Identity:\n";
    text += Fmt("  eboot.bin: %llu bytes\n", static_cast<unsigned long long>(eboot_size));
    text += "  SHA-256: " + (sha.empty() ? std::string("unreadable") : sha) + "\n";
    std::string reported = "none";
    for (const ReportedBuild& build : kReportedBuilds) {
        if (build.eboot_size == eboot_size && sha == build.eboot_sha256) {
            reported = build.label;
        }
    }
    text += "  Reported build match: " + reported + " (still unsupported)\n";
    text += SfoLine(sfo) + "\n";
    text += "  Supported-build check: " + detail + "\n";

    MappedExecutable image;
    const std::string mapping = MapExecutable(eboot_path, image);
    ScanResult scan;
    if (!mapping.empty()) {
        text += "  Executable: not readable (" + mapping + ")\n";
        scan.unresolved.push_back("executable");
    } else {
        text += Fmt("  Entry point: %s, mapped image: %s\n", Hex(image.entry).c_str(), Hex(image.image.size()).c_str());
        for (const MappedSegment& s : image.segments) {
            text += Fmt("  Segment: address %s, size %s, flags %u\n", Hex(s.vaddr).c_str(), Hex(s.memsz).c_str(), s.flags);
        }
        const uint64_t mapped_size = image.image.size();
        for (const char* name : kBundledModules) {
            const std::filesystem::path module_path = app0 / "sce_module" / name;
            MappedExecutable module;
            const std::string module_error = MapExecutable(module_path, module);
            const std::string link_error = module_error.empty() ? LinkModule(image, module) : module_error;
            text += Fmt("  Module %s: %s, SHA-256 %s\n", name, link_error.empty() ? "linked" : link_error.c_str(),
                        Sha256Hex(module_path).c_str());
        }
        scan = ScanExecutable(image, mapped_size);
    }
    report.odin = odin_content || scan.anything_found;
    if (!report.odin) {
        return {};
    }

    DescribeArchives(app0, prompt_patch, text);
    text += scan.text;
    text += "Overall diagnostic result:\n";
    if (scan.complete()) {
        text += "  Mapping appears complete\n";
    } else {
        text += "  Mapping incomplete\n  unresolved:";
        for (size_t i = 0; i < scan.unresolved.size(); ++i) {
            text += (i ? ", " : " ") + scan.unresolved[i];
        }
        text += "\n";
    }
    text += kEnd;
    report.text = std::move(text);
    return report;
}

UnsupportedBuildReport DiagnoseUnsupportedPackage(const std::string& sfo) {
    UnsupportedBuildReport report;
    if (SfoString(sfo, "CONTENT_ID").find("ODINSPHERE") == std::string::npos) {
        return report;
    }
    report.odin = true;
    report.text = std::string(kBegin) + "Identity:\n" + SfoLine(sfo) +
                  "\n  The package executable was not analyzed; select the extracted game folder to analyze it.\n" + kEnd;
    return report;
}

} // namespace osl
