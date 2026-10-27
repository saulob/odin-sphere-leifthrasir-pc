#include "hang_watchdog.h"
#include "crash_report.h"
#include "hang_report.h"
#include <windows.h>
#include <io.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>

enum { WINDOW_TIMEOUT_MS = 5000, FRAME_STALL_MS = 15000, POLL_MS = 1000, WALK_TIMEOUT_MS = 500 };

static HangWatchdogConfig config;
static char logs_dir[4200], session_log[260], version[64], game_build[128], os_name[192];
static HWND window;
static HANDLE log_handle = INVALID_HANDLE_VALUE;

/* The stack walk runs on its own thread, so the watchdog can resume the walked thread even when the unwinder
 * blocks on a lock that thread holds (the loader's function tables). */
static HANDLE walk_request, walk_done;
static DWORD walker_id;
static CONTEXT walk_context;
static DWORD64 walk_frames[HANG_REPORT_MAX_FRAMES];
static unsigned walk_count;
static int walker_busy;

/* The watchdog's own buffers: nothing is allocated while a thread is suspended. */
static DWORD thread_ids[HANG_REPORT_MAX_THREADS];
static DWORD64 frames[HANG_REPORT_MAX_FRAMES];
static char where[HANG_REPORT_MAX_FRAMES][160];
static char text[32768];

static int readable(DWORD64 address, size_t bytes) {
    MEMORY_BASIC_INFORMATION info;
    if (!address || VirtualQuery((const void *)(uintptr_t)address, &info, sizeof(info)) != sizeof(info)) return 0;
    const DWORD pages = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                        PAGE_EXECUTE_WRITECOPY;
    return info.State == MEM_COMMIT && (info.Protect & pages) && !(info.Protect & PAGE_GUARD) &&
           address + bytes <= (DWORD64)(uintptr_t)info.BaseAddress + info.RegionSize;
}

static int read_word(DWORD64 address, DWORD64 *value) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)address, value, sizeof(*value), &got) &&
           got == sizeof(*value);
}

static int guest_code(DWORD64 address) {
    uint64_t offset = 0;
    int moved = 0;
    return config.guest_location((uintptr_t)address, &offset, &moved);
}

/* Host frames from the unwind tables, then guest frames from the rbp chain: guest code has no unwind tables but
 * keeps frame pointers. */
static unsigned walk(const CONTEXT *start, DWORD64 *out) {
    CONTEXT context = *start;
    unsigned count = 0;
    int guest = 0;
    while (count < HANG_REPORT_MAX_FRAMES && context.Rip) {
        out[count++] = context.Rip;
        if ((guest = guest_code(context.Rip))) break;
        const DWORD64 rsp = context.Rsp;
        if (!readable(rsp, 64)) break;
        DWORD64 base = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &base, NULL);
        if (function) {
            PVOID handler = NULL;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, function, &context, &handler, &establisher, NULL);
        } else {
            /* Only the innermost frame can be a leaf function, with its return address on top of the stack. */
            if (count > 1 || !read_word(rsp, &context.Rip)) break;
            context.Rsp += 8;
        }
        if (context.Rsp <= rsp) break;
    }
    for (DWORD64 rbp = context.Rbp; guest && count < HANG_REPORT_MAX_FRAMES && rbp;) {
        DWORD64 next = 0, ret = 0;
        if (!read_word(rbp, &next) || !read_word(rbp + 8, &ret) || !ret) break;
        out[count++] = ret;
        if (next <= rbp) break;
        rbp = next;
    }
    return count;
}

static DWORD WINAPI walker(LPVOID unused) {
    (void)unused;
    for (;;) {
        WaitForSingleObject(walk_request, INFINITE);
        walk_count = walk(&walk_context, walk_frames);
        SetEvent(walk_done);
    }
}

/* Suspends the thread only for its context and stack walk, and always resumes it. */
static unsigned capture(DWORD id, char *name, size_t name_size, const char **note) {
    name[0] = 0;
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!thread) {
        *note = "the thread could not be opened";
        return 0;
    }
    PWSTR description = NULL;
    if (SUCCEEDED(GetThreadDescription(thread, &description)) && description) {
        if (!WideCharToMultiByte(CP_UTF8, 0, description, -1, name, (int)name_size, NULL, NULL)) name[0] = 0;
        name[name_size - 1] = 0;
        LocalFree(description);
    }
    unsigned count = 0;
    if (SuspendThread(thread) == (DWORD)-1) {
        *note = "the thread could not be suspended";
    } else {
        CONTEXT context;
        memset(&context, 0, sizeof(context));
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (!GetThreadContext(thread, &context)) {
            *note = "no thread context";
        } else {
            frames[0] = context.Rip;
            count = 1;
            if (walker_busy && WaitForSingleObject(walk_done, 0) == WAIT_OBJECT_0) walker_busy = 0;
            if (walker_busy) {
                *note = "stack walk unavailable";
            } else {
                walk_context = context;
                SetEvent(walk_request);
                if (WaitForSingleObject(walk_done, WALK_TIMEOUT_MS) == WAIT_OBJECT_0) {
                    count = walk_count;
                    memcpy(frames, walk_frames, count * sizeof(*frames));
                } else {
                    walker_busy = 1;
                    *note = "stack walk timed out";
                }
            }
        }
        ResumeThread(thread);
    }
    CloseHandle(thread);
    return count;
}

/* Module file names only: no folders, so no user or install paths. */
static void describe(DWORD64 address, char *out, size_t size) {
    uint64_t offset = 0;
    int moved = 0;
    if (config.guest_location((uintptr_t)address, &offset, &moved)) {
        snprintf(out, size, "guest offset 0x%llx%s", (unsigned long long)offset, moved ? " (moved access)" : "");
        return;
    }
    MEMORY_BASIC_INFORMATION info;
    wchar_t path[MAX_PATH];
    char utf8[MAX_PATH * 3];
    if (VirtualQuery((const void *)(uintptr_t)address, &info, sizeof(info)) == sizeof(info) && info.Type == MEM_IMAGE &&
        K32GetMappedFileNameW(GetCurrentProcess(), info.AllocationBase, path, MAX_PATH) &&
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL)) {
        snprintf(out, size, "%s+0x%llx", hang_report_base_name(utf8),
                 (unsigned long long)(address - (DWORD64)(uintptr_t)info.AllocationBase));
        return;
    }
    snprintf(out, size, "0x%016llx", (unsigned long long)address);
}

static unsigned list_threads(unsigned *total) {
    *total = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    const DWORD process = GetCurrentProcessId(), self = GetCurrentThreadId();
    unsigned count = 0;
    THREADENTRY32 entry = {.dwSize = sizeof(entry)};
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process) continue;
        ++*total;
        if (entry.th32ThreadID != self && entry.th32ThreadID != walker_id && count < HANG_REPORT_MAX_THREADS)
            thread_ids[count++] = entry.th32ThreadID;
    }
    CloseHandle(snapshot);
    return count;
}

/* The next free hang_NNN.txt, created exclusively. */
static HANDLE create_report(char *name, size_t size) {
    char pattern[4300];
    snprintf(pattern, sizeof(pattern), "%s\\hang_*.txt", logs_dir);
    unsigned last = 0;
    WIN32_FIND_DATAA found;
    HANDLE find = FindFirstFileA(pattern, &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            unsigned number = 0;
            if (hang_report_number(found.cFileName, &number) && number > last) last = number;
        } while (FindNextFileA(find, &found));
        FindClose(find);
    }
    CreateDirectoryA(logs_dir, NULL);
    for (unsigned number = last + 1, tries = 0; tries < 1000; ++number, ++tries) {
        char path[4300];
        if (!hang_report_file_name(number, name, size)) break;
        snprintf(path, sizeof(path), "%s\\%s", logs_dir, name);
        HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) return file;
    }
    return INVALID_HANDLE_VALUE;
}

static void put(HANDLE file, const char *data, size_t length) {
    DWORD written = 0;
    if (length) WriteFile(file, data, (DWORD)length, &written, NULL);
}

/* Straight to the log's file handle: the C runtime's stream lock may be held by a frozen thread. */
static void log_line(const char *line) {
    if (log_handle != INVALID_HANDLE_VALUE && log_handle != (HANDLE)-2) put(log_handle, line, strlen(line));
}

/* Each block goes to the file as soon as it is captured, so a watchdog stuck on a later thread still leaves the
 * earlier ones. */
