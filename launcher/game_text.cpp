#include "game_text.h"

#include <algorithm>
#include <cstring>

#include "crypto.h"

namespace osl {
namespace {

constexpr size_t kFmsHeader = 0x20;
constexpr size_t kStoryHeader = 0x40;
constexpr size_t kStoryCountAt = 0x10;

uint32_t ReadU32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
void WriteU32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}

std::string Lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return out;
}

bool StartsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }
bool EndsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}
bool IsAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool IsDigit(char c) { return c >= '0' && c <= '9'; }

bool ValidUtf8(std::string_view s) {
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = uint8_t(s[i]);
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || (n == 2 && c < 0xC2) || i + n > s.size()) return false;
        for (size_t k = 1; k < n; ++k) {
            if ((uint8_t(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += n;
    }
    return true;
}

// The length of the control sequence at `i`, or 0.
size_t TokenAt(std::string_view t, size_t i) {
    const auto closed = [&](size_t open, bool stop_at_newline) -> size_t {
        for (size_t k = open + 1; k < t.size(); ++k) {
            if (t[k] == ')') return k + 1 - i;
            if (stop_at_newline && t[k] == '\n') return 0;
        }
        return 0;
    };
    if (StartsWith(t.substr(i), "##(") || StartsWith(t.substr(i), "$$(")) return closed(i + 2, false);
    if (t[i] != '@' || i + 1 >= t.size()) return 0;
    const char c = t[i + 1];
    if (c == '/') return i + 2 < t.size() && IsAlpha(t[i + 2]) ? 3 : 0;
    if (!IsAlpha(c) && c != '#' && c != '$') return 0;
    if (i + 2 < t.size() && t[i + 2] == '(') {
        if (const size_t n = closed(i + 2, true)) return n;
    }
    return 2;
}

// The length of the printf conversion at `i` (a '%'), or 0.
size_t ConversionAt(std::string_view t, size_t i) {
    if (i + 1 < t.size() && t[i + 1] == '%') return 2;
    size_t k = i + 1;
    while (k < t.size() && std::strchr("-+0#", t[k])) ++k;
    while (k < t.size() && IsDigit(t[k])) ++k;
    if (k + 1 < t.size() && t[k] == '.' && IsDigit(t[k + 1])) {
        ++k;
        while (k < t.size() && IsDigit(t[k])) ++k;
    }
    while (k < t.size() && std::strchr("hlLzjt", t[k])) ++k;
    return k < t.size() && std::strchr("diouxXeEfgGcsp", t[k]) ? k + 1 - i : 0;
}

size_t LonePercents(std::string_view t) {
    size_t n = 0;
    for (size_t i = 0; i < t.size();) {
        if (t[i] == '%') {
            if (const size_t c = ConversionAt(t, i)) {
                i += c;
                continue;
            }
            ++n;
        }
        ++i;
    }
    return n;
}

bool IsButton(std::string_view token) {
    if (StartsWith(token, "##(")) return true;
    if (!StartsWith(token, "@#(") || token.size() < 5) return false;
    return std::all_of(token.begin() + 3, token.end() - 1, IsDigit);
}

// Python's str.strip() for the whitespace the corpus uses: ASCII, no-break space and the ideographic space.
std::string_view Strip(std::string_view s) {
    const auto lead = [](std::string_view v) -> size_t {
        if (!v.empty() && std::strchr(" \t\n\r\f\v", v[0]) && v[0]) return 1;
        if (StartsWith(v, "\xC2\xA0")) return 2;
        if (StartsWith(v, "\xE3\x80\x80")) return 3;
        return 0;
    };
    const auto trail = [](std::string_view v) -> size_t {
        if (!v.empty() && std::strchr(" \t\n\r\f\v", v.back()) && v.back()) return 1;
        if (EndsWith(v, "\xC2\xA0")) return 2;
        if (EndsWith(v, "\xE3\x80\x80")) return 3;
        return 0;
    };
    for (size_t n; (n = lead(s));) s.remove_prefix(n);
    for (size_t n; (n = trail(s));) s.remove_suffix(n);
    return s;
}

std::string Join(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& p : parts) {
        out += p;
        out += '\x1f';
    }
    return out;
}

std::string BlankComments(std::string_view text) {
    std::string out(text);
    for (size_t i = 0; i + 1 < out.size(); ++i) {
        if (out[i] == '/' && out[i + 1] == '/') {
            while (i < out.size() && out[i] != '\r' && out[i] != '\n') out[i++] = ' ';
        }
    }
    return out;
}

std::optional<TextContainer> ParseFms(std::span<const uint8_t> d) {
    if (d.size() < kFmsHeader || std::memcmp(d.data(), "FMSB", 4)) return std::nullopt;
    const size_t header = ReadU32(d.data() + 8), count = ReadU32(d.data() + 0x14);
    const size_t trailer = size_t(ReadU32(d.data() + 4)) + kFmsHeader;
    const size_t start = header + count * 8;
    if (header < kFmsHeader || start > trailer || trailer + 4 > d.size() || std::memcmp(d.data() + trailer, "FEOC", 4)) {
        return std::nullopt;
    }
    if (std::any_of(d.begin() + header, d.begin() + start, [](uint8_t b) { return b != 0; })) return std::nullopt;
    TextContainer c;
    c.kind = TextKind::kMsgSheet;
    c.header.assign(d.begin(), d.begin() + header);
    c.trailer.assign(d.begin() + trailer, d.end());
    size_t at = start;
    for (size_t r = 0; r < count; ++r) {
        const auto* end = static_cast<const uint8_t*>(std::memchr(d.data() + at, 0, trailer - at));
        if (!end) return std::nullopt;
        c.units.emplace_back(reinterpret_cast<const char*>(d.data() + at), size_t(end - (d.data() + at)));
        if (!ValidUtf8(c.units.back())) return std::nullopt;
        at = size_t(end - d.data()) + 1;
    }
    if (std::any_of(d.begin() + at, d.begin() + trailer, [](uint8_t b) { return b != 0; })) return std::nullopt;
    return c;
}

std::optional<TextContainer> ParseStory(std::span<const uint8_t> d) {
    if (d.size() < kStoryHeader) return std::nullopt;
    const size_t count = ReadU32(d.data() + kStoryCountAt);
    TextContainer c;
    c.kind = TextKind::kStory;
    c.header.assign(d.begin(), d.begin() + kStoryHeader);
    size_t at = kStoryHeader;
    for (size_t r = 0; r < count; ++r) {
        if (at >= d.size()) return std::nullopt;
        const auto* end = static_cast<const uint8_t*>(std::memchr(d.data() + at, 0, d.size() - at));
        if (!end) return std::nullopt;
        c.units.emplace_back(reinterpret_cast<const char*>(d.data() + at), size_t(end - (d.data() + at)));
        if (!ValidUtf8(c.units.back())) c.shift_jis = true;
        at = size_t(end - d.data()) + 1;
    }
    if (std::any_of(d.begin() + at, d.end(), [](uint8_t b) { return b != 0; })) return std::nullopt;
    c.padding = d.size() - at;
    return c;
}

std::optional<TextContainer> ParseCredits(std::span<const uint8_t> d) {
    TextContainer c;
    c.kind = TextKind::kCredits;
    c.bom = d.size() >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF;
    const std::string text(reinterpret_cast<const char*>(d.data()) + (c.bom ? 3 : 0), d.size() - (c.bom ? 3 : 0));
    if (!ValidUtf8(text)) return std::nullopt;
    size_t at = 0;
    for (size_t line = 0; line < text.size();) {
        size_t end = text.find_first_of("\r\n", line);
        if (end == std::string::npos) end = text.size();
        size_t k = line;
        while (k < end && (text[k] == ' ' || text[k] == '\t')) ++k;
        if (text.compare(k, 3, "@F1") == 0) {
            c.layout.push_back(text.substr(at, k + 3 - at));
            c.units.push_back(text.substr(k + 3, end - k - 3));
            at = end;
        }
        line = end;
        while (line < text.size() && (text[line] == '\r' || text[line] == '\n')) ++line;
    }
    c.layout.push_back(text.substr(at));
    return c;
}

std::optional<TextContainer> ParsePage(std::span<const uint8_t> d) {
    TextContainer c;
    c.kind = TextKind::kArchivePage;
    c.bom = d.size() >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF;
    const std::string text(reinterpret_cast<const char*>(d.data()) + (c.bom ? 3 : 0), d.size() - (c.bom ? 3 : 0));
    if (!ValidUtf8(text)) return std::nullopt;
    const std::string scan = BlankComments(text);
    size_t at = 0, pos = 0;
    while ((pos = scan.find('@', pos)) != std::string::npos) {
        size_t start = 0, end = 0, next = 0;
        if (scan.compare(pos, 7, "@TITLE(") == 0) {
            size_t k = pos + 7;
            while (k < scan.size() && std::strchr(" \t\r\n\f\v", scan[k]) && scan[k]) ++k;
            if (k < scan.size() && scan[k] == '"') {
                size_t e = k + 1;
                while (e < scan.size() && scan[e] != '"' && scan[e] != '\r' && scan[e] != '\n') ++e;
                if (e < scan.size() && scan[e] == '"') {
                    start = k + 1;
                    end = e;
                    next = e + 1;
                }
            }
        } else if (scan.compare(pos, 6, "@TEXT(") == 0) {
            for (size_t e = pos + 6; e < scan.size() && !next; ++e) {
                size_t k = scan[e] == '\r' && e + 1 < scan.size() && scan[e + 1] == '\n' ? e + 2 : scan[e] == '\n' ? e + 1 : 0;
                if (!k) continue;
                while (k < scan.size() && (scan[k] == ' ' || scan[k] == '\t')) ++k;
                if (k < scan.size() && scan[k] == ')') {
                    start = pos + 6;
                    end = e;
                    next = k + 1;
                }
            }
        }
        if (!next) {
            ++pos;
            continue;
        }
        c.layout.push_back(text.substr(at, start - at));
        c.units.push_back(text.substr(start, end - start));
        at = end;
        pos = next;
    }
    c.layout.push_back(text.substr(at));
    return c;
}

} // namespace

