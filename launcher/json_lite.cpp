#include "json_lite.h"

#include <cstdio>

namespace osl::json {
namespace {

constexpr int kMostDepth = 8;

void AppendCodePoint(std::string& out, uint32_t code) {
    if (code < 0x80) {
        out += char(code);
    } else if (code < 0x800) {
        out += char(0xC0 | (code >> 6));
        out += char(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += char(0xE0 | (code >> 12));
        out += char(0x80 | ((code >> 6) & 0x3F));
        out += char(0x80 | (code & 0x3F));
    } else {
        out += char(0xF0 | (code >> 18));
        out += char(0x80 | ((code >> 12) & 0x3F));
        out += char(0x80 | ((code >> 6) & 0x3F));
        out += char(0x80 | (code & 0x3F));
    }
}

size_t Utf8Size(std::string_view text, size_t at) {
    const unsigned char lead = static_cast<unsigned char>(text[at]);
    size_t size = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
    if (size == 0 || at + size > text.size()) return 0;
    for (size_t i = 1; i < size; ++i) {
        if ((static_cast<unsigned char>(text[at + i]) & 0xC0) != 0x80) return 0;
    }
    if (size == 2 && lead < 0xC2) return 0;
    if (size == 3) {
        const unsigned char second = static_cast<unsigned char>(text[at + 1]);
        if ((lead == 0xE0 && second < 0xA0) || (lead == 0xED && second >= 0xA0)) return 0;
    }
    if (size == 4) {
        const unsigned char second = static_cast<unsigned char>(text[at + 1]);
        if (lead > 0xF4 || (lead == 0xF0 && second < 0x90) || (lead == 0xF4 && second >= 0x90)) return 0;
    }
    return size;
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::optional<Value> ParseDocument() {
        std::optional<Value> value = ParseValue(0);
        SkipSpace();
        return value && at_ == text_.size() ? std::move(value) : std::nullopt;
    }

private:
    void SkipSpace() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) ++at_;
    }

    bool Take(std::string_view word) {
        if (text_.substr(at_, word.size()) != word) return false;
        at_ += word.size();
        return true;
    }

    std::optional<Value> ParseValue(int depth) {
        if (depth > kMostDepth) return std::nullopt;
        SkipSpace();
        if (at_ >= text_.size()) return std::nullopt;
        Value value;
        const char c = text_[at_];
        if (c == '{') return ParseObject(depth);
        if (c == '[') return ParseArray(depth);
        if (c == '"') {
            value.kind = Value::Kind::kString;
            if (!ParseString(value.string)) return std::nullopt;
            return value;
        }
        const bool is_true = Take("true");
        if (is_true || Take("false")) {
            value.kind = Value::Kind::kBool;
            value.boolean = is_true;
            return value;
        }
        if (Take("null")) return value;
        return ParseNumber();
    }

    std::optional<Value> ParseNumber() {
        const size_t start = at_;
        if (at_ < text_.size() && text_[at_] == '-') ++at_;
        const size_t digits = at_;
        while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
        if (at_ == digits || at_ - digits > 18 || (text_[digits] == '0' && at_ - digits > 1)) return std::nullopt;
        if (at_ < text_.size() && (text_[at_] == '.' || text_[at_] == 'e' || text_[at_] == 'E')) return std::nullopt;
        Value value;
        value.kind = Value::Kind::kNumber;
        value.number = std::stoll(std::string(text_.substr(start, at_ - start)));
        return value;
    }

    bool ParseHex4(uint32_t& code) {
        if (at_ + 4 > text_.size()) return false;
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[at_++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') code |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code |= uint32_t(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool ParseString(std::string& out) {
        ++at_;
        while (at_ < text_.size()) {
            const char c = text_[at_];
            if (c == '"') {
                ++at_;
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') {
                const size_t size = Utf8Size(text_, at_);
                if (size == 0) return false;
                out.append(text_.substr(at_, size));
                at_ += size;
                continue;
            }
            if (++at_ >= text_.size()) return false;
            const char escape = text_[at_++];
            switch (escape) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t code = 0;
                if (!ParseHex4(code)) return false;
                if (code >= 0xD800 && code < 0xDC00) {
                    uint32_t low = 0;
                    if (!Take("\\u") || !ParseHex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                } else if (code >= 0xDC00 && code <= 0xDFFF) {
                    return false;
                }
                AppendCodePoint(out, code);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    std::optional<Value> ParseArray(int depth) {
        ++at_;
        Value value;
        value.kind = Value::Kind::kArray;
        SkipSpace();
        if (at_ < text_.size() && text_[at_] == ']') {
            ++at_;
            return value;
        }
        for (;;) {
            std::optional<Value> item = ParseValue(depth + 1);
            if (!item) return std::nullopt;
            value.array.push_back(std::move(*item));
            SkipSpace();
            if (at_ >= text_.size()) return std::nullopt;
            if (text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (text_[at_++] == ']') return value;
            return std::nullopt;
        }
    }

    std::optional<Value> ParseObject(int depth) {
        ++at_;
        Value value;
        value.kind = Value::Kind::kObject;
        SkipSpace();
        if (at_ < text_.size() && text_[at_] == '}') {
            ++at_;
            return value;
        }
        for (;;) {
            SkipSpace();
            std::string key;
            if (at_ >= text_.size() || text_[at_] != '"' || !ParseString(key)) return std::nullopt;
            SkipSpace();
            if (at_ >= text_.size() || text_[at_++] != ':') return std::nullopt;
            std::optional<Value> member = ParseValue(depth + 1);
            if (!member) return std::nullopt;
            value.object.emplace_back(std::move(key), std::move(*member));
            SkipSpace();
            if (at_ >= text_.size()) return std::nullopt;
            if (text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (text_[at_++] == '}') return value;
            return std::nullopt;
        }
    }

    std::string_view text_;
    size_t at_ = 0;
};

} // namespace

std::string Quote(std::string_view text) {
    std::string out = "\"";
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 0x20 || c == 0x7F) {
            char escape[8];
            std::snprintf(escape, sizeof(escape), "\\u%04x", unsigned(c));
            out += escape;
        } else if (c < 0x80) {
            out += char(c);
        } else if (const size_t size = Utf8Size(text, i); size > 0) {
            out.append(text.substr(i, size));
            i += size;
            continue;
        } else {
            out += "\\ufffd";
        }
        ++i;
    }
    return out + '"';
}

const Value* Value::Find(std::string_view key) const {
    if (kind != Kind::kObject) return nullptr;
    for (const auto& [name, member] : object) {
        if (name == key) return &member;
    }
    return nullptr;
}

std::string Value::StringOr(std::string_view key) const {
    const Value* member = Find(key);
    return member && member->IsString() ? member->string : std::string();
}

int64_t Value::NumberOr(std::string_view key) const {
    const Value* member = Find(key);
    return member && member->IsNumber() ? member->number : 0;
}

bool Value::BoolOr(std::string_view key) const {
    const Value* member = Find(key);
    return member && member->IsBool() && member->boolean;
}

std::optional<Value> Parse(std::string_view text) { return Parser(text).ParseDocument(); }

} // namespace osl::json
