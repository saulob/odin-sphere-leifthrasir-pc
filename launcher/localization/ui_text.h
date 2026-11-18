// The launcher's and the port's in-game text in Portuguese (Brazil), keyed by the English string that is drawn.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace osl {

// The text of the active language for an English UI string. English text with no translation is returned as is.
const char* Tr(const char* english);
std::string Tr(const std::string& english);
// Tr for text built at runtime: a row with %s ("Cannot write %s.") also matches that text with any non-empty value in
// each %s, in order. The row is never used as a format string, so a value can hold anything.
std::string TrValue(const std::string& english);

namespace localization {

// The language the port's own text is shown in: Portuguese (Brazil) for kPortugueseBrazilKey, English for any other.
void SetActiveLanguage(std::string_view key);
bool PortugueseBrazilSelected();

// TRG's translator: Tr, then TrValue for text composed with values. The result lives as long as the program.
const char* TranslateLauncherText(const char* english);

struct UiTextRow {
    std::string english;
    std::string text;
};

struct UiTextTable {
    std::vector<UiTextRow> rows;
    std::vector<std::string> errors;
};

// Rows are `english<TAB>text`; both columns use \n, \s (space), \t and \\ escapes.
UiTextTable ParseUiTextTable(std::string_view tsv);

const UiTextTable& PortugueseBrazilUiText();

} // namespace localization
} // namespace osl
