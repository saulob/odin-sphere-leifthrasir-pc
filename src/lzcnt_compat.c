#include "lzcnt_compat.h"

#include <cpuid.h>
#include <string.h>

enum { STUB_STRIDE = 96, JMP_REL32 = 5, REL32_REACH = 0x7ff00000 };

const LzcntShape lzcnt_shapes[LZCNT_RANGE_COUNT] = {
    {5, 0, {0x66, 0xf3, 0x0f, 0xbd, 0xe8}},
    {5, 0, {0x66, 0xf3, 0x0f, 0xbd, 0xc8}},
    {10, 0, {0xf3, 0x4b, 0x0f, 0xbd, 0xb4, 0x37, 0xb0, 0x03, 0x00, 0x00}},
    {5, 0, {0xf3, 0x48, 0x0f, 0xbd, 0xca}},
    {10, 0, {0xf3, 0x4b, 0x0f, 0xbd, 0x84, 0x37, 0xc8, 0x03, 0x00, 0x00}},
    {10, 0, {0xf3, 0x4b, 0x0f, 0xbd, 0x84, 0x37, 0xd8, 0x03, 0x00, 0x00}},
    {10, 0, {0xf3, 0x4b, 0x0f, 0xbd, 0x84, 0x37, 0xe8, 0x03, 0x00, 0x00}},
    {10, 0, {0xf3, 0x49, 0x0f, 0xbd, 0x94, 0x07, 0xf8, 0x03, 0x00, 0x00}},
    {10, 0, {0xf3, 0x4b, 0x0f, 0xbd, 0x84, 0x07, 0xb0, 0x03, 0x00, 0x00}},
    {5, 0, {0xf3, 0x48, 0x0f, 0xbd, 0xca}},
    {10, 0, {0xf3, 0x4b, 0x0f, 0xbd, 0x84, 0x07, 0xc8, 0x03, 0x00, 0x00}},
    {10, 0, {0xf3, 0x4b, 0x0f, 0xbd, 0x84, 0x07, 0xd8, 0x03, 0x00, 0x00}},
    {10, 0, {0xf3, 0x4b, 0x0f, 0xbd, 0x84, 0x07, 0xe8, 0x03, 0x00, 0x00}},
    {10, 0, {0xf3, 0x4b, 0x0f, 0xbd, 0x84, 0x07, 0xf8, 0x03, 0x00, 0x00}},
    {7, 0, {0xf3, 0x0f, 0xbd, 0xc1, 0x89, 0x4d, 0x9c}},
    {5, 0, {0xf3, 0x48, 0x0f, 0xbd, 0xdf}},
    {5, 0, {0xf3, 0x48, 0x0f, 0xbd, 0xeb}},
    {5, 0, {0xf3, 0x48, 0x0f, 0xbd, 0xcb}},
    {5, 0, {0xf3, 0x41, 0x0f, 0xbd, 0xc1}},
    {10, 0, {0xf3, 0x0f, 0xbd, 0xc7, 0x41, 0xba, 0x09, 0x00, 0x00, 0x00}},
    {7, 0, {0xf3, 0x0f, 0xbd, 0xcf, 0x83, 0xc1, 0xfb}},
    {7, 0, {0xf3, 0x0f, 0xbd, 0xc9, 0xc1, 0xe0, 0x05}},
    {5, 0, {0xf3, 0x48, 0x0f, 0xbd, 0xd0}},
    {5, 0, {0xf3, 0x49, 0x0f, 0xbd, 0xdb}},
    {5, 0, {0xf3, 0x49, 0x0f, 0xbd, 0xff}},
    {10, 0, {0xf3, 0x0f, 0xbd, 0xc6, 0x41, 0xba, 0x09, 0x00, 0x00, 0x00}},
    {5, 0, {0xf3, 0x41, 0x0f, 0xbd, 0xc3}},
    {6, 0, {0xf3, 0x0f, 0xbd, 0xd6, 0x89, 0xf6}},
    {6, 0, {0xf3, 0x0f, 0xbd, 0xf2, 0x89, 0xd2}},
    {6, 0, {0xf3, 0x0f, 0xbd, 0xc6, 0x29, 0xc2}},
    {9, 0, {0xf3, 0x0f, 0xbd, 0xc7, 0xba, 0x1f, 0x00, 0x00, 0x00}},
    {9, 0, {0xf3, 0x0f, 0xbd, 0xcf, 0xb8, 0x1f, 0x00, 0x00, 0x00}},
    {5, 0, {0xf3, 0x48, 0x0f, 0xbd, 0xc2}},
    {5, 0, {0xf3, 0x49, 0x0f, 0xbd, 0xc5}},
    {10, 0, {0xf3, 0x0f, 0xbd, 0xd7, 0x41, 0xb9, 0x09, 0x00, 0x00, 0x00}},
    {8, 0, {0xf3, 0x0f, 0xbd, 0xd6, 0x41, 0x83, 0xc1, 0x09}},
    {9, 4, {0xf3, 0x0f, 0xbd, 0xd1, 0xf3, 0x41, 0x0f, 0xbd, 0xc1}},
    {5, 0, {0xf3, 0x44, 0x0f, 0xbd, 0xc1}},
    {5, 0, {0xf3, 0x41, 0x0f, 0xbd, 0xc1}},
    {5, 0, {0xf3, 0x44, 0x0f, 0xbd, 0xc6}},
    {5, 0, {0xf3, 0x41, 0x0f, 0xbd, 0xc1}},
    {5, 0, {0xf3, 0x49, 0x0f, 0xbd, 0xea}},
    {5, 0, {0xf3, 0x48, 0x0f, 0xbd, 0xc6}},
    {5, 0, {0xf3, 0x4d, 0x0f, 0xbd, 0xfa}},
    {5, 0, {0xf3, 0x48, 0x0f, 0xbd, 0xc6}},
    {5, 0, {0xf3, 0x4d, 0x0f, 0xbd, 0xf9}},
    {5, 0, {0xf3, 0x48, 0x0f, 0xbd, 0xc6}},
    {5, 0, {0xf3, 0x44, 0x0f, 0xbd, 0xce}},
    {7, 0, {0xf3, 0x0f, 0xbd, 0xd7, 0x45, 0x89, 0xc8}},
};

