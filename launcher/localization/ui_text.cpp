#include "localization/ui_text.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "game_profile.h"
#include "osl_ui_text.h"
#include "settings.h"
#include "utf8.h"

namespace osl {
namespace localization {

extern const char kPtBrUiTextTsv[];

namespace {

std::atomic<bool> g_portuguese_brazil{false};

bool DecodeUi(std::string_view escaped, std::string& out) {
    out.clear();
    out.reserve(escaped.size());
    for (size_t i = 0; i < escaped.size(); ++i) {
        if (escaped[i] != '\\') {
            out.push_back(escaped[i]);
            continue;
        }
        if (++i >= escaped.size()) {
            return false;
        }
        switch (escaped[i]) {
        case 'n': out.push_back('\n'); break;
        case 's': out.push_back(' '); break;
        case 't': out.push_back('\t'); break;
        case '\\': out.push_back('\\'); break;
        default: return false;
        }
    }
    return true;
}

} // namespace

void SetActiveLanguage(std::string_view key) { g_portuguese_brazil = key == kPortugueseBrazilKey; }

bool PortugueseBrazilSelected() { return g_portuguese_brazil; }

UiTextTable ParseUiTextTable(std::string_view tsv) {
    UiTextTable table;
    size_t start = 0;
    uint32_t number = 0;
    while (start < tsv.size()) {
        size_t end = tsv.find('\n', start);
        if (end == std::string_view::npos) {
            end = tsv.size();
        }
        std::string_view line = tsv.substr(start, end - start);
        start = end + 1;
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            continue;
        }
        const size_t tab = line.find('\t');
        UiTextRow row;
        if (tab == std::string_view::npos || line.find('\t', tab + 1) != std::string_view::npos ||
            !DecodeUi(line.substr(0, tab), row.english) || !DecodeUi(line.substr(tab + 1), row.text)) {
            table.errors.push_back("row " + std::to_string(number) + ": expected english and text columns");
            continue;
        }
        if (row.english.empty() || row.text.empty()) {
            table.errors.push_back("row " + std::to_string(number) + ": empty english or text");
            continue;
        }
        table.rows.push_back(std::move(row));
    }
    return table;
}

const UiTextTable& PortugueseBrazilUiText() {
    static const UiTextTable table = ParseUiTextTable(kPtBrUiTextTsv);
    return table;
}

} // namespace localization

namespace {

constexpr std::string_view kValueMark = "%s";

const std::unordered_map<std::string_view, const std::string*>& PortugueseBrazilIndex() {
    static const auto index = [] {
        std::unordered_map<std::string_view, const std::string*> map;
        for (const localization::UiTextRow& row : localization::PortugueseBrazilUiText().rows) {
            map.emplace(row.english, &row.text);
        }
        return map;
    }();
    return index;
}

std::vector<std::string_view> SplitAtValues(std::string_view text) {
    std::vector<std::string_view> parts;
    size_t from = 0;
    for (size_t at = text.find(kValueMark); at != std::string_view::npos; at = text.find(kValueMark, from)) {
        parts.push_back(text.substr(from, at - from));
        from = at + kValueMark.size();
    }
    parts.push_back(text.substr(from));
    return parts;
}

// A row with values: the English text around each %s, and the translation around the same number of them.
struct ValueRow {
    std::vector<std::string_view> english, text;
    size_t fixed = 0;
};

const std::vector<ValueRow>& PortugueseBrazilValueRows() {
    static const auto rows = [] {
        std::vector<ValueRow> found;
        for (const localization::UiTextRow& row : localization::PortugueseBrazilUiText().rows) {
            ValueRow value{SplitAtValues(row.english), SplitAtValues(row.text)};
            if (value.english.size() < 2 || value.english.size() != value.text.size()) {
                continue;
            }
            for (const std::string_view part : value.english) {
                value.fixed += part.size();
            }
            found.push_back(std::move(value));
        }
        // The most specific row first: "Cannot write the update file %s." before "Cannot write %s.".
        std::stable_sort(found.begin(), found.end(),
                         [](const ValueRow& a, const ValueRow& b) { return a.fixed > b.fixed; });
        return found;
    }();
    return rows;
}

// The values of `text` in the row's %s places, or nothing when the text is not the row's. Every value is non-empty.
bool MatchValues(const ValueRow& row, std::string_view text, std::vector<std::string_view>& values) {
    const std::vector<std::string_view>& parts = row.english;
    if (text.size() < row.fixed + (parts.size() - 1) || !text.starts_with(parts.front()) ||
        !text.ends_with(parts.back())) {
        return false;
    }
    values.clear();
    size_t at = parts.front().size();
    const size_t end = text.size() - parts.back().size();
    for (size_t i = 1; i + 1 < parts.size(); ++i) {
        const size_t found = text.find(parts[i], at + 1);
        if (found == std::string_view::npos || found + parts[i].size() > end) {
            return false;
        }
        values.push_back(text.substr(at, found - at));
        at = found + parts[i].size();
    }
    if (end <= at) {
        return false;
    }
    values.push_back(text.substr(at, end - at));
    return true;
}

std::string TranslateValues(const std::string& english) {
    const std::string_view text = english;
    std::vector<std::string_view> values;
    for (const ValueRow& row : PortugueseBrazilValueRows()) {
        if (!MatchValues(row, text, values)) {
            continue;
        }
        std::string out(row.text.front());
        for (size_t i = 0; i < values.size(); ++i) {
            out.append(values[i]).append(row.text[i + 1]);
        }
        return out;
    }
    return english;
}

} // namespace

