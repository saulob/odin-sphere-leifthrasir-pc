// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/div_ceil.h"
#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"

namespace Shader::Backend::SPIRV {
namespace {

// GCN reads 0 past the end of the LDS allocation and drops such writes; Vulkan leaves
// out-of-range workgroup memory accesses undefined.
u32 NumSharedElements(const EmitContext& ctx, u32 shift) {
    return Common::DivCeil(ctx.shared_memory_size, 1U << shift);
}

Id LoadShared(EmitContext& ctx, Id offset, u32 shift, Id type, Id pointer_type, Id variable,
              Id zero) {
    const u32 num_elements = NumSharedElements(ctx, shift);
    const Id index{ctx.OpShiftRightLogical(ctx.U32[1], offset, ctx.ConstU32(shift))};
    const Id in_range{ctx.OpULessThan(ctx.U1[1], index, ctx.ConstU32(num_elements))};
    const Id last{ctx.ConstU32(num_elements > 0 ? num_elements - 1 : 0U)};
    const Id safe_index{ctx.OpUMin(ctx.U32[1], index, last)};
    const Id pointer{ctx.EmitSharedMemoryAccess(pointer_type, variable, safe_index)};
    return ctx.OpSelect(type, in_range, ctx.OpLoad(type, pointer), zero);
}

void WriteShared(EmitContext& ctx, Id offset, u32 shift, Id pointer_type, Id variable, Id value) {
    const u32 num_elements = NumSharedElements(ctx, shift);
    const Id index{ctx.OpShiftRightLogical(ctx.U32[1], offset, ctx.ConstU32(shift))};
    const Id in_range{ctx.OpULessThan(ctx.U1[1], index, ctx.ConstU32(num_elements))};
    const Id store_label{ctx.OpLabel()};
    const Id merge_label{ctx.OpLabel()};
    ctx.OpSelectionMerge(merge_label, spv::SelectionControlMask::MaskNone);
    ctx.OpBranchConditional(in_range, store_label, merge_label);
    ctx.AddLabel(store_label);
    ctx.OpStore(ctx.EmitSharedMemoryAccess(pointer_type, variable, index), value);
    ctx.OpBranch(merge_label);
    ctx.AddLabel(merge_label);
}

} // Anonymous namespace

Id EmitLoadSharedU16(EmitContext& ctx, Id offset) {
    return LoadShared(ctx, offset, 1U, ctx.U16, ctx.shared_u16, ctx.shared_memory_u16,
                      ctx.u16_zero_value);
}

Id EmitLoadSharedU32(EmitContext& ctx, Id offset) {
    return LoadShared(ctx, offset, 2U, ctx.U32[1], ctx.shared_u32, ctx.shared_memory_u32,
                      ctx.u32_zero_value);
}

Id EmitLoadSharedU64(EmitContext& ctx, Id offset) {
    return LoadShared(ctx, offset, 3U, ctx.U64, ctx.shared_u64, ctx.shared_memory_u64,
                      ctx.u64_zero_value);
}

void EmitWriteSharedU16(EmitContext& ctx, Id offset, Id value) {
    WriteShared(ctx, offset, 1U, ctx.shared_u16, ctx.shared_memory_u16, value);
}

void EmitWriteSharedU32(EmitContext& ctx, Id offset, Id value) {
    WriteShared(ctx, offset, 2U, ctx.shared_u32, ctx.shared_memory_u32, value);
}

void EmitWriteSharedU64(EmitContext& ctx, Id offset, Id value) {
    WriteShared(ctx, offset, 3U, ctx.shared_u64, ctx.shared_memory_u64, value);
}

} // namespace Shader::Backend::SPIRV
