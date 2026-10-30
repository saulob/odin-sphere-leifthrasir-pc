#include "isa_emulate.h"
#include "lzcnt_compat.h"
#include <cpuid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

static int failures;
#define CHECK(condition) do { if (!(condition)) { printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
enum { CF = 1 << 0, ZF = 1 << 6, SF = 1 << 7, OF = 1 << 11, OTHER_FLAGS = 0x202 };

static size_t run(const uint8_t *code, size_t size, uint64_t rip, uint64_t gpr[16]) {
    uint8_t padded[32] = {0};
    memcpy(padded, code, size);
    return isa_emulate_movbe(padded, rip, gpr);
}

static size_t run_flags(const uint8_t *code, size_t size, uint64_t gpr[16], uint64_t *flags) {
    uint8_t padded[32] = {0};
    memcpy(padded, code, size);
    return isa_emulate(padded, 0, gpr, flags);
}

static void movbe_game_forms(void) {
    uint8_t memory[0x100] = {0};
    uint64_t gpr[16] = {0}, flags = OTHER_FLAGS | CF | ZF | SF | OF;

    const uint8_t store64[] = {0x48, 0x0f, 0x38, 0xf1, 0x45, 0x80}; /* movbe [rbp-0x80], rax */
    gpr[RBP] = (uintptr_t)memory + 0x80;
    gpr[RAX] = 0x65006e006f004e00ull;
    CHECK(run_flags(store64, sizeof store64, gpr, &flags) == sizeof store64);
    const uint8_t expected[8] = {0x65, 0x00, 0x6e, 0x00, 0x6f, 0x00, 0x4e, 0x00};
    CHECK(memcmp(memory, expected, 8) == 0);
    CHECK(gpr[RBP] == (uintptr_t)memory + 0x80 && gpr[RAX] == 0x65006e006f004e00ull);
    CHECK(flags == (OTHER_FLAGS | CF | ZF | SF | OF));

    const uint8_t load64[] = {0x48, 0x0f, 0x38, 0xf0, 0x56, 0x20}; /* movbe rdx, [rsi+0x20] */
    memset(memory, 0, sizeof memory);
    for (unsigned i = 0; i < 8; ++i) memory[0x20 + i] = (uint8_t)(0x11 * (i + 1));
    gpr[RSI] = (uintptr_t)memory;
    CHECK(run_flags(load64, sizeof load64, gpr, &flags) == sizeof load64);
    CHECK(gpr[RDX] == 0x1122334455667788ull);

    const uint8_t store16[] = {0x66, 0x0f, 0x38, 0xf1, 0x02}; /* movbe [rdx], ax */
    memset(memory, 0xcc, sizeof memory);
    gpr[RDX] = (uintptr_t)memory;
    gpr[RAX] = 0xdeadbeef00001234ull;
    CHECK(run(store16, sizeof store16, 0, gpr) == sizeof store16);
    CHECK(memory[0] == 0x12 && memory[1] == 0x34 && memory[2] == 0xcc && gpr[RAX] == 0xdeadbeef00001234ull);

    const uint8_t store32_r8[] = {0x44, 0x0f, 0x38, 0xf1, 0x47, 0x18}; /* movbe [rdi+0x18], r8d */
    gpr[RDI] = (uintptr_t)memory;
    gpr[R8] = 0xffffffff12345678ull;
    CHECK(run(store32_r8, sizeof store32_r8, 0, gpr) == sizeof store32_r8);
    CHECK(memory[0x18] == 0x12 && memory[0x1b] == 0x78 && memory[0x1c] == 0xcc);

    const uint8_t store32_r11[] = {0x41, 0x0f, 0x38, 0xf1, 0x03}; /* movbe [r11], eax */
    gpr[R11] = (uintptr_t)memory + 0x40;
    gpr[RAX] = 0x01020304;
    CHECK(run(store32_r11, sizeof store32_r11, 0, gpr) == sizeof store32_r11);
    CHECK(memory[0x40] == 0x01 && memory[0x43] == 0x04);

    const uint8_t store64_r13[] = {0x4c, 0x0f, 0x38, 0xf1, 0x6f, 0x60}; /* movbe [rdi+0x60], r13 */
    gpr[R13] = 0x0102030405060708ull;
    CHECK(run(store64_r13, sizeof store64_r13, 0, gpr) == sizeof store64_r13);
    CHECK(memory[0x60] == 0x01 && memory[0x67] == 0x08);
}

