#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace trg::update {

// The developer test switch and the overrides it enables; the restarted program never inherits them.
inline constexpr char kUpdateTestSwitch[] = "ODIN_UPDATE_TEST";
inline constexpr char kUpdateTestCurrentVersion[] = "ODIN_UPDATE_TEST_CURRENT_VERSION";
inline constexpr char kUpdateTestReleaseJson[] = "ODIN_UPDATE_TEST_RELEASE_JSON";
inline constexpr char kUpdateTestAssetSha256[] = "ODIN_UPDATE_TEST_ASSET_SHA256";
inline constexpr std::string_view kUpdateTestVariables[] = {kUpdateTestSwitch, kUpdateTestCurrentVersion,
                                                            kUpdateTestReleaseJson, kUpdateTestAssetSha256};

unsigned long CurrentProcessId();
std::vector<std::string> CurrentProcessArguments();
std::filesystem::path CurrentExecutablePath();

bool HasOriginalAppImage(std::filesystem::path& path);
// On Windows the program is restarted only by trg-updater (OSL_UPDATER_HELPER_BUILD), which clears the update test
// variables for it; the AppImage restart clears them on Linux.
#if defined(_WIN32) && !defined(OSL_UPDATER_HELPER_BUILD)
bool LaunchApplication(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                       const std::filesystem::path& working_directory, std::string& error);
#else
bool LaunchApplication(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                       const std::filesystem::path& working_directory, std::string& error,
                       bool clear_update_test_environment = false);
#endif
bool LaunchUpdateHelper(const std::filesystem::path& helper, const std::filesystem::path& job_file,
                        std::string& error);
bool ApplyAppImageUpdate(const std::filesystem::path& downloaded, const std::filesystem::path& appimage,
                         const std::vector<std::string>& arguments, std::string& error);
bool WaitForProcess(unsigned long pid, int timeout_ms, std::string& error);

}  // namespace trg::update
