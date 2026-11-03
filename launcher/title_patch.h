// The title menu patch (title/title-exit.dcpx) compiled into the executable: launcher/embed.cmake turns the file into a
// source with its bytes at build time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace osl {

extern const uint8_t kTitleExitPatch[];
extern const size_t kTitleExitPatchSize;

inline std::span<const uint8_t> TitleExitPatch() {
    return {kTitleExitPatch, kTitleExitPatchSize};
}

} // namespace osl
