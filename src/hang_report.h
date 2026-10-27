#ifndef HANG_REPORT_H
#define HANG_REPORT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* A freeze (no crash) writes logs/hang_NNN.txt: the header below, one block per thread, then CRASH_REPORT_RULE.
 * The run's log is left as it is, apart from one line naming the file. */
enum {
    HANG_REPORT_MAX_THREADS = 256,
    HANG_REPORT_MAX_FRAMES = 48,
    HANG_REPORT_MAX_BYTES = 1 << 20,
    HANG_REPORT_MAX_PER_RUN = 3,
};

typedef struct {
    const char *reason;
    const char *version;
    const char *game_build;  /* NULL before the build is known */
    const char *os;          /* NULL when unknown */
    const char *time;        /* local time, ISO 8601 */
    const char *session_log; /* the log's file name, NULL without one */
    uint64_t uptime_ms;
    uint64_t process_id;
    uint64_t frames;         /* presented so far */
    unsigned threads;        /* in the process */
    unsigned reported;       /* stacks that follow */
} HangReport;

typedef struct {
    uint64_t id;
    const char *name;         /* NULL or empty when unnamed */
    const char *note;         /* why the stack is short or missing, or NULL */
    int window;               /* the game window's thread */
    unsigned frame_count;
    const char *const *frames; /* module+offset, guest offset or address */
} HangThread;

/* NUL-terminated when size is not 0; return the characters written. */
size_t hang_report_format(const HangReport *report, char *out, size_t size);
size_t hang_report_format_thread(const HangThread *thread, char *out, size_t size);

/* hang_007.txt; 0 when it does not fit. */
int hang_report_file_name(unsigned number, char *out, size_t size);
/* 1 and its number for a name hang_report_file_name makes. */
int hang_report_number(const char *name, unsigned *number);
/* The part after the last / or \. */
const char *hang_report_base_name(const char *path);

#ifdef __cplusplus
}
#endif
#endif