static void movbe_widths(void) {
    uint8_t memory[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint64_t gpr[16];
    const uint8_t load64[] = {0x48, 0x0f, 0x38, 0xf0, 0x00};
    memset(gpr, 0xaa, sizeof gpr);
    gpr[RAX] = (uintptr_t)memory;
    CHECK(run(load64, sizeof load64, 0, gpr) == 5);
    CHECK(gpr[RAX] == 0x1122334455667788ull);

    const uint8_t load32[] = {0x0f, 0x38, 0xf0, 0x08};
    gpr[RAX] = (uintptr_t)memory;
    gpr[RCX] = ~0ull;
    CHECK(run(load32, sizeof load32, 0, gpr) == 4);
    CHECK(gpr[RCX] == 0x11223344u);

    const uint8_t load16[] = {0x66, 0x0f, 0x38, 0xf0, 0x08};
    gpr[RAX] = (uintptr_t)memory;
    gpr[RCX] = 0xffffffffffffffffull;
    CHECK(run(load16, sizeof load16, 0, gpr) == 5);
    CHECK(gpr[RCX] == 0xffffffffffff1122ull);

    const uint8_t store32[] = {0x0f, 0x38, 0xf1, 0x08};
    memset(memory, 0xcc, sizeof memory);
    gpr[RAX] = (uintptr_t)memory;
    gpr[RCX] = 0xdeadbeef12345678ull;
    CHECK(run(store32, sizeof store32, 0, gpr) == 4);
    CHECK(memory[0] == 0x12 && memory[3] == 0x78 && memory[4] == 0xcc);
}

static void addressing_forms(void) {
    uint8_t memory[256] = {0};
    uint64_t gpr[16] = {0};
    const uint8_t sib_disp8[] = {0x48, 0x0f, 0x38, 0xf1, 0x74, 0x3b, 0x48};
    gpr[RBX] = (uintptr_t)memory;
    gpr[RDI] = 8;
    gpr[RSI] = 0x0102030405060708ull;
    CHECK(run(sib_disp8, sizeof sib_disp8, 0, gpr) == 7);
    CHECK(memory[0x48 + 8] == 0x01 && memory[0x48 + 15] == 0x08);

    const uint8_t extended[] = {0x66, 0x44, 0x0f, 0x38, 0xf1, 0x59, 0x32};
    gpr[RCX] = (uintptr_t)memory;
    gpr[R11] = 0xabcd;
    CHECK(run(extended, sizeof extended, 0, gpr) == 7);
    CHECK(memory[0x32] == 0xab && memory[0x33] == 0xcd);

    const uint8_t r12_base[] = {0x49, 0x0f, 0x38, 0xf0, 0x04, 0x24};
    gpr[R12] = (uintptr_t)memory + 0x50;
    CHECK(run(r12_base, sizeof r12_base, 0, gpr) == 6);
    CHECK(gpr[RAX] == 0x0102030405060708ull);

    const uint8_t scaled_index_r9[] = {0x4a, 0x0f, 0x38, 0xf1, 0x14, 0xc8};
    memset(memory, 0, sizeof memory);
    gpr[RAX] = (uintptr_t)memory;
    gpr[R9] = 2;
    gpr[RDX] = 0x1122334455667788ull;
    CHECK(run(scaled_index_r9, sizeof scaled_index_r9, 0, gpr) == 6);
    CHECK(memory[16] == 0x11 && memory[23] == 0x88);

    const uint8_t no_index[] = {0x48, 0x0f, 0x38, 0xf1, 0x14, 0x24};
    gpr[RSP] = (uintptr_t)memory;
    CHECK(run(no_index, sizeof no_index, 0, gpr) == 6);
    CHECK(memory[0] == 0x11);

    const uint8_t disp32[] = {0x48, 0x0f, 0x38, 0xf1, 0x87, 0xc0, 0x00, 0x00, 0x00}; /* movbe [rdi+0xc0], rax */
    uint8_t large[0x100] = {0};
    gpr[RDI] = (uintptr_t)large;
    gpr[RAX] = 0x0102030405060708ull;
    CHECK(run(disp32, sizeof disp32, 0, gpr) == sizeof disp32);
    CHECK(large[0xc0] == 0x01 && large[0xc7] == 0x08);

    const uint8_t rip_relative[] = {0x48, 0x0f, 0x38, 0xf1, 0x05, 0x10, 0x00, 0x00, 0x00};
    uint8_t target[16] = {0};
    gpr[RAX] = 0x0102030405060708ull;
    CHECK(run(rip_relative, sizeof rip_relative, (uintptr_t)target - 0x10 - sizeof rip_relative, gpr) == sizeof rip_relative);
    CHECK(target[0] == 0x01 && target[7] == 0x08);
}

static void movbe_rejected(void) {
    uint64_t gpr[16] = {0};
    uint64_t before[16];
    uint8_t memory[8] = {0};
    gpr[RAX] = (uintptr_t)memory;
    memcpy(before, gpr, sizeof gpr);

    const uint8_t register_form[] = {0x48, 0x0f, 0x38, 0xf1, 0xc1};
    const uint8_t crc32[] = {0xf2, 0x48, 0x0f, 0x38, 0xf1, 0x00};
    const uint8_t other_opcode[] = {0x48, 0x0f, 0x38, 0xf2, 0x00};
    const uint8_t not_0f38[] = {0x48, 0x0f, 0x3a, 0xf1, 0x00};
    const uint8_t ud2[] = {0x0f, 0x0b};
    const uint8_t segment[] = {0x65, 0x48, 0x0f, 0x38, 0xf1, 0x00};
    const uint8_t address_size[] = {0x67, 0x48, 0x0f, 0x38, 0xf1, 0x00};
    const uint8_t rex_before_66[] = {0x48, 0x66, 0x0f, 0x38, 0xf1, 0x00};
    CHECK(run(register_form, sizeof register_form, 0, gpr) == 0);
    CHECK(run(crc32, sizeof crc32, 0, gpr) == 0);
    CHECK(run(other_opcode, sizeof other_opcode, 0, gpr) == 0);
    CHECK(run(not_0f38, sizeof not_0f38, 0, gpr) == 0);
    CHECK(run(ud2, sizeof ud2, 0, gpr) == 0);
    CHECK(run(segment, sizeof segment, 0, gpr) == 0);
    CHECK(run(address_size, sizeof address_size, 0, gpr) == 0);
    CHECK(run(rex_before_66, sizeof rex_before_66, 0, gpr) == 0);
    CHECK(memcmp(gpr, before, sizeof gpr) == 0);
    CHECK(memory[0] == 0 && memory[7] == 0);
}

static void strlen_site(void) {
    /* The instruction that stopped issue 7 on an Ivy Bridge: libc strlen, andn rax, rax, rcx. */
    const uint8_t andn_rax_rax_rcx[] = {0xc4, 0xe2, 0xf8, 0xf2, 0xc1};
    uint64_t gpr[16] = {0}, flags = OTHER_FLAGS | CF | OF | ZF | SF;
    gpr[RAX] = 0x486362694c656353ull;
    gpr[RCX] = 0x476261684b646252ull;
    CHECK(run_flags(andn_rax_rax_rcx, sizeof andn_rax_rax_rcx, gpr, &flags) == 5);
    CHECK(gpr[RAX] == (~0x486362694c656353ull & 0x476261684b646252ull));
    CHECK(gpr[RCX] == 0x476261684b646252ull);
    CHECK(flags == OTHER_FLAGS);
}

static void andn_forms(void) {
    uint64_t gpr[16] = {0}, flags = OTHER_FLAGS;
    const uint8_t andn_esi_ecx_eax[] = {0xc4, 0xe2, 0x70, 0xf2, 0xf0};
    gpr[RSI] = ~0ull;
    gpr[RCX] = 0xffffffff0000ffffull;
    gpr[RAX] = 0x123456787fff0000ull;
    CHECK(run_flags(andn_esi_ecx_eax, sizeof andn_esi_ecx_eax, gpr, &flags) == 5);
    CHECK(gpr[RSI] == 0x7fff0000ull);
    CHECK(gpr[RCX] == 0xffffffff0000ffffull && gpr[RAX] == 0x123456787fff0000ull);
    CHECK(flags == OTHER_FLAGS);

    uint8_t memory[0x1000] = {0};
    const uint32_t value = 0x80000001u;
    memcpy(memory + 0xf24, &value, 4);
    const uint8_t andn_eax_eax_mem[] = {0xc4, 0xc2, 0x78, 0xf2, 0x87, 0x24, 0x0f, 0x00, 0x00}; /* [r15+0xf24] */
    gpr[R15] = (uintptr_t)memory;
    gpr[RAX] = 1;
    CHECK(run_flags(andn_eax_eax_mem, sizeof andn_eax_eax_mem, gpr, &flags) == sizeof andn_eax_eax_mem);
    CHECK(gpr[RAX] == 0x80000000ull);
    CHECK(flags == (OTHER_FLAGS | SF));

    const uint8_t andn_r8d_edx_r8d[] = {0xc4, 0x42, 0x68, 0xf2, 0xc0};
    gpr[RDX] = 0x0f0f;
    gpr[R8] = 0xffffffff0000ffffull;
    CHECK(run_flags(andn_r8d_edx_r8d, sizeof andn_r8d_edx_r8d, gpr, &flags) == 5);
    CHECK(gpr[R8] == 0x0000f0f0ull && flags == OTHER_FLAGS);
    gpr[RDX] = 0xffff;
    CHECK(run_flags(andn_r8d_edx_r8d, sizeof andn_r8d_edx_r8d, gpr, &flags) == 5);
    CHECK(gpr[R8] == 0 && flags == (OTHER_FLAGS | ZF));
}

static void bextr_forms(void) {
    uint64_t gpr[16] = {0}, flags = OTHER_FLAGS | CF | OF;
    uint32_t table[4] = {0, 0, 0xabcd1234u, 0};
    const uint8_t bextr_eax_sib_edx[] = {0xc4, 0xe2, 0x68, 0xf7, 0x04, 0x81};
    gpr[RCX] = (uintptr_t)table;
    gpr[RAX] = 2;
    gpr[RDX] = 0x0804;
    CHECK(run_flags(bextr_eax_sib_edx, sizeof bextr_eax_sib_edx, gpr, &flags) == 6);
    CHECK(gpr[RAX] == 0x23);
    CHECK(flags == OTHER_FLAGS);

    const uint8_t bextr_eax_mem_eax[] = {0xc4, 0xe2, 0x78, 0xf7, 0x47, 0x28}; /* bextr eax, [rdi+0x28], eax */
    uint8_t object[0x40] = {0};
    const uint32_t packed = 0x00123456u;
    memcpy(object + 0x28, &packed, 4);
    gpr[RDI] = (uintptr_t)object;
    gpr[RAX] = 0x20d; /* start 13, length 2 */
    CHECK(run_flags(bextr_eax_mem_eax, sizeof bextr_eax_mem_eax, gpr, &flags) == 6);
    CHECK(gpr[RAX] == ((packed >> 13) & 3));

    const uint8_t bextr_ecx_eax_r14d[] = {0xc4, 0xe2, 0x08, 0xf7, 0xc8};
    gpr[RAX] = 0xffffffff80000000ull;
    gpr[R14] = 0x401f;
    CHECK(run_flags(bextr_ecx_eax_r14d, sizeof bextr_ecx_eax_r14d, gpr, &flags) == 5);
    CHECK(gpr[RCX] == 1);
    gpr[R14] = 0x0820;
    CHECK(run_flags(bextr_ecx_eax_r14d, sizeof bextr_ecx_eax_r14d, gpr, &flags) == 5);
    CHECK(gpr[RCX] == 0 && flags == (OTHER_FLAGS | ZF));
    gpr[R14] = 0x0004;
    CHECK(run_flags(bextr_ecx_eax_r14d, sizeof bextr_ecx_eax_r14d, gpr, &flags) == 5);
    CHECK(gpr[RCX] == 0);

    const uint8_t bextr_rax_rcx_rdx[] = {0xc4, 0xe2, 0xe8, 0xf7, 0xc1};
    gpr[RCX] = 0xf000000000000000ull;
    gpr[RDX] = 0xff3c;
    CHECK(run_flags(bextr_rax_rcx_rdx, sizeof bextr_rax_rcx_rdx, gpr, &flags) == 5);
    CHECK(gpr[RAX] == 0xf);
}

static void bls_forms(void) {
    uint64_t gpr[16] = {0}, flags = OTHER_FLAGS;
    const uint8_t blsi_edx_r8d[] = {0xc4, 0xc2, 0x68, 0xf3, 0xd8}; /* blsi edx, r8d */
    gpr[RDX] = ~0ull;
    gpr[R8] = 0x1234567800000c00ull;
    CHECK(run_flags(blsi_edx_r8d, sizeof blsi_edx_r8d, gpr, &flags) == 5);
    CHECK(gpr[RDX] == 0x400 && gpr[R8] == 0x1234567800000c00ull && flags == (OTHER_FLAGS | CF));
    gpr[R8] = 0;
    CHECK(run_flags(blsi_edx_r8d, sizeof blsi_edx_r8d, gpr, &flags) == 5);
    CHECK(gpr[RDX] == 0 && flags == (OTHER_FLAGS | ZF));

    const uint8_t blsr_rax_rcx[] = {0xc4, 0xe2, 0xf8, 0xf3, 0xc9};
    gpr[RCX] = 0x8000000000000000ull;
    CHECK(run_flags(blsr_rax_rcx, sizeof blsr_rax_rcx, gpr, &flags) == 5);
    CHECK(gpr[RAX] == 0 && flags == (OTHER_FLAGS | ZF));
    gpr[RCX] = 0;
    CHECK(run_flags(blsr_rax_rcx, sizeof blsr_rax_rcx, gpr, &flags) == 5);
    CHECK(gpr[RAX] == 0 && flags == (OTHER_FLAGS | CF | ZF));

    const uint8_t blsmsk_eax_ecx[] = {0xc4, 0xe2, 0x78, 0xf3, 0xd1};
    gpr[RCX] = 0x18;
    CHECK(run_flags(blsmsk_eax_ecx, sizeof blsmsk_eax_ecx, gpr, &flags) == 5);
    CHECK(gpr[RAX] == 0xf && flags == OTHER_FLAGS);
    gpr[RCX] = 0;
    CHECK(run_flags(blsmsk_eax_ecx, sizeof blsmsk_eax_ecx, gpr, &flags) == 5);
    CHECK(gpr[RAX] == 0xffffffffull && flags == (OTHER_FLAGS | CF | SF));
}

static void bmi1_rejected(void) {
    uint64_t gpr[16], before[16], flags = OTHER_FLAGS;
    for (int i = 0; i < 16; ++i) gpr[i] = 0x1111111111111111ull * (uint64_t)i;
    memcpy(before, gpr, sizeof gpr);
    const uint8_t shlx[] = {0xc4, 0xe2, 0xf9, 0xf7, 0xc1};
    const uint8_t andn_l1[] = {0xc4, 0xe2, 0xfc, 0xf2, 0xc2};
    const uint8_t bls_group0[] = {0xc4, 0xe2, 0x78, 0xf3, 0xc1};
    const uint8_t bls_group4[] = {0xc4, 0xe2, 0x78, 0xf3, 0xe1};
    const uint8_t map_0f[] = {0xc4, 0xe1, 0xf8, 0xf2, 0xc2};
    const uint8_t bzhi[] = {0xc4, 0xe2, 0xf8, 0xf5, 0xc1};
    const uint8_t vex2[] = {0xc5, 0xf8, 0xf2, 0xc2};
    const uint8_t vaddps[] = {0xc5, 0xf0, 0x58, 0xc2};
    CHECK(run_flags(shlx, sizeof shlx, gpr, &flags) == 0);
    CHECK(run_flags(andn_l1, sizeof andn_l1, gpr, &flags) == 0);
    CHECK(run_flags(bls_group0, sizeof bls_group0, gpr, &flags) == 0);
    CHECK(run_flags(bls_group4, sizeof bls_group4, gpr, &flags) == 0);
    CHECK(run_flags(map_0f, sizeof map_0f, gpr, &flags) == 0);
    CHECK(run_flags(bzhi, sizeof bzhi, gpr, &flags) == 0);
    CHECK(run_flags(vex2, sizeof vex2, gpr, &flags) == 0);
    CHECK(run_flags(vaddps, sizeof vaddps, gpr, &flags) == 0);
    CHECK(memcmp(gpr, before, sizeof gpr) == 0 && flags == OTHER_FLAGS);
}

static void host_sets(void) {
    char text[64];
    const IsaHostFeatures haswell = {bit_POPCNT | bit_AVX | bit_MOVBE, bit_BMI, bit_LZCNT};
    CHECK(isa_missing_sets(haswell, 0, text, sizeof text) == 0 && text[0] == 0);
    CHECK(isa_missing_sets(haswell, 1, text, sizeof text) == 0);
    CHECK(isa_emulation_needed(haswell) == 0);
    CHECK(!lzcnt_compat_needed(haswell));
    CHECK(isa_present_sets(haswell, text, sizeof text) == 5 && strcmp(text, "POPCNT, AVX, LZCNT, MOVBE, BMI1") == 0);

    /* i7-3770K: AVX and POPCNT, none of MOVBE, BMI1 and LZCNT */
    const IsaHostFeatures ivy_bridge = {bit_SSE4_1 | bit_SSE4_2 | bit_POPCNT | bit_AVX, 0, 0};
    CHECK(isa_missing_sets(ivy_bridge, 0, text, sizeof text) == 0 && text[0] == 0);
    CHECK(isa_missing_sets(ivy_bridge, 1, text, sizeof text) == 3 && strcmp(text, "LZCNT, MOVBE, BMI1") == 0);
    CHECK(isa_emulation_needed(ivy_bridge) == (ISA_EMULATE_MOVBE | ISA_EMULATE_BMI1));
    CHECK(lzcnt_compat_needed(ivy_bridge));
    CHECK(isa_present_sets(ivy_bridge, text, sizeof text) == 2 && strcmp(text, "POPCNT, AVX") == 0);

    const IsaHostFeatures bulldozer = {bit_SSE4_1 | bit_SSE4_2 | bit_POPCNT | bit_AVX, 0, bit_LZCNT};
    CHECK(isa_missing_sets(bulldozer, 0, text, sizeof text) == 0);
    CHECK(isa_missing_sets(bulldozer, 1, text, sizeof text) == 2 && strcmp(text, "MOVBE, BMI1") == 0);
    CHECK(!lzcnt_compat_needed(bulldozer));

    const IsaHostFeatures piledriver = {bit_POPCNT | bit_AVX, bit_BMI, bit_LZCNT};
    CHECK(isa_emulation_needed(piledriver) == ISA_EMULATE_MOVBE);

    const IsaHostFeatures nehalem = {bit_SSE4_1 | bit_SSE4_2 | bit_POPCNT, 0, bit_LZCNT};
    CHECK(isa_missing_sets(nehalem, 0, text, sizeof text) == 1 && strcmp(text, "AVX") == 0);
    const IsaHostFeatures no_popcnt = {bit_AVX, 0, 0};
    CHECK(isa_missing_sets(no_popcnt, 0, text, sizeof text) == 1 && strcmp(text, "POPCNT") == 0);

    const IsaHostFeatures none = {0, 0, 0};
    CHECK(isa_missing_sets(none, 0, text, sizeof text) == 2 && strcmp(text, "POPCNT, AVX") == 0);
    CHECK(isa_missing_sets(none, 0, text, 5) == 2 && strlen(text) < 5);
    CHECK(isa_present_sets(none, text, sizeof text) == 0 && text[0] == 0);

    isa_host_name(text, sizeof text);
    CHECK(text[0] != 0 && text[0] != ' ');
}

enum { PROBE_PAGE = 4096, PROBE_RSP = 4, PROBE_FLAGS = 11 };

uint64_t osl_probe_in[16], osl_probe_out[16];
const void *osl_probe_code;
void osl_probe_run(void);
__asm__(".text\n.globl osl_probe_run\nosl_probe_run:\n"
        " push %rbx\n push %rbp\n push %rdi\n push %rsi\n push %r12\n push %r13\n push %r14\n push %r15\n"
        " lea osl_probe_in(%rip), %rax\n"
        " mov 8(%rax), %rcx\n mov 16(%rax), %rdx\n mov 24(%rax), %rbx\n mov 40(%rax), %rbp\n"
        " mov 48(%rax), %rsi\n mov 56(%rax), %rdi\n mov 64(%rax), %r8\n mov 72(%rax), %r9\n mov 80(%rax), %r10\n"
        " mov 88(%rax), %r11\n mov 96(%rax), %r12\n mov 104(%rax), %r13\n mov 112(%rax), %r14\n mov 120(%rax), %r15\n"
        " mov (%rax), %rax\n call *osl_probe_code(%rip)\n"
        " mov %rax, osl_probe_out(%rip)\n mov %rcx, osl_probe_out+8(%rip)\n mov %rdx, osl_probe_out+16(%rip)\n"
        " mov %rbx, osl_probe_out+24(%rip)\n mov %rbp, osl_probe_out+40(%rip)\n mov %rsi, osl_probe_out+48(%rip)\n"
        " mov %rdi, osl_probe_out+56(%rip)\n mov %r8, osl_probe_out+64(%rip)\n mov %r9, osl_probe_out+72(%rip)\n"
        " mov %r10, osl_probe_out+80(%rip)\n mov %r11, osl_probe_out+88(%rip)\n mov %r12, osl_probe_out+96(%rip)\n"
        " mov %r13, osl_probe_out+104(%rip)\n mov %r14, osl_probe_out+112(%rip)\n mov %r15, osl_probe_out+120(%rip)\n"
        " pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rsi\n pop %rdi\n pop %rbp\n pop %rbx\n ret\n");

static uint8_t *executable_block(size_t size) {
#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#else
    void *block = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return block == MAP_FAILED ? NULL : block;
#endif
}

static uint64_t leading_zeros(uint64_t value, unsigned bits) {
    if (bits < 64) value &= (1ull << bits) - 1;
    return value ? (uint64_t)__builtin_clzll(value) - (64 - bits) : bits;
}

static IsaHostFeatures host;
static int host_has_lzcnt(void) { return (host.ext1_ecx & bit_LZCNT) != 0; }
static int host_has_movbe(void) { return (host.leaf1_ecx & bit_MOVBE) != 0; }
static int host_has_bmi1(void) { return (host.leaf7_ebx & bit_BMI) != 0; }

/* pushfq; pop r11; ret: the registers come back through osl_probe_out, the flags in r11. */
static const uint8_t probe_epilogue[] = {0x9c, 0x41, 0x5b, 0xc3};

/* Page 0 holds the code under test and the epilogue, page 1 the stubs, page 2 the same code untouched. */
static uint8_t *probe_block;
static uint8_t probe_arena[PROBE_PAGE] __attribute__((aligned(64)));

static int prepare_probe_block(void) {
    if (!probe_block) probe_block = executable_block(3 * PROBE_PAGE);
    if (probe_block) memset(probe_block, 0xcc, 3 * PROBE_PAGE);
    return probe_block != NULL;
}

/* `length` bytes of code with the epilogue after them, the range `shape` of it rewritten in page 0 and the code as it is
 * in page 2. */
static int prepare_probe(const uint8_t *bytes, size_t length, const LzcntShape *shape) {
    if (!prepare_probe_block()) return 0;
    memcpy(probe_block, bytes, length);
    memcpy(probe_block + length, probe_epilogue, sizeof probe_epilogue);
    memcpy(probe_block + 2 * PROBE_PAGE, probe_block, length + sizeof probe_epilogue);
    const uint32_t offset = 0;
    return lzcnt_compat_apply(probe_block, PROBE_PAGE, &offset, shape, 1, probe_block + PROBE_PAGE, PROBE_PAGE) ==
           LZCNT_COMPAT_OK;
}

/* One instruction and the epilogue, not rewritten. */
static int prepare_plain(const uint8_t *insn, size_t length) {
    if (!prepare_probe_block()) return 0;
    memcpy(probe_block, insn, length);
    memcpy(probe_block + length, probe_epilogue, sizeof probe_epilogue);
    return 1;
}

static int prepare_single(const uint8_t *insn, size_t length) {
    LzcntShape shape = {(uint8_t)(length < 5 ? length + 1 : length), 0, {0}};
    memcpy(shape.bytes, insn, length);
    if (length < 5) shape.bytes[length] = probe_epilogue[0];
    return prepare_probe(insn, length, &shape);
}

static void probe(const uint8_t *code, const uint64_t in[16], uint64_t out[16]) {
    memcpy(osl_probe_in, in, sizeof osl_probe_in);
    osl_probe_code = code;
    osl_probe_run();
    memcpy(out, osl_probe_out, sizeof osl_probe_out);
}

static const uint64_t probe_values[] = {
    0, 1, 2, 3, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x10000, 0x12345, 0x7fffffffull, 0x80000000ull,
    0xffffffffull, 0x100000000ull, 0x0123456789abcdefull, 0x00ff00ff00ff00ffull, 0x7fffffffffffffffull,
    0x8000000000000000ull, ~0ull,
};

/* Runs one LZCNT form on each value, rewritten and, on a host that has LZCNT, as it is. `source` is the register
 * holding the value, or -1 for the memory operand at `value_at`. */
static void check_form(const uint8_t *insn, size_t length, unsigned bits, unsigned dst, int source,
                       const uint64_t regs[16], uint8_t *value_at, int all_bits) {
    if (!prepare_single(insn, length)) {
        CHECK(0 && "probe block");
        return;
    }
    const size_t count = sizeof probe_values / sizeof *probe_values;
    const uint64_t mask = bits == 64 ? ~0ull : (1ull << bits) - 1;
    for (size_t n = 0; n < count + (all_bits ? 64u : 0u); ++n) {
        const uint64_t value = n < count ? probe_values[n] : 1ull << (n - count);
        uint64_t in[16], out[16];
        memcpy(in, regs, sizeof in);
        if (source >= 0) in[source] = value;
        else memcpy(value_at, &value, bits / 8);
        const uint64_t result = leading_zeros(value, bits);
        const uint64_t expected = bits == 16 ? (in[dst] & ~0xffffull) | result : result;
        for (int pass = 0; pass < 2; ++pass) {
            if (pass && !host_has_lzcnt()) break;
            probe(probe_block + (pass ? 2 * PROBE_PAGE : 0), in, out);
            int same = out[dst] == expected;
            for (unsigned r = 0; r < 16; ++r)
                if (r != dst && r != PROBE_RSP && r != PROBE_FLAGS && out[r] != in[r]) same = 0;
            const uint64_t flags = out[PROBE_FLAGS];
            same = same && (flags & 1) == ((value & mask) == 0) && (flags >> 6 & 1) == (result == 0);
            if (!same)
                printf("FAIL %s bits=%u dst=%u source=%d value=%llx: got %llx flags %llx, expected %llx\n",
                       pass ? "hardware" : "rewritten", bits, dst, source, (unsigned long long)value,
                       (unsigned long long)out[dst], (unsigned long long)flags, (unsigned long long)expected);
            CHECK(same);
        }
    }
}

static void fill_registers(uint64_t regs[16]) {
    for (unsigned r = 0; r < 16; ++r) regs[r] = 0xc0de0000face0000ull + r * 0x100001ull;
}

static size_t encode_register_form(uint8_t *out, unsigned bits, unsigned dst, unsigned source) {
    size_t n = 0;
    if (bits == 16) out[n++] = 0x66;
    out[n++] = 0xf3;
    const unsigned rex = (bits == 64 ? 8u : 0u) | (dst >= 8 ? 4u : 0u) | (source >= 8 ? 1u : 0u);
    if (rex) out[n++] = (uint8_t)(0x40 | rex);
    out[n++] = 0x0f;
    out[n++] = 0xbd;
    out[n++] = (uint8_t)(0xc0 | (dst & 7) << 3 | (source & 7));
    return n;
}

/* mode 0: [base], 1: [base+0x10], 2: [base+0x3a8], 3: [base+rcx*4+8]; *displacement is what the address adds. */
static size_t encode_memory_form(uint8_t *out, unsigned bits, unsigned dst, unsigned base, int mode,
                                 uint64_t *displacement) {
    size_t n = 0;
    if (bits == 16) out[n++] = 0x66;
    out[n++] = 0xf3;
    const unsigned rex = (bits == 64 ? 8u : 0u) | (dst >= 8 ? 4u : 0u) | (base >= 8 ? 1u : 0u);
    if (rex) out[n++] = (uint8_t)(0x40 | rex);
    out[n++] = 0x0f;
    out[n++] = 0xbd;
    const int indexed = mode == 3, sib = indexed || (base & 7) == 4;
    const unsigned mod = mode == 0 ? 0 : mode == 2 ? 2 : 1;
    out[n++] = (uint8_t)(mod << 6 | (dst & 7) << 3 | (sib ? 4 : base & 7));
    if (sib) out[n++] = (uint8_t)(indexed ? 0x88 | (base & 7) : 0x24);
    *displacement = mode == 0 ? 0 : mode == 1 ? 0x10 : mode == 2 ? 0x3a8 : 8;
    if (mod == 1) out[n++] = (uint8_t)*displacement;
    if (mod == 2) {
        const uint32_t d = 0x3a8;
        memcpy(out + n, &d, 4);
        n += 4;
    }
    return n;
}

static const unsigned probe_registers[] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15};
enum { PROBE_REGISTER_COUNT = sizeof probe_registers / sizeof *probe_registers };
static const unsigned probe_widths[] = {16, 32, 64};