const char* Tr(const char* english) {
    if (english == nullptr || !localization::PortugueseBrazilSelected()) {
        return english;
    }
    const auto& index = PortugueseBrazilIndex();
    const auto found = index.find(english);
    return found == index.end() ? english : found->second->c_str();
}

std::string Tr(const std::string& english) {
    if (!localization::PortugueseBrazilSelected()) {
        return english;
    }
    const auto& index = PortugueseBrazilIndex();
    const auto found = index.find(english);
    return found == index.end() ? english : *found->second;
}

std::string TrValue(const std::string& english) {
    if (!localization::PortugueseBrazilSelected()) {
        return english;
    }
    const auto& index = PortugueseBrazilIndex();
    if (const auto found = index.find(english); found != index.end()) {
        return *found->second;
    }
    return TranslateValues(english);
}

namespace localization {

const char* TranslateLauncherText(const char* english) {
    if (english == nullptr || !PortugueseBrazilSelected()) {
        return english;
    }
    const char* exact = Tr(english);
    if (exact != english) {
        return exact;
    }
    // Composed text is translated once and kept, so the pointer outlives the frame it is drawn in.
    static std::mutex mutex;
    static std::unordered_map<std::string, std::unique_ptr<const std::string>> composed;
    const std::lock_guard<std::mutex> lock(mutex);
    if (const auto found = composed.find(english); found != composed.end()) {
        return found->second->c_str();
    }
    std::string translated = TranslateValues(english);
    if (translated == english) {
        return english;
    }
    auto& slot = composed[english];
    slot = std::make_unique<const std::string>(std::move(translated));
    return slot->c_str();
}

} // namespace localization
} // namespace osl

extern "C" const char* osl_ui_text(const char* english) { return osl::Tr(english); }

extern "C" size_t osl_ui_text_value(const char* english, char* out, size_t out_size) {
    const std::string text = osl::TrValue(english ? english : "");
    if (out && out_size) {
        const size_t copied = std::min(text.size(), out_size - 1);
        std::memcpy(out, text.data(), copied);
        out[copied] = '\0';
    }
    return text.size();
}

extern "C" void osl_ui_text_load_language(const char* settings_file) {
    if (!settings_file || !*settings_file) {
        osl::localization::SetActiveLanguage(osl::kEnglishKey);
        return;
    }
    const osl::LauncherSettings settings(osl::PathFromUtf8(settings_file));
    osl::localization::SetActiveLanguage(settings.Get(osl::kTextLanguageKey, osl::kEnglishKey));
}
