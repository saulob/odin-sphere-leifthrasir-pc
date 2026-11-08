// The little JSON the bug report service speaks: objects, arrays, strings, integers, booleans and null.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace osl::json {

// `text` as a JSON string literal, quotes included. Invalid UTF-8 and control characters are escaped, never passed on.
std::string Quote(std::string_view text);

class Value {
public:
    enum class Kind { kNull, kBool, kNumber, kString, kArray, kObject };

    Kind kind = Kind::kNull;
    bool boolean = false;
    int64_t number = 0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    bool IsObject() const { return kind == Kind::kObject; }
    bool IsArray() const { return kind == Kind::kArray; }
    bool IsString() const { return kind == Kind::kString; }
    bool IsNumber() const { return kind == Kind::kNumber; }
    bool IsBool() const { return kind == Kind::kBool; }

    // The member named `key` of an object, or nullptr.
    const Value* Find(std::string_view key) const;
    std::string StringOr(std::string_view key) const;
    int64_t NumberOr(std::string_view key) const;
    bool BoolOr(std::string_view key) const;
};

// nullopt for anything that is not exactly one JSON value; nesting is limited and fractions are refused.
std::optional<Value> Parse(std::string_view text);

} // namespace osl::json