static void rewritten_register_forms(void) {
    for (unsigned w = 0; w < 3; ++w)
        for (unsigned d = 0; d < PROBE_REGISTER_COUNT; ++d)
            for (unsigned s = 0; s < PROBE_REGISTER_COUNT; ++s) {
                uint8_t insn[16];
                uint64_t regs[16];
                fill_registers(regs);
                const size_t length =
                    encode_register_form(insn, probe_widths[w], probe_registers[d], probe_registers[s]);
                check_form(insn, length, probe_widths[w], probe_registers[d], (int)probe_registers[s], regs, NULL,
                           d == s && s < 3);
            }
}

static void rewritten_memory_forms(void) {
    static const unsigned bases[] = {0, 3, 5, 6, 8, 12, 13, 15};
    for (unsigned w = 0; w < 3; ++w)
        for (unsigned d = 0; d < PROBE_REGISTER_COUNT; ++d)
            for (unsigned b = 0; b < sizeof bases / sizeof *bases; ++b)
                for (int mode = 0; mode < 4; ++mode) {
                    const unsigned base = bases[b];
                    if ((mode == 0 && (base & 7) == 5) || (mode == 2 && ((base & 7) == 4 || probe_widths[w] == 16)))
                        continue;
                    uint8_t insn[16];
                    uint64_t regs[16], displacement;
                    fill_registers(regs);
                    const size_t length =
                        encode_memory_form(insn, probe_widths[w], probe_registers[d], base, mode, &displacement);
                    regs[1] = mode == 3 ? 4 : regs[1];
                    regs[base] = (uint64_t)(uintptr_t)probe_arena + 64;
                    uint8_t *value_at = probe_arena + 64 + displacement + (mode == 3 ? 16 : 0);
                    check_form(insn, length, probe_widths[w], probe_registers[d], -1, regs, value_at, 0);
                }
}

