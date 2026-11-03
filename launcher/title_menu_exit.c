#include "osl_title_menu_exit.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define ANY (-1)

typedef struct {
    const short *bytes;
    size_t count;
    size_t anchor; /* a byte that is neither a wildcard nor zero, to find candidates fast */
} Signature;

#define SIGNATURE(name, anchor, ...)                                              \
    static const short name##_bytes[] = {__VA_ARGS__};                            \
    static const Signature name = {name##_bytes, sizeof(name##_bytes) / sizeof(*name##_bytes), anchor}

#define FIELD_REFINED_ROW 0x80, 0x0c, 0x00, 0x00
#define FIELD_REFINED_COUNT 0x84, 0x0c, 0x00, 0x00
#define FIELD_CLASSIC_ROW 0x54, 0x08, 0x00, 0x00

SIGNATURE(refined_ctor, 0, 0xc7, 0x83, FIELD_REFINED_COUNT, 0x04, 0x00, 0x00, 0x00);
SIGNATURE(refined_up, 0, 0x89, 0x83, FIELD_REFINED_ROW, 0x83, 0xf8, 0x03, 0x0f, 0x85, ANY, ANY, ANY, ANY, 0xc7, 0x83,
          FIELD_REFINED_ROW, 0x02, 0x00, 0x00, 0x00);
SIGNATURE(refined_down, 0, 0x89, 0x83, FIELD_REFINED_ROW, 0x83, 0xf8, 0x03, 0x0f, 0x85, ANY, ANY, ANY, ANY, 0xc7, 0x83,
          FIELD_REFINED_ROW, 0x04, 0x00, 0x00, 0x00);
SIGNATURE(refined_confirm, 0, 0x8b, 0x83, FIELD_REFINED_ROW, 0x83, 0xf8, 0x01, 0x0f, 0x84, ANY, ANY, ANY, ANY, 0x83,
          0xf8, 0x03, 0x0f, 0x85, ANY, ANY, ANY, ANY, 0x48, 0x8b, 0x03, 0xbe, 0x08, 0x00, 0x00, 0x00, 0x48, 0x8b,
          0x80, 0xd8, 0x00, 0x00, 0x00, 0xe9, ANY, ANY, ANY, ANY);
SIGNATURE(refined_result, 0, 0x89, 0x83, 0x64, 0x02, 0x00, 0x00, 0x48, 0x89, 0xdf, 0x48, 0x8b, 0x03, 0x48, 0x83, 0xc4,
          0x08, 0x5b, 0x41, 0x5e, 0x41, 0x5f, 0x5d, 0xff, 0xa0, 0x68, 0x01, 0x00, 0x00);
SIGNATURE(refined_rows, 4, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00,
          0x00, 0x24, 0x00, 0x00, 0x00);

SIGNATURE(classic_down, 0, 0x8b, 0x83, FIELD_CLASSIC_ROW, 0x83, 0xf8, 0xff, 0x7c, 0x0f, 0xff, 0xc0, 0x41, 0xbe, 0x02,
          0x00, 0x00, 0x00, 0x83, 0xf8, 0x02, 0x44, 0x0f, 0x4e, 0xf0, 0x44, 0x89, 0xb3, FIELD_CLASSIC_ROW);
SIGNATURE(classic_other, 0, 0x85, 0xc0, 0x75, ANY, 0x49, 0x8b, 0x04, 0x24, 0x48, 0x8b, 0xb8, 0x18, 0x01, 0x00, 0x00,
          0x48, 0x85, 0xff, 0x74, ANY, 0xe8, ANY, ANY, ANY, ANY, 0x48, 0x8b, 0x03, 0xbe, 0x02, 0x00, 0x00, 0x00, 0xba,
          0x04, 0x00, 0x00, 0x00);
SIGNATURE(classic_lookup_a, 0, 0x48, 0x63, 0x83, FIELD_CLASSIC_ROW, 0x31, 0xf6, 0x48, 0x83, 0xf8, 0x02, 0x77, ANY, 0x48,
          0x8d, 0x0d, ANY, ANY, ANY, ANY, 0x8b, 0x34, 0x81);
SIGNATURE(classic_lookup_b, 0, 0x48, 0x63, 0x83, FIELD_CLASSIC_ROW, 0x48, 0x83, 0xf8, 0x02, 0x77, ANY, 0x48, 0x8d, 0x0d,
          ANY, ANY, ANY, ANY, 0x44, 0x8b, 0x34, 0x81);
SIGNATURE(classic_lookup_c, 0, 0x44, 0x8b, 0xb3, FIELD_CLASSIC_ROW, 0x31, 0xf6, 0x41, 0x83, 0xfe, 0x02, 0x77, ANY, 0x49,
          0x63, 0xc6, 0x48, 0x8d, 0x0d, ANY, ANY, ANY, ANY, 0x8b, 0x34, 0x81);

const OslTitleExitSignature *osl_title_exit_signatures(size_t *count) {
#define LISTED(name) {#name, name##_bytes, sizeof(name##_bytes) / sizeof(*name##_bytes)}
    static const OslTitleExitSignature listed[] = {
        LISTED(refined_ctor),     LISTED(refined_up),        LISTED(refined_down),      LISTED(refined_confirm),
        LISTED(refined_result),   LISTED(refined_rows),      LISTED(classic_down),      LISTED(classic_other),
        LISTED(classic_lookup_a), LISTED(classic_lookup_b),  LISTED(classic_lookup_c)};
#undef LISTED
    *count = sizeof(listed) / sizeof(*listed);
    return listed;
}

enum {
    REFINED_ROW_EXIT = 4,
    CLASSIC_ROW_EXIT = 3,
    REFINED_ROW_COUNT = 5,
    REFINED_BLOCK_AT = 24,
    REFINED_BLOCK_BYTES = 20,
    CLASSIC_OTHER_BYTES = 8,
    CLASSIC_TABLE_BYTES = 16,
    NO_SITE = -1
};

static int fail(char *error, size_t size, const char *what) {
    snprintf(error, size, "%s", what);
    return 0;
}

static int matches_at(const unsigned char *at, const Signature *signature) {
    for (size_t i = 0; i < signature->count; ++i)
        if (signature->bytes[i] != ANY && at[i] != (unsigned char)signature->bytes[i]) return 0;
    return 1;
}

/* 1 with the site in *found, 0 when absent, -1 when ambiguous. */
static int find_site(const unsigned char *image, uint64_t size, const Signature *signature, uint64_t *found) {
    if (size < signature->count) return 0;
    const unsigned char anchor = (unsigned char)signature->bytes[signature->anchor];
    const unsigned char *first = image + signature->anchor;
    const unsigned char *end = image + size - signature->count + signature->anchor + 1;
    int hits = 0;
    for (const unsigned char *at = first; at < end; ++at) {
        at = memchr(at, anchor, (size_t)(end - at));
        if (!at) break;
        const unsigned char *start = at - signature->anchor;
        if (!matches_at(start, signature)) continue;
        if (++hits > 1) return -1;
        *found = (uint64_t)(start - image);
    }
    return hits;
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

static int add_patch(OslTitleExitPlan *plan, const unsigned char *image, uint64_t at, const unsigned char *bytes,
                     size_t length) {
    if (plan->count >= OSL_TITLE_EXIT_MAX_PATCHES || length > OSL_TITLE_EXIT_PATCH_BYTES) return 0;
    OslTitleExitPatch *patch = &plan->patches[plan->count++];
    memset(patch, 0, sizeof(*patch));
    patch->at = at;
    patch->length = (uint8_t)length;
    memcpy(patch->expected, image + at, length);
    memcpy(patch->bytes, bytes, length);
    return 1;
}

static int add_byte(OslTitleExitPlan *plan, const unsigned char *image, uint64_t at, unsigned char value) {
    return add_patch(plan, image, at, &value, 1);
}

static size_t emit(OslTitleExitPlan *plan, const void *bytes, size_t length) {
    const size_t at = plan->stub_used;
    if (at + length > sizeof(plan->stub)) return (size_t)-1;
    memcpy(plan->stub + at, bytes, length);
    plan->stub_used += length;
    return at;
}

/* jmp rel32 from `from` (the jump's own address) to `target`, image offsets. */
static void make_jump(unsigned char *at, uint64_t from, uint64_t target) {
    at[0] = 0xe9;
    write32(at + 1, (int64_t)target - (int64_t)(from + 5));
}

static const unsigned char call_exit_head[] = {0x50, 0x50, 0x48, 0xb8}; /* push rax; push rax; movabs rax, */
static const unsigned char call_exit_tail[] = {0xff, 0xd0, 0x58, 0x58}; /* call rax; pop rax; pop rax */

static size_t emit_exit_call(OslTitleExitPlan *plan, uint64_t exit_request) {
    unsigned char call[sizeof(call_exit_head) + 8 + sizeof(call_exit_tail)];
    memcpy(call, call_exit_head, sizeof(call_exit_head));
    memcpy(call + sizeof(call_exit_head), &exit_request, 8);
    memcpy(call + sizeof(call_exit_head) + 8, call_exit_tail, sizeof(call_exit_tail));
    return emit(plan, call, sizeof(call));
}

static int plan_refined(const unsigned char *image, uint64_t size, uint64_t stub_offset, uint64_t exit_request,
                        OslTitleExitPlan *plan, char *error, size_t error_size) {
    uint64_t ctor, up, down, confirm, result, rows;
    const struct {
        const Signature *signature;
        uint64_t *site;
    } sites[] = {{&refined_ctor, &ctor}, {&refined_up, &up},         {&refined_down, &down},
                 {&refined_confirm, &confirm}, {&refined_result, &result}, {&refined_rows, &rows}};
    for (size_t i = 0; i < sizeof(sites) / sizeof(*sites); ++i) {
        const int found = find_site(image, size, sites[i].signature, sites[i].site);
        if (found != 1) return fail(error, error_size, found ? "Refined menu code is ambiguous" : "Refined menu code not found");
    }
    int32_t variants[REFINED_ROW_COUNT];
    memcpy(variants, image + rows, sizeof(variants));
    for (int i = 0; i < REFINED_ROW_COUNT; ++i) ++variants[i];
    if (!add_byte(plan, image, ctor + 6, REFINED_ROW_EXIT + 1) || !add_byte(plan, image, up + 8, 0xff) ||
        !add_byte(plan, image, down + 8, 0xff) || !add_patch(plan, image, rows, (const unsigned char *)variants, sizeof(variants)))
        return fail(error, error_size, "Refined menu patch plan is full");

    /* EXIT is row 4: the stub leaves the original result store for every other row. */
    const uint64_t stub_start = stub_offset + plan->stub_used;
    unsigned char head[] = {0x83, 0xf8, REFINED_ROW_EXIT, 0x75, 0x00};
    const size_t head_at = emit(plan, head, sizeof(head));
    const size_t call_at = emit_exit_call(plan, exit_request);
    static const unsigned char store[] = {0x89, 0x83, 0x64, 0x02, 0x00, 0x00};
    const size_t store_at = emit(plan, store, sizeof(store));
    unsigned char back[5];
    make_jump(back, stub_offset + plan->stub_used, result + 6);
    const size_t back_at = emit(plan, back, sizeof(back));
    if (head_at == (size_t)-1 || call_at == (size_t)-1 || store_at == (size_t)-1 || back_at == (size_t)-1)
        return fail(error, error_size, "no room for the Refined stub");
    plan->stub[head_at + 4] = (unsigned char)(store_at - (head_at + 5));
    unsigned char redirect[6] = {0};
    make_jump(redirect, result, stub_start);
    redirect[5] = 0x90;
    if (!add_patch(plan, image, result, redirect, sizeof(redirect))) return fail(error, error_size, "Refined menu patch plan is full");

    /* Row 3 is MODE now: it takes the generic result path with the value the game expects for MODE. */
    unsigned char block[REFINED_BLOCK_BYTES];
    memset(block, 0x90, sizeof(block));
    static const unsigned char store_mode[] = {0xc7, 0x83, 0x64, 0x02, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00};
    memcpy(block, store_mode, sizeof(store_mode));
    make_jump(block + sizeof(store_mode), confirm + REFINED_BLOCK_AT + sizeof(store_mode), result + 6);
    if (!add_patch(plan, image, confirm + REFINED_BLOCK_AT, block, sizeof(block)))
        return fail(error, error_size, "Refined menu patch plan is full");
    return 1;
}

static int plan_classic_table(const unsigned char *image, uint64_t size, uint64_t stub_offset, uint64_t lookup,
                              size_t bound_at, size_t disp_at, OslTitleExitPlan *plan, char *error,
                              size_t error_size) {
    const uint64_t disp_site = lookup + disp_at;
    const int64_t table = (int64_t)disp_site + 4 + read32(image + disp_site);
    if (table < 0 || (uint64_t)table + 12 > size) return fail(error, error_size, "Classic menu table is out of range");
    int32_t rows[4];
    memcpy(rows, image + table, 12);
    if (rows[1] - rows[0] != 6 || rows[2] - rows[1] != 12) return fail(error, error_size, "Classic menu table differs");
    const int32_t shown[4] = {rows[0] + 1, rows[1] + 1, rows[1] + 7, rows[2] + 1};
    const size_t at = emit(plan, shown, CLASSIC_TABLE_BYTES);
    if (at == (size_t)-1) return fail(error, error_size, "no room for the Classic tables");
    if (!add_byte(plan, image, lookup + bound_at, 3)) return fail(error, error_size, "Classic menu patch plan is full");
    unsigned char disp[4];
    write32(disp, (int64_t)(stub_offset + at) - (int64_t)(disp_site + 4));
    if (!add_patch(plan, image, disp_site, disp, 4)) return fail(error, error_size, "Classic menu patch plan is full");
    return 1;
}

static int plan_classic(const unsigned char *image, uint64_t size, uint64_t stub_offset, uint64_t exit_request,
                        OslTitleExitPlan *plan, char *error, size_t error_size) {
    uint64_t down, other, look_a, look_b, look_c;
    const struct {
        const Signature *signature;
        uint64_t *site;
    } sites[] = {{&classic_down, &down},       {&classic_other, &other},         {&classic_lookup_a, &look_a},
                 {&classic_lookup_b, &look_b}, {&classic_lookup_c, &look_c}};
    for (size_t i = 0; i < sizeof(sites) / sizeof(*sites); ++i) {
        const int found = find_site(image, size, sites[i].signature, sites[i].site);
        if (found != 1) return fail(error, error_size, found ? "Classic menu code is ambiguous" : "Classic menu code not found");
    }
    if (!plan_classic_table(image, size, stub_offset, look_a, 12, 18, plan, error, error_size) ||
        !plan_classic_table(image, size, stub_offset, look_b, 10, 16, plan, error, error_size) ||
        !plan_classic_table(image, size, stub_offset, look_c, 12, 21, plan, error, error_size))
        return 0;
    unsigned char clamp_row[4] = {3, 0, 0, 0};
    if (!add_patch(plan, image, down + 15, clamp_row, 4) || !add_byte(plan, image, down + 21, 3))
        return fail(error, error_size, "Classic menu patch plan is full");

    /* Rows other than the first reach this test; the stub keeps its two instructions and adds row 3. */
    const uint64_t stub_start = stub_offset + plan->stub_used;
    static const unsigned char test_row[] = {0x85, 0xc0, 0x75, 0x00};
    static const unsigned char load_object[] = {0x49, 0x8b, 0x04, 0x24};
    static const unsigned char is_exit[] = {0x83, 0xf8, CLASSIC_ROW_EXIT, 0x75, 0x00};
    const size_t test_at = emit(plan, test_row, sizeof(test_row));
    const size_t load_at = emit(plan, load_object, sizeof(load_object));
    unsigned char resume[5];
    make_jump(resume, stub_offset + plan->stub_used, other + CLASSIC_OTHER_BYTES);
    const size_t resume_at = emit(plan, resume, sizeof(resume));
    const size_t is_exit_at = emit(plan, is_exit, sizeof(is_exit));
    const size_t call_at = emit_exit_call(plan, exit_request);
    const int64_t leave = (int64_t)other + 4 + (signed char)image[other + 3];
    unsigned char out[5];
    make_jump(out, stub_offset + plan->stub_used, (uint64_t)leave);
    const size_t out_at = emit(plan, out, sizeof(out));
    if (test_at == (size_t)-1 || load_at == (size_t)-1 || resume_at == (size_t)-1 || is_exit_at == (size_t)-1 ||
        call_at == (size_t)-1 || out_at == (size_t)-1)
        return fail(error, error_size, "no room for the Classic stub");
    plan->stub[test_at + 3] = (unsigned char)(is_exit_at - (test_at + 4));
    plan->stub[is_exit_at + 4] = (unsigned char)(out_at - (is_exit_at + 5));
    unsigned char redirect[CLASSIC_OTHER_BYTES];
    memset(redirect, 0x90, sizeof(redirect));
    make_jump(redirect, other, stub_start);
    if (!add_patch(plan, image, other, redirect, sizeof(redirect))) return fail(error, error_size, "Classic menu patch plan is full");
    return 1;
}

int osl_title_exit_plan(const unsigned char *image, uint64_t image_size, uint64_t stub_offset, uint64_t exit_request,
                        OslTitleExitPlan *plan, char *error, size_t error_size) {
    memset(plan, 0, sizeof(*plan));
    return plan_refined(image, image_size, stub_offset, exit_request, plan, error, error_size) &&
           plan_classic(image, image_size, stub_offset, exit_request, plan, error, error_size);
}

int osl_title_exit_validate(const OslTitleExitPlan *plan, const unsigned char *image, uint64_t image_size,
                            uint64_t stub_offset, size_t stub_bytes, char *error, size_t error_size) {
    if (plan->stub_used > stub_bytes || plan->stub_used > sizeof(plan->stub))
        return fail(error, error_size, "stub space too small");
    if (stub_offset < image_size || stub_offset + stub_bytes < stub_offset)
        return fail(error, error_size, "stub space inside the game image");
    for (size_t i = 0; i < plan->count; ++i) {
        const OslTitleExitPatch *patch = &plan->patches[i];
        if (!patch->length || patch->length > OSL_TITLE_EXIT_PATCH_BYTES || patch->at + patch->length > image_size)
            return fail(error, error_size, "patch outside the game code");
        if (memcmp(image + patch->at, patch->expected, patch->length)) return fail(error, error_size, "unexpected game code");
        for (size_t j = i + 1; j < plan->count; ++j) {
            const OslTitleExitPatch *other = &plan->patches[j];
            if (patch->at < other->at + other->length && other->at < patch->at + patch->length)
                return fail(error, error_size, "overlapping patches");
        }
    }
    for (size_t i = 0; i < plan->stub_used; ++i)
        if (image[stub_offset + i]) return fail(error, error_size, "stub space in use");
    return 1;
}

int osl_apply_title_menu_exit(unsigned char *image, uint64_t image_size, uint64_t stub_offset, size_t stub_bytes,
                              uint64_t exit_request, char *error, size_t error_size) {
    OslTitleExitPlan plan;
    if (!osl_title_exit_plan(image, image_size, stub_offset, exit_request, &plan, error, error_size) ||
        !osl_title_exit_validate(&plan, image, image_size, stub_offset, stub_bytes, error, error_size))
        return 0;
    for (size_t i = 0; i < plan.count; ++i) memcpy(image + plan.patches[i].at, plan.patches[i].bytes, plan.patches[i].length);
    memcpy(image + stub_offset, plan.stub, plan.stub_used);
    return 1;
}

static void (*host_exit)(void);
static atomic_int requested;

void osl_title_menu_exit_set_host(void (*host)(void)) {
    host_exit = host;
}

void osl_title_menu_exit_rearm(void) {
    atomic_store(&requested, 0);
}

__attribute__((sysv_abi)) void osl_title_menu_exit_request(void) {
    if (atomic_exchange(&requested, 1)) return;
    puts("Title menu Exit Game: exit requested");
    if (host_exit) host_exit();
}
