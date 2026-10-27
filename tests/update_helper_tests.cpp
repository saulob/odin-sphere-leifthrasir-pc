// The real trg-updater.exe copied alone into a temporary folder: its imports, a start without the project's DLLs on
// PATH, the parent wait, the replacement, the restart with quoted arguments and without the update test variables,
// and the rollback when the restart fails. Paths contain spaces. No window opens: the helper and the marker that stands
// in for odin_sphere_leifthrasir.exe are GUI-subsystem programs that create none.
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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

constexpr const wchar_t* kTestVariables[] = {L"ODIN_UPDATE_TEST", L"ODIN_UPDATE_TEST_CURRENT_VERSION",
                                             L"ODIN_UPDATE_TEST_RELEASE_JSON", L"ODIN_UPDATE_TEST_ASSET_SHA256"};
constexpr wchar_t kUnrelatedVariable[] = L"ODIN_UNRELATED_MARKER";

class TempDir {
public:
    TempDir() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("Odin Update Test " + std::to_string(now));
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::filesystem::path path;
};

// The variables the launcher has in test mode, plus one the restarted program must keep.
class ScopedEnvironment {
public:
    ScopedEnvironment() {
        for (const wchar_t* name : kTestVariables) {
            Save(name, L"1");
        }
        Save(kUnrelatedVariable, L"keep");
    }
    ~ScopedEnvironment() {
        for (const auto& [name, value] : saved_) {
            SetEnvironmentVariableW(name.c_str(), value ? value->c_str() : nullptr);
        }
    }

private:
    void Save(const wchar_t* name, const wchar_t* value) {
        std::optional<std::wstring> old;
        if (const DWORD length = GetEnvironmentVariableW(name, nullptr, 0); length > 0) {
            old.emplace(static_cast<size_t>(length), L'\0');
            GetEnvironmentVariableW(name, old->data(), length);
            old->resize(static_cast<size_t>(length - 1));
        }
        saved_.emplace_back(name, old);
        SetEnvironmentVariableW(name, value);
    }

    std::vector<std::pair<std::wstring, std::optional<std::wstring>>> saved_;
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

bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::wstring Quote(const std::wstring& argument) {
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted += L'"';
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted += c;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'"';
    return quoted;
}

// This process's environment with PATH limited to Windows' own folders: a DLL that only the toolchain or the
// installation provides cannot be found.
std::vector<wchar_t> SystemOnlyEnvironment() {
    wchar_t windows[MAX_PATH] = L"";
    GetWindowsDirectoryW(windows, MAX_PATH);
    const std::wstring path = std::wstring(L"PATH=") + windows + L"\\System32;" + windows;
    std::vector<wchar_t> result;
    LPWCH block = GetEnvironmentStringsW();
    for (const wchar_t* entry = block; entry && *entry; entry += std::wcslen(entry) + 1) {
        if (_wcsnicmp(entry, L"PATH=", 5) != 0) {
            result.insert(result.end(), entry, entry + std::wcslen(entry) + 1);
        }
    }
    FreeEnvironmentStringsW(block);
    result.insert(result.end(), path.c_str(), path.c_str() + path.size() + 1);
    result.push_back(L'\0');
    return result;
}

bool Run(const std::filesystem::path& program, const std::wstring& arguments, const std::filesystem::path& directory,
         DWORD& exit_code, std::vector<wchar_t>* environment = nullptr) {
    std::wstring command = Quote(program.wstring()) + L" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const DWORD flags = CREATE_NO_WINDOW | (environment ? CREATE_UNICODE_ENVIRONMENT : 0);
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, flags,
                        environment ? environment->data() : nullptr, directory.c_str(), &startup, &process)) {
        std::printf("  CreateProcessW failed (%lu)\n", GetLastError());
        return false;
    }
    WaitForSingleObject(process.hProcess, 60000);
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

// A running marker that holds for a moment: the launcher process the helper waits for.
PROCESS_INFORMATION StartParent(const std::filesystem::path& marker, const std::filesystem::path& output,
                                const std::filesystem::path& directory) {
    std::wstring command = Quote(marker.wstring()) + L" --hold 750 " + Quote(output.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(marker.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        directory.c_str(), &startup, &process)) {
        std::printf("  CreateProcessW failed for the parent (%lu)\n", GetLastError());
    }
    return process;
}

void FinishParent(PROCESS_INFORMATION& parent) {
    if (parent.hProcess) {
        WaitForSingleObject(parent.hProcess, 10000);
        CloseHandle(parent.hThread);
        CloseHandle(parent.hProcess);
    }
}

