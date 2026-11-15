// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <bit>
#include <limits>
#include <unordered_map>
#include <vector>

#include "shader_recompiler/info.h"
#include "shader_recompiler/ir/basic_block.h"
#include "shader_recompiler/ir/program.h"
#include "video_core/renderer_opengl/gl_compute_fill.h"

namespace OpenGL {
namespace {

using Shader::IR::Opcode;
namespace IR = Shader::IR;

constexpr u64 Unbounded = std::numeric_limits<u64>::max();

/// workgroup * x.workgroup + local * x.local + x.constant (mod 2^32), or nothing known.
struct Number {
    bool known = false;
    u32 workgroup = 0;
    u32 local = 0;
    u32 constant = 0;

    static Number Of(u32 value) {
        return {true, 0, 0, value};
    }
    bool IsConstant() const {
        return known && workgroup == 0 && local == 0;
    }
    bool operator==(const Number&) const = default;
};

/// True for the global invocation indices in [lo, hi), or a constant, or nothing known.
struct Condition {
    enum class Kind : u8 { Unknown, Constant, Range } kind = Kind::Unknown;
    bool value = false;
    u64 lo = 0;
    u64 hi = Unbounded;

    static Condition Of(bool value) {
        return {Kind::Constant, value};
    }
    static Condition Between(u64 lo, u64 hi) {
        return lo >= hi ? Of(false) : Condition{Kind::Range, false, lo, hi};
    }
    bool operator==(const Condition&) const = default;
};

Condition Intersect(const Condition& a, const Condition& b) {
    using Kind = Condition::Kind;
    if (a.kind == Kind::Unknown || b.kind == Kind::Unknown) {
        return (a.kind == Kind::Constant && !a.value) || (b.kind == Kind::Constant && !b.value) ? Condition::Of(false)
                                                                                                : Condition{};
    }
    if (a.kind == Kind::Constant) {
        return a.value ? b : a;
    }
    if (b.kind == Kind::Constant) {
        return b.value ? a : b;
    }
    return Condition::Between(std::max(a.lo, b.lo), std::min(a.hi, b.hi));
}

class Analysis {
public:
    Analysis(const Shader::Info& info_, const DispatchShape& shape_, const ReadBufferDword& read_)
        : info{info_}, shape{shape_}, read{read_} {}

    std::optional<ComputeFill> Run(const IR::Program& program) {
        std::vector<Condition> path{Condition::Of(true)};
        for (const auto& node : program.syntax_list) {
            switch (node.type) {
            case IR::AbstractSyntaxNode::Type::Block:
                for (const IR::Inst& inst : *node.data.block) {
                    if (!Step(inst, path.back())) {
                        return std::nullopt;
                    }
                }
                break;
            case IR::AbstractSyntaxNode::Type::If:
                path.push_back(Intersect(path.back(), ConditionOf(IR::Value{node.data.if_node.cond})));
                break;
            case IR::AbstractSyntaxNode::Type::EndIf:
                if (path.size() < 2) {
                    return std::nullopt;
                }
                path.pop_back();
                break;
            case IR::AbstractSyntaxNode::Type::Return:
            case IR::AbstractSyntaxNode::Type::Unreachable:
                if (path.size() != 1) {
                    return std::nullopt;
                }
                break;
            default:
                return std::nullopt;
            }
        }
        if (!store) {
            return std::nullopt;
        }
        const u64 invocations = u64(shape.groups[0]) * shape.local_size[0];
        const Condition reached = Intersect(store->path, Condition::Between(0, invocations));
        if (reached.kind == Condition::Kind::Constant) {
            return ComputeFill{store->buffer, store->offset, 0, store->value};
        }
        return ComputeFill{store->buffer, store->offset + reached.lo, reached.hi - reached.lo, store->value};
    }

private:
    struct Store {
        u32 buffer;
        u64 offset;
        u32 value;
        Condition path;
    };

    /// The global invocation index plus a constant, as a number of this program is when it is one.
    std::optional<u32> GlobalOffset(const Number& n) const {
        if (!n.known || n.local != 1 || n.workgroup != shape.local_size[0]) {
            return std::nullopt;
        }
        // A wrap past 2^32 inside the dispatch would break the ranges below.
        const u64 invocations = u64(shape.groups[0]) * shape.local_size[0];
        if (u64(n.constant) + invocations > (u64{1} << 32)) {
            return std::nullopt;
        }
        return n.constant;
    }

    Number NumberOf(const IR::Value& value) const {
        if (value.IsImmediate()) {
            switch (value.Type()) {
            case IR::Type::U32:
                return Number::Of(value.U32());
            case IR::Type::F32:
                return Number::Of(std::bit_cast<u32>(value.F32()));
            default:
                return {};
            }
        }
        const auto it = numbers.find(value.Inst());
        return it == numbers.end() ? Number{} : it->second;
    }

