#ifndef OSL_AUDIO_BACKLOG_H
#define OSL_AUDIO_BACKLOG_H
#include <stddef.h>
#include <stdint.h>

#define AUDIO_BACKLOG_SLACK_FRAMES 4800 /* 100 ms at 48 kHz */

/* A queue more than 100 ms above the target is stale. `bytes` is one buffer as queued, `frames` its length. */
static inline int audio_backlog_stale(int queued, int low, size_t bytes, int frames) {
    if (frames<=0 || !bytes) return 0;
    int64_t cap=(int64_t)low+(int64_t)((uint64_t)bytes*AUDIO_BACKLOG_SLACK_FRAMES/(uint64_t)frames);
    return (int64_t)queued>cap;
}

/* Stale queue: forgets the queue level and the steering window and returns the bytes above the target
   (the caller clears the stream). Normal queue: returns 0 and touches nothing. */
static inline int audio_backlog_drop(int *queued, int low, size_t bytes, int frames, int *window_count, int64_t *adjust_ns) {
    if (!audio_backlog_stale(*queued,low,bytes,frames)) return 0;
    int excess=*queued-low;
    *queued=0; *window_count=0; *adjust_ns=0;
    return excess;
}

static inline double audio_backlog_ms(int excess_bytes, uint64_t period_ns, size_t bytes) {
    return (double)excess_bytes*(double)period_ns/(double)bytes/1e6;
}

#endif
