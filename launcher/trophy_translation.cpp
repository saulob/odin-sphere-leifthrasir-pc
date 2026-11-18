#include "trophy_translation.h"

#include <charconv>
#include <cstdint>
#include <set>
#include <span>

#include "embedded_file.h"
#include "game_files.h"

namespace osl {
extern const EmbeddedFile kTrophyTextFiles[];
} // namespace osl

namespace osl::trophies {
namespace {

constexpr char kHiddenNamePt[] = "Troféu Oculto";
constexpr char kHiddenDetailPt[] = "Desbloqueie este troféu para ver o que ele é.";

std::vector<std::string_view> SplitTabs(std::string_view line) {
    std::vector<std::string_view> fields;
    size_t start = 0;
    for (size_t tab = line.find('\t'); tab != std::string_view::npos; tab = line.find('\t', start)) {
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    fields.push_back(line.substr(start));
    return fields;
}

bool IsHash(std::string_view text) {
    if (text.size() != 64) return false;
    for (const char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

} // namespace

const TrophyTextRow* TrophyTextTable::Find(int id) const {
    for (const TrophyTextRow& row : rows) {
        if (row.id == id) return &row;
    }
    return nullptr;
}

TrophyTextTable ParseTrophyTextTable(std::string_view tsv) {
    TrophyTextTable table;
    std::set<int> ids;
    size_t line_number = 0;
    bool header = false;
    while (!tsv.empty()) {
        const size_t end = tsv.find('\n');
        std::string_view line = tsv.substr(0, end);
        tsv = end == std::string_view::npos ? std::string_view() : tsv.substr(end + 1);
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;
        const std::string where = "line " + std::to_string(line_number) + ": ";
        if (!header) {
            header = true;
            if (line != kTrophyTextHeader) table.errors.push_back(where + "header is not " + kTrophyTextHeader);
            continue;
        }
        const std::vector<std::string_view> fields = SplitTabs(line);
        if (fields.size() != 4) {
            table.errors.push_back(where + "expected 4 columns");
            continue;
        }
        TrophyTextRow row;
        const auto [ptr, ec] = std::from_chars(fields[0].data(), fields[0].data() + fields[0].size(), row.id);
        if (ec != std::errc() || ptr != fields[0].data() + fields[0].size() || row.id < 0 || row.id >= kMaxTrophies) {
            table.errors.push_back(where + "bad id");
            continue;
        }
        if (!IsHash(fields[1])) {
            table.errors.push_back(where + "bad source_sha256");
            continue;
        }
        if (fields[2].empty() || fields[3].empty()) {
            table.errors.push_back(where + "empty text");
            continue;
        }
        if (!ids.insert(row.id).second) {
            table.errors.push_back(where + "duplicate id");
            continue;
        }
        row.source_sha256 = fields[1];
        row.name = fields[2];
        row.detail = fields[3];
        table.rows.push_back(std::move(row));
    }
    if (!header) table.errors.push_back("no header");
    return table;
}

std::string TrophySourceSha256(std::string_view name, std::string_view detail) {
    std::string source;
    source.reserve(name.size() + 1 + detail.size());
    source.append(name).append("\n").append(detail);
    return Sha256Hex(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(source.data()), source.size()));
}

const TrophyTextTable& PortugueseBrazilTrophyText() {
    static const TrophyTextTable table = [] {
        for (const EmbeddedFile* file = kTrophyTextFiles; file->name; ++file) {
            if (std::string_view(file->name) == "trophies.tsv") {
                return ParseTrophyTextTable(std::string_view(reinterpret_cast<const char*>(file->data), file->size));
            }
        }
        return TrophyTextTable{};
    }();
    return table;
}

TrophyView TranslateTrophyView(const TrophyView& view, const TrophySet& set, const TrophyTextTable& table) {
    TrophyView translated = view;
    for (TrophyRow& row : translated.rows) {
        if (row.masked) {
            row.name = kHiddenNamePt;
            row.detail = kHiddenDetailPt;
            continue;
        }
        const Trophy* trophy = set.Find(row.id);
        const TrophyTextRow* text = table.Find(row.id);
        if (trophy && text && TrophySourceSha256(trophy->name, trophy->detail) == text->source_sha256) {
            row.name = text->name;
            row.detail = text->detail;
        }
    }
    return translated;
}

} // namespace osl::trophies
