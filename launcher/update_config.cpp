#include "update_config.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>

#include "paths.h"
#include "updater/update_paths.h"
#include "updater/update_platform.h"
#include "utf8.h"

namespace osl {
namespace {

constexpr char kForceLauncherArgument[] = "--launcher";
constexpr char kSkipLauncherArgument[] = "--osl-skip-launcher";

std::optional<std::string> Environment(const char* name) {
#ifdef _WIN32
    const std::wstring wide_name(name, name + std::char_traits<char>::length(name));
    const wchar_t* value = _wgetenv(wide_name.c_str());
    if (!value || !*value) {
        return std::nullopt;
    }
    return Utf8(std::filesystem::path(value));
#else
    const char* value = std::getenv(name);
    if (!value || !*value) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

std::optional<std::string> TestVariable(const char* name) {
    const std::optional<std::string> enabled = Environment(trg::update::kUpdateTestSwitch);
    if (!enabled || *enabled != "1") {
        return std::nullopt;
    }
    return Environment(name);
}

void Log(std::string_view text) { std::printf("Updater: %.*s\n", static_cast<int>(text.size()), text.data()); }

} // namespace

std::string WindowsPackageName(const trg::update::SemanticVersion& version) {
    return "OdinSphereLeifthrasir-v" + version.CoreString() + "-windows-x64.zip";
}

std::string WindowsChecksumName(const trg::update::SemanticVersion& version) {
    return "OdinSphereLeifthrasir-v" + version.CoreString() + "-windows-x64.sha256";
}

std::string LinuxPackageName(const trg::update::SemanticVersion& version) {
    return "OdinSphereLeifthrasir-v" + version.CoreString() + "-x86_64.AppImage";
}

std::string LinuxChecksumName(const trg::update::SemanticVersion& version) {
    return "OdinSphereLeifthrasir-v" + version.CoreString() + "-x86_64.sha256";
}

std::vector<std::string> UpdateRestartArguments(std::vector<std::string> arguments) {
    std::erase_if(arguments, [](const std::string& argument) {
        return argument == kSkipLauncherArgument || argument == kForceLauncherArgument;
    });
    arguments.emplace_back(kForceLauncherArgument);
    return arguments;
}

trg::update::UpdateConfig MakeUpdateConfig(const std::filesystem::path& install_dir, std::string_view current_version) {
    using trg::update::UpdatePackageKind;

    trg::update::UpdateConfig config;
    config.current_version = std::string(current_version);
    config.releases_url = kReleasesUrl;
    config.user_agent = "OdinSphereLeifthrasirPC-Updater/" + config.current_version;
    config.install_dir = install_dir;
    config.restart_arguments = UpdateRestartArguments(trg::update::CurrentProcessArguments());
    config.log = Log;

#ifdef _WIN32
    config.package_kind = UpdatePackageKind::kZip;
    config.asset_name = WindowsPackageName;
    config.checksum_asset_name = WindowsChecksumName;
    config.restart_executable = install_dir / kLoaderExecutable;
    config.required_staged_files = {kLoaderExecutable, kUpdaterExecutable};
    if (IsBuildTreeExecutableDir(install_dir)) {
        config.install_unsupported_reason = "This development build is not updated automatically. Use a release package.";
    } else if (!trg::update::IsRegularFile(install_dir / kUpdaterExecutable)) {
        config.install_unsupported_reason =
            "The update helper is missing from this installation. Reinstall this release to enable automatic updates.";
    } else {
        config.install_supported = true;
    }
#else
    config.package_kind = UpdatePackageKind::kAppImage;
    config.asset_name = LinuxPackageName;
    config.checksum_asset_name = LinuxChecksumName;
    std::filesystem::path appimage;
    if (trg::update::HasOriginalAppImage(appimage)) {
        config.appimage_path = appimage;
        config.restart_executable = appimage;
        config.install_supported = true;
    } else {
        config.install_unsupported_reason = "Automatic installation is available in the AppImage build.";
    }
#endif

    if (const std::optional<std::string> version = TestVariable(trg::update::kUpdateTestCurrentVersion)) {
        config.current_version = *version;
        Log("test mode: reporting version " + config.current_version);
    }
    if (const std::optional<std::string> json = TestVariable(trg::update::kUpdateTestReleaseJson)) {
        config.release_json_override = PathFromUtf8(*json);
        Log("test mode: release metadata from " + *json);
    }
    if (const std::optional<std::string> hash = TestVariable(trg::update::kUpdateTestAssetSha256)) {
        config.sha256_override = *hash;
        Log("test mode: package SHA-256 override");
    }
    return config;
}

} // namespace osl
