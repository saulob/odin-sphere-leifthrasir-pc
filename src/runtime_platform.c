/* Imports needed by this title's boot path that no shared runtime file owns. */
#include "runtime.h"

typedef struct { uint64_t size; uint32_t flag, get_segment_info; uint64_t *mspace_id_mask, *mstate_table; } HeapTraceInfo;

/* libc.prx's initialiser clears *mspace_id_mask unconditionally, so both pointers must be valid. */
static ABI int32_t libc_heap_trace_info(HeapTraceInfo *info) {
    static uint64_t mask, table[64];
    if (!info) return -1;
    info->get_segment_info = 0;
    info->mspace_id_mask = &mask;
    info->mstate_table = table;
    return 0;
}

static ABI int32_t ok_noop(void) { return 0; }
static ABI int32_t is_neo_mode(void) { return 0; }

static const RuntimeExport exports[] = {
    {"sceKernelIsNeoMode", is_neo_mode},
    {"sceGameLiveStreamingInitialize", ok_noop}, {"sceGameLiveStreamingTerminate", ok_noop},
    {"sceCoredumpRegisterCoredumpHandler", ok_noop}, {"sceCoredumpWriteUserData", ok_noop},
    {"sceLibcHeapGetTraceInfo", libc_heap_trace_info},
};
uintptr_t runtime_platform_resolve(const char *name) { return RUNTIME_LOOKUP(exports, name); }
