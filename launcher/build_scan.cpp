#include "build_scan.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <system_error>

#include "lzcnt_compat.h"
#include "osl_bookshelf.h"
#include "osl_cheats.h"
#include "osl_resolution.h"
#include "osl_skip_intro.h"

namespace osl {
namespace {

using Bytes = std::vector<uint8_t>;

struct Range {
    uint64_t begin, end;
};

constexpr uint64_t kMaxFile = 256ull << 20;
constexpr uint64_t kClusterSpan = 0x4000;
constexpr uint64_t kStubPage = 0x1000;
constexpr uint32_t kNativeScale = 0x3fc00000;
constexpr size_t kMaxListed = 16;
constexpr size_t kMinPartialRun = 6;
constexpr size_t kMaxPartialReports = 12;
constexpr size_t kMaxPartialHits = 4;
constexpr size_t kMaxPairedHits = 64;

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

// A part of `expected` sites that the scan matched `found` times in total.
ScanComponent Component(std::string name, size_t expected, size_t found) {
    ScanComponent c{std::move(name), expected, 0, 0};
    if (found > expected) {
        c.ambiguous = expected;
    } else {
        c.unique = found;
    }
    return c;
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
    std::vector<std::string> blocks;
    size_t partials = 0;

    const Bytes& image() const { return mapped.image; }
    FeatureScan& Begin(const char* name) {
        result.features.push_back({name, {}, {}, {}, FeatureState::kMissing});
        blocks.push_back(std::string(name) + ":\n");
        return result.features.back();
    }
    FeatureScan& Current() { return result.features.back(); }
    void Line(const std::string& text) { blocks.back() += text + "\n"; }
    void Unresolved(const std::string& what) { result.unresolved.push_back(what); }
    void Found() { result.anything_found = true; }
    void Broken(const std::string& why) {
        std::string& invariant = Current().invariant;
        invariant += (invariant.empty() ? "" : "; ") + why;
    }
    std::vector<uint64_t> InEboot(const std::vector<int16_t>& pattern) const {
        return Find(mapped.image, eboot_exec, pattern);
    }
};

// For a signature that is not found: where its two longest literal runs match, and the sites at which both match with
// the signature's spacing. Hints for adapting the signature, never a mapping.
void PartialRuns(Scan& s, const std::vector<int16_t>& pattern, int64_t site_offset) {
    if (s.partials >= kMaxPartialReports) {
        return;
    }
    std::vector<std::pair<size_t, size_t>> runs; // start, length
    for (size_t i = 0; i < pattern.size();) {
        if (pattern[i] < 0) {
            ++i;
            continue;
        }
        size_t end = i;
        while (end < pattern.size() && pattern[end] >= 0) {
            ++end;
        }
        if (end - i >= kMinPartialRun) {
            runs.emplace_back(i, end - i);
        }
        i = end;
    }
    if (runs.empty()) {
        return;
    }
    std::sort(runs.begin(), runs.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    runs.resize(std::min<size_t>(runs.size(), 2));
    std::sort(runs.begin(), runs.end());
    ++s.partials;
    std::vector<std::vector<uint64_t>> hits;
    std::string line = "    partial:";
    for (const auto& [start, length] : runs) {
        hits.push_back(s.InEboot(std::vector<int16_t>(pattern.begin() + start, pattern.begin() + start + length)));
        line += Fmt(" bytes %zu..%zu x%zu", start, start + length, hits.back().size());
        for (size_t k = 0; k < hits.back().size() && k < kMaxPartialHits; ++k) {
            line += " " + Hex(hits.back()[k]);
        }
    }
    s.Line(line);
    if (runs.size() != 2) {
        return;
    }
    size_t shown = 0;
    const uint64_t spacing = runs[1].first - runs[0].first;
    for (size_t a = 0; a < hits[0].size() && a < kMaxPairedHits; ++a) {
        for (size_t b = 0; b < hits[1].size() && b < kMaxPairedHits && shown < kMaxPartialHits; ++b) {
            if (hits[1][b] > hits[0][a] && hits[1][b] - hits[0][a] == spacing) {
                s.Line("    consistent candidate (not a mapping): site at " +
                       Hex(static_cast<uint64_t>(static_cast<int64_t>(hits[0][a] - runs[0].first) + site_offset)));
                ++shown;
            }
        }
    }
}

void ScanSkipIntro(Scan& s) {
    FeatureScan& feature = s.Begin("Skip intro");
    const struct {
        const char* name;
        const OslSkipIntroCode* code;
    } kinds[] = {{"notice scene", &osl_skip_intro_notice_code},
                 {"hd title check", &osl_skip_intro_hd_title_code},
                 {"classic title check", &osl_skip_intro_classic_title_code}};
    std::vector<uint64_t> classic;
    std::array<uint64_t, 3> picked{};
    int64_t classic_target = -1;
    size_t kind_index = 0;
    bool complete = true;
    for (const auto& kind : kinds) {
        const std::vector<int16_t> pattern = PatternOf(kind.code->bytes, kind.code->count);
        std::vector<uint64_t> sites = s.InEboot(pattern);
        for (uint64_t& site : sites) {
            site += kind.code->site;
        }
        s.Line(Fmt("  %s: %s %s", kind.name, Verdict(sites.size(), 1).c_str(), List(sites).c_str()));
        feature.components.push_back(Component(kind.name, 1, sites.size()));
        if (!sites.empty()) {
            s.Found();
        } else {
            PartialRuns(s, pattern, kind.code->site);
        }
        if (sites.size() != 1) {
            complete = false;
            s.Unresolved(std::string("Skip intro ") + kind.name);
        } else if (kind.code == &osl_skip_intro_classic_title_code) {
            classic = sites;
        }
        if (sites.size() == 1) {
            picked[kind_index] = sites[0];
        }
        ++kind_index;
    }
    if (classic.size() == 1) {
        const uint64_t site = classic[0];
        const int64_t target = static_cast<int64_t>(site) + 5 + static_cast<int32_t>(Read32(s.image(), site + 1));
        const bool consistent = target >= 0 && InRanges(s.eboot_exec, static_cast<uint64_t>(target));
        classic_target = target;
        s.Line(Fmt("  classic button: %s %s", consistent ? "inside the code" : "OUTSIDE THE CODE",
                   Hex(static_cast<uint64_t>(target)).c_str()));
        if (!consistent) {
            complete = false;
            s.Unresolved("Skip intro classic button");
            s.Broken("the classic title call leaves the code");
        }
    }
    uint64_t code_end = 0;
    for (const MappedSegment& seg : s.mapped.segments) {
        if ((seg.flags & 1) && seg.vaddr < s.eboot_size) {
            code_end = std::max(code_end, seg.vaddr + seg.memsz);
        }
    }
    const uint64_t page_end = std::min<uint64_t>((code_end + kStubPage - 1) / kStubPage * kStubPage, s.eboot_size);
    bool zeroed = code_end != 0 && page_end >= code_end && page_end - code_end >= OSL_SKIP_INTRO_STUB_BYTES &&
                  code_end + OSL_SKIP_INTRO_STUB_BYTES <= s.image().size();
    for (uint64_t i = 0; zeroed && i < OSL_SKIP_INTRO_STUB_BYTES; ++i) {
        zeroed = s.image()[code_end + i] == 0;
    }
    s.Line(Fmt("  stub space: %s %s (%llu bytes to the page end)", zeroed ? "free" : "NOT FREE", Hex(code_end).c_str(),
               static_cast<unsigned long long>(page_end > code_end ? page_end - code_end : 0)));
    if (!zeroed) {
        complete = false;
        s.Unresolved("Skip intro stub space");
        s.Broken("no free stub space after the code");
    }
    if (complete) {
        s.result.mapping.skip_intro =
            OslSkipIntroSites{static_cast<uint32_t>(picked[0]), static_cast<uint32_t>(picked[1]),
                              static_cast<uint32_t>(picked[2]), static_cast<uint32_t>(classic_target),
                              static_cast<uint32_t>((code_end + 15) & ~uint64_t{15})};
    }
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
    FeatureScan& feature = s.Begin("Unlock Bookshelf");
    const std::vector<int16_t> getter_pattern = PatternOf(osl_bookshelf_accessor_shape, OSL_BOOKSHELF_ACCESSOR_BYTES);
    const std::vector<int16_t> special_pattern = PatternOf(osl_bookshelf_total_code, OSL_BOOKSHELF_TOTAL_BYTES);
    const std::vector<uint64_t> getter = s.InEboot(getter_pattern);
    s.Line(Fmt("  getter: %s %s", Verdict(getter.size(), 1).c_str(), List(getter).c_str()));
    if (getter.empty()) {
        PartialRuns(s, getter_pattern, 0);
    }
    const std::vector<uint64_t> special = s.InEboot(special_pattern);
    s.Line(Fmt("  special test: %s %s", Verdict(special.size(), 1).c_str(), List(special).c_str()));
    if (special.empty()) {
        PartialRuns(s, special_pattern, 0);
    }
    s.Line("  stub space: allocated by the loader, not build specific");
    feature.components.push_back(Component("getter", 1, getter.size()));
    feature.components.push_back(Component("special test", 1, special.size()));
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
        feature.components.push_back(Component("reads", OSL_BOOKSHELF_READS, 0));
        feature.components.push_back(Component("marks", OSL_BOOKSHELF_MARKS, 0));
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
    ScanComponent read_component{"reads", OSL_BOOKSHELF_READS, 0, 0};
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
        const ScanComponent part = Component({}, group.size(), hits.size());
        read_component.unique += part.unique;
        read_component.ambiguous += part.ambiguous;
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
    feature.components.push_back(read_component);
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
            s.Broken("the reads are not in code order");
        }
    }
    std::vector<uint64_t> marks;
    ScanComponent mark_component{"marks", OSL_BOOKSHELF_MARKS, 0, 0};
    for (int i = 0; i < OSL_BOOKSHELF_MARKS; ++i) {
        const std::vector<uint64_t> hits = Near(matching(osl_bookshelf_mark_signatures[i]), anchored, anchor);
        const ScanComponent part = Component({}, 1, hits.size());
        mark_component.unique += part.unique;
        mark_component.ambiguous += part.ambiguous;
        if (hits.size() == 1) {
            marks.push_back(hits[0]);
        } else {
            s.Line(Fmt("  mark %d: %s %s", i, Verdict(hits.size(), 1).c_str(), List(hits).c_str()));
            s.Unresolved(Fmt("Bookshelf mark %d", i));
            complete = false;
        }
    }
    feature.components.push_back(mark_component);
    s.Line(Fmt("  marks: %zu/%d %s", marks.size(), OSL_BOOKSHELF_MARKS, List(marks).c_str()));
    if (complete) {
        OslBookshelfSites sites{};
        sites.accessor = static_cast<uint32_t>(getter[0]);
        sites.total = static_cast<uint32_t>(special[0]);
        std::copy(reads.begin(), reads.end(), sites.reads);
        std::copy(marks.begin(), marks.end(), sites.marks);
        sites.in_place_page_read = in_place ? 1 : 0;
        s.result.mapping.bookshelf = sites;
    }
}

float FloatOf(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

void ScanSnapshotScale(Scan& s) {
    FeatureScan& feature = s.Begin("Snapshot scale");
    const std::vector<int16_t> pattern = PatternOf(osl_snapshot_window, OSL_SNAPSHOT_WINDOW);
    const std::vector<uint64_t> hits = s.InEboot(pattern);
    s.Line(Fmt("  site: %s %s", Verdict(hits.size(), 1).c_str(), List(hits).c_str()));
    feature.components.push_back(Component("site", 1, hits.size()));
    if (!hits.empty()) {
        s.Found();
    } else {
        PartialRuns(s, pattern, 0);
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
            if (bits != kNativeScale) {
                complete = false;
                s.Broken("the native scale is not 1.5");
            }
        } else {
            s.Line("  native scale: outside the image");
            complete = false;
            s.Broken("the native scale is outside the image");
        }
        if (pair_ok) {
            s.Line(Fmt("  layer scale base: %g %g at %s", FloatOf(Read32(s.image(), static_cast<uint64_t>(pair))),
                       FloatOf(Read32(s.image(), static_cast<uint64_t>(pair) + 4)), Hex(static_cast<uint64_t>(pair)).c_str()));
        } else {
            s.Line("  layer scale base: outside the image");
            complete = false;
            s.Broken("the layer scale base is outside the image");
        }
    }
    if (!complete) {
        s.Unresolved("Snapshot scale");
    }
    if (complete) {
        s.result.mapping.snapshot_scale_site = static_cast<uint32_t>(hits[0]);
    }
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
    FeatureScan& feature = s.Begin("Resolution");
    const auto source = std::find_if(templates.begin(), templates.end(),
                                     [](const GameBuild& build) { return !build.resolution_sites.empty(); });
    if (source == templates.end()) {
        s.Line("  no supported build to take the instruction shapes from");
        s.Unresolved("Resolution sites");
        feature.components.push_back(Component("render size instructions", 1, 0));
        return;
    }
    const std::span<const OslResolutionSite> expected_sites = source->resolution_sites;
    ScanComponent sites_component{"render size instructions", expected_sites.size(), 0, 0};
    std::vector<uint64_t> mapped;
    bool complete = true;
    for (const SiteGroup& group : GroupsOf(expected_sites)) {
        std::vector<uint64_t> hits = s.InEboot(PatternOf(group.bytes.data(), group.bytes.size()));
        s.Line(Fmt("  %-34s %s %s", group.label.c_str(), Verdict(hits.size(), group.expected).c_str(), List(hits).c_str()));
        const ScanComponent part = Component({}, group.expected, hits.size());
        sites_component.unique += part.unique * group.starts.size();
        sites_component.ambiguous += part.ambiguous * group.starts.size();
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
    feature.components.push_back(sites_component);
    std::sort(mapped.begin(), mapped.end());
    s.Line(Fmt("  sites: %zu/%zu", mapped.size(), expected_sites.size()));
    if (complete) {
        s.Line("    " + List(mapped, expected_sites.size()));
    }
    const bool ordered = std::is_sorted(expected_sites.begin(), expected_sites.end(),
                                        [](const OslResolutionSite& a, const OslResolutionSite& b) { return a.offset < b.offset; });
    if (complete && (!ordered || mapped.size() != expected_sites.size())) {
        s.Broken("the sites do not follow the template's order");
    }
    if (complete && ordered && mapped.size() == expected_sites.size()) {
        s.result.mapping.resolution_offsets.assign(mapped.begin(), mapped.end());
    }
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

constexpr size_t kMaxLzcntListed = 64;
constexpr uint64_t kLzcntContext = 8;

std::string HexBytes(const Bytes& image, uint64_t from, uint64_t to) {
    std::string text;
    for (uint64_t at = from; at < to && at < image.size(); ++at) {
        text += (at == from ? "" : " ") + Fmt("%02x", image[at]);
    }
    return text;
}

std::string HexBytes(const uint8_t* bytes, size_t count) {
    std::string text;
    for (size_t i = 0; i < count; ++i) {
        text += (i ? " " : "") + Fmt("%02x", bytes[i]);
    }
    return text;
}

// The instructions that no placed shape covers, or every one, one line each with the bytes needed to rebuild a shape.
void ListLzcnt(Scan& s, const std::vector<uint64_t>& starts, const std::vector<std::vector<uint64_t>>& candidates,
               const std::array<uint64_t, LZCNT_RANGE_COUNT>& placed_at, size_t placed, bool only_unplaced) {
    const Bytes& image = s.image();
    size_t listed = 0, skipped = 0;
    for (uint64_t start : starts) {
        std::string shape;
        for (size_t i = 0; i < placed && shape.empty(); ++i) {
            if (start >= placed_at[i] && start < placed_at[i] + lzcnt_shapes[i].length) {
                shape = Fmt("placed in shape %zu", i);
            }
        }
        if (shape.empty()) {
            for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i) {
                if (std::find(candidates[i].begin(), candidates[i].end(), start) != candidates[i].end()) {
                    shape += (shape.empty() ? "matches shape " : ",") + Fmt("%zu", i);
                }
            }
            shape = shape.empty() ? "matches no shape" : shape + " out of order";
        } else if (only_unplaced) {
            continue;
        }
        if (listed == kMaxLzcntListed) {
            ++skipped;
            continue;
        }
        ++listed;
        const size_t available = static_cast<size_t>(std::min<uint64_t>(image.size() - start, 16));
        LzcntOperands operands;
        const bool supported = lzcnt_compat_decode(image.data() + start, available, &operands);
        const uint64_t end = start + (supported ? operands.length : std::min<size_t>(available, 8));
        s.Line("    " + Hex(start) + ": " + HexBytes(image, start, end) + " | " +
               (supported ? Fmt("%u-bit, destination r%u, %s source", operands.bits, operands.dst,
                                operands.memory ? "memory" : "register")
                          : std::string("form not supported by the rewrite")) +
               " | " + shape);
        if (shape.starts_with("placed")) {
            continue;
        }
        s.Line("      context: " + HexBytes(image, start > kLzcntContext ? start - kLzcntContext : 0, start) + " [" +
               HexBytes(image, start, end) + "] " + HexBytes(image, end, end + kLzcntContext));
    }
    if (skipped) {
        s.Line(Fmt("    (+%zu more not listed)", skipped));
    }
}

void ScanLzcnt(Scan& s) {
    FeatureScan& feature = s.Begin("Legacy CPU (LZCNT)");
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
    size_t failed = LZCNT_RANGE_COUNT, placed = 0;
    for (size_t i = 0; i < LZCNT_RANGE_COUNT && failed == LZCNT_RANGE_COUNT; ++i) {
        const auto pick = std::find_if(candidates[i].begin(), candidates[i].end(), [&](uint64_t p) { return p >= previous; });
        if (pick == candidates[i].end()) {
            failed = i;
        } else {
            early[i] = *pick;
            previous = *pick + lzcnt_shapes[i].length;
            placed = i + 1;
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
        feature.components.push_back({"ranges", LZCNT_RANGE_COUNT, placed, 0});
        s.Line(Fmt("  ranges: shape %zu has no candidate in order", failed));
        const LzcntShape& expected = lzcnt_shapes[failed];
        s.Line(Fmt("  ordered placement: %zu of %d shapes placed, stopped at shape %zu expecting %s at or after %s",
                   placed, LZCNT_RANGE_COUNT, failed, HexBytes(expected.bytes, expected.length).c_str(),
                   Hex(previous).c_str()));
        if (placed) {
            s.Line("    placed: " + List(std::vector<uint64_t>(early.begin(), early.begin() + placed), LZCNT_RANGE_COUNT));
        }
        if (image.size() <= s.eboot_size) {
            s.Line(Fmt("  bundled modules: not in the scanned image (eboot only, %s); LZCNT sites inside them cannot be seen",
                       Hex(s.eboot_size).c_str()));
        } else {
            s.Line(Fmt("  bundled modules: linked after the eboot (%s of %s)", Hex(image.size() - s.eboot_size).c_str(),
                       Hex(image.size()).c_str()));
        }
        size_t outside = 0;
        for (uint64_t start : starts) {
            bool inside = false;
            for (size_t i = 0; i < placed && !inside; ++i) {
                inside = start >= early[i] && start < early[i] + lzcnt_shapes[i].length;
            }
            outside += inside ? 0 : 1;
        }
        s.Line(Fmt("  detected instructions outside the placed shapes: %zu of %zu", outside, starts.size()));
        s.Line("  detected instructions:");
        ListLzcnt(s, starts, candidates, early, placed, false);
        s.Unresolved("LZCNT ranges");
        return;
    }
    const bool unique = early == late;
    size_t moved = 0;
    for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i) {
        moved += early[i] != late[i] ? 1 : 0;
    }
    feature.components.push_back({"ranges", LZCNT_RANGE_COUNT, LZCNT_RANGE_COUNT - moved, moved});
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
        s.Broken(Fmt("%zu LZCNT instruction(s) outside the shapes", uncovered.size()));
        ListLzcnt(s, starts, candidates, early, LZCNT_RANGE_COUNT, true);
    }
    if (unique && uncovered.empty()) {
        const uint64_t stubs_at = (image.size() + kStubPage - 1) / kStubPage * kStubPage;
        const size_t stub_bytes = lzcnt_compat_stub_bytes();
        Bytes scratch(stubs_at + stub_bytes, 0);
        std::copy(image.begin(), image.end(), scratch.begin());
        std::array<uint32_t, LZCNT_RANGE_COUNT> offsets{};
        std::copy(early.begin(), early.end(), offsets.begin());
        const LzcntCompatStatus status = lzcnt_compat_apply(scratch.data(), image.size(), offsets.data(), lzcnt_shapes,
                                                            LZCNT_RANGE_COUNT, scratch.data() + stubs_at, stub_bytes);
        s.Line(Fmt("  rewrite check on a scratch copy: %s", lzcnt_compat_status_text(status)));
        feature.validator = status == LZCNT_COMPAT_OK ? "accepted" : std::string("refused (") + lzcnt_compat_status_text(status) + ")";
        if (status != LZCNT_COMPAT_OK) {
            s.Unresolved("LZCNT rewrite check");
        } else {
            s.result.mapping.lzcnt_offsets.assign(early.begin(), early.end());
        }
    }
}

std::vector<OslResolutionSite> ResolutionSitesFor(std::span<const GameBuild> templates,
                                                  const std::vector<uint32_t>& offsets) {
    const auto source = std::find_if(templates.begin(), templates.end(),
                                     [](const GameBuild& build) { return !build.resolution_sites.empty(); });
    std::vector<OslResolutionSite> sites;
    if (source == templates.end() || source->resolution_sites.size() != offsets.size()) {
        return sites;
    }
    sites.assign(source->resolution_sites.begin(), source->resolution_sites.end());
    for (size_t i = 0; i < sites.size(); ++i) {
        sites[i].offset = offsets[i];
    }
    return sites;
}

FeatureScan* FeatureNamed(ScanResult& result, const char* name) {
    for (FeatureScan& feature : result.features) {
        if (feature.name == name) {
            return &feature;
        }
    }
    return nullptr;
}

// The loader's patches in its order, applied to a scratch copy: a feature whose patch refuses its sites is dropped.
void ValidateMapping(Scan& s, std::span<const GameBuild> templates) {
    ScannedMapping& mapping = s.result.mapping;
    const uint64_t size = s.image().size();
    Bytes scratch(size + OSL_BOOKSHELF_STUB_BYTES, 0);
    std::copy(s.image().begin(), s.image().end(), scratch.begin());
    char error[320] = "";
    const auto verdict = [&](const char* feature, bool accepted) {
        FeatureScan* scan = FeatureNamed(s.result, feature);
        if (scan) {
            scan->validator = accepted ? "accepted" : std::string("refused (") + error + ")";
        }
        if (!accepted) {
            s.Unresolved(std::string(feature) + " patch check");
        }
        return accepted;
    };
    const OslResolution target = osl_render_target(&osl_resolutions[OSL_RESOLUTION_COUNT - 1], &osl_aspect_ratios[0]);
    if (mapping.snapshot_scale_site &&
        !verdict("Snapshot scale", osl_apply_snapshot_scale(scratch.data(), size, mapping.snapshot_scale_site, &target,
                                                            error, sizeof(error)))) {
        mapping.snapshot_scale_site = 0;
    }
    if (!mapping.resolution_offsets.empty()) {
        const std::vector<OslResolutionSite> sites = ResolutionSitesFor(templates, mapping.resolution_offsets);
        if (sites.empty()) {
            std::snprintf(error, sizeof(error), "no template for %zu sites", mapping.resolution_offsets.size());
        }
        if (!verdict("Resolution", !sites.empty() && osl_apply_resolution(scratch.data(), size, sites.data(), sites.size(),
                                                                          &target, error, sizeof(error)))) {
            mapping.resolution_offsets.clear();
        }
    }
    if (mapping.skip_intro) {
        uint64_t code_end = 0;
        for (const MappedSegment& seg : s.mapped.segments) {
            const uint64_t end = seg.vaddr + ((seg.memsz + 0xfff) & ~uint64_t{0xfff});
            if ((seg.flags & 1) && mapping.skip_intro->stubs >= seg.vaddr && mapping.skip_intro->stubs < end &&
                end <= size) {
                code_end = end;
            }
        }
        if (!code_end) {
            std::snprintf(error, sizeof(error), "the stub space is not inside a code segment");
        }
        if (!verdict("Skip intro",
                     code_end && osl_apply_skip_intro(scratch.data(), code_end, &*mapping.skip_intro, 1, error, sizeof(error)))) {
            mapping.skip_intro.reset();
        }
    }
    if (mapping.bookshelf &&
        !verdict("Unlock Bookshelf", osl_apply_bookshelf(scratch.data(), size, &*mapping.bookshelf, scratch.data() + size,
                                                         OSL_BOOKSHELF_STUB_BYTES, error, sizeof(error)))) {
        mapping.bookshelf.reset();
    }
}

FeatureState CheatFeatureState(OslCheatState state) {
    switch (state) {
    case OSL_CHEAT_COMPLETE: return FeatureState::kComplete;
    case OSL_CHEAT_PARTIAL: return FeatureState::kPartial;
    case OSL_CHEAT_MISSING: return FeatureState::kMissing;
    case OSL_CHEAT_AMBIGUOUS: return FeatureState::kAmbiguous;
    case OSL_CHEAT_INVALID: return FeatureState::kInvalid;
    case OSL_CHEAT_INHERENT: return FeatureState::kInherent;
    }
    return FeatureState::kInvalid;
}

std::string CheatSiteLine(const Scan& s, const OslCheatSite& site, const OslCheatSiteScan& found) {
    const std::string head = Fmt("    %s %s: ", site.id, site.name);
    const std::string original = HexBytes(site.signature + site.patch_at, site.patch_size);
    switch (found.state) {
    case OSL_CHEAT_SITE_MISSING: return head + "not found";
    case OSL_CHEAT_SITE_AMBIGUOUS: {
        std::vector<uint64_t> at;
        for (size_t i = 0; i < found.hits && i < OSL_CHEAT_MAX_HITS; ++i) {
            at.push_back(found.at[i] + site.patch_at);
        }
        return head + Fmt("ambiguous (%zu matches) at ", found.hits) + List(at);
    }
    case OSL_CHEAT_SITE_UNIQUE:
        if (site.kind == OSL_CHEAT_SITE_PLAYER_ACCESSOR) {
            return head + "unique at " + Hex(found.at[0]) + ", calls the player accessor (global " + Hex(found.global) + ")";
        }
        if (site.kind == OSL_CHEAT_SITE_SAME_FUNCTION) {
            return head + "unique at " + Hex(found.at[0]);
        }
        if (site.kind == OSL_CHEAT_SITE_GLOBAL) {
            return head + "unique at " + Hex(found.patch) + ", root object global " + Hex(found.global);
        }
        return head + "unique at " + Hex(found.patch) + ", original bytes " + original + " valid";
    case OSL_CHEAT_SITE_PATCHED:
    case OSL_CHEAT_SITE_MODIFIED:
        if (site.kind == OSL_CHEAT_SITE_PLAYER_ACCESSOR) {
            return head + "unique at " + Hex(found.at[0]) + ", but the call does not reach the player accessor";
        }
        if (site.kind == OSL_CHEAT_SITE_GLOBAL) {
            return head + "unique at " + Hex(found.patch) + ", but the lea does not give a global in the image";
        }
        return head + "unique at " + Hex(found.patch) + ", original bytes " + original + " invalid: found " +
               HexBytes(s.image(), found.patch, found.patch + site.patch_size) +
               (found.state == OSL_CHEAT_SITE_PATCHED ? " (already changed)" : "");
    }
    return head;
}

// One cheat's or the Experience Multiplier's sites, how they relate, and whether the runtime hooked them.
FeatureScan ReportSites(Scan& s, const char* label, const OslCheatSite* sites, size_t site_count, const OslCheatScan& scan,
                        bool applied, std::string& text) {
    FeatureScan feature{label, {}, {}, {}, CheatFeatureState(scan.state)};
    text += std::string("  ") + label + ":\n";
    if (scan.state == OSL_CHEAT_INHERENT) {
        text += std::string("    nothing to change: ") + scan.reason + "\n    status: " + FeatureStateName(feature.state) + "\n";
        return feature;
    }
    size_t once = 0;
    for (size_t i = 0; i < site_count; ++i) {
        const OslCheatSite& site = sites[i];
        const OslCheatSiteScan& found = scan.sites[i];
        feature.components.push_back(Component(Fmt("%s %s", site.id, site.name), 1, found.hits));
        text += CheatSiteLine(s, site, found) + "\n";
        once += found.hits == 1;
        if (found.hits == 0) {
            std::vector<int16_t> pattern(site.signature, site.signature + site.signature_size);
            std::fill_n(pattern.begin() + static_cast<std::ptrdiff_t>(site.patch_at), site.patch_size, int16_t{-1});
            for (size_t k = 0; site.mask && k < site.signature_size; ++k) {
                if (site.mask[k] != 'x') {
                    pattern[k] = -1;
                }
            }
            s.blocks.emplace_back();
            PartialRuns(s, pattern, static_cast<int64_t>(site.patch_at));
            text += s.blocks.back();
            s.blocks.pop_back();
        }
    }
    const std::string relationship =
        once != site_count ? "not checked (a site is not found exactly once)"
        : scan.related ? Fmt("accepted (%zu distinct site%s, apart from every other cheat's)", site_count,
                             site_count == 1 ? "" : "s")
                       : "rejected (" + std::string(scan.reason) + ")";
    text += "    relationship: " + relationship + "\n";
    if (scan.state == OSL_CHEAT_INVALID) {
        feature.invariant = scan.reason;
    }
    if (scan.state == OSL_CHEAT_COMPLETE) {
        feature.validator = applied ? "accepted" : "refused (" + std::string(scan.reason) + ")";
        if (!applied) {
            feature.state = FeatureState::kInvalid;
        }
    }
    text += std::string("    status: ") + FeatureStateName(feature.state) + "\n";
    return feature;
}

// The cheats' and the Experience Multiplier's sites with the runtime's own locator, and the runtime's hook installation
// on a scratch copy, in the loader's order, as the validator.
std::string ScanCheats(Scan& s) {
    std::vector<OslCodeRange> ranges;
    for (const Range& r : s.eboot_exec) {
        ranges.push_back({r.begin, r.end});
    }
    OslCheatScan scans[OSL_ENGINE_COUNT][OSL_CHEAT_COUNT];
    OslCheatScan experience[OSL_ENGINE_COUNT];
    unsigned hooked[OSL_ENGINE_COUNT];
    const size_t image_size = s.image().size();
    const size_t stubs_size = size_t{OSL_CHEAT_STUBS_BYTES} + size_t{OSL_EXPERIENCE_STUBS_BYTES};
    Bytes scratch(image_size + stubs_size + OSL_CHEAT_COUNT + 1, 0);
    std::copy(s.image().begin(), s.image().end(), scratch.begin());
    unsigned char* stubs = scratch.data() + image_size;
    unsigned char* flags = stubs + stubs_size;
    osl_install_cheats_engines(scratch.data(), image_size, ranges.data(), ranges.size(), stubs, OSL_CHEAT_STUBS_BYTES, flags,
                               scans[OSL_ENGINE_REFINED], scans[OSL_ENGINE_CLASSIC], hooked);
    const unsigned experience_hooked = osl_install_experience_engines(
        scratch.data(), image_size, ranges.data(), ranges.size(), stubs + OSL_CHEAT_STUBS_BYTES, OSL_EXPERIENCE_STUBS_BYTES,
        flags + OSL_CHEAT_COUNT, &experience[OSL_ENGINE_REFINED], &experience[OSL_ENGINE_CLASSIC]);
    std::string text = "Cheats (identified only; applied only for a supported build whose sites validate):\n";
    for (int e = 0; e < OSL_ENGINE_COUNT; ++e) {
        const OslEngine engine = static_cast<OslEngine>(e);
        std::vector<FeatureScan>& cheats = e == OSL_ENGINE_CLASSIC ? s.result.classic_cheats : s.result.cheats;
        if (e == OSL_ENGINE_CLASSIC) {
            text += "Cheats, Classic engine:\n";
        }
        for (int c = 0; c < OSL_CHEAT_COUNT; ++c) {
            const OslCheatInfo& info = osl_cheats[c];
            size_t site_count = 0;
            const OslCheatSite* sites = osl_cheat_engine_sites(&info, engine, &site_count);
            cheats.push_back(ReportSites(s, info.label, sites, site_count, scans[e][c],
                                         (hooked[e] & osl_cheat_bit(info.cheat)) != 0, text));
        }
        const bool classic = e == OSL_ENGINE_CLASSIC;
        FeatureScan report = ReportSites(s, osl_experience_multiplier.label,
                                         classic ? osl_experience_multiplier.classic_sites : osl_experience_multiplier.sites,
                                         classic ? osl_experience_multiplier.classic_site_count
                                                 : osl_experience_multiplier.site_count,
                                         experience[e], (experience_hooked & (1u << e)) != 0, text);
        (classic ? s.result.classic_experience : s.result.experience) = std::move(report);
        text += "  " + (classic ? ClassicCheatSummary(s.result) : CheatSummary(s.result)) + "\n";
        text += "  " + (classic ? ClassicExperienceSummary(s.result) : ExperienceSummary(s.result)) + "\n";
    }
    return text;
}

FeatureState StateOf(const FeatureScan& feature) {
    bool all_unique = !feature.components.empty(), any_found = false, any_short = false, any_over = false;
    for (const ScanComponent& c : feature.components) {
        all_unique = all_unique && c.unique == c.expected;
        any_found = any_found || c.unique || c.ambiguous;
        any_short = any_short || c.missing();
        any_over = any_over || c.ambiguous;
    }
    if (all_unique) {
        const bool refused = !feature.validator.empty() && feature.validator != "accepted";
        return !feature.invariant.empty() || refused || feature.validator.empty() ? FeatureState::kInvalid
                                                                                  : FeatureState::kComplete;
    }
    if (!any_found) {
        return FeatureState::kMissing;
    }
    return any_over && !any_short ? FeatureState::kAmbiguous : FeatureState::kPartial;
}

// The scan's text: each feature's findings, its validator and its state.
std::string Compose(const Scan& s) {
    std::string text;
    for (size_t i = 0; i < s.blocks.size() && i < s.result.features.size(); ++i) {
        const FeatureScan& feature = s.result.features[i];
        text += s.blocks[i];
        if (!feature.invariant.empty()) {
            text += "  scan invariant failed: " + feature.invariant + "\n";
        }
        text += "  runtime validator: " + (feature.validator.empty() ? std::string("not run") : feature.validator) + "\n";
        text += std::string("  status: ") + FeatureStateName(feature.state) + "\n";
    }
    return text;
}

} // namespace

