// Focused tests of the launcher's self-update: semantic versions, the GitHub release JSON, SHA-256 trust, ZIP and path
// safety, the protected Odin files, the install transaction with its rollback, the update check's states and the
// update configuration. Synthetic files in a temporary folder: no network, no window, no game files.
//   osl-update-tests                      the tests
//   osl-update-tests --check-package ZIP  extracts a release ZIP with the updater's own rules (tools\package.ps1)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <miniz.h>

#include "update_config.h"
#include "updater/github_release.h"
#include "updater/install_transaction.h"
#include "updater/sha256.h"
#include "updater/update_controller.h"
#include "updater/update_job.h"
#include "updater/update_paths.h"
#include "updater/update_platform.h"
#include "updater/update_zip.h"
#include "updater/version.h"

using namespace trg::update;

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition)                                                                                       \
    do {                                                                                                       \
        ++g_checks;                                                                                            \
        if (!(condition)) {                                                                                    \
            ++g_failures;                                                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
        }                                                                                                      \
    } while (0)

class TempDir {
public:
    explicit TempDir(const char* name) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / "osl-update-tests" / (std::string(name) + " " + std::to_string(now));
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::filesystem::path path;
};

void WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string MakeZip(const std::vector<std::pair<std::string, std::string>>& entries) {
    mz_zip_archive archive{};
    if (!mz_zip_writer_init_heap(&archive, 0, 0)) {
        return {};
    }
    for (const auto& [name, content] : entries) {
        if (!mz_zip_writer_add_mem(&archive, name.c_str(), content.data(), content.size(), MZ_BEST_COMPRESSION)) {
            mz_zip_writer_end(&archive);
            return {};
        }
    }
    void* data = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&archive, &data, &size)) {
        mz_zip_writer_end(&archive);
        return {};
    }
    std::string result(static_cast<const char*>(data), size);
    mz_free(data);
    mz_zip_writer_end(&archive);
    return result;
}

class ScopedVariable {
public:
    ScopedVariable(const char* name, const char* value) : name_(name) { Set(value); }
    ~ScopedVariable() { Set(""); }

private:
    void Set(const char* value) {
#ifdef _WIN32
        const std::wstring name(name_.begin(), name_.end());
        const std::string text(value);
        _wputenv_s(name.c_str(), std::wstring(text.begin(), text.end()).c_str());
#else
        if (*value) {
            setenv(name_.c_str(), value, 1);
        } else {
            unsetenv(name_.c_str());
        }
#endif
    }

    std::string name_;
};

std::string ReleaseJson(std::string_view tag, bool draft = false, bool prerelease = false,
                        std::string_view asset_url = "https://github.com/saulob/odin-sphere-leifthrasir-pc/releases/download/x.zip") {
    const std::optional<SemanticVersion> version = ParseVersion(tag);
    const std::string zip = version ? osl::WindowsPackageName(*version) : "x.zip";
    return std::string(R"({"tag_name":")") + std::string(tag) + R"(","draft":)" + (draft ? "true" : "false") +
           R"(,"prerelease":)" + (prerelease ? "true" : "false") +
           R"(,"html_url":"https://github.com/saulob/odin-sphere-leifthrasir-pc/releases/tag/)" + std::string(tag) +
           R"(","assets":[{"name":")" + zip + R"(","browser_download_url":")" + std::string(asset_url) +
           R"(","size":1234,"digest":"sha256:)" + std::string(64, 'a') + R"("}]})";
}

void TestVersions() {
    std::printf("semantic versions\n");
    const auto v010 = ParseVersion("v0.1.0");
    const auto plain = ParseVersion("0.1.0");
    const auto v0110 = ParseVersion("0.1.10");
    const auto v019 = ParseVersion("0.1.9");
    const auto v020 = ParseVersion("0.2.0");
    const auto v100 = ParseVersion("1.0.0");
    CHECK(v010 && plain && v0110 && v019 && v020 && v100);
    CHECK(*v010 == *plain);
    CHECK(*v019 < *v0110);
    CHECK(!(*v0110 < *v019));
    CHECK(*v010 < *v0110);
    CHECK(*v0110 < *v020);
    CHECK(*v020 < *v100);
    CHECK(v010->CoreString() == "0.1.0");
    CHECK(ParseVersion("V0.1.0").has_value());

    const auto rc1 = ParseVersion("0.2.0-rc.1");
    const auto rc2 = ParseVersion("0.2.0-rc.2");
    const auto rc10 = ParseVersion("0.2.0-rc.10");
    const auto alpha = ParseVersion("0.2.0-alpha");
    CHECK(rc1 && rc2 && rc10 && alpha);
    CHECK(*rc1 < *rc2);
    CHECK(*rc2 < *rc10);
    CHECK(*alpha < *rc1);
    CHECK(*rc10 < *v020);
    CHECK(*v0110 < *rc1);
    CHECK(!rc1->stable());
    const auto build = ParseVersion("0.1.0+abc");
    CHECK(build && SemanticVersion::Compare(*build, *v010) == 0);

    for (const char* bad : {"1.0", "1.0.0.0", "01.0.0", "1.0.0-", "v", "", "1.x.0", "1.0.0-01"}) {
        CHECK(!ParseVersion(bad));
    }
    const auto port = ParseVersion(OSL_PORT_VERSION);
    CHECK(port && port->stable());
}

