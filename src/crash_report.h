#ifndef CRASH_REPORT_H
#define CRASH_REPORT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* A fatal fault appends one CRASH REPORT block to this run's log (stderr): the header below, the loader's registers
 * and stack, then CRASH_REPORT_RULE. No other file or minidump is written. */
#define CRASH_REPORT_RULE "============================================================\n"
enum { CRASH_REPORT_MAX_PARAMETERS = 15 };

typedef struct {
    const char *event;      /* "Windows unhandled exception", "Linux fatal signal" */
    const char *reason;     /* why the port stopped itself (abort(), a pure call), or NULL */
    const char *version;
    const char *game_build; /* NULL before the build is known */
    uint64_t uptime_ms;
    int signal;             /* POSIX; 0 for a Windows exception */
    int signal_code;
    uint32_t code;          /* Windows exception code */
    uint32_t flags;
    uint64_t address;       /* the faulting instruction */
    const char *location;   /* address as module+offset or guest offset; NULL when unknown */
    uint32_t parameter_count;
    uint64_t parameters[CRASH_REPORT_MAX_PARAMETERS];
    int has_referenced_address;
    uint64_t referenced_address;
    uint64_t thread_id;
    uint64_t process_id;
} CrashReport;

/* "ACCESS_VIOLATION" for 0xC0000005; NULL outside the table. */
const char *crash_exception_name(uint32_t code);
const char *crash_signal_name(int signal);

/* Writes the opening of the block; NUL-terminated when size is not 0. Returns the characters written. */
size_t crash_report_format(const CrashReport *report, char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif
