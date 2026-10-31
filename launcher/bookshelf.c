#include "osl_bookshelf.h"

#include <stdio.h>
#include <string.h>

#define ANY (-1)

enum {
    SAVE_UNLOCKED = 0x184c5,
    CALL_BYTES = 5,
    SIGNATURE_BYTES = OSL_BOOKSHELF_SIGNATURE_BYTES,
    ACCESSOR_BYTES = 22,
    TOTAL_BYTES = OSL_BOOKSHELF_TOTAL_BYTES,
    TOTAL_TEST_BYTES = 7,
    STUB_SPACE = 0x400,
    REL32_REACH = 0x7ff00000,
    READ_STATE = 2
};

static const short accessor_shape[ACCESSOR_BYTES] = {
    0x48, 0x8d, 0x05, ANY, ANY, ANY, ANY, 0x48, 0x8b, 0x00, 0x48, 0x8b, 0xb8, 0x88, 0x01, 0x00, 0x00, 0xe9, ANY, ANY, ANY, ANY,
};

/* cmp byte [rax+0x18567],1; sbb ecx,-1; mov eax,ecx: the entry count's last term, Special. */
const unsigned char osl_bookshelf_total_code[TOTAL_BYTES] = {0x80, 0xb8, 0x67, 0x85, 0x01, 0x00, 0x01, 0x83, 0xd9, 0xff, 0x89, 0xc8};

/* clc; a six byte nop: the term counts Special as unlocked. */
static const unsigned char total_patch[TOTAL_TEST_BYTES] = {0xf8, 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00};

/* What follows each getter call, build by build the same. */
const unsigned char osl_bookshelf_read_signatures[OSL_BOOKSHELF_READS][SIGNATURE_BYTES] = {
    {0x48, 0x89, 0x45, 0xb8, 0x66, 0x0f, 0x1f, 0x84, 0x00}, {0x80, 0xbc, 0x18, 0xc5, 0x84, 0x01, 0x00, 0x00, 0x75},
    {0x48, 0x89, 0xc1, 0x48, 0x63, 0xd3, 0xb0, 0x01, 0x80}, {0x42, 0x80, 0x3c, 0x38, 0x00, 0x75, 0x71, 0x49, 0x8d},
    {0x80, 0x3c, 0x18, 0x00, 0x75, 0x28, 0x48, 0x8d, 0x43}, {0x42, 0x80, 0xbc, 0x30, 0xc6, 0x84, 0x01, 0x00, 0x00},
    {0x80, 0xbc, 0x18, 0xc6, 0x84, 0x01, 0x00, 0x00, 0x75}, {0x48, 0x8d, 0xb8, 0xc5, 0x84, 0x01, 0x00, 0x44, 0x89},
    {0x49, 0x8b, 0xbe, 0xd0, 0x17, 0x00, 0x00, 0x80, 0xbc}, {0x80, 0xbc, 0x18, 0xc5, 0x84, 0x01, 0x00, 0x00, 0x0f},
    {0x80, 0xbc, 0x18, 0xc5, 0x84, 0x01, 0x00, 0x00, 0x0f}, {0x42, 0x80, 0xbc, 0x20, 0xc5, 0x84, 0x01, 0x00, 0x00},
    {0x41, 0x81, 0xff, 0xa3, 0x00, 0x00, 0x00, 0x77, 0x68}, {0x48, 0x89, 0xc1, 0xb0, 0x01, 0x42, 0x80, 0xbc, 0x31},
};
const unsigned char osl_bookshelf_in_place_page_signature[SIGNATURE_BYTES] = {0x42, 0x0f, 0xbe, 0x9c, 0x20, 0x68, 0x85, 0x01, 0x00};
enum { PAGE_READ = 7 };

const unsigned char osl_bookshelf_mark_signatures[OSL_BOOKSHELF_MARKS][SIGNATURE_BYTES] = {
    {0x48, 0x85, 0xc0, 0x74, 0x08, 0xc6, 0x84, 0x18, 0xc5}, {0x48, 0x85, 0xc0, 0x74, 0x09, 0x42, 0xc6, 0x84, 0x30},
};

int osl_bookshelf_entry(int slot) {
    return (slot >= 0 && slot <= 54) || (slot >= 102 && slot <= OSL_BOOKSHELF_SPECIAL_SLOT);
}

