// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fstream>
#include <unordered_map>
#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/scope_exit.h"
#include "common/types.h"

#ifdef __APPLE__
#include <CoreFoundation/CFBundle.h>
#include <dlfcn.h>
#include <sys/param.h>
#endif

#ifndef MAX_PATH
#ifdef _WIN32
// This is the maximum number of UTF-16 code units permissible in Windows file paths
#define MAX_PATH 260
#include <Shlobj.h>
#include <windows.h>
#else
// This is the maximum number of UTF-8 code units permissible in all other OSes' file paths
#define MAX_PATH 1024
#endif
#endif

namespace Common::FS {

namespace fs = std::filesystem;

#ifdef __APPLE__
using IsTranslocatedURLFunc = Boolean (*)(CFURLRef path, bool* isTranslocated,
                                          CFErrorRef* __nullable error);
using CreateOriginalPathForURLFunc = CFURLRef __nullable (*)(CFURLRef translocatedPath,
                                                             CFErrorRef* __nullable error);

static CFURLRef UntranslocateBundlePath(const CFURLRef bundle_path) {
    if (void* security_handle =
            dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY)) {
        SCOPE_EXIT {
            dlclose(security_handle);
        };

        const auto IsTranslocatedURL = reinterpret_cast<IsTranslocatedURLFunc>(
            dlsym(security_handle, "SecTranslocateIsTranslocatedURL"));
        const auto CreateOriginalPathForURL = reinterpret_cast<CreateOriginalPathForURLFunc>(
            dlsym(security_handle, "SecTranslocateCreateOriginalPathForURL"));

        bool is_translocated = false;
        if (IsTranslocatedURL && CreateOriginalPathForURL &&
            IsTranslocatedURL(bundle_path, &is_translocated, nullptr) && is_translocated) {
            return CreateOriginalPathForURL(bundle_path, nullptr);
        }
    }
    return nullptr;
}

static std::optional<std::filesystem::path> GetBundleParentDirectory() {
    if (CFBundleRef bundle_ref = CFBundleGetMainBundle()) {
        if (CFURLRef bundle_url_ref = CFBundleCopyBundleURL(bundle_ref)) {
            SCOPE_EXIT {
                CFRelease(bundle_url_ref);
            };

            CFURLRef untranslocated_url_ref = UntranslocateBundlePath(bundle_url_ref);
            SCOPE_EXIT {
                if (untranslocated_url_ref) {
                    CFRelease(untranslocated_url_ref);
                }
            };

            char app_bundle_path[MAXPATHLEN];
            if (CFURLGetFileSystemRepresentation(
                    untranslocated_url_ref ? untranslocated_url_ref : bundle_url_ref, true,
                    reinterpret_cast<u8*>(app_bundle_path), sizeof(app_bundle_path))) {
                std::filesystem::path bundle_path{app_bundle_path};
                return bundle_path.parent_path();
            }
        }
    }
    return std::nullopt;
}
#endif

// osl: every path follows the port's user folder (SetUserDir). Nothing is created up front: the code that writes
// into a folder creates it.
static std::unordered_map<PathType, fs::path> MakeUserPaths(const fs::path& user_dir) {
    return {
        {PathType::UserDir, user_dir},
        {PathType::LogDir, user_dir / LOG_DIR},
        {PathType::ScreenshotsDir, user_dir / SCREENSHOTS_DIR},
        {PathType::ShaderDir, user_dir / SHADER_DIR},
        {PathType::GameDataDir, user_dir / GAMEDATA_DIR},
        {PathType::TempDataDir, user_dir / TEMPDATA_DIR},
        {PathType::SysModuleDir, user_dir / SYSMODULES_DIR},
        {PathType::DownloadDir, user_dir / DOWNLOAD_DIR},
        {PathType::CapturesDir, user_dir / CAPTURES_DIR},
        {PathType::CheatsDir, user_dir / CHEATS_DIR},
        {PathType::PatchesDir, user_dir / PATCHES_DIR},
        {PathType::MetaDataDir, user_dir / METADATA_DIR},
        {PathType::CustomTrophy, user_dir / CUSTOM_TROPHY},
        {PathType::CustomConfigs, user_dir / CUSTOM_CONFIGS},
        {PathType::CacheDir, user_dir / CACHE_DIR},
        {PathType::FontsDir, user_dir / FONTS_DIR},
        {PathType::TrophyDir, user_dir / TROPHY_DIR},
        {PathType::HomeDir, user_dir / HOME_DIR},
        {PathType::CustomModulesDir, user_dir / CUSTOM_MODULES_DIR},
    };
}

static auto UserPaths = MakeUserPaths(PORTABLE_DIR);

void SetUserDir(const fs::path& user_dir) {
    UserPaths = MakeUserPaths(user_dir);
}

bool ValidatePath(const fs::path& path) {
    if (path.empty()) {
        LOG_ERROR(Common_Filesystem, "Input path is empty, path={}", PathToUTF8String(path));
        return false;
    }

#ifdef _WIN32
    if (path.u16string().size() >= MAX_PATH) {
        LOG_ERROR(Common_Filesystem, "Input path is too long, path={}", PathToUTF8String(path));
        return false;
    }
#else
    if (path.u8string().size() >= MAX_PATH) {
        LOG_ERROR(Common_Filesystem, "Input path is too long, path={}", PathToUTF8String(path));
        return false;
    }
#endif

    return true;
}

std::string PathToUTF8String(const std::filesystem::path& path) {
    const auto u8_string = path.u8string();
    return std::string{u8_string.begin(), u8_string.end()};
}

const fs::path& GetUserPath(PathType shad_path) {
    return UserPaths.at(shad_path);
}

std::string GetUserPathString(PathType shad_path) {
    return PathToUTF8String(GetUserPath(shad_path));
}

void SetUserPath(PathType shad_path, const fs::path& new_path) {
    if (!std::filesystem::is_directory(new_path)) {
        LOG_ERROR(Common_Filesystem, "Filesystem object at new_path={} is not a directory",
                  PathToUTF8String(new_path));
        return;
    }

    UserPaths.insert_or_assign(shad_path, new_path);
}

std::optional<fs::path> FindGameByID(const fs::path& dir, const std::string& game_id,
                                     int max_depth) {
    if (max_depth < 0) {
        return std::nullopt;
    }

    const auto boot_path_for = [](const fs::path& root) -> std::optional<fs::path> {
        std::error_code ec;
        if (fs::is_directory(root, ec) && !ec) {
            if (!fs::exists(root / "sce_sys" / "param.sfo")) {
                return std::nullopt;
            }
            if (auto eboot_path = root / "eboot.bin"; fs::exists(eboot_path)) {
                return eboot_path;
            }
            return std::nullopt;
        }
        // osl: packed (ZArchive) game images are not supported by the port.
        return std::nullopt;
    };

    // Check if this is the game we're looking for
    if (dir.filename() == game_id) {
        if (auto found = boot_path_for(dir)) {
            return found;
        }
    }

    if (auto found = boot_path_for(dir / game_id)) {
        return found;
    }
    if (auto found = boot_path_for(dir / (game_id + ".zar"))) {
        return found;
    }

    // Recursively search subdirectories
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        std::error_code entry_ec;
        if (!entry.is_directory(entry_ec) || entry_ec) {
            continue;
        }
        if (auto found = FindGameByID(entry.path(), game_id, max_depth - 1)) {
            return found;
        }
    }

    return std::nullopt;
}

} // namespace Common::FS