std::optional<std::string> TextResourceOf(std::string_view entry_path) {
    std::string path(entry_path);
    std::replace(path.begin(), path.end(), '\\', '/');
    const std::string low = Lower(path);
    const size_t slash = path.rfind('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.rfind('.');
    const std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
    if (StartsWith(low, "msgsheet/") && EndsWith(low, ".fms")) return "msgsheet/" + name;
    if (StartsWith(low, "archiveviewer/") && EndsWith(low, ".txt")) return "ArchiveViewer/" + stem + ".txt";
    if (StartsWith(low, "staffroll/") && EndsWith(low, ".txt")) return "StaffRoll/" + stem + ".txt";
    if (EndsWith(low, ".txt") && (slash == std::string::npos || StartsWith(low, "eventtxt/"))) return "story/" + Lower(stem);
    return std::nullopt;
}

TextKind KindOf(std::string_view resource) {
    if (StartsWith(resource, "msgsheet/")) return TextKind::kMsgSheet;
    if (StartsWith(resource, "story/")) return TextKind::kStory;
    if (StartsWith(resource, "StaffRoll/")) return TextKind::kCredits;
    return TextKind::kArchivePage;
}

std::optional<TextContainer> ParseTextContainer(std::string_view resource, std::span<const uint8_t> data) {
    switch (KindOf(resource)) {
    case TextKind::kMsgSheet: return ParseFms(data);
    case TextKind::kStory: return ParseStory(data);
    case TextKind::kArchivePage: return ParsePage(data);
    case TextKind::kCredits: return ParseCredits(data);
    }
    return std::nullopt;
}

std::vector<uint8_t> BuildTextContainer(const TextContainer& c) {
    std::vector<uint8_t> out;
    const auto append = [&](std::string_view s) { out.insert(out.end(), s.begin(), s.end()); };
    switch (c.kind) {
    case TextKind::kMsgSheet:
        out = c.header;
        out.resize(out.size() + c.units.size() * 8, 0);
        for (const std::string& u : c.units) {
            append(u);
            out.push_back(0);
        }
        out.resize(out.size() + (16 - out.size() % 16) % 16, 0);
        WriteU32(out.data() + 4, uint32_t(out.size() - kFmsHeader));
        WriteU32(out.data() + 0x14, uint32_t(c.units.size()));
        out.insert(out.end(), c.trailer.begin(), c.trailer.end());
        break;
    case TextKind::kStory:
        out = c.header;
        WriteU32(out.data() + kStoryCountAt, uint32_t(c.units.size()));
        for (const std::string& u : c.units) {
            append(u);
            out.push_back(0);
        }
        out.resize(out.size() + c.padding, 0);
        break;
    case TextKind::kArchivePage:
    case TextKind::kCredits:
        if (c.bom) append("\xEF\xBB\xBF");
        for (size_t i = 0; i < c.layout.size(); ++i) {
            append(c.layout[i]);
            if (i < c.units.size()) append(c.units[i]);
        }
        break;
    }
    return out;
}

std::string PageLayout(const TextContainer& page) {
    std::string joined;
    for (const std::string& part : page.layout) joined += part;
    const std::string scan = BlankComments(joined);
    std::string out;
    bool space = false;
    for (size_t i = 0; i < scan.size(); ++i) {
        const bool ws = std::strchr(" \t\r\n\f\v", scan[i]) && scan[i];
        if (ws) {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += scan[i];
    }
    return out;
}

std::vector<std::string> ControlTokens(std::string_view text) {
    std::vector<std::string> out;
    for (size_t i = 0; i < text.size();) {
        if (const size_t n = TokenAt(text, i)) {
            out.emplace_back(text.substr(i, n));
            i += n;
        } else {
            ++i;
        }
    }
    return out;
}

std::vector<std::string> Conversions(std::string_view text) {
    std::vector<std::string> out;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '%') {
            if (const size_t n = ConversionAt(text, i)) {
                out.emplace_back(text.substr(i, n));
                i += n;
                continue;
            }
        }
        ++i;
    }
    return out;
}

