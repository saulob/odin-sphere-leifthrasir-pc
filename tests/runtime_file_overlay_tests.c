/* runtime_file_overlay: a game file read through byte ranges laid over it and past its end, the host file untouched. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(path) _mkdir(path)
#else
#define make_dir(path) mkdir(path, 0777)
#endif

static int checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

int runtime_savedata_protects(const char *host_path) {
    (void)host_path;
    return 0;
}

static int32_t error_slot;
int32_t *runtime_errno(void) { return &error_slot; }
int32_t runtime_guest_errno(int host_errno) { return host_errno; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) {
    (void)table; (void)count; (void)nid;
    return 0;
}

static void write_host_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (file) {
        fwrite(text, 1, strlen(text), file);
        fclose(file);
    }
}

static long host_size(const char *path) {
    struct stat s;
    return stat(path, &s) ? -1 : (long)s.st_size;
}

static int same_counts(RuntimeFileWatchCounts c, size_t hits, size_t unique, size_t missing, size_t untranslated) {
    return c.visual_hits == hits && c.visual_unique_hits == unique && c.visual_missing == missing &&
           c.localized_untranslated_hits == untranslated;
}

/* Localized visual telemetry: an entry is reported on the first read that reaches its start, once per session whatever
 * the alias or the number of reads; a file without watches costs nothing and reports nothing. */
static void test_visual_watches(const char *app0) {
    char path[700];
    snprintf(path, sizeof(path), "%s/Odin2_RE_ES.CPK", app0);
    write_host_file(path, "HEADER--MISSED--UNTRANS-SHARED--");
    static const unsigned char page[] = "PTBRPAGEPTBRATLS";
    const RuntimeFilePatch moved[] = {{32, 16, page}};
    CHECK(runtime_file_overlay("/app0/Odin2_RE_ES.CPK", 48, moved, 1) == 0);
    const RuntimeFileWatch watches[] = {
        {40, 8, "GUI/HD_Installation.ftx", RUNTIME_WATCH_PROLOGUE},
        {32, 8, "GUI/REHD_Installation.mbs", RUNTIME_WATCH_PROLOGUE},
        {8, 8, "GUI/HD_title.ftx", RUNTIME_WATCH_MISSED},
        {16, 8, "Other/HD_M00.ftx", RUNTIME_WATCH_UNTRANSLATED},
    };
    CHECK(runtime_file_watch("/app0/Odin2_RE_ES.CPK", "Odin2_RE_ES.CPK", "ES", watches, 4) == 0);
    CHECK(runtime_file_watch("/app0/Odin2_RE_ES.CPK", "Odin2_RE_ES.CPK", "ES", watches, 4) != 0);
    RuntimeFileWatchCounts c;
    runtime_file_watch_counts(&c);
    CHECK(same_counts(c, 0, 0, 0, 0));

    char buffer[64];
    const int64_t fd = runtime_file_open("/app0/Odin2_RE_ES.CPK", 0, 0);
    CHECK(fd >= 3);
    CHECK(runtime_file_pread((int)fd, buffer, 8, 0) == 8);
    CHECK(runtime_file_pread((int)fd, buffer, 8, 24) == 8);
    runtime_file_watch_counts(&c);
    CHECK(same_counts(c, 0, 0, 0, 0));

    CHECK(runtime_file_pread((int)fd, buffer, 16, 32) == 16);
    CHECK(!memcmp(buffer, "PTBRPAGEPTBRATLS", 16));
    runtime_file_watch_counts(&c);
    CHECK(same_counts(c, 2, 2, 0, 0));
    CHECK(runtime_file_pread((int)fd, buffer, 8, 32) == 8);
    CHECK(runtime_file_pread((int)fd, buffer, 4, 34) == 4);
    runtime_file_watch_counts(&c);
    CHECK(same_counts(c, 3, 2, 0, 0));

    const int64_t alias = runtime_file_open("/hostapp/Odin2_RE_ES.CPK", 0, 0);
    CHECK(alias >= 3);
    CHECK(runtime_file_lseek((int)alias, 8, 0) == 8);
    CHECK(runtime_file_read((int)alias, buffer, 16) == 16);
    CHECK(!memcmp(buffer, "MISSED--UNTRANS-", 16));
    CHECK(runtime_file_pread((int)fd, buffer, 16, 8) == 16);
    runtime_file_watch_counts(&c);
    CHECK(same_counts(c, 3, 2, 1, 1));
    runtime_file_close((int)alias);
    runtime_file_close((int)fd);

    char shared[700];
    snprintf(shared, sizeof(shared), "%s/Odin2_RE_UK_NOWATCH.CPK", app0);
    write_host_file(shared, "0123456789abcdef");
    const int64_t plain = runtime_file_open("/app0/Odin2_RE_UK_NOWATCH.CPK", 0, 0);
    CHECK(plain >= 3);
    CHECK(runtime_file_pread((int)plain, buffer, 16, 0) == 16);
    runtime_file_close((int)plain);
    runtime_file_watch_counts(&c);
    CHECK(same_counts(c, 3, 2, 1, 1));
    runtime_file_report();
}