static int is_memory_shape(const LzcntShape *shape) {
    size_t i = 0;
    while (shape->bytes[i] == 0x66 || shape->bytes[i] == 0xf3) ++i;
    if ((shape->bytes[i] & 0xf0) == 0x40) ++i;
    return (shape->bytes[i + 2] >> 6) != 3;
}

static uint64_t next_random(uint64_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

/* Every range of the table as it sits in the game, rewritten against the original on a host that has LZCNT, and
 * against the architectural results either way: the registers the range writes, CF and ZF of the LZCNT it ends in. */
static void rewritten_game_ranges(void) {
    uint64_t state = 0x9e3779b97f4a7c15ull;
    for (size_t s = 0; s < LZCNT_RANGE_COUNT; ++s) {
        const LzcntShape *shape = &lzcnt_shapes[s];
        if (!prepare_probe(shape->bytes, shape->length, shape)) {
            CHECK(0 && "probe block");
            continue;
        }
        const int memory = is_memory_shape(shape);
        for (unsigned n = 0; n < 120; ++n) {
            uint64_t in[16], out[16], reference[16];
            for (unsigned r = 0; r < 16; ++r) {
                const uint64_t pick = next_random(&state);
                in[r] = pick % 5 == 0 ? probe_values[pick / 5 % (sizeof probe_values / sizeof *probe_values)] : next_random(&state);
                if (memory && (r == RAX || r == R8 || r == R14)) in[r] = (pick >> 20) & 0x38;
            }
            in[R15] = (uint64_t)(uintptr_t)probe_arena;
            in[RBP] = (uint64_t)(uintptr_t)probe_arena + 0x800;
            for (unsigned k = 0; k < 64; ++k) {
                const uint64_t pick = next_random(&state);
                const uint64_t value = pick % 3 == 0 ? probe_values[pick / 3 % (sizeof probe_values / sizeof *probe_values)] : next_random(&state);
                memcpy(probe_arena + 0x3a8 + 8 * k, &value, 8);
            }
            memcpy(reference, in, sizeof in);
            probe(probe_block, in, out);
            if (host_has_lzcnt()) {
                probe(probe_block + 2 * PROBE_PAGE, in, reference);
                for (unsigned r = 0; r < 16; ++r) {
                    if (r == PROBE_RSP || r == PROBE_FLAGS) continue;
                    if (out[r] != reference[r]) printf("FAIL range %zu register %u: %llx against %llx\n", s, r, (unsigned long long)out[r], (unsigned long long)reference[r]);
                    CHECK(out[r] == reference[r]);
                }
                CHECK(((out[PROBE_FLAGS] ^ reference[PROBE_FLAGS]) & (CF | ZF)) == 0);
            }
        }
    }
}

static void rewritten_pair(void) {
    const LzcntShape *pair = NULL;
    for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i)
        if (lzcnt_shapes[i].second) pair = &lzcnt_shapes[i];
    CHECK(pair != NULL && lzcnt_compat_instructions() == LZCNT_RANGE_COUNT + 1);
    if (!pair || !prepare_probe(pair->bytes, pair->length, pair)) return;
    /* lzcnt edx, ecx; lzcnt eax, r9d */
    uint64_t regs[16], out[16];
    fill_registers(regs);
    regs[RCX] = 0x00ff0000;
    regs[R9] = 0;
    probe(probe_block, regs, out);
    CHECK(out[RDX] == 8 && (out[RAX] & 0xffffffff) == 32);
    CHECK((out[PROBE_FLAGS] & CF) == CF && (out[PROBE_FLAGS] & ZF) == 0);
    regs[RCX] = 0;
    regs[R9] = 0x80000000;
    probe(probe_block, regs, out);
    CHECK(out[RDX] == 32 && (out[RAX] & 0xffffffff) == 0);
    CHECK((out[PROBE_FLAGS] & CF) == 0 && (out[PROBE_FLAGS] & ZF) == ZF);
    regs[RCX] = 0x80000000;
    regs[R9] = 1;
    probe(probe_block, regs, out);
    CHECK(out[RDX] == 0 && (out[RAX] & 0xffffffff) == 31);
    CHECK((out[PROBE_FLAGS] & CF) == 0 && (out[PROBE_FLAGS] & ZF) == 0);
}

