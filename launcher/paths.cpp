#include "paths.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <cstdlib>
#include <cstring>

#include "osl_launcher.h"
#include "game_files.h"
#include "utf8.h"

namespace fs = std::filesystem;

namespace osl {
namespace {

struct Defaults {
    GameRoots roots;
    fs::path user_dir;
    fs::path logs_dir;
};

Defaults DefaultsFor(const fs::path& exe_dir) {
    if (IsBuildTreeExecutableDir(exe_dir)) {
        const fs::path build = exe_dir.parent_path().parent_path().parent_path();
        const fs::path root = build.parent_path();
        return {{root / "game" / "prepared", build / "image"}, root / "user", build / "logs"};
    }
    return {{exe_dir / "game" / "prepared", exe_dir / "game" / "image"}, exe_dir / "user", exe_dir / "logs"};
}

fs::path OrDefault(const std::string& given, const fs::path& fallback) {
    return given.empty() ? fallback : PathFromUtf8(given);
}

bool Copy(char (&out)[4096], const fs::path& path) {
    const std::string text = Utf8(path);
    if (text.size() >= sizeof(out)) {
        return false;
    }
    std::memcpy(out, text.c_str(), text.size() + 1);
    return true;
}

} // namespace

bool IsBuildTreeExecutableDir(const fs::path& exe_dir) {
    const fs::path build = exe_dir.parent_path().parent_path().parent_path();
    return exe_dir.filename() == "bin" && exe_dir.parent_path().parent_path().filename() == "cmake" &&
           build.filename() == "build";
}

Paths ResolvePaths(const PathRequest& request, const fs::path& exe_dir) {
    const Defaults defaults = DefaultsFor(exe_dir);
    const GamePaths active = ActiveBuildPaths(defaults.roots);
    Paths paths;
    if (request.boot.empty()) {
        paths.image_dir = active.image_dir;
        paths.boot = paths.image_dir / "boot.bin";
    } else {
        paths.boot = PathFromUtf8(request.boot);
        paths.image_dir = paths.boot.has_parent_path() ? paths.boot.parent_path() : fs::path(".");
    }
    paths.app0 = OrDefault(request.app0, active.app0);
    paths.user_dir = OrDefault(request.user_dir, defaults.user_dir);
    paths.logs_dir = OrDefault(request.logs_dir, defaults.logs_dir);
    return paths;
}

} // namespace osl

namespace osl {
namespace {

#ifdef _WIN32
bool ExecutableDirectory(fs::path& dir) {
    std::wstring module(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0 || length >= module.size()) {
        return false;
    }
    dir = fs::path(module.substr(0, length)).parent_path();
    return true;
}
#else
bool ExecutableDirectory(fs::path& dir) {
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec || !exe.has_parent_path()) {
        return false;
    }
    dir = exe.parent_path();
    return true;
}

bool Writable(const fs::path& dir) { return access(dir.c_str(), W_OK | X_OK) == 0; }

// The portable folder (game, user, logs): OSL_PORTABLE_DIR, else the folder holding the AppImage (the AppImage's own
// mount is read-only), else next to the executable. An AppImage in a read-only folder keeps its data under
// $XDG_DATA_HOME/OdinSphereLeifthrasirPC.
fs::path PortableRoot(const fs::path& exe_dir) {
    if (const char* given = std::getenv("OSL_PORTABLE_DIR"); given && *given) {
        return PathFromUtf8(given);
    }
    const char* image = std::getenv("APPIMAGE");
    if (!image || !*image) {
        return exe_dir;
    }
    const fs::path beside = fs::path(PathFromUtf8(image)).parent_path();
    if (Writable(beside)) {
        return beside;
    }
    const char* data = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    const fs::path base = data && *data ? fs::path(data) : fs::path(home ? home : ".") / ".local" / "share";
    return base / "OdinSphereLeifthrasirPC";
}
#endif

} // namespace
} // namespace osl

extern "C" int osl_logs_directory(const char* logs_dir, const char* log_file, char* out, size_t size) {
    if (!out || size == 0) {
        return 0;
    }
    fs::path directory = logs_dir && *logs_dir ? osl::PathFromUtf8(logs_dir) : fs::path();
    if (log_file && *log_file) {
        std::error_code ec;
        const fs::path file = osl::PathFromUtf8(log_file);
        const fs::path absolute = fs::absolute(file, ec);
        directory = (ec ? file : absolute).parent_path();
    }
    if (directory.empty()) {
        return 0;
    }
    const std::string text = osl::Utf8(directory);
    if (text.size() >= size) {
        return 0;
    }
    std::memcpy(out, text.c_str(), text.size() + 1);
    return 1;
}

extern "C" int osl_resolve_paths(const OslPathRequest* request, OslPaths* out) {
    fs::path exe_dir;
    if (!osl::ExecutableDirectory(exe_dir)) {
        return 0;
    }
#ifndef _WIN32
    exe_dir = osl::PortableRoot(exe_dir);
#endif
    const auto text = [](const char* value) { return std::string(value ? value : ""); };
    const osl::PathRequest asked{text(request->boot), text(request->app0), text(request->user_dir),
                                 text(request->logs_dir)};
    const osl::Paths paths = osl::ResolvePaths(asked, exe_dir);
    return osl::Copy(out->boot, paths.boot) && osl::Copy(out->app0, paths.app0) &&
           osl::Copy(out->image_dir, paths.image_dir) && osl::Copy(out->user_dir, paths.user_dir) &&
           osl::Copy(out->logs_dir, paths.logs_dir);
}