    static std::optional<u32> Slot(const IR::Value& handle) {
        return handle.IsImmediate() && handle.Type() == IR::Type::U32 ? std::optional<u32>{handle.U32()}
                                                                      : std::nullopt;
    }

    using Vector = std::array<Number, 4>;

    Vector VectorOf(const IR::Value& value) const {
        if (value.IsImmediate()) {
            return {};
        }
        const auto it = vectors.find(value.Inst());
        return it == vectors.end() ? Vector{} : it->second;
    }

    Condition ConditionOf(const IR::Value& value) const {
        if (value.IsImmediate()) {
            return value.Type() == IR::Type::U1 ? Condition::Of(value.U1()) : Condition{};
        }
        const auto it = conditions.find(value.Inst());
        return it == conditions.end() ? Condition{} : it->second;
    }

    // `x < bound` for the global index form of x; `inclusive` makes it `x <= bound`.
    Condition Below(const Number& x, const Number& bound, bool inclusive) const {
        const auto offset = GlobalOffset(x);
        if (!offset || !bound.IsConstant()) {
            return {};
        }
        const u64 limit = u64(bound.constant) + (inclusive ? 1 : 0);
        return Condition::Between(0, limit > *offset ? limit - *offset : 0);
    }

    // `x > bound`; `inclusive` makes it `x >= bound`.
    Condition Above(const Number& x, const Number& bound, bool inclusive) const {
        const auto offset = GlobalOffset(x);
        if (!offset || !bound.IsConstant()) {
            return {};
        }
        const u64 limit = u64(bound.constant) + (inclusive ? 0 : 1);
        return Condition::Between(limit > *offset ? limit - *offset : 0, Unbounded);
    }

    Condition Compare(Opcode op, const Number& a, const Number& b) const {
        if (a.IsConstant() && b.IsConstant()) {
            const u32 x = a.constant, y = b.constant;
            const s32 sx = s32(x), sy = s32(y);
            switch (op) {
            case Opcode::ULessThan32:
                return Condition::Of(x < y);
            case Opcode::ULessThanEqual32:
                return Condition::Of(x <= y);
            case Opcode::UGreaterThan32:
                return Condition::Of(x > y);
            case Opcode::UGreaterThanEqual32:
                return Condition::Of(x >= y);
            case Opcode::SLessThan32:
                return Condition::Of(sx < sy);
            case Opcode::SLessThanEqual32:
                return Condition::Of(sx <= sy);
            case Opcode::SGreaterThan32:
                return Condition::Of(sx > sy);
            case Opcode::SGreaterThanEqual32:
                return Condition::Of(sx >= sy);
            case Opcode::IEqual32:
                return Condition::Of(x == y);
            case Opcode::INotEqual32:
                return Condition::Of(x != y);
            default:
                return {};
            }
        }
        switch (op) {
        case Opcode::ULessThan32:
            return b.IsConstant() ? Below(a, b, false) : Above(b, a, false);
        case Opcode::ULessThanEqual32:
            return b.IsConstant() ? Below(a, b, true) : Above(b, a, true);
        case Opcode::UGreaterThan32:
            return b.IsConstant() ? Above(a, b, false) : Below(b, a, false);
        case Opcode::UGreaterThanEqual32:
            return b.IsConstant() ? Above(a, b, true) : Below(b, a, true);
        default:
            return {};
        }
    }

    Number Attribute(const IR::Inst& inst) const {
        const auto attribute = inst.Arg(0).Attribute();
        if (!inst.Arg(1).IsImmediate()) {
            return {};
        }
        const u32 component = inst.Arg(1).U32();
        if (component > 2) {
            return {};
        }
        const bool workgroup = attribute == IR::Attribute::WorkgroupId;
        if (!workgroup && attribute != IR::Attribute::LocalInvocationId) {
            return {};
        }
        if (component != 0) {
            const u32 extent = workgroup ? shape.groups[component] : shape.local_size[component];
            return extent == 1 ? Number::Of(0) : Number{};
        }
        return workgroup ? Number{true, 1, 0, 0} : Number{true, 0, 1, 0};
    }

    Number Load(const IR::Inst& inst) {
        const auto slot = Slot(inst.Arg(0));
        if (!slot) {
            return {};
        }
        const u32 buffer = *slot;
        loaded_buffers.push_back(buffer);
        const Number address = NumberOf(inst.Arg(1));
        if (!address.IsConstant() || (store && store->buffer == buffer)) {
            return {};
        }
        const auto value = read(buffer, address.constant);
        return value ? Number::Of(*value) : Number{};
    }