bool WaitForMarker(const std::filesystem::path& path) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (Contains(ReadFile(path), "done=1\n")) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

std::vector<std::string> ImportedLibraries(const std::filesystem::path& executable) {
    std::vector<std::string> names;
    const std::string image = ReadFile(executable);
    const auto* base = reinterpret_cast<const unsigned char*>(image.data());
    if (image.size() < sizeof(IMAGE_DOS_HEADER)) {
        return names;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > image.size()) {
        return names;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    const auto to_offset = [&](DWORD rva) -> size_t {
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const IMAGE_SECTION_HEADER& section = sections[i];
            if (rva >= section.VirtualAddress && rva < section.VirtualAddress + section.Misc.VirtualSize) {
                return size_t(section.PointerToRawData + (rva - section.VirtualAddress));
            }
        }
        return image.size();
    };
    for (size_t at = to_offset(imports.VirtualAddress);
         imports.Size && at + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= image.size(); at += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + at);
        if (descriptor->Name == 0) {
            break;
        }
        if (const size_t name = to_offset(descriptor->Name); name < image.size()) {
            names.emplace_back(image.c_str() + name);
        }
    }
    return names;
}

bool IsSystemLibrary(std::string name) {
    for (char& c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (name.starts_with("api-ms-win-")) {
        return true;
    }
    for (const char* system : {"kernel32.dll", "shell32.dll", "user32.dll", "advapi32.dll", "ntdll.dll", "shlwapi.dll",
                               "ole32.dll", "bcrypt.dll"}) {
        if (name == system) {
            return true;
        }
    }
    return false;
}

// As the launcher stages a release: the contents of the ZIP's one folder.
struct Layout {
    explicit Layout(const std::filesystem::path& root, const std::string& name)
        : workspace(root / (name + " workspace")), install(root / (name + " install dir")),
          staging(workspace / "staging" / "OdinSphereLeifthrasirPC-v0.2.0-windows-x64"), backup(workspace / "backup"),
          manifest(workspace / "manifest.txt"),
          job(workspace / "job.txt"), log(workspace / "trg-updater.log") {}

    std::filesystem::path workspace, install, staging, backup, manifest, job, log;
};

// The player's files in an Odin installation, which an update never touches.
const std::vector<std::pair<std::string, std::string>> kPlayerFiles = {
    {"user/osl.ini", "show_launcher=1\n"},
    {"user/savedata/CUSA05083/SAVEDATA00/data.bin", "save"},
    {"user/trophy/CUSA05083/progress_user.txt", "trophies"},
    {"user/keys.json", "{}"},
    {"user/cache/CUSA05083/pipeline.bin", "cache"},
    {"game/prepared/CUSA05083-1.00/eboot.bin", "game"},
    {"game/image/CUSA05083-1.00/boot.bin", "image"},
    {"logs/odin_sphere_leifthrasir_001.log", "log"},
    {"notes.txt", "keep me"},
};

void WriteJob(const Layout& layout, DWORD parent, const std::vector<std::string>& arguments) {
    std::string text = "format=1\n";
    text += "parent_pid=" + std::to_string(parent) + "\n";
    text += "install_dir=" + layout.install.string() + "\n";
    text += "staging_dir=" + layout.staging.string() + "\n";
    text += "backup_dir=" + layout.backup.string() + "\n";
    text += "manifest=" + layout.manifest.string() + "\n";
    text += "restart=" + (layout.install / "odin_sphere_leifthrasir.exe").string() + "\n";
    text += "log=" + layout.log.string() + "\n";
    for (const std::string& argument : arguments) {
        text += "arg=" + argument + "\n";
    }
    WriteFile(layout.job, text);
}

void CheckPlayerFiles(const std::filesystem::path& install) {
    for (const auto& [relative, content] : kPlayerFiles) {
        CHECK(ReadFile(install / relative) == content);
    }
}

void CheckRestartedWithoutTestVariables(const std::string& marker) {
    for (const wchar_t* name : kTestVariables) {
        std::string line = "env_";
        for (const wchar_t* c = name; *c; ++c) {
            line += static_cast<char>(*c);
        }
        CHECK(Contains(marker, line + "=absent\n"));
    }
    CHECK(Contains(marker, "env_ODIN_UNRELATED_MARKER=present\n"));
}

void TestStandalone(const std::filesystem::path& helper, const std::filesystem::path& root) {
    std::printf("trg-updater.exe alone in a temporary folder\n");
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(helper.parent_path(), ec)) {
        CHECK(entry.path().filename() == "trg-updater.exe");
    }
    const std::vector<std::string> imports = ImportedLibraries(helper);
    CHECK(!imports.empty());
    for (const std::string& library : imports) {
        if (!IsSystemLibrary(library)) {
            std::printf("  trg-updater.exe imports %s\n", library.c_str());
        }
        CHECK(IsSystemLibrary(library));
    }

    const std::filesystem::path standalone = root / "standalone dir";
    std::filesystem::create_directories(standalone, ec);
    std::vector<wchar_t> environment = SystemOnlyEnvironment();
    DWORD exit_code = 1;
    CHECK(Run(helper, L"--job " + Quote((standalone / "missing job.txt").wstring()), standalone, exit_code, &environment));
    CHECK(exit_code == 2);
    const std::string log = ReadFile(standalone / "trg-updater.log");
    CHECK(Contains(log, "trg-updater starting\n"));
    CHECK(Contains(log, "job: " + (standalone / "missing job.txt").string() + "\n"));
    CHECK(Contains(log, "job read failed: Cannot read the update job.\n"));
    CHECK(Contains(log, "exit code: 2\n"));
}

