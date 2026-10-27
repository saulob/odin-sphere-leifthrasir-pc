#include "osl_skip_intro.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define ANY (-1)

/* The notice task's SetScene(2) once its save data and trophy checks are done. */
static const short notice_bytes[] = {
    0x48, 0x8b, 0x03, 0x48, 0x8b, 0x80, 0xd8, 0x00, 0x00, 0x00, 0x49, 0x8b, 0x0e, 0x48, 0x3b, 0x4d, 0xe8, 0x75, ANY,
    0xbe, 0x02, 0x00, 0x00, 0x00, 0x48, 0x89, 0xdf, 0x48, 0x83, 0xc4, 0x10, 0x5b, 0x41, 0x5e, 0x5d, 0xff, 0xe0,
};
const OslSkipIntroCode osl_skip_intro_notice_code = {notice_bytes, sizeof(notice_bytes) / sizeof(*notice_bytes), 19};

/* The Refined title intro's three skip-input checks, then its skip, which first drops the title music task. */
static const short hd_title_bytes[] = {
    0x49, 0x8b, 0x04, 0x24, 0x4c, 0x89, 0xe7, 0xff, 0x90, 0x48, 0x02, 0x00, 0x00, 0x84, 0xc0, 0x75, ANY,
    0x49, 0x8b, 0x04, 0x24, 0x4c, 0x89, 0xe7, 0xff, 0x90, 0x00, 0x02, 0x00, 0x00, 0x84, 0xc0, 0x75, ANY,
    0x49, 0x8b, 0x04, 0x24, 0x4c, 0x89, 0xe7, 0xff, 0x90, 0x08, 0x02, 0x00, 0x00, 0x84, 0xc0, 0x0f, 0x84, ANY, ANY,
    ANY, ANY, 0x49, 0x8b, 0xbc, 0x24, 0x70, 0x02, 0x00, 0x00,
};
const OslSkipIntroCode osl_skip_intro_hd_title_code = {hd_title_bytes, sizeof(hd_title_bytes) / sizeof(*hd_title_bytes),
                                                       7};

/* The Classic title intro: its skip allowed, the end of its animation, then the first button check. */
static const short classic_title_bytes[] = {
    0xc6, 0x00, 0x01, 0xf6, 0x83, 0x59, 0x01, 0x00, 0x00, 0x20, 0x75, ANY, 0x48, 0x8d, 0x05, ANY, ANY, ANY, ANY,
    0x80, 0x38, 0x00, 0x0f, 0x84, ANY, ANY, ANY, ANY, 0x49, 0x8b, 0x04, 0x24, 0x31, 0xf6, 0x4c, 0x8b, 0xb0, 0xf0,
    0x00, 0x00, 0x00, 0x4c, 0x89, 0xf7, 0xe8, ANY, ANY, ANY, ANY, 0x84, 0xc0, 0x75, ANY,
};
const OslSkipIntroCode osl_skip_intro_classic_title_code = {
    classic_title_bytes, sizeof(classic_title_bytes) / sizeof(*classic_title_bytes), 44};

enum { HD_STUB = 0, CLASSIC_STUB = 48, STUB_JUMP = 24 };

static int fail(char *error, size_t size, const char *what, uint64_t offset) {
    snprintf(error, size, "%s at image offset 0x%llx", what, (unsigned long long)offset);
    return 0;
}

static int matches(const unsigned char *image, uint64_t code_end, const OslSkipIntroCode *code, uint32_t site) {
    if (site < code->site || site - code->site + code->count > code_end) return 0;
    const unsigned char *at = image + site - code->site;
    for (size_t i = 0; i < code->count; ++i)
        if (code->bytes[i] != ANY && at[i] != (unsigned char)code->bytes[i]) return 0;
    return 1;
}

static int32_t read32(const unsigned char *at) {
    int32_t value;
    memcpy(&value, at, 4);
    return value;
}

