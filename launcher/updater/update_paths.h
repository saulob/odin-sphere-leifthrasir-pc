#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace trg::update {

// Every release file is named <kReleaseName>-v<version>-...; tools/package.ps1 and tools/package-linux.sh use the same.
inline constexpr char kReleaseName[] = "OdinSphereLeifthrasirPC";

std::string Utf8Path(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(std::string_view text);

bool NormalizeRelativePath(std::string_view input, std::string& normalized, std::string* reason = nullptr);
bool IsSafeRelativePath(std::string_view path, std::string* reason = nullptr);
bool IsProtectedRelativePath(std::string_view normalized_path);
bool PathIsWithin(const std::filesystem::path& path, const std::filesystem::path& root);
bool IsRegularFile(const std::filesystem::path& path);
bool HasReparseAncestor(const std::filesystem::path& path, const std::filesystem::path& root);
// Whether appimage is the AppImage the running program was started from: a regular Odin Sphere Leifthrasir AppImage
// outside its own mount, with the running program inside `appdir`.
bool IsRunningAppImage(const std::filesystem::path& appimage, const std::filesystem::path& appdir,
                       const std::filesystem::path& executable);

}  // namespace trg::update