const LzcntBuild lzcnt_builds[LZCNT_BUILD_COUNT] = {
    {"CUSA01290 1.00 Japan", 0xff4570, 0x675dc0,
     {
      0x67b380, 0x67b450, 0x67c80c, 0x67c823, 0x67c8c8, 0x67c959, 0x67c9ea, 0x67ca33,
      0x67cc70, 0x67cc7f, 0x67cd3c, 0x67cdd6, 0x67ce6e, 0x67ce98, 0x67cfe0, 0xdd361c,
      0xdd3649, 0xdd36df, 0xdd3854, 0xdd3873, 0xdd3901, 0xdd3af6, 0xdd3b7f, 0xdd458c,
      0xdd45aa, 0xdd4c76, 0xdd4c90, 0xdd5268, 0xdd5b23, 0xdd5b7e, 0xdd6008, 0xdd6048,
      0xdd6919, 0xdd6937, 0xdd7075, 0xdd7090, 0xdd7f62, 0xdd7fdf, 0xdd7fe4, 0xdd801b,
      0xdd8020, 0xdd82ba, 0xdd82bf, 0xdd8352, 0xdd8357, 0xdd8398, 0xdd839d, 0xdd862a,
      0xdd862f,
     }},
    {"CUSA01290 1.02 Japan", 0xf94570, 0x67c850,
     {
      0x681e10, 0x681ee0, 0x68329c, 0x6832b3, 0x683358, 0x6833e9, 0x68347a, 0x6834c3,
      0x683700, 0x68370f, 0x6837cc, 0x683866, 0x6838fe, 0x683928, 0x683a70, 0xd7361c,
      0xd73649, 0xd736df, 0xd73854, 0xd73873, 0xd73901, 0xd73af6, 0xd73b7f, 0xd7458c,
      0xd745aa, 0xd74c76, 0xd74c90, 0xd75268, 0xd75b23, 0xd75b7e, 0xd76008, 0xd76048,
      0xd76919, 0xd76937, 0xd77075, 0xd77090, 0xd77f62, 0xd77fdf, 0xd77fe4, 0xd7801b,
      0xd78020, 0xd782ba, 0xd782bf, 0xd78352, 0xd78357, 0xd78398, 0xd7839d, 0xd7862a,
      0xd7862f,
     }},
    {"CUSA05083 1.00 USA", 0xf04570, 0x67eee0,
     {
      0x6844a0, 0x684570, 0x68592c, 0x685943, 0x6859e8, 0x685a79, 0x685b0a, 0x685b53,
      0x685d90, 0x685d9f, 0x685e5c, 0x685ef6, 0x685f8e, 0x685fb8, 0x686100, 0xce361c,
      0xce3649, 0xce36df, 0xce3854, 0xce3873, 0xce3901, 0xce3af6, 0xce3b7f, 0xce458c,
      0xce45aa, 0xce4c76, 0xce4c90, 0xce5268, 0xce5b23, 0xce5b7e, 0xce6008, 0xce6048,
      0xce6919, 0xce6937, 0xce7075, 0xce7090, 0xce7f62, 0xce7fdf, 0xce7fe4, 0xce801b,
      0xce8020, 0xce82ba, 0xce82bf, 0xce8352, 0xce8357, 0xce8398, 0xce839d, 0xce862a,
      0xce862f,
     }},
    {"CUSA05262 1.00 Europe", 0xf04570, 0x680220,
     {
      0x6857e0, 0x6858b0, 0x686c6c, 0x686c83, 0x686d28, 0x686db9, 0x686e4a, 0x686e93,
      0x6870d0, 0x6870df, 0x68719c, 0x687236, 0x6872ce, 0x6872f8, 0x687440, 0xce361c,
      0xce3649, 0xce36df, 0xce3854, 0xce3873, 0xce3901, 0xce3af6, 0xce3b7f, 0xce458c,
      0xce45aa, 0xce4c76, 0xce4c90, 0xce5268, 0xce5b23, 0xce5b7e, 0xce6008, 0xce6048,
      0xce6919, 0xce6937, 0xce7075, 0xce7090, 0xce7f62, 0xce7fdf, 0xce7fe4, 0xce801b,
      0xce8020, 0xce82ba, 0xce82bf, 0xce8352, 0xce8357, 0xce8398, 0xce839d, 0xce862a,
      0xce862f,
     }},
    {"CUSA05262 1.01 Europe", 0xf04570, 0x6803d0,
     {
      0x685990, 0x685a60, 0x686e1c, 0x686e33, 0x686ed8, 0x686f69, 0x686ffa, 0x687043,
      0x687280, 0x68728f, 0x68734c, 0x6873e6, 0x68747e, 0x6874a8, 0x6875f0, 0xce361c,
      0xce3649, 0xce36df, 0xce3854, 0xce3873, 0xce3901, 0xce3af6, 0xce3b7f, 0xce458c,
      0xce45aa, 0xce4c76, 0xce4c90, 0xce5268, 0xce5b23, 0xce5b7e, 0xce6008, 0xce6048,
      0xce6919, 0xce6937, 0xce7075, 0xce7090, 0xce7f62, 0xce7fdf, 0xce7fe4, 0xce801b,
      0xce8020, 0xce82ba, 0xce82bf, 0xce8352, 0xce8357, 0xce8398, 0xce839d, 0xce862a,
      0xce862f,
     }},
};