static void write32(unsigned char *at, int64_t value) {
    const int32_t narrow = (int32_t)value;
    memcpy(at, &narrow, 4);
}

/* push rdi; push rsi; push rax; movabs rax, claim; call rax; mov ecx, eax; pop rax; pop rsi; pop rdi;
 * test ecx, ecx; jne pressed; <the original check, as a jump>; pressed: mov eax, 1; ret
 * It stands in for a call, so the claim may clobber what a call clobbers. The pushes keep the check's arguments and
 * rax (the vtable of an indirect check) and align rsp to 16 bytes for the claim. */
static void write_stub(unsigned char *at, uint64_t claim, const unsigned char *jump, size_t jump_size) {
    static const unsigned char ask[] = {0x57, 0x56, 0x50, 0x48, 0xb8};
    static const unsigned char answer[] = {0xff, 0xd0, 0x89, 0xc1, 0x58, 0x5e, 0x5f, 0x85, 0xc9, 0x75};
    static const unsigned char pressed[] = {0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3};
    memcpy(at, ask, sizeof(ask));
    memcpy(at + sizeof(ask), &claim, 8);
    memcpy(at + sizeof(ask) + 8, answer, sizeof(answer));
    at[STUB_JUMP - 1] = (unsigned char)jump_size;
    memcpy(at + STUB_JUMP, jump, jump_size);
    memcpy(at + STUB_JUMP + jump_size, pressed, sizeof(pressed));
}

int osl_apply_skip_intro(unsigned char *image, uint64_t code_end, const OslSkipIntroSites *sites, uint64_t claim,
                         char *error, size_t error_size) {
    if (!matches(image, code_end, &osl_skip_intro_notice_code, sites->notice_scene))
        return fail(error, error_size, "unexpected code", sites->notice_scene);
    if (!matches(image, code_end, &osl_skip_intro_hd_title_code, sites->hd_title_check))
        return fail(error, error_size, "unexpected code", sites->hd_title_check);
    const uint32_t classic = sites->classic_title_check;
    if (!matches(image, code_end, &osl_skip_intro_classic_title_code, classic) ||
        (int64_t)classic + 5 + read32(image + classic + 1) != (int64_t)sites->classic_button)
        return fail(error, error_size, "unexpected code", classic);
    if ((uint64_t)sites->stubs + OSL_SKIP_INTRO_STUB_BYTES > code_end)
        return fail(error, error_size, "no room for the stubs", sites->stubs);
    for (size_t i = 0; i < OSL_SKIP_INTRO_STUB_BYTES; ++i)
        if (image[sites->stubs + i]) return fail(error, error_size, "stub space in use", sites->stubs);

    const uint64_t hd_stub = (uint64_t)sites->stubs + HD_STUB, classic_stub = (uint64_t)sites->stubs + CLASSIC_STUB;
    const uint32_t hd = sites->hd_title_check;
    unsigned char jump[6] = {0xff, 0xa0};
    memcpy(jump + 2, image + hd + 2, 4);
    write_stub(image + hd_stub, claim, jump, 6);
    jump[0] = 0xe9;
    write32(jump + 1, (int64_t)sites->classic_button - (int64_t)(classic_stub + STUB_JUMP + 5));
    write_stub(image + classic_stub, claim, jump, 5);

    image[hd] = 0xe8;
    write32(image + hd + 1, (int64_t)hd_stub - (int64_t)(hd + 5));
    image[hd + 5] = 0x90;
    write32(image + classic + 1, (int64_t)classic_stub - (int64_t)(classic + 5));
    image[sites->notice_scene + 1] = 3;
    return 1;
}

static atomic_int armed;

void osl_skip_intro_arm(void) {
    atomic_store(&armed, 1);
}

__attribute__((sysv_abi)) int osl_skip_intro_claim(void) {
    if (!atomic_exchange(&armed, 0)) return 0;
    puts("Skip intro: startup sequence skipped");
    return 1;
}