const char* FeatureStateName(FeatureState state) {
    switch (state) {
    case FeatureState::kComplete: return "COMPLETE";
    case FeatureState::kPartial: return "PARTIAL";
    case FeatureState::kMissing: return "MISSING";
    case FeatureState::kAmbiguous: return "AMBIGUOUS";
    case FeatureState::kInvalid: return "INVALID";
    case FeatureState::kInherent: return "INHERENTLY SATISFIED";
    }
    return "?";
}

ScanResult ScanExecutable(const MappedExecutable& image, uint64_t eboot_size, std::span<const GameBuild> templates) {
    ScanResult result;
    Scan scan{image, eboot_size, ExecutableRanges(image, eboot_size), result, {}, 0};
    ScanSkipIntro(scan);
    ScanBookshelf(scan);
    ScanSnapshotScale(scan);
    ScanResolution(scan, templates);
    ScanLzcnt(scan);
    ValidateMapping(scan, templates);
    for (FeatureScan& feature : result.features) {
        feature.state = StateOf(feature);
    }
    result.text = Compose(scan);
    result.text += ScanCheats(scan);
    return result;
}

std::string CheatSummary(const ScanResult& scan) {
    if (scan.cheats.empty()) {
        return "Cheats: not scanned (the executable did not map)";
    }
    size_t complete = 0;
    std::string others;
    for (const FeatureScan& cheat : scan.cheats) {
        if (cheat.state == FeatureState::kComplete) {
            ++complete;
        } else {
            others += ", " + cheat.name + " " + FeatureStateName(cheat.state);
        }
    }
    return "Cheats: " + std::to_string(complete) + "/" + std::to_string(scan.cheats.size()) + " COMPLETE" + others;
}