static void rewritten_game_forms(void) {
    uint64_t regs[16];
    fill_registers(regs);
    regs[R15] = (uint64_t)(uintptr_t)probe_arena;
    regs[R14] = 0;
    const uint8_t memory_r15_r14[] = {0xf3, 0x4b, 0x0f, 0xbd, 0xb4, 0x37, 0xb0, 0x03, 0x00, 0x00};
    check_form(memory_r15_r14, sizeof memory_r15_r14, 64, RSI, -1, regs, probe_arena + 0x3b0, 0);
    const uint8_t sixteen_bit[] = {0x66, 0xf3, 0x0f, 0xbd, 0xe8};
    fill_registers(regs);
    check_form(sixteen_bit, sizeof sixteen_bit, 16, RBP, RAX, regs, NULL, 1);
}

/* The MOVBE and BMI1 encodings run on the host and through the emulation, which must agree on every register, the
 * memory and the flags the instruction defines. */
static void differential_movbe(void) {
    if (!host_has_movbe()) {
        puts("MOVBE differential: skipped, the host lacks MOVBE");
        return;
    }
    static const unsigned regs_used[] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15};
    static const unsigned bases[] = {0, 3, 5, 6, 7, 8, 12, 13, 15};
    uint64_t state = 0x1234567887654321ull;
    unsigned compared = 0;
    for (unsigned store = 0; store < 2; ++store)
        for (unsigned w = 0; w < 3; ++w)
            for (unsigned d = 0; d < sizeof regs_used / sizeof *regs_used; ++d)
                for (unsigned b = 0; b < sizeof bases / sizeof *bases; ++b)
                    for (int mode = 0; mode < 4; ++mode) {
                        const unsigned bits = probe_widths[w], reg = regs_used[d], base = bases[b];
                        if ((mode == 0 && (base & 7) == 5) || (mode == 2 && (base & 7) == 4)) continue;
                        uint8_t insn[16];
                        size_t n = 0;
                        if (bits == 16) insn[n++] = 0x66;
                        const unsigned rex = (bits == 64 ? 8u : 0u) | (reg >= 8 ? 4u : 0u) | (base >= 8 ? 1u : 0u);
                        if (rex) insn[n++] = (uint8_t)(0x40 | rex);
                        insn[n++] = 0x0f;
                        insn[n++] = 0x38;
                        insn[n++] = store ? 0xf1 : 0xf0;
                        const int indexed = mode == 3, sib = indexed || (base & 7) == 4;
                        const unsigned mod = mode == 0 ? 0 : mode == 2 ? 2 : 1;
                        insn[n++] = (uint8_t)(mod << 6 | (reg & 7) << 3 | (sib ? 4 : base & 7));
                        if (sib) insn[n++] = (uint8_t)(indexed ? 0x88 | (base & 7) : 0x24);
                        const uint32_t displacement = mode == 2 ? 0x1a8 : 0x10;
                        if (mod == 1) insn[n++] = (uint8_t)displacement;
                        if (mod == 2) { memcpy(insn + n, &displacement, 4); n += 4; }
                        if (!prepare_plain(insn, n)) { CHECK(0 && "probe block"); return; }
                        for (unsigned v = 0; v < 4; ++v) {
                            uint64_t in[16], native[16], soft[16];
                            for (unsigned r = 0; r < 16; ++r) in[r] = next_random(&state);
                            in[base] = (uint64_t)(uintptr_t)probe_arena + 0x100;
                            if (mode == 3) in[RCX] = 4;
                            if (mode == 3 && base == RCX) continue;
                            if (mode == 3 && reg == RCX) in[RCX] = 4;
                            uint8_t arena_native[PROBE_PAGE], arena_soft[PROBE_PAGE];
                            for (unsigned k = 0; k < PROBE_PAGE; ++k) probe_arena[k] = (uint8_t)next_random(&state);
                            memcpy(arena_soft, probe_arena, PROBE_PAGE);
                            probe(probe_block, in, native);
                            memcpy(arena_native, probe_arena, PROBE_PAGE);
                            memcpy(probe_arena, arena_soft, PROBE_PAGE);
                            memcpy(soft, in, sizeof soft);
                            uint8_t padded[32] = {0};
                            memcpy(padded, insn, n);
                            CHECK(isa_emulate_movbe(padded, 0, soft) == n);
                            memcpy(arena_soft, probe_arena, PROBE_PAGE);
                            int same = memcmp(arena_native, arena_soft, PROBE_PAGE) == 0;
                            for (unsigned r = 0; r < 16; ++r)
                                if (r != PROBE_RSP && r != PROBE_FLAGS && native[r] != soft[r]) same = 0;
                            if (!same) printf("FAIL MOVBE bits=%u reg=%u base=%u mode=%d store=%u\n", bits, reg, base, mode, store);
                            CHECK(same);
                            ++compared;
                        }
                    }
    printf("MOVBE differential: %u cases agree with the hardware\n", compared);
}