typedef struct {
    uint8_t bytes[STUB_STRIDE];
    size_t length;
    int overflow;
} Code;

typedef struct {
    unsigned bits, dst;
    size_t length, rep;
} Form;

static void put(Code *c, const void *data, size_t length) {
    if (c->length + length > sizeof c->bytes) {
        c->overflow = 1;
        return;
    }
    memcpy(c->bytes + c->length, data, length);
    c->length += length;
}

static void put_byte(Code *c, unsigned value) {
    const uint8_t byte = (uint8_t)value;
    put(c, &byte, 1);
}

static void put_prefixes(Code *c, unsigned bits, unsigned reg, unsigned rm) {
    if (bits == 16) put_byte(c, 0x66);
    const unsigned rex = (bits == 64 ? 8u : 0u) | (reg >= 8 ? 4u : 0u) | (rm >= 8 ? 1u : 0u);
    if (rex) put_byte(c, 0x40 | rex);
}

/* [66] F3 [REX] 0F BD /r with a register or a base/index memory operand, and its length. */
static int parse_lzcnt(const uint8_t *p, size_t available, Form *form) {
    size_t i = 0, rep = (size_t)-1;
    int operand16 = 0;
    for (; i < available && (p[i] == 0x66 || p[i] == 0xf3); ++i) {
        if (p[i] == 0x66) operand16 = 1;
        else if (rep == (size_t)-1) rep = i;
        else return 0;
    }
    unsigned rex = 0;
    if (i < available && (p[i] & 0xf0) == 0x40) rex = p[i++];
    if (rep == (size_t)-1 || i + 3 > available || p[i] != 0x0f || p[i + 1] != 0xbd) return 0;
    const uint8_t modrm = p[i + 2];
    i += 3;
    const unsigned mod = modrm >> 6, rm = modrm & 7;
    if (mod != 3 && rm == 4) {
        if (i >= available) return 0;
        const uint8_t sib = p[i++];
        if (mod == 0 && (sib & 7) == 5) i += 4;
    } else if (mod == 0 && rm == 5) {
        return 0;
    }
    i += mod == 1 ? 1 : mod == 2 ? 4 : 0;
    if (i > available) return 0;
    form->bits = rex & 8 ? 64 : operand16 ? 16 : 32;
    form->dst = (modrm >> 3 & 7) | (rex & 4 ? 8 : 0);
    form->length = i;
    form->rep = rep;
    return 1;
}