    bool Step(const IR::Inst& inst, const Condition& path) {
        const auto arg = [&](size_t i) { return NumberOf(inst.Arg(i)); };
        const auto both = [&](auto op) {
            const Number a = arg(0), b = arg(1);
            return a.IsConstant() && b.IsConstant() ? Number::Of(op(a.constant, b.constant)) : Number{};
        };
        const Opcode op = inst.GetOpcode();
        switch (op) {
        case Opcode::Void:
        case Opcode::Prologue:
        case Opcode::Epilogue:
        case Opcode::Barrier:
        case Opcode::WorkgroupMemoryBarrier:
        case Opcode::DeviceMemoryBarrier:
        case Opcode::SubgroupBarrier:
            return true;
        case Opcode::StoreBufferU32:
        case Opcode::StoreBufferF32:
            return Record(inst, path);
        case Opcode::GetUserData: {
            const u32 reg = u32(inst.Arg(0).ScalarReg());
            numbers[&inst] = reg < info.user_data.size() ? Number::Of(info.user_data[reg]) : Number{};
            return true;
        }
        case Opcode::GetAttributeU32:
            numbers[&inst] = Attribute(inst);
            return true;
        case Opcode::LoadBufferU32:
        case Opcode::LoadBufferF32:
        case Opcode::ReadConstBuffer:
            numbers[&inst] = Load(inst);
            return true;
        case Opcode::BitCastU32F32:
        case Opcode::BitCastF32U32:
            numbers[&inst] = arg(0);
            return true;
        case Opcode::CompositeConstructU32x2:
        case Opcode::CompositeConstructU32x3:
        case Opcode::CompositeConstructU32x4:
        case Opcode::CompositeConstructF32x2:
        case Opcode::CompositeConstructF32x3:
        case Opcode::CompositeConstructF32x4: {
            Vector& vector = vectors[&inst];
            for (size_t i = 0; i < std::min<size_t>(inst.NumArgs(), vector.size()); ++i) {
                vector[i] = arg(i);
            }
            return true;
        }
        case Opcode::CompositeExtractU32x2:
        case Opcode::CompositeExtractU32x3:
        case Opcode::CompositeExtractU32x4:
        case Opcode::CompositeExtractF32x2:
        case Opcode::CompositeExtractF32x3:
        case Opcode::CompositeExtractF32x4: {
            const Number index = arg(1);
            numbers[&inst] = index.IsConstant() && index.constant < 4 ? VectorOf(inst.Arg(0))[index.constant]
                                                                       : Number{};
            return true;
        }
        case Opcode::CompositeInsertU32x2:
        case Opcode::CompositeInsertU32x3:
        case Opcode::CompositeInsertU32x4:
        case Opcode::CompositeInsertF32x2:
        case Opcode::CompositeInsertF32x3:
        case Opcode::CompositeInsertF32x4: {
            Vector vector = VectorOf(inst.Arg(0));
            const Number index = arg(2);
            if (index.IsConstant() && index.constant < 4) {
                vector[index.constant] = arg(1);
            } else {
                vector = {};
            }
            vectors[&inst] = vector;
            return true;
        }
        case Opcode::CompositeShuffleU32x2:
        case Opcode::CompositeShuffleU32x3:
        case Opcode::CompositeShuffleU32x4:
        case Opcode::CompositeShuffleF32x2:
        case Opcode::CompositeShuffleF32x3:
        case Opcode::CompositeShuffleF32x4: {
            const size_t width = inst.NumArgs() - 2;
            const Vector a = VectorOf(inst.Arg(0)), b = VectorOf(inst.Arg(1));
            Vector& vector = vectors[&inst];
            for (size_t i = 0; i < width && i < vector.size(); ++i) {
                const Number index = arg(2 + i);
                if (index.IsConstant() && index.constant < 2 * width) {
                    vector[i] = index.constant < width ? a[index.constant] : b[index.constant - width];
                }
            }
            return true;
        }
        case Opcode::IAdd32:
        case Opcode::ISub32: {
            const Number a = arg(0), b = arg(1);
            const bool add = op == Opcode::IAdd32;
            numbers[&inst] = a.known && b.known
                                 ? Number{true, add ? a.workgroup + b.workgroup : a.workgroup - b.workgroup,
                                          add ? a.local + b.local : a.local - b.local,
                                          add ? a.constant + b.constant : a.constant - b.constant}
                                 : Number{};
            return true;
        }
        case Opcode::IMul32:
        case Opcode::ShiftLeftLogical32: {
            Number a = arg(0), b = arg(1);
            if (op == Opcode::ShiftLeftLogical32) {
                b = b.IsConstant() && b.constant < 32 ? Number::Of(1u << b.constant) : Number{};
            } else if (a.IsConstant()) {
                std::swap(a, b);
            }
            numbers[&inst] = a.known && b.IsConstant()
                                 ? Number{true, a.workgroup * b.constant, a.local * b.constant, a.constant * b.constant}
                                 : Number{};
            return true;
        }
        case Opcode::ShiftRightLogical32:
            numbers[&inst] = both([](u32 x, u32 y) { return y < 32 ? x >> y : 0u; });
            return true;
        case Opcode::BitwiseAnd32:
            numbers[&inst] = both([](u32 x, u32 y) { return x & y; });
            return true;
        case Opcode::BitwiseOr32:
            numbers[&inst] = both([](u32 x, u32 y) { return x | y; });
            return true;
        case Opcode::SelectU32:
        case Opcode::SelectF32: {
            const Condition c = ConditionOf(inst.Arg(0));
            const Number a = arg(1), b = arg(2);
            numbers[&inst] = c.kind == Condition::Kind::Constant ? (c.value ? a : b) : (a == b ? a : Number{});
            return true;
        }
        case Opcode::ULessThan32:
        case Opcode::ULessThanEqual32:
        case Opcode::UGreaterThan32:
        case Opcode::UGreaterThanEqual32:
        case Opcode::SLessThan32:
        case Opcode::SLessThanEqual32:
        case Opcode::SGreaterThan32:
        case Opcode::SGreaterThanEqual32:
        case Opcode::IEqual32:
        case Opcode::INotEqual32:
            conditions[&inst] = Compare(op, arg(0), arg(1));
            return true;
        case Opcode::ConditionRef:
            conditions[&inst] = ConditionOf(inst.Arg(0));
            return true;
        case Opcode::LogicalAnd:
            conditions[&inst] = Intersect(ConditionOf(inst.Arg(0)), ConditionOf(inst.Arg(1)));
            return true;
        case Opcode::LogicalNot: {
            const Condition c = ConditionOf(inst.Arg(0));
            if (c.kind == Condition::Kind::Constant) {
                conditions[&inst] = Condition::Of(!c.value);
            } else if (c.kind == Condition::Kind::Range && c.lo == 0) {
                conditions[&inst] = Condition::Between(c.hi, Unbounded);
            } else if (c.kind == Condition::Kind::Range && c.hi == Unbounded) {
                conditions[&inst] = Condition::Between(0, c.lo);
            }
            return true;
        }
        case Opcode::Phi: {
            if (inst.NumArgs() == 0) {
                return true;
            }
            const Number first = arg(0);
            const Condition first_condition = ConditionOf(inst.Arg(0));
            bool same_number = true, same_condition = true;
            for (size_t i = 1; i < inst.NumArgs(); ++i) {
                same_number &= arg(i) == first;
                same_condition &= ConditionOf(inst.Arg(i)) == first_condition;
            }
            if (same_number) {
                numbers[&inst] = first;
            }
            if (same_condition) {
                conditions[&inst] = first_condition;
            }
            return true;
        }
        default:
            return !inst.MayHaveSideEffects();
        }
    }

