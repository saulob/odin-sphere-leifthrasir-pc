// Stand-in for osl-loader.exe in the update helper tests: [--hold <ms>] <output> [args...]. It writes its arguments and
// which test variables it inherited to <output>, then optionally stays alive (the launcher the helper waits for).
// A GUI-subsystem program, so the helper starts it without a console window.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

std::string Utf8(const wchar_t* text) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    result.resize(static_cast<size_t>(length - 1));
    return result;
}

const char* Presence(const wchar_t* name) { return GetEnvironmentVariableW(name, nullptr, 0) > 0 ? "present" : "absent"; }

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) {
        return 2;
    }
    const bool hold = count >= 4 && std::wstring_view(arguments[1]) == L"--hold";
    const int output_index = hold ? 3 : 1;
    if (count <= output_index) {
        LocalFree(arguments);
        return 2;
    }
    std::ofstream out(std::filesystem::path(arguments[output_index]), std::ios::binary | std::ios::trunc);
    if (!out) {
        LocalFree(arguments);
        return 3;
    }
    out << "argc=" << count - output_index << '\n';
    for (int i = output_index; i < count; ++i) {
        out << "arg" << i - output_index << '=' << Utf8(arguments[i]) << '\n';
    }
    for (const wchar_t* name : {L"ODIN_UPDATE_TEST", L"ODIN_UPDATE_TEST_CURRENT_VERSION", L"ODIN_UPDATE_TEST_RELEASE_JSON",
                                L"ODIN_UPDATE_TEST_ASSET_SHA256", L"ODIN_UNRELATED_MARKER"}) {
        out << "env_" << Utf8(name) << '=' << Presence(name) << '\n';
    }
    out << "done=1\n";
    out.close();
    if (hold) {
        Sleep(static_cast<DWORD>(std::wcstoul(arguments[2], nullptr, 10)));
    }
    LocalFree(arguments);
    return 0;
}