/* BSR with the same operands, then the result LZCNT gives: width - 1 - index, or width with CF set for zero. */
static void emit_lzcnt(Code *c, const uint8_t *insn, const Form *form) {
    for (size_t i = 0; i < form->length; ++i)
        if (i != form->rep) put_byte(c, insn[i]);

    const unsigned bits = form->bits, dst = form->dst;
    Code nonzero = {{0}, 0, 0}, zero = {{0}, 0, 0};
    put_prefixes(&nonzero, bits, 0, dst);
    put_byte(&nonzero, 0x83);
    put_byte(&nonzero, 0xf0 | (dst & 7));
    put_byte(&nonzero, bits - 1);

    const uint32_t width = bits;
    put_prefixes(&zero, bits, 0, dst);
    if (bits == 64) {
        put_byte(&zero, 0xc7);
        put_byte(&zero, 0xc0 | (dst & 7));
    } else {
        put_byte(&zero, 0xb8 | (dst & 7));
    }
    put(&zero, &width, bits == 16 ? 2 : 4);
    put_prefixes(&zero, bits, dst, dst);
    put_byte(&zero, 0x85);
    put_byte(&zero, 0xc0 | (dst & 7) << 3 | (dst & 7));
    put_byte(&zero, 0xf9);

    put_byte(c, 0x74);
    put_byte(c, (unsigned)nonzero.length + 2);
    put(c, nonzero.bytes, nonzero.length);
    put_byte(c, 0xeb);
    put_byte(c, (unsigned)zero.length);
    put(c, zero.bytes, zero.length);
    c->overflow |= nonzero.overflow | zero.overflow;
}

static int build_stub(const LzcntShape *shape, const uint8_t *stub, const uint8_t *resume, Code *c) {
    size_t at = 0;
    const size_t starts[2] = {0, shape->second};
    for (size_t k = 0; k < (shape->second ? 2u : 1u); ++k) {
        Form form;
        if (starts[k] < at || starts[k] >= shape->length ||
            !parse_lzcnt(shape->bytes + starts[k], shape->length - starts[k], &form))
            return 0;
        put(c, shape->bytes + at, starts[k] - at);
        emit_lzcnt(c, shape->bytes + starts[k], &form);
        at = starts[k] + form.length;
    }
    put(c, shape->bytes + at, shape->length - at);
    const int64_t distance = (int64_t)(intptr_t)resume - (int64_t)(intptr_t)(stub + c->length + JMP_REL32);
    const int32_t rel = (int32_t)distance;
    put_byte(c, 0xe9);
    put(c, &rel, 4);
    return !c->overflow && distance == rel;
}

