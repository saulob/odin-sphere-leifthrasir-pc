// The launcher's self-update: where Odin Sphere Leifthrasir PC releases are published and how a release is installed.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "updater/update_controller.h"

namespace osl {

inline constexpr char kReleasesUrl[] = "https://api.github.com/repos/saulob/odin-sphere-leifthrasir-pc/releases/latest";
inline constexpr char kGameExecutable[] = "odin_sphere_leifthrasir.exe";
inline constexpr char kUpdaterExecutable[] = "trg-updater.exe";

std::string WindowsPackageName(const trg::update::SemanticVersion& version);
std::string WindowsChecksumName(const trg::update::SemanticVersion& version);
std::string LinuxPackageName(const trg::update::SemanticVersion& version);
std::string LinuxChecksumName(const trg::update::SemanticVersion& version);

// The updated program starts with the original arguments and the launcher shown (--launcher, never
// --osl-skip-launcher), so the player sees that the update worked.
std::vector<std::string> UpdateRestartArguments(std::vector<std::string> arguments);

// The ODIN_UPDATE_TEST_* overrides apply only while ODIN_UPDATE_TEST=1.
trg::update::UpdateConfig MakeUpdateConfig(const std::filesystem::path& install_dir, std::string_view current_version);

} // namespace osl
