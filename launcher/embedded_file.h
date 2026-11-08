// A file compiled into the executable (launcher/embed_files.cmake).
#pragma once

#include <cstddef>
#include <cstdint>

namespace osl {

struct EmbeddedFile {
    const char* name;
    const uint8_t* data;
    size_t size;
};

} // namespace osl