enum { BMI_ANDN, BMI_BEXTR, BMI_BLSI, BMI_BLSMSK, BMI_BLSR, BMI_OPS };

/* VEX.LZ.0F38.W0/W1 with the operands as the instruction names them. rm_mode 0: register, 1: [base+0x10],
 * 2: [base+rcx*4+0x1a8]. */
static size_t encode_bmi1(uint8_t *out, unsigned op, unsigned bits, unsigned reg, unsigned vvvv, unsigned rm, int rm_mode) {
    static const unsigned opcode[BMI_OPS] = {0xf2, 0xf7, 0xf3, 0xf3, 0xf3};
    static const unsigned group[BMI_OPS] = {0, 0, 3, 2, 1};
    const unsigned field = op >= BMI_BLSI ? group[op] : reg & 7;
    const unsigned extended_reg = op >= BMI_BLSI ? 0 : reg >> 3;
    size_t n = 0;
    out[n++] = 0xc4;
    out[n++] = (uint8_t)((extended_reg ? 0 : 0x80) | 0x40 | (rm >> 3 ? 0 : 0x20) | 0x02);
    out[n++] = (uint8_t)((bits == 64 ? 0x80 : 0) | (~vvvv & 15) << 3);
    out[n++] = (uint8_t)opcode[op];
    if (rm_mode == 0) {
        out[n++] = (uint8_t)(0xc0 | field << 3 | (rm & 7));
    } else if (rm_mode == 1) {
        out[n++] = (uint8_t)(0x40 | field << 3 | ((rm & 7) == 4 ? 4 : rm & 7));
        if ((rm & 7) == 4) out[n++] = 0x24;
        out[n++] = 0x10;
    } else {
        out[n++] = (uint8_t)(0x80 | field << 3 | 4);
        out[n++] = (uint8_t)(0x88 | (rm & 7));
        const uint32_t displacement = 0x1a8;
        memcpy(out + n, &displacement, 4);
        n += 4;
    }
    return n;
}

static uint64_t pick_value(uint64_t *state) {
    const uint64_t pick = next_random(state);
    return pick % 4 == 0 ? probe_values[pick / 4 % (sizeof probe_values / sizeof *probe_values)] : next_random(state);
}

static void differential_bmi1(void) {
    if (!host_has_bmi1()) {
        puts("BMI1 differential: skipped, the host lacks BMI1");
        return;
    }
    static const unsigned regs_used[] = {0, 1, 2, 3, 6, 8, 12, 15};
    static const unsigned bases[] = {0, 3, 7, 13};
    uint64_t state = 0xfeedfacecafebeefull;
    unsigned compared = 0;
    for (unsigned op = 0; op < BMI_OPS; ++op)
        for (unsigned w = 0; w < 2; ++w)
            for (unsigned a = 0; a < sizeof regs_used / sizeof *regs_used; ++a)
                for (unsigned v = 0; v < sizeof regs_used / sizeof *regs_used; ++v)
                    for (unsigned m = 0; m < sizeof regs_used / sizeof *regs_used + 2 * (sizeof bases / sizeof *bases); ++m) {
                        const unsigned bits = w ? 64 : 32, reg = regs_used[a], vvvv = regs_used[v];
                        const unsigned register_count = sizeof regs_used / sizeof *regs_used;
                        const int rm_mode = m < register_count ? 0 : (m - register_count) % 2 + 1;
                        const unsigned rm = m < register_count ? regs_used[m] : bases[(m - register_count) / 2];
                        if (op >= BMI_BLSI && a) continue;
                        if (rm_mode == 2 && rm == RCX) continue;
                        uint8_t insn[16], padded[32] = {0};
                        const size_t n = encode_bmi1(insn, op, bits, reg, vvvv, rm, rm_mode);
                        if (!prepare_plain(insn, n)) { CHECK(0 && "probe block"); return; }
                        memcpy(padded, insn, n);
                        for (unsigned k = 0; k < 6; ++k) {
                            uint64_t in[16], native[16], soft[16], flags = OTHER_FLAGS;
                            for (unsigned r = 0; r < 16; ++r) in[r] = pick_value(&state);
                            if (op == BMI_BEXTR && k % 2) in[vvvv] = (next_random(&state) % 72 << 8) | (next_random(&state) % 72);
                            if (rm_mode) in[rm] = (uint64_t)(uintptr_t)probe_arena + 0x100;
                            if (rm_mode == 2) in[RCX] = 4;
                            if (rm_mode && (rm == vvvv || rm == reg)) continue;
                            if (rm_mode == 2 && (vvvv == RCX || reg == RCX)) continue;
                            for (unsigned i = 0; i < PROBE_PAGE; ++i) probe_arena[i] = (uint8_t)next_random(&state);
                            probe(probe_block, in, native);
                            memcpy(soft, in, sizeof soft);
                            CHECK(isa_emulate(padded, 0, soft, &flags) == n);
                            const uint64_t defined = CF | ZF | OF | (op == BMI_BEXTR ? 0 : SF);
                            int same = ((native[PROBE_FLAGS] ^ flags) & defined) == 0;
                            for (unsigned r = 0; r < 16; ++r)
                                if (r != PROBE_RSP && r != PROBE_FLAGS && native[r] != soft[r]) same = 0;
                            if (!same)
                                printf("FAIL BMI1 op=%u bits=%u reg=%u vvvv=%u rm=%u mode=%d\n", op, bits, reg, vvvv, rm, rm_mode);
                            CHECK(same);
                            ++compared;
                        }
                    }
    printf("BMI1 differential: %u cases agree with the hardware\n", compared);
}

