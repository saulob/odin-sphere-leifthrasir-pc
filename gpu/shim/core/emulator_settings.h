// shadPS4's emulator settings as the video core uses them: fixed for this port. Only developer
// diagnostics that do not change the rendering are read from the environment (docs/GRAPHICS.md).
#pragma once
#include <cstdlib>
#include <string>
#include "common/types.h"

enum GpuReadbacksMode : int { Disabled, Relaxed, Precise };

class EmulatorSettingsImpl {
public:
    static EmulatorSettingsImpl* GetInstance() { static EmulatorSettingsImpl s; return &s; }
    static bool Flag(const char* name) {
        const char* v = std::getenv(name);
        return v && v[0] == '1';
    }
    s32 GetGpuId() { return -1; }
    u32 GetInternalScreenWidth() { return 1920; }
    u32 GetInternalScreenHeight() { return 1080; }
    u32 GetReadbacksMode() { return GpuReadbacksMode::Relaxed; }
    u32 GetVblankFrequency() { return 60; }
    u32 GetFrameLimit() { return 0; }
    bool IsCopyGpuBuffers() { return false; }
    bool IsDirectMemoryAccessEnabled() { return false; }
    bool IsDumpShaders() { static const bool value = Flag("OSL_DUMP_SHADERS"); return value; }
    bool IsHdrAllowed() { return false; }
    bool IsNullGPU() { return false; }
    bool IsPatchShaders() { return false; }
    bool IsPipelineCacheArchived() { return false; }
    bool IsPipelineCacheEnabled() { return true; }
    // Linear images the GPU wrote are copied back for the CPU: without it some surfaces stay black.
    bool IsReadbackLinearImagesEnabled() { return true; }
    bool IsRenderdocEnabled() { return false; }
    bool IsShaderCollect() { return false; }
    bool IsUserfaultfdTracking() { return false; }
    bool IsVkCrashDiagnosticEnabled() { return false; }
    bool IsVkGuestMarkersEnabled() { static const bool value = Flag("OSL_VK_MARKERS"); return value; }
    bool IsVkHostMarkersEnabled() { static const bool value = Flag("OSL_VK_MARKERS"); return value; }
    bool IsVkValidationCoreEnabled() { return true; }
    bool IsVkValidationEnabled() { static const bool value = Flag("OSL_VK_VALIDATION"); return value; }
    bool IsVkValidationGpuEnabled() { return false; }
    bool IsVkValidationSyncEnabled() { static const bool value = Flag("OSL_VK_VALIDATION_SYNC"); return value; }
};
#define EmulatorSettings (*EmulatorSettingsImpl::GetInstance())
