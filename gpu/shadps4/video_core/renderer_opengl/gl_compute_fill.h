// SPDX-License-Identifier: GPL-2.0-or-later
// Compute programs whose only effect is one constant dword per invocation, stored at the invocation's global index:
// OpenGL 3.3 and ES 3.0 have no compute stage, so such a dispatch is carried out on the CPU.
#pragma once

#include <array>
#include <functional>
#include <optional>
#include "common/types.h"

namespace Shader {
struct Info;
}
namespace Shader::IR {
struct Program;
}

namespace OpenGL {

struct ComputeFill {
    u32 buffer;      ///< the stored buffer's slot in Info::buffers
    u64 first_dword; ///< from the buffer's base address
    u64 dwords;
    u32 value;
};

struct DispatchShape {
    std::array<u32, 3> groups;
    std::array<u32, 3> local_size;
};

/// A dword of a buffer the program reads, by slot and dword index; empty when it cannot be read.
using ReadBufferDword = std::function<std::optional<u32>(u32 buffer, u64 dword)>;

/// The fill `program` performs for this dispatch, or empty when it does anything else or cannot be proven to.
std::optional<ComputeFill> FindComputeFill(const Shader::IR::Program& program, const Shader::Info& info,
                                           const DispatchShape& shape, const ReadBufferDword& read);

} // namespace OpenGL
