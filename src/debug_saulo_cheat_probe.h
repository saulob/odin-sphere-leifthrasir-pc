/* DEBUG_SAULO: temporary telemetry of the cheat hooks for Saulo's next manual test; remove with debug_saulo_cheat_probe.c. */
#ifndef DEBUG_SAULO_CHEAT_PROBE_H
#define DEBUG_SAULO_CHEAT_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Before installing the hooks: they will call the probe thunk. */
void debug_saulo_cheat_probe_start(void);
/* After installing them: one INSTALLED line per Classic hook. */
void debug_saulo_cheat_probe_report(void);
/* At exit: hits and applications per cheat. */
void debug_saulo_cheat_probe_summary(void);

/* For the headless tests. */
void debug_saulo_cheat_probe_quiet(int quiet);
void debug_saulo_cheat_probe_clear(void);
void debug_saulo_cheat_probe_totals(const char *site_id, uint64_t *hits, uint64_t *applied, uint64_t *original);
const void *debug_saulo_cheat_probe_thunk(void);

#ifdef __cplusplus
}
#endif

#endif