std::string ExperienceSummary(const ScanResult& scan) {
    if (!scan.experience) {
        return "Experience Multiplier: not scanned (the executable did not map)";
    }
    return std::string("Experience Multiplier: ") + FeatureStateName(scan.experience->state);
}

std::string ClassicCheatSummary(const ScanResult& scan) {
    if (scan.classic_cheats.empty()) {
        return "Classic cheats: not scanned (the executable did not map)";
    }
    size_t complete = 0, inherent = 0;
    std::string others;
    for (const FeatureScan& cheat : scan.classic_cheats) {
        if (cheat.state == FeatureState::kComplete) {
            ++complete;
        } else if (cheat.state == FeatureState::kInherent) {
            ++inherent;
        } else {
            others += ", " + cheat.name + " " + FeatureStateName(cheat.state);
        }
    }
    return "Classic cheats: " + std::to_string(complete) + "/" + std::to_string(scan.classic_cheats.size()) + " COMPLETE, " +
           std::to_string(inherent) + " INHERENTLY SATISFIED" + others;
}

std::string ClassicExperienceSummary(const ScanResult& scan) {
    if (!scan.classic_experience) {
        return "Classic Experience Multiplier: not scanned (the executable did not map)";
    }
    return std::string("Classic Experience Multiplier: ") + FeatureStateName(scan.classic_experience->state);
}

