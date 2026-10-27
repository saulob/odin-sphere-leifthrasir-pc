#ifndef HANG_WATCHDOG_H
#define HANG_WATCHDOG_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *window;             /* the game window's HWND */
    const char *logs_dir;
    const char *session_log;  /* this run's log, or NULL */
    const char *version;
    const char *game_build;   /* NULL when unknown */
    const char *os;           /* NULL when unknown */
    uint64_t start_ms;        /* GetTickCount64 at process start */
    uint64_t (*frames)(void); /* frames presented so far */
    /* 1 for guest code or a moved-access stub, with the guest offset and whether it is a stub. */
    int (*guest_location)(uintptr_t address, uint64_t *offset, int *moved);
} HangWatchdogConfig;

/* Windows only. Writes a hang report (hang_report.h) when the game window stops answering for 5 s, or when no
 * frame is presented for 15 s while the window is in front and not minimized, after the first frame. It never
 * stops the game. Reports at most HANG_REPORT_MAX_PER_RUN times, once per freeze. */
void hang_watchdog_start(const HangWatchdogConfig *config);

#ifdef __cplusplus
}
#endif
#endif
