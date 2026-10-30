/* Focused tests of the run log's retention on the real log runtime_log_start opens: stdout and stderr still write
 * to it when it is removed. Each start begins a new numbered log in a temporary folder; the results go to the
 * console the process started with. On Windows the log is also checked in a Windows-subsystem process
 * (log_gui_probe.c). */
#include "runtime_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define dup _dup
#define write _write
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static int console = -1, failures;

static void report(const char *text) {
    if (console < 0) return;
    const long written = (long)write(console, text, (unsigned)strlen(text));
    (void)written;
}

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            char line_[256];                                                            \
            snprintf(line_, sizeof(line_), "FAIL line %d: %s\n", __LINE__, #cond);      \
            report(line_);                                                              \
            ++failures;                                                                 \
        }                                                                               \
    } while (0)

static int exists(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file) fclose(file);
    return file != NULL;
}

static int holds(const char *path, const char *text) {
    char buffer[8192];
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    const size_t got = fread(buffer, 1, sizeof(buffer) - 1, file);
    fclose(file);
    buffer[got] = 0;
    return strstr(buffer, text) != NULL;
}

#ifdef _WIN32
/* The probe as Explorer starts a Windows-subsystem program: no console and no standard handles. */
static DWORD run_gui_probe(const char *log, const char *mode) {
    char command[8600];
    snprintf(command, sizeof(command), "\"%s\" \"%s\" %s", OSL_LOG_GUI_PROBE, log, mode);
    STARTUPINFOA startup = {0};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    PROCESS_INFORMATION process;
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) return 1000;
    DWORD code = 1001;
    if (WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
    else TerminateProcess(process.hProcess, code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code;
}

static void check_gui_process(const char *dir) {
    char kept[4200], removed[4200];
    snprintf(kept, sizeof(kept), "%s\\gui_kept.log", dir);
    snprintf(removed, sizeof(removed), "%s\\gui_removed.log", dir);
    CHECK(run_gui_probe(kept, "keep") == 0);
    CHECK(holds(kept, "stdout line\n") && holds(kept, "stderr line\n"));
    CHECK(holds(kept, "raw stdout\n") && holds(kept, "raw stderr\n"));
    CHECK(holds(kept, "win32 stdout\n") && holds(kept, "win32 stderr\n"));
    CHECK(run_gui_probe(removed, "remove") == 0);
    CHECK(!exists(removed));
    remove(kept);
}
#endif

static int start(const char *dir, const char *line, char *path, size_t size) {
    if (!runtime_log_start(dir, NULL, path, size)) return 0;
    printf("%s\n", line);
    fprintf(stderr, "%s on stderr\n", line);
    runtime_log_flush();
    return 1;
}

int main(void) {
    char dir[4096], kept[4200], removed[4200], detailed[4200];
#ifdef _WIN32
    char temp[MAX_PATH];
    const DWORD length = GetTempPathA(sizeof(temp), temp);
    if (!length || length >= sizeof(temp)) return 2;
    snprintf(dir, sizeof(dir), "%sosl_log_tests_%lu", temp, GetCurrentProcessId());
    CreateDirectoryA(dir, NULL);
#else
    const char *temp = getenv("TMPDIR");
    snprintf(dir, sizeof(dir), "%s/osl_log_tests_%ld", temp && *temp ? temp : "/tmp", (long)getpid());
    mkdir(dir, 0700);
#endif
    fflush(NULL);
    console = dup(1);

#ifdef _WIN32
    check_gui_process(dir);
#endif
    CHECK(start(dir, "crashed run", kept, sizeof(kept)));
    CHECK(holds(kept, "crashed run\n") && holds(kept, "crashed run on stderr\n"));
    runtime_log_set_detailed(0);
    runtime_log_mark_crash();
    runtime_log_mark_clean_exit();
    runtime_log_finalize();
    CHECK(exists(kept));

    CHECK(start(dir, "clean run", removed, sizeof(removed)));
    CHECK(strcmp(removed, kept) != 0 && holds(removed, "clean run\n"));
    runtime_log_set_detailed(0);
    runtime_log_mark_clean_exit();
    runtime_log_finalize();
    CHECK(!exists(removed));
    runtime_log_finalize();

    CHECK(start(dir, "detailed run", detailed, sizeof(detailed)));
    runtime_log_set_detailed(1);
    runtime_log_mark_clean_exit();
    runtime_log_finalize();
    CHECK(exists(detailed) && holds(detailed, "detailed run\n"));

    CHECK(exists(kept) && holds(kept, "crashed run\n"));
    remove(kept);
    remove(detailed);
#ifdef _WIN32
    RemoveDirectoryA(dir);
#else
    rmdir(dir);
#endif
    report(failures ? "log tests: FAILED\n" : "log tests: all passed\n");
    return failures ? 1 : 0;
}
