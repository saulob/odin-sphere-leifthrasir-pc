// Synthetic game data shared by the test executables: param.sfo and CPK writers, DCPX patches, and executable images
// with every scanned feature planted at known offsets.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "boot_image.h"
#include "build_scan.h"
#include "cpk_patch.h"
#include "game_profile.h"
#include "lzcnt_compat.h"
#include "osl_bookshelf.h"
#include "osl_resolution.h"
#include "osl_skip_intro.h"

namespace osl_test {

using namespace osl;

inline std::filesystem::path TempDir(const char* name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "osl-tests" / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

inline std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

inline void WriteSfo(const std::filesystem::path& path, const std::vector<std::pair<std::string, std::string>>& values) {
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

namespace cpk_test {

namespace fs = std::filesystem;

inline void Be16(std::string& o, uint32_t v) { o += char(v >> 8); o += char(v); }
inline void Be32(std::string& o, uint32_t v) { for (int i = 3; i >= 0; --i) o += char(v >> (8 * i)); }
inline void Be64(std::string& o, uint64_t v) { for (int i = 7; i >= 0; --i) o += char(v >> (8 * i)); }
inline void Le32(std::string& o, uint32_t v) { for (int i = 0; i < 4; ++i) o += char(v >> (8 * i)); }
inline void Le64(std::string& o, uint64_t v) { for (int i = 0; i < 8; ++i) o += char(v >> (8 * i)); }

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

inline Cell N(uint64_t n) { return {n, {}}; }
inline Cell S(std::string s) { return {0, std::move(s)}; }

// A @UTF table packet, as scripts/prompts/cpk.py's self-test builds it.
inline std::string Utf(const std::string& name, const std::vector<Column>& columns, const std::vector<Row>& rows) {
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
inline std::string Crypt(std::string packet) {
    uint32_t m = 0x5F;
    for (char& b : packet) {
        b = char(uint8_t(b) ^ uint8_t(m));
        m = (m * 0x15) & 0xFF;
    }
    return packet;
}

inline std::string Section(const char* magic, const std::string& packet) {
    std::string out(magic, 4);
    Le32(out, 0xFF);
    Le64(out, packet.size());
    return out + packet;
}

// A CRILAYLA block storing `payload` with literal bytes only: the stream is read backwards and writes the output
// backwards, and the file's first 0x100 bytes are stored raw after it.
inline std::string CrilaylaLiterals(const std::string& payload) {
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
inline std::string BuildCpk(const std::vector<Entry>& entries, bool encrypt_toc, bool shared_dir = false) {
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

inline std::span<const uint8_t> Span(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }
inline std::string Bytes(const std::vector<uint8_t>& v) { return std::string(v.begin(), v.end()); }

// A DCPX patch turning each `before` into its `after` (same size), as scripts/prompts/make_dcpx.py writes it; with
// `source_blocks` false, a DCPX1 patch whose runs hold only the replacement.
inline std::string MakePatch(const std::vector<std::pair<std::string, std::pair<std::string, std::string>>>& targets,
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

inline void WriteAll(const fs::path& path, const std::string& data) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << data;
}

inline std::string Pattern(size_t size, unsigned seed) {
    std::string out(size, '\0');
    for (size_t i = 0; i < size; ++i) out[i] = char((i * seed + i / 64 + seed) & 255);
    return out;
}

inline std::string Painted(std::string data, std::initializer_list<size_t> offsets, char ink) {
    for (const size_t off : offsets) data.replace(off, 16, 16, ink);
    return data;
}

} // namespace cpk_test

inline std::vector<int16_t> Shorts(const short* bytes, size_t count) {
    return std::vector<int16_t>(bytes, bytes + count);
}

inline void PlantPattern(MappedExecutable& image, size_t at, const std::vector<int16_t>& pattern) {
    for (size_t i = 0; i < pattern.size(); ++i) {
        image.image[at + i] = pattern[i] < 0 ? 0 : static_cast<uint8_t>(pattern[i]);
    }
}

inline void PlantRelative(MappedExecutable& image, size_t at, size_t target, size_t instruction_end) {
    const int32_t relative = static_cast<int32_t>(static_cast<int64_t>(target) - static_cast<int64_t>(instruction_end));
    std::memcpy(&image.image[at], &relative, 4);
}

inline void PlantFloat(MappedExecutable& image, size_t at, float value) {
    std::memcpy(&image.image[at], &value, 4);
}

inline constexpr size_t kPlantedNotice = 0x100, kPlantedHd = 0x200, kPlantedClassic = 0x300, kPlantedSnapshot = 0x400;
inline constexpr size_t kPlantedGetter = 0x2000, kPlantedTotal = 0x2100, kPlantedReads = 0x2200, kPlantedMarks = 0x2600;
inline constexpr size_t kPlantedLzcnt = 0x3000, kPlantedData = 0x31000, kPlantedDecoy = 0x20000;

struct PlantedLayout {
    size_t notice = 0, hd = 0, classic = 0, classic_target = 0, snapshot = 0, data = 0, getter = 0, total = 0, decoy = 0;
    std::vector<size_t> reads, marks, lzcnt;
    bool in_place = false;
    std::vector<OslResolutionSite> resolution; // at the offsets they are planted at
};

inline void PlantFeatures(MappedExecutable& image, const PlantedLayout& l) {
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

inline PlantedLayout SyntheticLayout(size_t skew) {
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
inline MappedExecutable SyntheticOdinImage(size_t skew = 0) {
    MappedExecutable image;
    image.image.assign(0x40000, 0);
    image.segments = {{0, 0x2f800, 5}, {0x30000, 0x8000, 4}};
    image.entry = 0x1234;
    PlantFeatures(image, SyntheticLayout(skew));
    return image;
}

inline bool Contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

inline std::string HexText(uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
}

inline const GameBuild* BuildWithLabel(std::string_view label) {
    for (const GameBuild& build : SupportedBuilds()) {
        if (build.label == label) {
            return &build;
        }
    }
    return nullptr;
}

inline std::string SfoText(const std::filesystem::path& dir, const std::vector<std::pair<std::string, std::string>>& values) {
    WriteSfo(dir / "param.sfo", values);
    return ReadFile(dir / "param.sfo");
}

inline std::vector<std::pair<std::string, std::string>> UsaSfo(const char* category, const char* app_ver, const char* version) {
    return {{"TITLE", "Odin Sphere Leifthrasir"}, {"TITLE_ID", "CUSA05083"},
            {"CONTENT_ID", "UP0177-CUSA05083_00-ODINSPHERELE0000"}, {"CATEGORY", category},
            {"APP_VER", app_ver}, {"VERSION", version}};
}

// The issue #19 executable as its unsupported-build log describes it: segments, entry, linked size and every site at
// the offset the log names. The scan has to find exactly those sites again.
inline constexpr uint32_t kIssue19Resolution[] = {0x6a9bb7, 0x6aa6d8, 0x6aa6dd, 0x6aa809, 0x6aa80e, 0x6aa9c8,
                                           0x6aa9cd, 0x6aa9e5, 0x6aa9ea, 0x6aa9fe, 0x6aaa03, 0x6ab2f0,
                                           0x6ab2f5, 0x6acca2, 0x6acca7, 0x6b2392, 0x6b7f6d};
inline constexpr OslSkipIntroSites kIssue19SkipIntro{0x53d, 0x66aa9b, 0x1791dd, 0x146980, 0xa8ace0};
inline constexpr OslBookshelfSites kIssue19Bookshelf{
    0x5c1910,
    0x5759b7,
    {0x5e849e, 0x5e92f6, 0x5e9343, 0x5e9d9e, 0x5e9df6, 0x5e9ee6, 0x5e9f36, 0x5ea2c4, 0x5ea334, 0x5ea4e8, 0x5ea53c,
     0x5ea64c, 0x5ea796, 0x5eacd2},
    {0x5eab81, 0x5eae36},
    0};
inline constexpr uint32_t kIssue19Lzcnt[] = {
    0x682bd0, 0x682ca0, 0x68405c, 0x684073, 0x684118, 0x6841a9, 0x68423a, 0x684283, 0x6844c0, 0x6844cf,
    0x68458c, 0x684626, 0x6846be, 0x6846e8, 0x684830, 0xde361c, 0xde3649, 0xde36df, 0xde3854, 0xde3873,
    0xde3901, 0xde3af6, 0xde3b7f, 0xde458c, 0xde45aa, 0xde4c76, 0xde4c90, 0xde5268, 0xde5b23, 0xde5b7e,
    0xde6008, 0xde6048, 0xde6919, 0xde6937, 0xde7075, 0xde7090, 0xde7f62, 0xde7fdf, 0xde7fe4, 0xde801b,
    0xde8020, 0xde82ba, 0xde82bf, 0xde8352, 0xde8357, 0xde8398, 0xde839d, 0xde862a, 0xde862f};
inline constexpr uint32_t kIssue19Snapshot = 0x542f87, kIssue19NativeScale = 0x968b6c, kIssue19LayerScale = 0xd6c800;
inline constexpr uint64_t kIssue19EbootImage = 0xd9d4e0, kIssue19LinkedImage = 0x1004570;

inline MappedExecutable Issue19Image() {
    PlantedLayout l;
    l.notice = kIssue19SkipIntro.notice_scene - osl_skip_intro_notice_code.site;
    l.hd = kIssue19SkipIntro.hd_title_check - osl_skip_intro_hd_title_code.site;
    l.classic = kIssue19SkipIntro.classic_title_check - osl_skip_intro_classic_title_code.site;
    l.classic_target = kIssue19SkipIntro.classic_button;
    l.snapshot = kIssue19Snapshot;
    l.data = kIssue19NativeScale;
    l.getter = kIssue19Bookshelf.accessor;
    l.total = kIssue19Bookshelf.total;
    l.reads.assign(kIssue19Bookshelf.reads, kIssue19Bookshelf.reads + OSL_BOOKSHELF_READS);
    l.marks.assign(kIssue19Bookshelf.marks, kIssue19Bookshelf.marks + OSL_BOOKSHELF_MARKS);
    l.lzcnt.assign(std::begin(kIssue19Lzcnt), std::end(kIssue19Lzcnt));
    const std::span<const OslResolutionSite> shapes = SupportedBuilds()[0].resolution_sites;
    for (size_t i = 0; i < shapes.size() && i < std::size(kIssue19Resolution); ++i) {
        OslResolutionSite site = shapes[i];
        site.offset = kIssue19Resolution[i];
        l.resolution.push_back(site);
    }
    MappedExecutable image;
    image.image.assign(kIssue19LinkedImage, 0);
    image.segments = {{0, 0xa8acd8, 5}, {0xa8c000, 0x1f8000, 4}, {0xc84000, 0x1194e0, 6},
                      {0xda0000, kIssue19LinkedImage - 0xda0000, 5}};
    image.entry = 0x67d610;
    PlantFeatures(image, l);
    std::fill_n(image.image.begin() + l.data + 8, 8, uint8_t{0});
    PlantRelative(image, l.snapshot + OSL_SNAPSHOT_PAIR, kIssue19LayerScale, l.snapshot + OSL_SNAPSHOT_PAIR + 4);
    return image;
}
inline bool Unresolved(const ScanResult& scan, const std::string& what) {
    return std::find(scan.unresolved.begin(), scan.unresolved.end(), what) != scan.unresolved.end();
}

} // namespace osl_test