const char *lzcnt_compat_status_text(LzcntCompatStatus status) {
    switch (status) {
    case LZCNT_COMPAT_OK: return "ok";
    case LZCNT_COMPAT_UNKNOWN_IMAGE: return "not a supported game image";
    case LZCNT_COMPAT_BAD_SITE: return "unexpected bytes at a site";
    case LZCNT_COMPAT_NO_ROOM: return "stub area too small or too far";
    }
    return "";
}

int lzcnt_compat_needed(IsaHostFeatures host) {
    return !(host.ext1_ecx & bit_LZCNT);
}

size_t lzcnt_compat_stub_bytes(void) {
    return LZCNT_RANGE_COUNT * STUB_STRIDE;
}

size_t lzcnt_compat_instructions(void) {
    size_t count = 0;
    for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i) count += lzcnt_shapes[i].second ? 2 : 1;
    return count;
}

LzcntCompatStatus lzcnt_compat_apply(uint8_t *image, uint64_t size, const uint32_t *offsets, const LzcntShape *shapes,
                                     size_t count, uint8_t *stubs, size_t stubs_size) {
    if (count > SIZE_MAX / STUB_STRIDE || count * STUB_STRIDE > stubs_size) return LZCNT_COMPAT_NO_ROOM;
    if ((uintptr_t)stubs < (uintptr_t)image + size && (uintptr_t)image < (uintptr_t)stubs + stubs_size)
        return LZCNT_COMPAT_NO_ROOM;
    for (size_t i = 0; i < count; ++i) {
        const LzcntShape *shape = &shapes[i];
        const uint8_t *site = image + offsets[i], *stub = stubs + i * STUB_STRIDE;
        if (shape->length < JMP_REL32 || shape->length > LZCNT_SHAPE_MAX ||
            (uint64_t)offsets[i] + shape->length > size || memcmp(site, shape->bytes, shape->length))
            return LZCNT_COMPAT_BAD_SITE;
        for (size_t j = 0; j < i; ++j)
            if (offsets[i] < offsets[j] + shapes[j].length && offsets[j] < offsets[i] + shape->length)
                return LZCNT_COMPAT_BAD_SITE;
        const int64_t distance = (int64_t)(intptr_t)stub - (int64_t)(intptr_t)site;
        if (distance > REL32_REACH || distance < -REL32_REACH) return LZCNT_COMPAT_NO_ROOM;
        Code scratch = {{0}, 0, 0};
        if (!build_stub(shape, stub, site + shape->length, &scratch)) return LZCNT_COMPAT_BAD_SITE;
    }
    for (size_t i = 0; i < count; ++i) {
        uint8_t *site = image + offsets[i], *stub = stubs + i * STUB_STRIDE;
        Code code = {{0}, 0, 0};
        build_stub(&shapes[i], stub, site + shapes[i].length, &code);
        memset(stub, 0xcc, STUB_STRIDE);
        memcpy(stub, code.bytes, code.length);
        const int32_t rel = (int32_t)((int64_t)(intptr_t)stub - (int64_t)(intptr_t)(site + JMP_REL32));
        site[0] = 0xe9;
        memcpy(site + 1, &rel, 4);
        memset(site + JMP_REL32, 0x90, shapes[i].length - JMP_REL32);
    }
    return LZCNT_COMPAT_OK;
}

LzcntCompatStatus lzcnt_compat_install(uint8_t *image, uint64_t size, uint64_t entry, uint8_t *stubs, size_t stubs_size,
                                       const char **build) {
    for (size_t i = 0; i < LZCNT_BUILD_COUNT; ++i) {
        if (lzcnt_builds[i].size != size || lzcnt_builds[i].entry != entry) continue;
        if (build) *build = lzcnt_builds[i].build;
        return lzcnt_compat_apply(image, size, lzcnt_builds[i].offsets, lzcnt_shapes, LZCNT_RANGE_COUNT, stubs,
                                  stubs_size);
    }
    return LZCNT_COMPAT_UNKNOWN_IMAGE;
}
