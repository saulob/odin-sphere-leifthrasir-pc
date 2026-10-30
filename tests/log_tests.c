/* Focused tests of the run log's retention on the real log runtime_log_start opens: stdout and stderr still write
 * to it when it is removed. Each start begins a new numbered log in a temporary folder; the results go to the
 * console the process started with. */
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
