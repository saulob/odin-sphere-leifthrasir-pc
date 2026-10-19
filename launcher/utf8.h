// TRG Launcher and its settings file take UTF-8 paths.
#pragma once

#include <filesystem>
#include <string>

namespace osl {

inline std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

inline std::filesystem::path PathFromUtf8(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

} // namespace osl