static int count_lzcnt_bytes(const uint8_t *image, uint64_t size) {
    int count = 0;
    for (uint64_t i = 0; i + 3 < size; ++i) {
        if (image[i] != 0xf3) continue;
        const uint64_t at = (image[i + 1] & 0xf0) == 0x40 ? i + 2 : i + 1;
        count += at + 1 < size && image[at] == 0x0f && image[at + 1] == 0xbd;
    }
    return count;
}

/* A readable image holding every range of the build, followed by the stub area. */
typedef struct {
    uint8_t *bytes;
    uint8_t *stubs;
} SyntheticImage;

static SyntheticImage synthetic_image(const LzcntBuild *build) {
    SyntheticImage image = {NULL, NULL};
    image.bytes = calloc(1, build->size + lzcnt_compat_stub_bytes());
    if (!image.bytes) return image;
    image.stubs = image.bytes + build->size;
    for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i)
        memcpy(image.bytes + build->offsets[i], lzcnt_shapes[i].bytes, lzcnt_shapes[i].length);
    return image;
}

static void site_tables(void) {
    const size_t stubs = lzcnt_compat_stub_bytes(), stride = stubs / LZCNT_RANGE_COUNT;
    int lzcnts = 0;
    for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i) {
        CHECK(lzcnt_shapes[i].length >= 5 && lzcnt_shapes[i].length <= LZCNT_SHAPE_MAX);
        CHECK(count_lzcnt_bytes(lzcnt_shapes[i].bytes, lzcnt_shapes[i].length) == (lzcnt_shapes[i].second ? 2 : 1));
        lzcnts += count_lzcnt_bytes(lzcnt_shapes[i].bytes, lzcnt_shapes[i].length);
    }
    CHECK(lzcnts == 50 && (size_t)lzcnts == lzcnt_compat_instructions());
    for (size_t b = 0; b < LZCNT_BUILD_COUNT; ++b) {
        const LzcntBuild *build = &lzcnt_builds[b];
        for (size_t i = 1; i < LZCNT_RANGE_COUNT; ++i)
            CHECK(build->offsets[i] >= build->offsets[i - 1] + lzcnt_shapes[i - 1].length);
        CHECK(build->offsets[LZCNT_RANGE_COUNT - 1] + lzcnt_shapes[LZCNT_RANGE_COUNT - 1].length <= build->size);
        for (size_t o = 0; o < b; ++o) CHECK(lzcnt_builds[o].size != build->size || lzcnt_builds[o].entry != build->entry);

        SyntheticImage image = synthetic_image(build);
        if (!image.bytes) {
            CHECK(0 && "synthetic image");
            continue;
        }
        uint8_t *before = malloc(build->size);
        memcpy(before, image.bytes, build->size);
        const char *name = NULL;
        CHECK(lzcnt_compat_install(image.bytes, build->size, build->entry, image.stubs, stubs, &name) == LZCNT_COMPAT_OK);
        CHECK(name == build->build);
        CHECK(count_lzcnt_bytes(image.bytes, build->size) == 0);
        size_t changed = 0, allowed = 0;
        for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i) {
            const uint8_t *site = image.bytes + build->offsets[i];
            int32_t rel;
            memcpy(&rel, site + 1, 4);
            CHECK(site[0] == 0xe9 && site + 5 + rel == image.stubs + i * stride);
            for (unsigned k = 5; k < lzcnt_shapes[i].length; ++k) CHECK(site[k] == 0x90);
            allowed += lzcnt_shapes[i].length;
        }
        for (uint64_t k = 0; k < build->size; ++k) changed += image.bytes[k] != before[k];
        CHECK(changed <= allowed);
        free(before);
        free(image.bytes);
    }
}