void TestInstallAndRestart(const std::filesystem::path& helper, const std::filesystem::path& marker,
                           const std::filesystem::path& root) {
    std::printf("install, restart and cleanup\n");
    const Layout layout(root, "update");
    const std::filesystem::path parent_output = root / "parent output.txt";
    const std::filesystem::path marker_output = root / "restarted output.txt";
    std::error_code ec;
    std::filesystem::create_directories(layout.staging, ec);
    WriteFile(layout.install / "odin_sphere_leifthrasir.exe", "DEVELOPMENT_BUILD");
    WriteFile(layout.install / "trg-updater.exe", "OLD_HELPER");
    for (const auto& [relative, content] : kPlayerFiles) {
        WriteFile(layout.install / relative, content);
    }
    std::filesystem::copy_file(marker, layout.staging / "odin_sphere_leifthrasir.exe", ec);
    CHECK(!ec);
    std::filesystem::copy_file(helper, layout.staging / "trg-updater.exe", ec);
    CHECK(!ec);
    WriteFile(layout.staging / "licenses" / "THIRD-PARTY-LICENSES.txt", "PUBLIC_RELEASE licences");
    WriteFile(layout.manifest, "licenses/THIRD-PARTY-LICENSES.txt\nodin_sphere_leifthrasir.exe\ntrg-updater.exe\n");

    PROCESS_INFORMATION parent = StartParent(marker, parent_output, root);
    CHECK(parent.dwProcessId != 0);
    WriteJob(layout, parent.dwProcessId,
             {marker_output.string(), "--user", "C:\\with space\\", "--name=say \"hi\"", "", "--launcher"});
    DWORD exit_code = 1;
    CHECK(Run(helper, L"--job " + Quote(layout.job.wstring()), layout.workspace, exit_code));
    FinishParent(parent);
    CHECK(exit_code == 0);

    // The parent ran in test mode; the restarted program does not inherit it.
    const std::string parent_text = ReadFile(parent_output);
    CHECK(Contains(parent_text, "env_ODIN_UPDATE_TEST=present\n"));
    CHECK(WaitForMarker(marker_output));
    const std::string restarted = ReadFile(marker_output);
    CHECK(Contains(restarted, "argc=6\n"));
    CHECK(Contains(restarted, "arg0=" + marker_output.string() + "\n"));
    CHECK(Contains(restarted, "arg1=--user\n"));
    CHECK(Contains(restarted, "arg2=C:\\with space\\\n"));
    CHECK(Contains(restarted, "arg3=--name=say \"hi\"\n"));
    CHECK(Contains(restarted, "arg4=\n"));
    CHECK(Contains(restarted, "arg5=--launcher\n"));
    CheckRestartedWithoutTestVariables(restarted);

    CHECK(ReadFile(layout.install / "odin_sphere_leifthrasir.exe") == ReadFile(marker));
    CHECK(ReadFile(layout.install / "trg-updater.exe") == ReadFile(helper));
    CHECK(ReadFile(layout.install / "licenses" / "THIRD-PARTY-LICENSES.txt") == "PUBLIC_RELEASE licences");
    CheckPlayerFiles(layout.install);

    const std::string log = ReadFile(layout.log);
    CHECK(Contains(log, "trg-updater starting\n"));
    CHECK(Contains(log, "job: " + layout.job.string() + "\n"));
    CHECK(Contains(log, "install root: " + layout.install.string() + "\n"));
    CHECK(Contains(log, "staging root: " + layout.staging.string() + "\n"));
    CHECK(Contains(log, "restart executable: " + (layout.install / "odin_sphere_leifthrasir.exe").string() + "\n"));
    CHECK(Contains(log, "restart arguments: " + marker_output.string() +
                            " --user C:\\with space\\ --name=say \"hi\"  --launcher\n"));
    CHECK(Contains(log, "parent pid: " + std::to_string(parent.dwProcessId) + "\n"));
    CHECK(Contains(log, "parent wait result: exited\n"));
    CHECK(Contains(log, "manifest entries: 3\n"));
    CHECK(Contains(log, "backup complete\n"));
    CHECK(Contains(log, "replacing 3 files\n"));
    CHECK(Contains(log, "replacement complete\n"));
    CHECK(Contains(log, "verification complete\n"));
    CHECK(Contains(log, "restart complete\n"));
    CHECK(Contains(log, "install result: success\n"));
    CHECK(Contains(log, "exit code: 0\n"));
    CHECK(!Contains(log, "rollback"));
    CHECK(!Contains(log, "odin_sphere_leifthrasir.exe: ok"));

    CHECK(!std::filesystem::exists(layout.job));
    CHECK(!std::filesystem::exists(layout.backup));
    CHECK(!std::filesystem::exists(layout.staging));
}