static void write_report(const char *reason) {
    char name[32], line[512];
    HANDLE file = create_report(name, sizeof(name));
    if (file == INVALID_HANDLE_VALUE) {
        snprintf(line, sizeof(line), "Hang diagnostic could not be written (%s)\n", reason);
        log_line(line);
        return;
    }
    unsigned total = 0;
    const unsigned count = list_threads(&total);
    SYSTEMTIME now;
    GetLocalTime(&now);
    char stamp[32];
    snprintf(stamp, sizeof(stamp), "%04u-%02u-%02uT%02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
             now.wSecond);
    HangReport report = {reason, version, game_build[0] ? game_build : NULL, os_name[0] ? os_name : NULL, stamp,
                         session_log[0] ? session_log : NULL, GetTickCount64() - config.start_ms, GetCurrentProcessId(),
                         config.frames(), total, count};
    size_t length = hang_report_format(&report, text, sizeof(text)), used = length;
    put(file, text, length);
    const DWORD window_thread = GetWindowThreadProcessId(window, NULL);
    for (unsigned i = 0; i < count; ++i) {
        char thread_name[64];
        const char *note = NULL;
        const unsigned depth = capture(thread_ids[i], thread_name, sizeof(thread_name), &note);
        const char *lines[HANG_REPORT_MAX_FRAMES];
        for (unsigned d = 0; d < depth; ++d) {
            describe(frames[d], where[d], sizeof(where[d]));
            lines[d] = where[d];
        }
        const HangThread thread = {thread_ids[i], thread_name, note, thread_ids[i] == window_thread, depth, lines};
        length = hang_report_format_thread(&thread, text, sizeof(text));
        if (used + length > HANG_REPORT_MAX_BYTES) {
            static const char cut[] = "\n(report cut at its size limit)\n";
            put(file, cut, sizeof(cut) - 1);
            break;
        }
        put(file, text, length);
        used += length;
    }
    put(file, "\n" CRASH_REPORT_RULE, sizeof("\n" CRASH_REPORT_RULE) - 1);
    CloseHandle(file);
    snprintf(line, sizeof(line), "Hang diagnostic written: %s\\%s (%s)\n", hang_report_base_name(logs_dir), name, reason);
    log_line(line);
}

static DWORD WINAPI watch(LPVOID unused) {
    (void)unused;
    int reports = 0, window_hung = 0, stalled = 0;
    uint64_t last_frames = config.frames();
    ULONGLONG changed = GetTickCount64();
    while (reports < HANG_REPORT_MAX_PER_RUN) {
        Sleep(POLL_MS);
        if (!IsWindow(window)) return 0;
        DWORD_PTR result = 0;
        const int answered =
            SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, WINDOW_TIMEOUT_MS, &result) != 0;
        if (!answered && !window_hung) {
            write_report("the game window stopped answering for 5 s");
            ++reports;
        }
        window_hung = !answered;
        const ULONGLONG now = GetTickCount64();
        const uint64_t frames_now = config.frames();
        /* Minimized or in the background, the game may stop presenting on purpose. */
        if (frames_now != last_frames || IsIconic(window) || GetForegroundWindow() != window) {
            last_frames = frames_now;
            changed = now;
            stalled = 0;
        } else if (frames_now && !stalled && reports < HANG_REPORT_MAX_PER_RUN && now - changed >= FRAME_STALL_MS) {
            stalled = 1;
            write_report("the game presented no frame for 15 s");
            ++reports;
        }
    }
    return 0;
}

static void copy(char *out, size_t size, const char *value) { snprintf(out, size, "%s", value ? value : ""); }

void hang_watchdog_start(const HangWatchdogConfig *settings) {
    static int started;
    if (started || !settings->window || !settings->frames || !settings->guest_location) return;
    started = 1;
    config = *settings;
    window = (HWND)settings->window;
    copy(logs_dir, sizeof(logs_dir), settings->logs_dir);
    copy(session_log, sizeof(session_log), settings->session_log ? hang_report_base_name(settings->session_log) : NULL);
    copy(version, sizeof(version), settings->version);
    copy(game_build, sizeof(game_build), settings->game_build);
    copy(os_name, sizeof(os_name), settings->os);
    log_handle = (HANDLE)_get_osfhandle(2);
    walk_request = CreateEventW(NULL, FALSE, FALSE, NULL);
    walk_done = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!walk_request || !walk_done) return;
    HANDLE walk_thread = CreateThread(NULL, 64 * 1024, walker, NULL, 0, &walker_id);
    HANDLE watch_thread = walk_thread ? CreateThread(NULL, 256 * 1024, watch, NULL, 0, NULL) : NULL;
    if (walk_thread) {
        SetThreadDescription(walk_thread, L"osl:hang_walk");
        CloseHandle(walk_thread);
    }
    if (watch_thread) {
        SetThreadDescription(watch_thread, L"osl:hang_watch");
        CloseHandle(watch_thread);
    }
}