    bool Record(const IR::Inst& inst, const Condition& path) {
        if (path.kind == Condition::Kind::Constant && !path.value) {
            return true;
        }
        const auto offset = GlobalOffset(NumberOf(inst.Arg(1)));
        const Number value = NumberOf(inst.Arg(2));
        const auto slot = Slot(inst.Arg(0));
        if (store || path.kind == Condition::Kind::Unknown || !offset || !value.IsConstant() || !slot) {
            return false;
        }
        const u32 buffer = *slot;
        if (std::ranges::any_of(loaded_buffers, [&](u32 b) { return b == buffer; })) {
            return false;
        }
        store = Store{buffer, *offset, value.constant, path};
        return true;
    }

    const Shader::Info& info;
    const DispatchShape& shape;
    const ReadBufferDword& read;
    std::unordered_map<const IR::Inst*, Number> numbers;
    std::unordered_map<const IR::Inst*, Condition> conditions;
    std::unordered_map<const IR::Inst*, Vector> vectors;
    std::vector<u32> loaded_buffers;
    std::optional<Store> store;
};

} // namespace

std::optional<ComputeFill> FindComputeFill(const Shader::IR::Program& program, const Shader::Info& info,
                                           const DispatchShape& shape, const ReadBufferDword& read) {
    if (shape.local_size[0] == 0) {
        return std::nullopt;
    }
    return Analysis{info, shape, read}.Run(program);
}

} // namespace OpenGL