std::vector<OslResolutionSite> ResolutionSitesAt(std::span<const GameBuild> templates,
                                                 const std::vector<uint32_t>& offsets) {
    return ResolutionSitesFor(templates, offsets);
}

LinkedImage LinkExecutable(std::span<const uint8_t> eboot, std::span<const std::vector<uint8_t>> modules) {
    LinkedImage linked;
    linked.eboot_error = MapExecutableBytes(eboot, linked.image);
    linked.eboot_size = linked.eboot_error.empty() ? linked.image.image.size() : 0;
    for (size_t i = 0; i < std::size(kBundledModules); ++i) {
        LinkedModule module{kBundledModules[i], 0, 0, {}};
        MappedExecutable mapped;
        if (i >= modules.size() || modules[i].empty()) {
            module.error = "missing or unreadable";
        } else if (!linked.eboot_error.empty()) {
            module.error = "the executable did not map";
        } else if (std::string error = MapExecutableBytes(modules[i], mapped); !error.empty()) {
            module.error = error;
        } else {
            for (const MappedSegment& s : mapped.segments) {
                module.size = std::max(module.size, s.vaddr + s.memsz);
            }
            module.error = LinkModule(linked.image, mapped, &module.base);
        }
        linked.modules.push_back(std::move(module));
    }
    return linked;
}