static void patch_rejections(void) {
    const LzcntBuild *build = &lzcnt_builds[0];
    SyntheticImage image = synthetic_image(build);
    if (!image.bytes) {
        CHECK(0 && "synthetic image");
        return;
    }
    const size_t stubs = lzcnt_compat_stub_bytes(), total = build->size + stubs;
    uint8_t *before = malloc(total);
    memcpy(before, image.bytes, total);
#define UNTOUCHED() (memcmp(before, image.bytes, total) == 0)

    CHECK(lzcnt_compat_install(image.bytes, build->size + 16, build->entry, image.stubs, stubs, NULL) ==
          LZCNT_COMPAT_UNKNOWN_IMAGE);
    CHECK(lzcnt_compat_install(image.bytes, build->size, build->entry + 16, image.stubs, stubs, NULL) ==
          LZCNT_COMPAT_UNKNOWN_IMAGE);
    CHECK(lzcnt_compat_install(image.bytes, build->size, build->entry, image.stubs, stubs - 1, NULL) ==
          LZCNT_COMPAT_NO_ROOM);
    uint8_t *far_stubs = (uint8_t *)((uintptr_t)image.bytes + 0x90000000u);
    CHECK(lzcnt_compat_install(image.bytes, build->size, build->entry, far_stubs, stubs, NULL) == LZCNT_COMPAT_NO_ROOM);
    CHECK(lzcnt_compat_install(image.bytes, build->size, build->entry, image.bytes + 0x1000, stubs, NULL) ==
          LZCNT_COMPAT_NO_ROOM);
    CHECK(UNTOUCHED());

    for (size_t i = 0; i < LZCNT_RANGE_COUNT; i += 6)
        for (unsigned k = 0; k < lzcnt_shapes[i].length; ++k) {
            image.bytes[build->offsets[i] + k] ^= 0x40;
            CHECK(lzcnt_compat_install(image.bytes, build->size, build->entry, image.stubs, stubs, NULL) ==
                  LZCNT_COMPAT_BAD_SITE);
            image.bytes[build->offsets[i] + k] ^= 0x40;
            CHECK(UNTOUCHED());
        }
    /* The last range changed: every earlier range was valid and still nothing is written. */
    image.bytes[build->offsets[LZCNT_RANGE_COUNT - 1] + 1] ^= 0x01;
    CHECK(lzcnt_compat_install(image.bytes, build->size, build->entry, image.stubs, stubs, NULL) == LZCNT_COMPAT_BAD_SITE);
    image.bytes[build->offsets[LZCNT_RANGE_COUNT - 1] + 1] ^= 0x01;
    CHECK(UNTOUCHED());
    CHECK(lzcnt_compat_apply(image.bytes, build->offsets[LZCNT_RANGE_COUNT - 1] + 4, build->offsets, lzcnt_shapes,
                             LZCNT_RANGE_COUNT, image.stubs, stubs) == LZCNT_COMPAT_BAD_SITE);
    CHECK(UNTOUCHED());

    /* Another build's offsets, and a build of the same size and another entry. */
    const LzcntBuild *other = &lzcnt_builds[1];
    CHECK(lzcnt_compat_apply(image.bytes, build->size, other->offsets, lzcnt_shapes, LZCNT_RANGE_COUNT, image.stubs,
                             stubs) == LZCNT_COMPAT_BAD_SITE);
    CHECK(UNTOUCHED());
    free(before);
    free(image.bytes);

    image = synthetic_image(&lzcnt_builds[2]);
    CHECK(lzcnt_builds[2].size == lzcnt_builds[3].size && lzcnt_builds[2].entry != lzcnt_builds[3].entry);
    before = malloc(lzcnt_builds[2].size + stubs);
    memcpy(before, image.bytes, lzcnt_builds[2].size + stubs);
    CHECK(lzcnt_compat_install(image.bytes, lzcnt_builds[2].size, lzcnt_builds[3].entry, image.stubs, stubs, NULL) ==
          LZCNT_COMPAT_BAD_SITE);
    CHECK(memcmp(before, image.bytes, lzcnt_builds[2].size + stubs) == 0);
    free(before);
    free(image.bytes);
#undef UNTOUCHED

    /* Shapes the rewrite cannot handle, each matching the bytes it sits on. */
    static const LzcntShape bad[] = {
        {4, 0, {0xf3, 0x0f, 0xbd, 0xc8}},                                      /* too short for a jmp */
        {8, 0, {0xf3, 0x0f, 0xbd, 0x05, 0x10, 0x00, 0x00, 0x00}},              /* RIP-relative operand */
        {5, 0, {0x0f, 0xbd, 0xc8, 0x90, 0x90}},                                /* BSR, not LZCNT */
        {5, 0, {0xf2, 0xf3, 0x0f, 0xbd, 0xc8}},                                /* another prefix */
        {6, 0, {0xf3, 0x0f, 0xbc, 0xc8, 0x90, 0x90}},                          /* TZCNT */
        {9, 4, {0xf3, 0x0f, 0xbd, 0xc9, 0x90, 0x90, 0x90, 0x90, 0x90}},        /* the second is not an LZCNT */
        {9, 9, {0xf3, 0x0f, 0xbd, 0xc9, 0x90, 0x90, 0x90, 0x90, 0x90}},        /* the second is beyond the range */
        {5, 0, {0xf3, 0x0f, 0xbd, 0x44, 0x24}},                                /* operand cut by the range */
        {11, 0, {0xf3, 0x0f, 0xbd, 0xc9, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90}}, /* longer than a range may be */
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; ++i) {
        uint8_t buffer[64 + 128], copy[sizeof buffer];
        memset(buffer, 0, sizeof buffer);
        const uint32_t offset = 16;
        memcpy(buffer + offset, bad[i].bytes, LZCNT_SHAPE_MAX);
        memcpy(copy, buffer, sizeof buffer);
        CHECK(lzcnt_compat_apply(buffer, 64, &offset, &bad[i], 1, buffer + 64, 128) == LZCNT_COMPAT_BAD_SITE);
        CHECK(memcmp(buffer, copy, sizeof buffer) == 0);
    }

    /* Two ranges that overlap, each matching the bytes it sits on. */
    uint8_t buffer[64 + 256], copy[sizeof buffer];
    memset(buffer, 0, sizeof buffer);
    const LzcntShape pair[2] = {
        {10, 0, {0xf3, 0x48, 0x0f, 0xbd, 0xc8, 0xf3, 0x48, 0x0f, 0xbd, 0xc8}},
        {5, 0, {0xf3, 0x48, 0x0f, 0xbd, 0xc8}},
    };
    const uint32_t overlapping[2] = {16, 21};
    memcpy(buffer + 16, pair[0].bytes, 10);
    memcpy(copy, buffer, sizeof buffer);
    CHECK(lzcnt_compat_apply(buffer, 64, overlapping, pair, 2, buffer + 64, 256) == LZCNT_COMPAT_BAD_SITE);
    CHECK(memcmp(buffer, copy, sizeof buffer) == 0);
    const uint32_t apart[2] = {16, 26};
    memcpy(buffer + 26, pair[1].bytes, 5);
    CHECK(lzcnt_compat_apply(buffer, 64, apart, pair, 2, buffer + 64, 256) == LZCNT_COMPAT_OK);
}

/* The memory image inside build/image/<folder>/boot.bin (layout written by launcher/boot_image.cpp). */
static uint64_t read64(FILE *f) {
    unsigned char b[8];
    if (fread(b, 1, 8, f) != 8) return 0;
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | b[i];
    return v;
}

static uint8_t *boot_image(const char *folder, uint64_t *size, uint64_t *entry, size_t extra) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/build/image/%s/boot.bin", OSL_SOURCE_DIR, folder);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "OSLBOOT1", 8)) return fclose(f), NULL;
    *size = read64(f);
    *entry = read64(f);
    const uint64_t segments = read64(f), relocs = read64(f), names = read64(f);
    read64(f), read64(f);
    for (int i = 0; i < 4; ++i) read64(f);
    const uint64_t modules = read64(f);
    fseek(f, (long)(modules * 56), SEEK_CUR);
    const uint64_t bindings = read64(f);
    fseek(f, (long)(bindings * 24 + segments * 24 + names * 128 + relocs * 32), SEEK_CUR);
    uint8_t *image = calloc(1, *size + extra);
    if (image && fread(image, 1, *size, f) != *size) free(image), image = NULL;
    fclose(f);
    return image;
}

static void real_images(void) {
    static const char *const folders[LZCNT_BUILD_COUNT] = {"CUSA01290-1.00", "CUSA01290-1.02", "CUSA05083-1.00",
                                                           "CUSA05262-1.00", "CUSA05262-1.01"};
    const size_t stubs = lzcnt_compat_stub_bytes();
    for (size_t b = 0; b < LZCNT_BUILD_COUNT; ++b) {
        const LzcntBuild *build = &lzcnt_builds[b];
        uint64_t size = 0, entry = 0;
        uint8_t *image = boot_image(folders[b], &size, &entry, stubs);
        if (!image) {
            printf("no boot image for %s: skipped\n", folders[b]);
            continue;
        }
        CHECK(size == build->size && entry == build->entry);
        CHECK((size_t)count_lzcnt_bytes(image, size) == lzcnt_compat_instructions());
        uint8_t *before = malloc(size);
        memcpy(before, image, size);

        const LzcntBuild *wrong = &lzcnt_builds[(b + 1) % LZCNT_BUILD_COUNT];
        CHECK(lzcnt_compat_apply(image, size, wrong->offsets, lzcnt_shapes, LZCNT_RANGE_COUNT, image + size, stubs) ==
              LZCNT_COMPAT_BAD_SITE);
        CHECK(memcmp(image, before, size) == 0);

        CHECK(lzcnt_compat_install(image, size, build->entry, image + size, stubs, NULL) == LZCNT_COMPAT_OK);
        CHECK(count_lzcnt_bytes(image, size) == 0);
        size_t changed = 0, inside = 0;
        for (uint64_t k = 0; k < size; ++k) {
            if (image[k] == before[k]) continue;
            ++changed;
            for (size_t i = 0; i < LZCNT_RANGE_COUNT; ++i)
                inside += k >= build->offsets[i] && k < build->offsets[i] + lzcnt_shapes[i].length;
        }
        CHECK(changed == inside);
        printf("%s: %zu bytes changed in %d ranges holding %zu LZCNT instructions\n", folders[b], changed,
               LZCNT_RANGE_COUNT, lzcnt_compat_instructions());
        free(before);
        free(image);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    host = isa_host_features();
    printf("host: MOVBE %s, BMI1 %s, LZCNT %s\n", host_has_movbe() ? "yes" : "no", host_has_bmi1() ? "yes" : "no",
           host_has_lzcnt() ? "yes" : "no");
    movbe_game_forms();
    movbe_widths();
    addressing_forms();
    movbe_rejected();
    strlen_site();
    andn_forms();
    bextr_forms();
    bls_forms();
    bmi1_rejected();
    host_sets();
    differential_movbe();
    differential_bmi1();
    site_tables();
    patch_rejections();
    rewritten_register_forms();
    rewritten_memory_forms();
    rewritten_game_forms();
    rewritten_game_ranges();
    rewritten_pair();
    real_images();
    if (failures) return 1;
    puts("isa tests passed");
    return 0;
}
