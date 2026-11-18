// The Trophies page's Portuguese (Brazil) text (localization/pt-BR/trophies.tsv). Display only: the trophy pack is
// never changed, and a row is used only while the pack's English still hashes to the row's source_sha256.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "trophies/trophy_data.h"
#include "trophies/trophy_view.h"

namespace osl::trophies {

struct TrophyTextRow {
    int id = 0;
    std::string source_sha256;
    std::string name;
    std::string detail;
};

struct TrophyTextTable {
    std::vector<TrophyTextRow> rows;
    std::vector<std::string> errors;

    const TrophyTextRow* Find(int id) const;
};

inline constexpr char kTrophyTextHeader[] = "id\tsource_sha256\tname_pt\tdetail_pt";

// Rows are `id<TAB>source_sha256<TAB>name_pt<TAB>detail_pt` after the header line.
TrophyTextTable ParseTrophyTextTable(std::string_view tsv);

// SHA-256 (lowercase hex) of the UTF-8 name + "\n" + detail.
std::string TrophySourceSha256(std::string_view name, std::string_view detail);

const TrophyTextTable& PortugueseBrazilTrophyText();

// `view` with each row's name and detail from `table` where the set's text of that trophy matches the row's hash;
// hidden rows show the Portuguese hidden-trophy text.
TrophyView TranslateTrophyView(const TrophyView& view, const TrophySet& set, const TrophyTextTable& table);

} // namespace osl::trophies