int main(void) {
    char root[512], app0[600], user[600], archive[700];
    snprintf(root, sizeof(root), "%s", OSL_OVERLAY_TEST_ROOT);
    make_dir(root);
    snprintf(app0, sizeof(app0), "%s/app0", root);
    snprintf(user, sizeof(user), "%s/user", root);
    make_dir(app0);
    make_dir(user);
    snprintf(archive, sizeof(archive), "%s/Odin2_RE_UK.CPK", app0);
    write_host_file(archive, "0123456789");
    runtime_file_configure(app0, user);

    static const unsigned char toc[] = "AB";
    static const unsigned char appended[] = "NEWDATA";
    const RuntimeFilePatch patches[] = {{2, 2, toc}, {12, 7, appended}};
    CHECK(runtime_file_overlay("/app0/Odin2_RE_UK.CPK", 19, patches, 2) == 0);
    CHECK(runtime_file_overlay("/app0/Odin2_RE_UK.CPK", 19, patches, 2) != 0);
    const RuntimeFilePatch past_end[] = {{18, 4, appended}};
    CHECK(runtime_file_overlay("/app0/Other.CPK", 19, past_end, 1) != 0);

    const int64_t fd = runtime_file_open("/app0/odin2_re_uk.cpk", 0, 0);
    CHECK(fd >= 3);
    char buffer[32] = {0};
    CHECK(runtime_file_pread((int)fd, buffer, sizeof(buffer), 0) == 19);
    CHECK(!memcmp(buffer, "01AB456789\0\0NEWDATA", 19));
    memset(buffer, 0, sizeof(buffer));
    CHECK(runtime_file_pread((int)fd, buffer, 4, 11) == 4);
    CHECK(!memcmp(buffer, "\0NEW", 4));
    CHECK(runtime_file_lseek((int)fd, 0, 2) == 19);
    CHECK(runtime_file_lseek((int)fd, 1, 0) == 1);
    memset(buffer, 0, sizeof(buffer));
    CHECK(runtime_file_read((int)fd, buffer, 3) == 3);
    CHECK(!memcmp(buffer, "1AB", 3));
    CHECK(runtime_file_read((int)fd, buffer, 32) == 15);
    CHECK(runtime_file_read((int)fd, buffer, 32) == 0);
    unsigned char stat_buffer[120];
    CHECK(runtime_file_fstat((int)fd, stat_buffer) == 0);
    int64_t size = 0;
    memcpy(&size, stat_buffer + 72, sizeof(size));
    CHECK(size == 19);
    CHECK(runtime_file_stat("/app0/Odin2_RE_UK.CPK", stat_buffer) == 0);
    memcpy(&size, stat_buffer + 72, sizeof(size));
    CHECK(size == 19);
    CHECK(runtime_file_close((int)fd) == 0);

    char other[700];
    snprintf(other, sizeof(other), "%s/Odin2_OR_UK.CPK", app0);
    write_host_file(other, "abcdef");
    /* A text container replaced in place: served under /app0, /hostapp and any spelling of the same path. */
    static const unsigned char head[] = "XYZ";
    const RuntimeFilePatch story[] = {{0, 3, head}};
    CHECK(runtime_file_overlay("/app0/Odin2_OR_UK.CPK", 6, story, 1) == 0);
    const char *spellings[] = {"/app0/Odin2_OR_UK.CPK", "/hostapp/Odin2_OR_UK.CPK", "Odin2_OR_UK.CPK",
                               "./Odin2_OR_UK.CPK", "/app0//Odin2_OR_UK.CPK", "/app0/./Odin2_OR_UK.CPK"};
    for (size_t i = 0; i < sizeof(spellings) / sizeof(spellings[0]); ++i) {
        const int64_t fd2 = runtime_file_open(spellings[i], 0, 0);
        memset(buffer, 0, sizeof(buffer));
        CHECK(fd2 >= 3);
        CHECK(runtime_file_pread((int)fd2, buffer, 6, 0) == 6);
        CHECK(!memcmp(buffer, "XYZdef", 6));
        runtime_file_close((int)fd2);
    }
    CHECK(runtime_file_overlay("/hostapp/Odin2_OR_UK.CPK", 6, story, 1) != 0);

    CHECK(runtime_file_open("/app0/Odin2_RE_UK.CPK", 2, 0) < 0);
    CHECK(host_size(archive) == 10);

    test_visual_watches(app0);
    printf("runtime file overlay: %s (%d checks, %d failed)\n", failures ? "FAIL" : "OK", checks, failures);
    return failures ? 1 : 0;
}