bool IsFormat(std::string_view text) { return !Conversions(text).empty(); }

std::vector<std::string> Arguments(std::string_view text) {
    std::vector<std::string> out = Conversions(text);
    std::vector<std::string> buttons;
    for (std::string& t : ControlTokens(text)) {
        if (IsButton(t)) buttons.push_back(std::move(t));
    }
    std::sort(buttons.begin(), buttons.end());
    out.emplace_back("|");
    out.insert(out.end(), buttons.begin(), buttons.end());
    return out;
}

std::string VisibleText(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        if (const size_t n = TokenAt(text, i)) {
            i += n;
        } else {
            out += text[i++];
        }
    }
    return out;
}

std::vector<std::string> Digits(std::string_view text) {
    const std::string v = VisibleText(text);
    std::vector<std::string> out;
    std::string run;
    for (size_t i = 0; i <= v.size(); ++i) {
        char d = 0;
        if (i < v.size() && IsDigit(v[i])) {
            d = v[i];
        } else if (i + 2 < v.size() && uint8_t(v[i]) == 0xEF && uint8_t(v[i + 1]) == 0xBC && uint8_t(v[i + 2]) >= 0x90 &&
                   uint8_t(v[i + 2]) <= 0x99) {
            d = char('0' + (uint8_t(v[i + 2]) - 0x90));
            i += 2;
        }
        if (d) {
            run += d;
        } else if (!run.empty()) {
            out.push_back(std::move(run));
            run.clear();
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string Shape(std::string_view text) {
    const std::string visible = VisibleText(text);
    std::string_view seen = Strip(visible);
    std::string kind = "text";
    if (seen.empty()) {
        kind = "empty";
    } else {
        bool placeholder = true;
        for (size_t i = 0; i < seen.size() && placeholder;) {
            if (seen[i] == '-') {
                ++i;
            } else if (StartsWith(seen.substr(i), "－") || StartsWith(seen.substr(i), "ー")) {
                i += 3;
            } else {
                placeholder = false;
            }
        }
        if (placeholder) kind = "placeholder";
    }
    return kind + '\x1e' + Join(Arguments(text));
}

bool Translatable(std::string_view text) {
    const std::string visible = VisibleText(text);
    const std::string_view seen = Strip(visible);
    return std::any_of(seen.begin(), seen.end(), IsAlpha) && seen != "---" && seen != "DUMMY" && seen != "dummy";
}

std::vector<std::string> CheckTranslation(std::string_view english, std::string_view pt) {
    std::vector<std::string> problems;
    if (Strip(pt).empty()) return {"empty translation"};
    if (!ValidUtf8(pt)) return {"not UTF-8"};
    if (pt.find("\xEF\xBF\xBD") != std::string_view::npos || pt.find("â€") != std::string_view::npos) {
        problems.emplace_back("replacement character or mojibake");
    }
    for (size_t i = 0; i + 3 < pt.size(); ++i) {
        if ((pt.compare(i, 2, "Ã") == 0 || pt.compare(i, 2, "Â") == 0) && uint8_t(pt[i + 2]) == 0xC2) {
            problems.emplace_back("mojibake");
            break;
        }
    }
    const bool crlf = english.find("\r\n") != std::string_view::npos;
    if (std::any_of(pt.begin(), pt.end(), [&](char c) { return uint8_t(c) < 0x20 && c != '\n' && !(crlf && c == '\r'); })) {
        problems.emplace_back("control character");
    }
    std::vector<std::string> a = ControlTokens(english), b = ControlTokens(pt);
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    if (a != b) problems.emplace_back("control tokens differ");
    if (Conversions(english) != Conversions(pt)) problems.emplace_back("printf conversions differ");
    if (IsFormat(english) && LonePercents(english) != LonePercents(pt)) problems.emplace_back("lone % count differs");
    return problems;
}

std::string TextSha256(std::string_view bytes) {
    uint8_t digest[32];
    if (!crypto::Sha256(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), digest)) return {};
    static const char kHex[] = "0123456789abcdef";
    std::string out(64, '0');
    for (int i = 0; i < 32; ++i) {
        out[2 * i] = kHex[digest[i] >> 4];
        out[2 * i + 1] = kHex[digest[i] & 15];
    }
    return out;
}

} // namespace osl