int osl_bookshelf_pages(int slot) {
    if (slot == OSL_BOOKSHELF_SPECIAL_SLOT || !osl_bookshelf_entry(slot)) return 0;
    if (slot == 2 || slot == 3 || slot == 33) return 2;
    return slot == 34 ? 3 : 1;
}

void osl_bookshelf_fill_view(unsigned char *view, const unsigned char *unlocked, const unsigned char *pages) {
    for (int slot = 0; slot < OSL_BOOKSHELF_SLOTS; ++slot) {
        unsigned char state = unlocked[slot], count = pages[slot];
        if (osl_bookshelf_entry(slot)) {
            if (!state) state = READ_STATE;
            if (count < osl_bookshelf_pages(slot)) count = (unsigned char)osl_bookshelf_pages(slot);
        }
        view[slot] = state;
        view[OSL_BOOKSHELF_SLOTS + slot] = count;
    }
}

__attribute__((sysv_abi)) void *osl_bookshelf_view(const unsigned char *save) {
    static _Thread_local unsigned char view[2 * OSL_BOOKSHELF_SLOTS];
    if (!save) return NULL;
    osl_bookshelf_fill_view(view, save + SAVE_UNLOCKED, save + SAVE_UNLOCKED + OSL_BOOKSHELF_SLOTS);
    return view - SAVE_UNLOCKED;
}

__attribute__((sysv_abi)) void *osl_bookshelf_mark(unsigned char *save, long slot) {
    if (!save || slot < 0 || slot >= OSL_BOOKSHELF_SLOTS || !save[SAVE_UNLOCKED + slot]) return NULL;
    return save;
}

static int fail(char *error, size_t size, const char *what, uint32_t offset) {
    snprintf(error, size, "unlock bookshelf: %s at image offset 0x%x", what, (unsigned)offset);
    return 0;
}

static int32_t read32(const unsigned char *at) {
    int32_t value;
    memcpy(&value, at, 4);
    return value;
}

static int call_matches(const unsigned char *image, uint64_t size, uint32_t offset, uint32_t accessor,
                        const unsigned char *signature) {
    if ((uint64_t)offset + CALL_BYTES + SIGNATURE_BYTES > size || image[offset] != 0xe8) return 0;
    return (int64_t)offset + CALL_BYTES + read32(image + offset + 1) == (int64_t)accessor &&
           !memcmp(image + offset + CALL_BYTES, signature, SIGNATURE_BYTES);
}

static int in_reach(const unsigned char *at, const unsigned char *target) {
    const int64_t delta = (int64_t)(intptr_t)target - (int64_t)(intptr_t)at;
    return delta > -(int64_t)REL32_REACH && delta < (int64_t)REL32_REACH;
}

/* A call at `final` that is written to `at`. */
static void put_call(unsigned char *at, const unsigned char *final, const unsigned char *target) {
    const int32_t delta = (int32_t)((intptr_t)target - (intptr_t)(final + CALL_BYTES));
    at[0] = 0xe8;
    memcpy(at + 1, &delta, 4);
}

static size_t put_bytes(unsigned char *code, size_t at, const unsigned char *bytes, size_t count) {
    memcpy(code + at, bytes, count);
    return at + count;
}

static size_t spill_xmm(unsigned char *code, size_t at, int store) {
    for (int i = 0; i < 16; ++i) {
        const int32_t offset = i * 16;
        if (i >= 8) code[at++] = 0x44;
        code[at++] = 0x0f;
        code[at++] = store ? 0x11 : 0x10;
        code[at++] = (unsigned char)(0x84 + 8 * (i & 7));
        code[at++] = 0x24;
        memcpy(code + at, &offset, 4);
        at += 4;
    }
    return at;
}

/* A stand-in for the getter call: every register but rax comes back as it went in, the getter runs, then
 * host(save, rbx or r14). slot_register: 0 none, 1 rbx, 2 r14. */
