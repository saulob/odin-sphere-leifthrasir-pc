// The text containers of Odin Sphere Leifthrasir and their control syntax; scripts/localization/odin_text.py reads and
// writes the same layouts and applies the same rules. Units are kept as the container stores them: UTF-8, or Shift-JIS
// for the Japanese executable's story text, which is never decoded.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace osl {

enum class TextKind { kMsgSheet, kStory, kArchivePage, kCredits };

// msgsheet/SystemMsg.fms, story/a1_1, ArchiveViewer/Cafe00.txt; nothing for an entry that is not translated text.
std::optional<std::string> TextResourceOf(std::string_view entry_path);
TextKind KindOf(std::string_view resource);

struct TextContainer {
    TextKind kind = TextKind::kMsgSheet;
    std::vector<uint8_t> header;  // msgsheet: up to the pointer table; story: the 0x40-byte header
    std::vector<uint8_t> trailer; // msgsheet: from FEOC
    size_t padding = 0;           // story: zero bytes after the last record
    bool shift_jis = false;       // story: a record is not UTF-8
    bool bom = false;             // archive page
    std::vector<std::string> layout; // archive page: the script around the units, one more than the units
    std::vector<std::string> units;
};

std::optional<TextContainer> ParseTextContainer(std::string_view resource, std::span<const uint8_t> data);
std::vector<uint8_t> BuildTextContainer(const TextContainer& container);
// The script of a page without its text and comments, whitespace collapsed.
std::string PageLayout(const TextContainer& page);

// Control sequences: @#(N) button glyphs, ##(KEY), $$(N), @x(...) and @/x tags.
std::vector<std::string> ControlTokens(std::string_view text);
// printf conversions (%d, %g, %s, %02d, %%); "20% or" is prose.
std::vector<std::string> Conversions(std::string_view text);
bool IsFormat(std::string_view text);
// What a slot in another language must share with the English record: conversions in order and button glyphs.
std::vector<std::string> Arguments(std::string_view text);
std::vector<std::string> Digits(std::string_view text);
// empty, placeholder or text, with the arguments.
std::string Shape(std::string_view text);
std::string VisibleText(std::string_view text);
// A unit the port translates: it shows letters and is not a bare placeholder.
bool Translatable(std::string_view text);

// Why `translation` may not be served for `english`; empty when it may.
std::vector<std::string> CheckTranslation(std::string_view english, std::string_view translation);

std::string TextSha256(std::string_view bytes);

} // namespace osl