void TestReleases() {
    std::printf("release JSON\n");
    std::string error;
    const std::optional<Release> release = ParseRelease(ReleaseJson("v0.2.0"), &error);
    CHECK(release && error.empty());
    if (release) {
        CHECK(release->version.CoreString() == "0.2.0");
        CHECK(IsInstallableRelease(*release));
        const ReleaseAsset* asset = FindAsset(*release, "OdinSphereLeifthrasir-v0.2.0-windows-x64.zip");
        CHECK(asset && asset->size == 1234 && asset->sha256() == std::string(64, 'a'));
        CHECK(!FindAsset(*release, "OdinSphereLeifthrasir-v0.2.1-windows-x64.zip"));
        CHECK(!FindAsset(*release, "OdinSphereLeifthrasir-v0.2.0-windows-x64.sha256"));
    }
    const std::optional<Release> draft = ParseRelease(ReleaseJson("v0.2.0", true));
    CHECK(draft && !IsInstallableRelease(*draft));
    const std::optional<Release> prerelease = ParseRelease(ReleaseJson("v0.2.0", false, true));
    CHECK(prerelease && !IsInstallableRelease(*prerelease));
    const std::optional<Release> prerelease_tag = ParseRelease(ReleaseJson("v0.2.0-rc.1"));
    CHECK(prerelease_tag && !IsInstallableRelease(*prerelease_tag));
    const std::optional<Release> no_assets = ParseRelease(R"({"tag_name":"v0.2.0"})");
    CHECK(no_assets && no_assets->assets.empty());

    CHECK(!ParseRelease(R"({"message":"Not Found","documentation_url":"https://docs.github.com"})"));
    CHECK(!ParseRelease(R"({"tag_name": 42})"));
    CHECK(!ParseRelease(R"({"tag_name": "latest"})"));
    CHECK(!ParseRelease("{"));
    CHECK(!ParseRelease(R"({"tag_name":"v0.2.0","assets":{}})"));
    CHECK(!ParseRelease(R"({"tag_name":"v0.2.0","assets":[{"name":"x"}]})"));
    CHECK(!ParseRelease(R"({"tag_name":"v0.2.0","assets":[{"name":"x","browser_download_url":"https://x","size":-1}]})"));
}

void TestChecksums() {
    std::printf("SHA-256 trust\n");
    CHECK(Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256Hex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    CHECK(ParseSha256Digest("sha256:abc") == std::nullopt);
    CHECK(ParseSha256Digest("md5:" + std::string(32, 'a')) == std::nullopt);
    CHECK(ParseSha256Digest("SHA256:" + std::string(64, 'A')) == std::string(64, 'a'));
    CHECK(ParseSha256File(std::string(64, 'B') + "  OdinSphereLeifthrasir-v0.2.0-windows-x64.zip\n") ==
          std::string(64, 'b'));
    CHECK(ParseSha256File("not-a-hash  file.zip") == std::nullopt);

    // The .sha256 asset is trusted only when its own published digest matches its content.
    const std::string checksum_file = std::string(64, 'c') + "  OdinSphereLeifthrasir-v0.2.0-windows-x64.zip\n";
    ReleaseAsset checksum_asset;
    std::string error;
    CHECK(TrustedChecksum(checksum_file, checksum_asset, error) == std::string(64, 'c'));
    checksum_asset.digest = "sha256:" + Sha256Hex(checksum_file);
    CHECK(TrustedChecksum(checksum_file, checksum_asset, error) == std::string(64, 'c'));
    checksum_asset.digest = "sha256:" + std::string(64, '0');
    CHECK(!TrustedChecksum(checksum_file, checksum_asset, error));
    CHECK(error == "The published checksum file does not match its own digest.");
    checksum_asset.digest.clear();
    CHECK(!TrustedChecksum("garbage", checksum_asset, error));

    TempDir temp("checksums");
    const std::filesystem::path download = temp.path / "update.zip";
    WriteFile(download, "abc");
    const std::string abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    CHECK(VerifyDownload(download, 3, abc, error));
    CHECK(!VerifyDownload(download, 4, abc, error));
    CHECK(error == "The update download is incomplete.");
    CHECK(!VerifyDownload(download, 3, std::string(64, 'a'), error));
    CHECK(error == "The downloaded update does not match its SHA-256 checksum.");
    CHECK(!VerifyDownload(temp.path / "missing.zip", 3, abc, error));
}

void TestPaths() {
    std::printf("path safety and protected Odin files\n");
    for (const char* safe : {"osl-loader.exe", "trg-updater.exe", "SDL3.dll", "assets/launcher/header-art.png",
                             "./osl-loader.exe", "licenses/THIRD-PARTY-LICENSES.txt"}) {
        CHECK(IsSafeRelativePath(safe));
    }
    for (const char* unsafe : {"../osl-loader.exe", "/absolute", "\\absolute", "C:/osl-loader.exe", "C:osl-loader.exe",
                               "\\\\server\\share\\x", "a//b", "a/./../b", "a\\..\\b", "con", "aux.txt", "lpt1",
                               "a/b.", "a/b ", "a|b", "a\x01" "b"}) {
        CHECK(!IsSafeRelativePath(unsafe));
    }
    std::string normalized;
    CHECK(NormalizeRelativePath("assets\\launcher\\header-art.png", normalized) &&
          normalized == "assets/launcher/header-art.png");
    CHECK(NormalizeRelativePath("USER\\osl.ini", normalized) && IsProtectedRelativePath(normalized));

    for (const char* player : {"game/prepared/CUSA05083-1.00/eboot.bin", "game/image/CUSA05083-1.00/boot.bin",
                               "game/prepared/CUSA05083-1.00.install-backup/x", "user/osl.ini", "osl.ini",
                               "user/savedata/CUSA05083/SAVEDATA00/data", "user/trophy/CUSA05083/progress_user.txt",
                               "user/keys.json", "user/cache/CUSA05083/shader", "logs/odin_sphere_leifthrasir_001.log",
                               "cache/x", "screenshots/a.png", "OdinSphere.pkg", "UP0177-CUSA05083.PKG",
                               "sce_sys/param.sfo", "Odin2_BT_UK.CPK", "crash.dmp", "osl-loader.pdb",
                               "osl-loader.debug", "update.zip", "OdinSphereLeifthrasir-v0.1.0-x86_64.AppImage",
                               "osl.ini.bak", "x.tmp", "download.part"}) {
        if (!IsProtectedRelativePath(player)) {
            std::printf("  not protected: %s\n", player);
        }
        CHECK(IsProtectedRelativePath(player));
    }
    for (const char* release : {"osl-loader.exe", "trg-updater.exe", "SDL3.dll", "libc++.dll", "README.txt",
                                "LICENSE.txt", "assets/launcher/header-art.png", "licenses/THIRD-PARTY-LICENSES.txt",
                                "licenses/MSYS2-PACKAGES.txt", "licenses/shadPS4/LICENSE", "licenses/NotoSansTC/LICENSE-NotoSansTC.txt"}) {
        CHECK(!IsProtectedRelativePath(release));
    }
}

void TestZip() {
    std::printf("ZIP extraction and staging\n");
    TempDir temp("zip");
    const std::filesystem::path archive = temp.path / "release.zip";
    WriteFile(archive, MakeZip({{"osl-loader.exe", "PUBLIC_RELEASE"},
                                {"trg-updater.exe", "helper"},
                                {"assets/launcher/header-art.png", "art"},
                                {"licenses/THIRD-PARTY-LICENSES.txt", "licences"}}));
    const std::filesystem::path staging = temp.path / "staging";
    std::string error;
    CHECK(ExtractZip(archive, staging, {}, error));
    CHECK(ReadFile(staging / "osl-loader.exe") == "PUBLIC_RELEASE");
    CHECK(ReadFile(staging / "assets" / "launcher" / "header-art.png") == "art");
    CHECK(ValidateStaging(staging, {"osl-loader.exe", "trg-updater.exe"}, error));
    std::vector<std::string> files;
    for (const std::string& file : ListStagingFiles(staging, error)) {
        std::string normalized;
        CHECK(NormalizeRelativePath(file, normalized));
        files.push_back(normalized);
    }
    CHECK(files == std::vector<std::string>({"assets/launcher/header-art.png", "licenses/THIRD-PARTY-LICENSES.txt",
                                             "osl-loader.exe", "trg-updater.exe"}));
    CHECK(!ValidateStaging(staging, {"osl-loader.exe", "missing.exe"}, error));
    for (const char* leftover : {"user/osl.ini", "game/prepared/CUSA05083-1.00/eboot.bin", "logs/a.log"}) {
        WriteFile(staging / leftover, "player");
        CHECK(!ValidateStaging(staging, {"osl-loader.exe"}, error));
        std::filesystem::remove(staging / leftover);
    }

    const std::vector<std::string> unsafe_names = {
        "../evil", "C:/evil", "C:\\evil", "\\\\server\\share\\evil", "a/../../evil", "a\\..\\..\\evil", "a//b",
        "C:evil", "con", "nul.txt", "a/b.", "user/osl.ini", "osl.ini", "game/prepared/x/eboot.bin",
        "user/savedata/x", "logs/a.log", "OdinSphere.pkg", "sce_sys/param.sfo"};
    int rejected = 0;
    for (const std::string& name : unsafe_names) {
        const std::string bytes = MakeZip({{"osl-loader.exe", "ok"}, {name, "payload"}});
        CHECK(!bytes.empty());
        WriteFile(temp.path / "unsafe.zip", bytes);
        const std::filesystem::path unsafe_staging = temp.path / "unsafe staging";
        std::filesystem::remove_all(unsafe_staging);
        if (!ExtractZip(temp.path / "unsafe.zip", unsafe_staging, {}, error)) {
            ++rejected;
        } else {
            std::printf("  accepted %s\n", name.c_str());
        }
        CHECK(!std::filesystem::exists(temp.path / "evil"));
    }
    CHECK(rejected == static_cast<int>(unsafe_names.size()));

#ifdef _WIN32
    WriteFile(temp.path / "duplicate.zip", MakeZip({{"osl-loader.exe", "a"}, {"OSL-LOADER.EXE", "b"}}));
    CHECK(!ExtractZip(temp.path / "duplicate.zip", temp.path / "duplicate", {}, error));
#endif
    WriteFile(temp.path / "twice.zip", MakeZip({{"osl-loader.exe", "a"}, {"./osl-loader.exe", "b"}}));
    CHECK(!ExtractZip(temp.path / "twice.zip", temp.path / "twice", {}, error));

    CHECK(IsSymlinkZipEntry(0120000u << 16));
    CHECK(!IsSymlinkZipEntry(0100644u << 16));
    CHECK(!IsSymlinkZipEntry(0));

    ZipLimits small;
    small.max_total_bytes = 8;
    WriteFile(temp.path / "large.zip", MakeZip({{"osl-loader.exe", std::string(64, 'x')}}));
    CHECK(!ExtractZip(temp.path / "large.zip", temp.path / "large", small, error));
    ZipLimits tiny_archive;
    tiny_archive.max_archive_bytes = 16;
    CHECK(!ExtractZip(archive, temp.path / "tiny", tiny_archive, error));
    ZipLimits few;
    few.max_entries = 2;
    CHECK(!ExtractZip(archive, temp.path / "few", few, error));
    WriteFile(temp.path / "garbage.zip", "not a zip");
    CHECK(!ExtractZip(temp.path / "garbage.zip", temp.path / "garbage", {}, error));
}

void TestJobs() {
    std::printf("update jobs\n");
    TempDir temp("jobs");
    UpdateJob job;
    job.parent_pid = 1234;
    job.install_dir = temp.path / "install dir";
    job.staging_dir = temp.path / "workspace" / "staging";
    job.backup_dir = temp.path / "workspace" / "backup";
    job.manifest_file = temp.path / "workspace" / "manifest.txt";
    job.restart_executable = job.install_dir / "osl-loader.exe";
    job.log_file = temp.path / "workspace" / "trg-updater.log";
    job.restart_arguments = {"--user", "C:\\with space\\", "", "--launcher"};
    std::filesystem::create_directories(job.install_dir);
    std::filesystem::create_directories(job.staging_dir);
    WriteFile(job.manifest_file, "osl-loader.exe\n");

    std::string error;
    const std::filesystem::path file = temp.path / "workspace" / "job.txt";
    CHECK(WriteUpdateJob(file, job, error));
    const std::optional<UpdateJob> read = ReadUpdateJob(file, error);
    CHECK(read && read->parent_pid == 1234 && read->install_dir == job.install_dir &&
          read->restart_arguments == job.restart_arguments);
    CHECK(ValidateInstallJob(job, error));

    UpdateJob inside = job;
    inside.staging_dir = job.install_dir / "staging";
    std::filesystem::create_directories(inside.staging_dir);
    CHECK(!ValidateInstallJob(inside, error));
    UpdateJob outside = job;
    outside.restart_executable = temp.path / "osl-loader.exe";
    CHECK(!ValidateInstallJob(outside, error));
    UpdateJob newline = job;
    newline.restart_arguments = {"bad\nargument"};
    CHECK(!WriteUpdateJob(file, newline, error));
    WriteFile(file, "format=2\n");
    CHECK(!ReadUpdateJob(file, error));
}

struct Installation {
    explicit Installation(const std::filesystem::path& root)
        : install(root / "installed"), staging(root / "workspace" / "staging"), backup(root / "workspace" / "backup"),
          manifest(root / "workspace" / "manifest.txt") {
        WriteFile(install / "osl-loader.exe", "DEVELOPMENT_BUILD");
        WriteFile(install / "trg-updater.exe", "old helper");
        WriteFile(install / "SDL3.dll", "old sdl");
        for (const auto& [relative, content] : player_files) {
            WriteFile(install / relative, content);
        }
        std::filesystem::create_directories(staging);
    }

    void Stage() const {
        WriteFile(staging / "osl-loader.exe", "PUBLIC_RELEASE");
        WriteFile(staging / "trg-updater.exe", "new helper");
        WriteFile(staging / "SDL3.dll", "new sdl");
        WriteFile(staging / "assets" / "launcher" / "header-art.png", "new art");
        WriteFile(manifest, "assets/launcher/header-art.png\nosl-loader.exe\nSDL3.dll\ntrg-updater.exe\n");
    }

    UpdateJob Job() const {
        UpdateJob job;
        job.parent_pid = 1234;
        job.install_dir = install;
        job.staging_dir = staging;
        job.backup_dir = backup;
        job.manifest_file = manifest;
        job.restart_executable = install / "osl-loader.exe";
        job.log_file = manifest.parent_path() / "trg-updater.log";
        return job;
    }

    void CheckPlayerFiles() const {
        for (const auto& [relative, content] : player_files) {
            CHECK(ReadFile(install / relative) == content);
        }
    }

    std::filesystem::path install, staging, backup, manifest;
    const std::vector<std::pair<std::string, std::string>> player_files = {
        {"user/osl.ini", "render_resolution=1440p\n"},
        {"user/savedata/CUSA05083/SAVEDATA00/data", "save"},
        {"user/trophy/CUSA05083/progress_user.txt", "trophies"},
        {"user/keys.json", "{}"},
        {"game/prepared/CUSA05083-1.00/eboot.bin", "game"},
        {"game/image/CUSA05083-1.00/boot.bin", "image"},
        {"logs/odin_sphere_leifthrasir_001.log", "log"},
        {"notes.txt", "unrelated"},
    };
};

void TestInstallTransaction() {
    std::printf("install transaction: a public release replaces a development build\n");
    TempDir temp("install");
    const Installation installation(temp.path);
    installation.Stage();
    std::string log;
    InstallOptions options;
    options.log = [&log](std::string_view text) { log += std::string(text) + "\n"; };
    const InstallOutcome outcome = InstallFiles(installation.Job(), options, false);
    CHECK(outcome.status == InstallStatus::kSuccess);
    CHECK(ReadFile(installation.install / "osl-loader.exe") == "PUBLIC_RELEASE");
    CHECK(ReadFile(installation.install / "trg-updater.exe") == "new helper");
    CHECK(ReadFile(installation.install / "SDL3.dll") == "new sdl");
    CHECK(ReadFile(installation.install / "assets" / "launcher" / "header-art.png") == "new art");
    installation.CheckPlayerFiles();
    CHECK(log.find("manifest entries: 4\n") != std::string::npos);
    CHECK(log.find("backup complete\n") != std::string::npos);
    CHECK(log.find("verification complete\n") != std::string::npos);
    CHECK(!std::filesystem::exists(installation.backup));
    CHECK(!std::filesystem::exists(installation.staging));

    TempDir protected_temp("protected manifest");
    const Installation guarded(protected_temp.path);
    guarded.Stage();
    WriteFile(guarded.staging / "user" / "osl.ini", "release defaults");
    WriteFile(guarded.manifest, "osl-loader.exe\nuser/osl.ini\n");
    CHECK(InstallFiles(guarded.Job(), {}, false).status == InstallStatus::kInvalidJob);
    CHECK(ReadFile(guarded.install / "osl-loader.exe") == "DEVELOPMENT_BUILD");
    guarded.CheckPlayerFiles();

    WriteFile(guarded.manifest, "SDL3.dll\n");
    CHECK(InstallFiles(guarded.Job(), {}, false).status == InstallStatus::kInvalidJob);
}

void TestRollback() {
    std::printf("install transaction: a failed replacement is rolled back\n");
    TempDir temp("rollback");
    const Installation installation(temp.path);
    installation.Stage();
    int replacements = 0;
    InstallOptions failing;
    failing.replace = [&replacements](const std::filesystem::path& from, const std::filesystem::path& to) {
        if (++replacements == 3) {
            return false;
        }
        std::error_code ec;
        std::filesystem::rename(from, to, ec);
        return !ec;
    };
    std::string log;
    failing.log = [&log](std::string_view text) { log += std::string(text) + "\n"; };
    const InstallOutcome outcome = InstallFiles(installation.Job(), failing, false);
    CHECK(outcome.status == InstallStatus::kInstallFailedRolledBack);
    CHECK(ReadFile(installation.install / "osl-loader.exe") == "DEVELOPMENT_BUILD");
    CHECK(ReadFile(installation.install / "trg-updater.exe") == "old helper");
    CHECK(ReadFile(installation.install / "SDL3.dll") == "old sdl");
    CHECK(!std::filesystem::exists(installation.install / "assets" / "launcher" / "header-art.png"));
    installation.CheckPlayerFiles();
    CHECK(log.find("rollback complete\n") != std::string::npos);
    CHECK(log.find("replacement complete") == std::string::npos);
}

// Polls a check to its end, as the launcher does every frame.
const UpdateSnapshot& Settle(UpdateController& controller) {
    for (int i = 0; i < 500 && (controller.Busy() || controller.snapshot().state == UpdateState::kChecking); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        controller.Poll();
    }
    controller.Poll();
    return controller.snapshot();
}

UpdateConfig CheckConfig(const std::filesystem::path& install, const std::filesystem::path& release_json,
                         std::string current_version) {
    UpdateConfig config = osl::MakeUpdateConfig(install, current_version);
    config.release_json_override = release_json;
    config.log = nullptr;
    return config;
}

void TestUpdateCheck() {
    std::printf("update check states\n");
    TempDir temp("check");
    const std::filesystem::path json = temp.path / "release.json";

    // No public release (GitHub answers 404), a private repository or no network: nothing is shown.
    {
        UpdateController controller(CheckConfig(temp.path, temp.path / "missing.json", "0.1.0"));
        controller.StartCheck();
        const UpdateSnapshot& snapshot = Settle(controller);
        CHECK(snapshot.state == UpdateState::kIdle);
        CHECK(!ShowsUpdateBanner(snapshot));
    }
    for (const std::string& body : {std::string(R"({"message":"Not Found"})"), ReleaseJson("v0.2.0", false, false, "http://insecure/x.zip"),
                                    std::string(R"({"tag_name":"v0.2.0","assets":[]})")}) {
        WriteFile(json, body);
        UpdateController controller(CheckConfig(temp.path, json, "0.1.0"));
        controller.StartCheck();
        CHECK(!ShowsUpdateBanner(Settle(controller)));
    }
    // A check the player asked for reports its failure, with RETRY.
    {
        UpdateController controller(CheckConfig(temp.path, temp.path / "missing.json", "0.1.0"));
        controller.StartCheck(true);
        const UpdateSnapshot& snapshot = Settle(controller);
        CHECK(snapshot.state == UpdateState::kFailed && snapshot.retry_available && ShowsUpdateBanner(snapshot));
    }
    const auto state_for = [&](const std::string& body, const std::string& current) {
        WriteFile(json, body);
        UpdateController controller(CheckConfig(temp.path, json, current));
        controller.StartCheck();
        return Settle(controller);
    };
    CHECK(state_for(ReleaseJson("v0.1.0"), "0.1.0").state == UpdateState::kNoUpdate);
    CHECK(state_for(ReleaseJson("v0.0.9"), "0.1.0").state == UpdateState::kNoUpdate);
    CHECK(state_for(ReleaseJson("v0.2.0", true), "0.1.0").state == UpdateState::kNoUpdate);
    CHECK(state_for(ReleaseJson("v0.2.0", false, true), "0.1.0").state == UpdateState::kNoUpdate);
    CHECK(state_for(ReleaseJson("v0.2.0-rc.1"), "0.1.0").state == UpdateState::kNoUpdate);
    CHECK(state_for(ReleaseJson("v0.1.10"), "0.1.9").state == UpdateState::kAvailable);
    const UpdateSnapshot available = state_for(ReleaseJson("v0.2.0"), "0.1.0");
    CHECK(available.state == UpdateState::kAvailable && ShowsUpdateBanner(available));
    CHECK(available.latest_version == "0.2.0" && available.current_version == "0.1.0");
    CHECK(available.asset_name == "OdinSphereLeifthrasir-v0.2.0-windows-x64.zip");
    CHECK(available.release_url == "https://github.com/saulob/odin-sphere-leifthrasir-pc/releases/tag/v0.2.0");

    // An invalid local version disables the check instead of offering anything.
    UpdateController invalid(CheckConfig(temp.path, json, "dev"));
    invalid.StartCheck();
    CHECK(Settle(invalid).state == UpdateState::kFailed);
}

void TestSameVersionDeveloperTransaction() {
    std::printf("a test-reported older version installs the public release of the same number\n");
    TempDir temp("same version");
    const std::filesystem::path json = temp.path / "release.json";
    WriteFile(json, ReleaseJson(std::string("v") + OSL_PORT_VERSION));
    UpdateController controller(CheckConfig(temp.path, json, "0.0.9"));
    controller.StartCheck();
    const UpdateSnapshot& snapshot = Settle(controller);
    CHECK(snapshot.state == UpdateState::kAvailable);
    CHECK(snapshot.latest_version == OSL_PORT_VERSION);

    // Once accepted, nothing compares the versions again: the transaction replaces the same-numbered build.
    const Installation installation(temp.path / "files");
    installation.Stage();
    CHECK(InstallFiles(installation.Job(), {}, false).status == InstallStatus::kSuccess);
    CHECK(ReadFile(installation.install / "osl-loader.exe") == "PUBLIC_RELEASE");
    installation.CheckPlayerFiles();
}

void TestConfig() {
    std::printf("update configuration\n");
    const SemanticVersion version = *ParseVersion("0.2.0");
    CHECK(osl::WindowsPackageName(version) == "OdinSphereLeifthrasir-v0.2.0-windows-x64.zip");
    CHECK(osl::WindowsChecksumName(version) == "OdinSphereLeifthrasir-v0.2.0-windows-x64.sha256");
    CHECK(osl::LinuxPackageName(version) == "OdinSphereLeifthrasir-v0.2.0-x86_64.AppImage");
    CHECK(osl::LinuxChecksumName(version) == "OdinSphereLeifthrasir-v0.2.0-x86_64.sha256");
    CHECK(std::string(osl::kReleasesUrl) == "https://api.github.com/repos/saulob/odin-sphere-leifthrasir-pc/releases/latest");

    CHECK(osl::UpdateRestartArguments({}) == std::vector<std::string>({"--launcher"}));
    CHECK(osl::UpdateRestartArguments({"--osl-skip-launcher", "--user", "D:\\Odin data\\user", "--launcher",
                                       "--log", "run.log", ""}) ==
          std::vector<std::string>({"--user", "D:\\Odin data\\user", "--log", "run.log", "", "--launcher"}));
    CHECK(osl::UpdateRestartArguments({"boot.bin", "--osl-skip-launcher-not", "--cpu-only"}) ==
          std::vector<std::string>({"boot.bin", "--osl-skip-launcher-not", "--cpu-only", "--launcher"}));

    TempDir temp("config");
    const std::filesystem::path release = temp.path / "Odin Sphere Leifthrasir PC";
    WriteFile(release / "osl-loader.exe", "program");
    UpdateConfig without_helper = osl::MakeUpdateConfig(release, "0.1.0");
#ifdef _WIN32
    CHECK(!without_helper.install_supported && !without_helper.install_unsupported_reason.empty());
    WriteFile(release / "trg-updater.exe", "helper");
    const UpdateConfig config = osl::MakeUpdateConfig(release, "0.1.0");
    CHECK(config.install_supported);
    CHECK(config.package_kind == UpdatePackageKind::kZip);
    CHECK(config.restart_executable == release / "osl-loader.exe");
    CHECK(config.required_staged_files == std::vector<std::string>({"osl-loader.exe", "trg-updater.exe"}));
    CHECK(config.asset_name(version) == "OdinSphereLeifthrasir-v0.2.0-windows-x64.zip");
    CHECK(config.checksum_asset_name(version) == "OdinSphereLeifthrasir-v0.2.0-windows-x64.sha256");
    const std::filesystem::path build_tree = temp.path / "repo" / "build" / "cmake" / "clang64" / "bin";
    WriteFile(build_tree / "trg-updater.exe", "helper");
    CHECK(!osl::MakeUpdateConfig(build_tree, "0.1.0").install_supported);
#else
    CHECK(without_helper.package_kind == UpdatePackageKind::kAppImage);
#endif
    CHECK(without_helper.user_agent == "OdinSphereLeifthrasirPC-Updater/0.1.0");
    CHECK(without_helper.current_version == "0.1.0");
    CHECK(!without_helper.restart_arguments.empty() && without_helper.restart_arguments.back() == "--launcher");

    // The overrides do nothing without the switch, and the switch needs the value 1.
    {
        ScopedVariable current(kUpdateTestCurrentVersion, "0.0.1");
        ScopedVariable json(kUpdateTestReleaseJson, "release.json");
        ScopedVariable hash(kUpdateTestAssetSha256, "abc");
        UpdateConfig inert = osl::MakeUpdateConfig(release, "0.1.0");
        CHECK(inert.current_version == "0.1.0" && !inert.release_json_override && !inert.sha256_override);
        {
            ScopedVariable wrong(kUpdateTestSwitch, "yes");
            inert = osl::MakeUpdateConfig(release, "0.1.0");
            CHECK(inert.current_version == "0.1.0" && !inert.release_json_override && !inert.sha256_override);
        }
        ScopedVariable enabled(kUpdateTestSwitch, "1");
        const UpdateConfig test = osl::MakeUpdateConfig(release, "0.1.0");
        CHECK(test.current_version == "0.0.1");
        CHECK(test.release_json_override && *test.release_json_override == "release.json");
        CHECK(test.sha256_override == "abc");
        CHECK(test.releases_url == osl::kReleasesUrl);
    }
    CHECK(osl::MakeUpdateConfig(release, "0.1.0").current_version == "0.1.0");
}

void TestVersionSource() {
    std::printf("one version source\n");
    const std::string cmake = ReadFile(std::filesystem::path(OSL_SOURCE_DIR) / "CMakeLists.txt");
    CHECK(cmake.find(std::string("set(OSL_PORT_VERSION \"") + OSL_PORT_VERSION + "\")") != std::string::npos);
    const std::string package = ReadFile(std::filesystem::path(OSL_SOURCE_DIR) / "tools" / "package.ps1");
    CHECK(package.find("OdinSphereLeifthrasir-v$version-windows-x64") != std::string::npos);
    CHECK(package.find("set\\(OSL_PORT_VERSION") != std::string::npos);
    CHECK(package.find("trg-updater.exe") != std::string::npos);
    const std::string launcher = ReadFile(std::filesystem::path(OSL_SOURCE_DIR) / "launcher" / "launcher.cpp");
    CHECK(launcher.find("{\"v\" OSL_PORT_VERSION \"   \\xC2\\xB7   \", \"\"}") != std::string::npos);
}

void TestAppImageTarget() {
    std::printf("only the AppImage the program was started from is replaced\n");
    TempDir temp("appimage");
    const std::filesystem::path appimage = temp.path / "OdinSphereLeifthrasir-v0.1.0-x86_64.AppImage";
    WriteFile(appimage, "appimage");
    const std::filesystem::path mount = temp.path / "mount";
    const std::filesystem::path program = mount / "usr" / "bin" / "osl-loader";
    WriteFile(program, "program");
    CHECK(IsRunningAppImage(appimage, mount, program));
    const std::filesystem::path extracted = temp.path / "squashfs-root";
    WriteFile(extracted / "usr" / "bin" / "osl-loader", "program");
    CHECK(IsRunningAppImage(appimage, extracted, extracted / "usr" / "bin" / "osl-loader"));
    WriteFile(temp.path / "build" / "osl-loader", "program");
    CHECK(!IsRunningAppImage(appimage, mount, temp.path / "build" / "osl-loader"));
    WriteFile(temp.path / "Other-v1.0.0-x86_64.AppImage", "other");
    CHECK(!IsRunningAppImage(temp.path / "Other-v1.0.0-x86_64.AppImage", mount, program));
    WriteFile(temp.path / "OdinSphereLeifthrasir-vx-x86_64.AppImage", "other");
    CHECK(!IsRunningAppImage(temp.path / "OdinSphereLeifthrasir-vx-x86_64.AppImage", mount, program));
    WriteFile(mount / "OdinSphereLeifthrasir-v0.1.0-x86_64.AppImage", "inside");
    CHECK(!IsRunningAppImage(mount / "OdinSphereLeifthrasir-v0.1.0-x86_64.AppImage", mount, program));
    CHECK(!IsRunningAppImage({}, mount, program));
}

// The release ZIP through the updater's own extraction and staging rules: what an installed release will accept.
int CheckPackage(const std::filesystem::path& zip) {
    TempDir temp("package");
    std::string error;
    if (!ExtractZip(zip, temp.path / "staging", {}, error) ||
        !ValidateStaging(temp.path / "staging", {osl::kLoaderExecutable, osl::kUpdaterExecutable}, error)) {
        std::printf("The updater would refuse %s: %s\n", zip.string().c_str(), error.c_str());
        return 1;
    }
    const std::vector<std::string> files = ListStagingFiles(temp.path / "staging", error);
    if (!error.empty() || files.empty()) {
        std::printf("The updater would refuse %s: %s\n", zip.string().c_str(), error.c_str());
        return 1;
    }
    std::printf("The updater accepts %s (%zu files)\n", zip.string().c_str(), files.size());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 3 && std::string_view(argv[1]) == "--check-package") {
        return CheckPackage(argv[2]);
    }
    TestVersions();
    TestReleases();
    TestChecksums();
    TestPaths();
    TestZip();
    TestJobs();
    TestInstallTransaction();
    TestRollback();
    TestUpdateCheck();
    TestSameVersionDeveloperTransaction();
    TestConfig();
    TestVersionSource();
    TestAppImageTarget();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