static size_t emit_stub(unsigned char *code, const unsigned char *final, const unsigned char *accessor,
                        const void *host, int slot_register) {
    static const unsigned char push[] = {0x57, 0x56, 0x52, 0x51, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x48, 0x81, 0xec, 0x08, 0x01, 0x00, 0x00};
    static const unsigned char pop[] = {0x48, 0x81, 0xc4, 0x08, 0x01, 0x00, 0x00, 0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, 0x59, 0x5a, 0x5e, 0x5f, 0xc3};
    static const unsigned char save_to_rdi[] = {0x48, 0x89, 0xc7};
    static const unsigned char slot_rbx[] = {0x48, 0x89, 0xde}, slot_r14[] = {0x4c, 0x89, 0xf6};
    static const unsigned char call_host[] = {0xff, 0xd0};
    const uintptr_t target = (uintptr_t)host;
    size_t n = put_bytes(code, 0, push, sizeof(push));
    n = spill_xmm(code, n, 1);
    put_call(code + n, final + n, accessor);
    n += CALL_BYTES;
    n = put_bytes(code, n, save_to_rdi, sizeof(save_to_rdi));
    if (slot_register == 1) n = put_bytes(code, n, slot_rbx, sizeof(slot_rbx));
    if (slot_register == 2) n = put_bytes(code, n, slot_r14, sizeof(slot_r14));
    code[n++] = 0x48;
    code[n++] = 0xb8;
    memcpy(code + n, &target, 8);
    n += 8;
    n = put_bytes(code, n, call_host, sizeof(call_host));
    n = spill_xmm(code, n, 0);
    return put_bytes(code, n, pop, sizeof(pop));
}

int osl_apply_bookshelf(unsigned char *image, uint64_t image_size, const OslBookshelfSites *sites,
                        unsigned char *stubs, size_t stubs_size, char *error, size_t error_size) {
    if (stubs_size < OSL_BOOKSHELF_STUB_BYTES || !in_reach(image, stubs) || !in_reach(image + image_size, stubs))
        return fail(error, error_size, "no room for the stubs", 0);
    if ((uint64_t)sites->accessor + ACCESSOR_BYTES > image_size)
        return fail(error, error_size, "site outside the image", sites->accessor);
    for (int i = 0; i < ACCESSOR_BYTES; ++i)
        if (accessor_shape[i] != ANY && image[sites->accessor + i] != (unsigned char)accessor_shape[i])
            return fail(error, error_size, "unexpected save data getter", sites->accessor);
    if ((uint64_t)sites->total + TOTAL_BYTES > image_size || memcmp(image + sites->total, osl_bookshelf_total_code, TOTAL_BYTES))
        return fail(error, error_size, "unexpected code", sites->total);
    for (int i = 0; i < OSL_BOOKSHELF_READS; ++i) {
        const unsigned char *signature =
            i == PAGE_READ && sites->in_place_page_read ? osl_bookshelf_in_place_page_signature : osl_bookshelf_read_signatures[i];
        if (!call_matches(image, image_size, sites->reads[i], sites->accessor, signature))
            return fail(error, error_size, "unexpected code", sites->reads[i]);
    }
    for (int i = 0; i < OSL_BOOKSHELF_MARKS; ++i)
        if (!call_matches(image, image_size, sites->marks[i], sites->accessor, osl_bookshelf_mark_signatures[i]))
            return fail(error, error_size, "unexpected code", sites->marks[i]);

    unsigned char *const stub[3] = {stubs, stubs + STUB_SPACE, stubs + 2 * STUB_SPACE};
    const unsigned char *accessor = image + sites->accessor;
    unsigned char code[3][STUB_SPACE];
    memset(code, 0xcc, sizeof(code));
    if (emit_stub(code[0], stub[0], accessor, (const void *)osl_bookshelf_view, 0) > STUB_SPACE ||
        emit_stub(code[1], stub[1], accessor, (const void *)osl_bookshelf_mark, 1) > STUB_SPACE ||
        emit_stub(code[2], stub[2], accessor, (const void *)osl_bookshelf_mark, 2) > STUB_SPACE)
        return fail(error, error_size, "no room for the stubs", 0);
    for (int i = 0; i < 3; ++i)
        if (!in_reach(accessor, stub[i])) return fail(error, error_size, "no room for the stubs", 0);

    for (int i = 0; i < 3; ++i) memcpy(stub[i], code[i], STUB_SPACE);
    for (int i = 0; i < OSL_BOOKSHELF_READS; ++i) put_call(image + sites->reads[i], image + sites->reads[i], stub[0]);
    for (int i = 0; i < OSL_BOOKSHELF_MARKS; ++i)
        put_call(image + sites->marks[i], image + sites->marks[i], stub[1 + i]);
    memcpy(image + sites->total, total_patch, TOTAL_TEST_BYTES);
    return 1;
}
