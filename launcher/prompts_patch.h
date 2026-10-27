// The Xbox prompts patch (prompts/xbox-prompts.dcpx) compiled into the executable: launcher/embed.cmake turns the file
// into a source with its bytes at build time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace osl {

extern const uint8_t kXboxPromptsPatch[];
extern const size_t kXboxPromptsPatchSize;

inline std::span<const uint8_t> XboxPromptsPatch() {
    return {kXboxPromptsPatch, kXboxPromptsPatchSize};
}

} // namespace osl