ScanResult ScanGameFolder(const std::filesystem::path& app0) {
    const auto read = [](const std::filesystem::path& path) {
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        std::vector<uint8_t> bytes;
        if (!ec && size <= kMaxFile) {
            std::ifstream in(path, std::ios::binary);
            bytes.resize(static_cast<size_t>(size));
            if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
                bytes.clear();
            }
        }
        return bytes;
    };
    std::vector<std::vector<uint8_t>> modules;
    for (const char* name : kBundledModules) {
        modules.push_back(read(app0 / "sce_module" / name));
    }
    const LinkedImage linked = LinkExecutable(read(app0 / "eboot.bin"), modules);
    if (!linked.eboot_error.empty()) {
        ScanResult scan;
        scan.unresolved.push_back("executable");
        return scan;
    }
    return ScanExecutable(linked.image, linked.eboot_size);
}

std::vector<std::string> UnresolvedFeatures(const ScannedMapping& mapping) {
    std::vector<std::string> missing;
    if (mapping.resolution_offsets.empty() || !mapping.snapshot_scale_site) {
        missing.push_back("Render resolution");
    }
    if (!mapping.skip_intro) {
        missing.push_back("Skip intro");
    }
    if (!mapping.bookshelf) {
        missing.push_back("Unlock Bookshelf");
    }
    if (mapping.lzcnt_offsets.empty()) {
        missing.push_back("Legacy CPU (LZCNT)");
    }
    return missing;
}

} // namespace osl
