// osl: shadPS4 debug/profiler helpers without Tracy.
#pragma once
#include <tracy/Tracy.hpp>
#define BREAKPOINT __builtin_trap
static inline bool IsProfilerConnected() { return false; }
#define TRACY_GPU_ENABLED 0
#define CUSTOM_LOCK(type, varname) type varname
#define TRACK_ALLOC(ptr, size, pool) ((void)(ptr), (void)(size))
#define TRACK_FREE(ptr, pool) ((void)(ptr))
enum MarkersPalette : int {
    EmulatorMarkerColor = 0x264653,
    RendererMarkerColor = 0x2a9d8f,
    HleMarkerColor = 0xe9c46a,
    GpuMarkerColor = 0xf4a261,
    Reserved1 = 0xe76f51,
};
#define EMULATOR_TRACE
#define RENDERER_TRACE
#define HLE_TRACE
// Not evaluated: without Tracy nothing reads them (some callers format a string per call).
#define TRACE_HINT(str) ((void)0)
#define TRACE_WARN(msg) ((void)0)
#define TRACE_ERROR(msg) ((void)0)
#define TRACE_CRIT(msg) ((void)0)
#define GPU_SCOPE_LOCATION(name, color) tracy::SourceLocationData{name, TracyFunction, TracyFile, (uint32_t)TracyLine, color};
#define MUTEX_LOCATION(name) tracy::SourceLocationData{nullptr, name, TracyFile, (uint32_t)TracyLine, 0};
#define FRAME_END
#define FIBER_ENTER(name)
#define FIBER_EXIT