void TestRestartFailureRollsBack(const std::filesystem::path& helper, const std::filesystem::path& marker,
                                 const std::filesystem::path& root) {
    std::printf("a release that cannot start is rolled back and the previous program restarted\n");
    const Layout layout(root, "rollback");
    const std::filesystem::path parent_output = root / "rollback parent.txt";
    const std::filesystem::path marker_output = root / "rollback restarted.txt";
    std::error_code ec;
    std::filesystem::create_directories(layout.staging, ec);
    std::filesystem::create_directories(layout.install, ec);
    std::filesystem::copy_file(marker, layout.install / "odin_sphere_leifthrasir.exe", ec);
    CHECK(!ec);
    for (const auto& [relative, content] : kPlayerFiles) {
        WriteFile(layout.install / relative, content);
    }
    WriteFile(layout.staging / "odin_sphere_leifthrasir.exe", "BROKEN_RELEASE");
    WriteFile(layout.staging / "assets" / "launcher" / "header-art.png", "new art");
    WriteFile(layout.manifest, "odin_sphere_leifthrasir.exe\nassets/launcher/header-art.png\n");

    PROCESS_INFORMATION parent = StartParent(marker, parent_output, root);
    WriteJob(layout, parent.dwProcessId, {marker_output.string(), "--launcher"});
    DWORD exit_code = 0;
    CHECK(Run(helper, L"--job " + Quote(layout.job.wstring()), layout.workspace, exit_code));
    FinishParent(parent);
    CHECK(exit_code == 7);

    CHECK(ReadFile(layout.install / "odin_sphere_leifthrasir.exe") == ReadFile(marker));
    CHECK(!std::filesystem::exists(layout.install / "assets" / "launcher" / "header-art.png"));
    CheckPlayerFiles(layout.install);
    CHECK(WaitForMarker(marker_output));
    CheckRestartedWithoutTestVariables(ReadFile(marker_output));

    const std::string log = ReadFile(layout.log);
    CHECK(Contains(log, "restart failed: Could not start the program ("));
    CHECK(Contains(log, "rollback complete\n"));
    CHECK(Contains(log, "previous launcher restarted\n"));
    CHECK(Contains(log, "exit code: 7\n"));
    // A failed update keeps its workspace for diagnosis.
    CHECK(std::filesystem::exists(layout.job));
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::filesystem::path updater = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(OSL_UPDATER_EXE);
    const std::filesystem::path marker = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path(OSL_MARKER_EXE);
    ScopedEnvironment environment;
    TempDir temp;
    const std::filesystem::path helper = temp.path / "helper dir" / "trg-updater.exe";
    std::error_code ec;
    std::filesystem::create_directories(helper.parent_path(), ec);
    std::filesystem::copy_file(updater, helper, ec);
    CHECK(!ec);

    TestStandalone(helper, temp.path);
    TestInstallAndRestart(helper, marker, temp.path);
    TestRestartFailureRollsBack(helper, marker, temp.path);

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
