/* DEBUG_SAULO: temporary telemetry of the cheat hooks. Every hook calls the thunk at its entry and after its block; the
 * host side logs a few lines per Classic site (installed, first hit, first change, first relevant skip) and a summary at
 * exit. Remove this file, its header and every DEBUG_SAULO line once Saulo's gameplay test passes. */
#include "debug_saulo_cheat_probe.h"

#include "osl_cheats.h"

#include <stdio.h>
#include <string.h>

/* The guest is in the middle of its own code: every register, the flags, the vector registers, MXCSR and the x87
 * control word are saved; the host function is SysV like the guest. The frame it receives is the pushed registers
 * followed by the flags, the return address and the probe's id. */
__asm__(".text\n"
        ".p2align 4\n"
        ".globl debug_saulo_probe_thunk\n"
        "debug_saulo_probe_thunk:\n"
        ".intel_syntax noprefix\n"
        "pushfq\n"
        "cld\n"
        "push rax\n push rcx\n push rdx\n push rbx\n push rbp\n push rsi\n push rdi\n"
        "push r8\n push r9\n push r10\n push r11\n push r12\n push r13\n push r14\n push r15\n"
        "mov rbx, rsp\n"
        "and rsp, -32\n"
        "sub rsp, 0x220\n"
        "vmovdqu ymmword ptr [rsp+0x000], ymm0\n vmovdqu ymmword ptr [rsp+0x020], ymm1\n"
        "vmovdqu ymmword ptr [rsp+0x040], ymm2\n vmovdqu ymmword ptr [rsp+0x060], ymm3\n"
        "vmovdqu ymmword ptr [rsp+0x080], ymm4\n vmovdqu ymmword ptr [rsp+0x0a0], ymm5\n"
        "vmovdqu ymmword ptr [rsp+0x0c0], ymm6\n vmovdqu ymmword ptr [rsp+0x0e0], ymm7\n"
        "vmovdqu ymmword ptr [rsp+0x100], ymm8\n vmovdqu ymmword ptr [rsp+0x120], ymm9\n"
        "vmovdqu ymmword ptr [rsp+0x140], ymm10\n vmovdqu ymmword ptr [rsp+0x160], ymm11\n"
        "vmovdqu ymmword ptr [rsp+0x180], ymm12\n vmovdqu ymmword ptr [rsp+0x1a0], ymm13\n"
        "vmovdqu ymmword ptr [rsp+0x1c0], ymm14\n vmovdqu ymmword ptr [rsp+0x1e0], ymm15\n"
        "stmxcsr dword ptr [rsp+0x200]\n"
        "fnstcw word ptr [rsp+0x204]\n"
        "mov rdi, rbx\n"
        "mov rsi, rsp\n"
        "call debug_saulo_probe_host\n"
        "fldcw word ptr [rsp+0x204]\n"
        "ldmxcsr dword ptr [rsp+0x200]\n"
        "vmovdqu ymm0, ymmword ptr [rsp+0x000]\n vmovdqu ymm1, ymmword ptr [rsp+0x020]\n"
        "vmovdqu ymm2, ymmword ptr [rsp+0x040]\n vmovdqu ymm3, ymmword ptr [rsp+0x060]\n"
        "vmovdqu ymm4, ymmword ptr [rsp+0x080]\n vmovdqu ymm5, ymmword ptr [rsp+0x0a0]\n"
        "vmovdqu ymm6, ymmword ptr [rsp+0x0c0]\n vmovdqu ymm7, ymmword ptr [rsp+0x0e0]\n"
        "vmovdqu ymm8, ymmword ptr [rsp+0x100]\n vmovdqu ymm9, ymmword ptr [rsp+0x120]\n"
        "vmovdqu ymm10, ymmword ptr [rsp+0x140]\n vmovdqu ymm11, ymmword ptr [rsp+0x160]\n"
        "vmovdqu ymm12, ymmword ptr [rsp+0x180]\n vmovdqu ymm13, ymmword ptr [rsp+0x1a0]\n"
        "vmovdqu ymm14, ymmword ptr [rsp+0x1c0]\n vmovdqu ymm15, ymmword ptr [rsp+0x1e0]\n"
        "mov rsp, rbx\n"
        "pop r15\n pop r14\n pop r13\n pop r12\n pop r11\n pop r10\n pop r9\n pop r8\n"
        "pop rdi\n pop rsi\n pop rbp\n pop rbx\n pop rdx\n pop rcx\n pop rax\n"
        "popfq\n"
        "ret\n"
        ".att_syntax prefix\n");

void debug_saulo_probe_thunk(void);

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8, rdi, rsi, rbp, rbx, rdx, rcx, rax, flags, ret, id;
} Frame;

typedef enum { K_OTHER, K_DAMAGE, K_HIT, K_STAMINA_RBX, K_STAMINA_R12, K_STAMINA_SET, K_PP_SPEND, K_PP_FILL, K_POINTS, K_EXP,
               K_SALE, K_PICKUP, K_SHOT } Kind;

typedef struct {
    const char *id;
    Kind kind;
    const char *summary;
} Watch;

static const Watch watches[] = {
    {"C-H1", K_DAMAGE, "Health"},       {"C-H3", K_HIT, "Health"},           {"C-W1", K_STAMINA_RBX, "POW"},
    {"C-W3", K_STAMINA_R12, "POW"},     {"C-W4", K_STAMINA_RBX, "POW"},      {"C-W5", K_STAMINA_RBX, "POW"},
    {"C-W6", K_STAMINA_RBX, "POW"},     {"C-W7", K_STAMINA_SET, "POW"},      {"C-K1", K_PP_SPEND, "PP"},
    {"C-E1", K_EXP, "EXP"},             {"C-G1", K_SALE, "GoldSale"},        {"C-G2", K_PICKUP, "GoldPickup"},
    {"C-P1", K_PP_FILL, "Phozon"},      {"C-P2", K_POINTS, "PhozonLevel"},   {"C-A1", K_SHOT, "Arrows"},
};
static const char *const summary_order[] = {"Health", "POW", "PP", "EXP", "GoldSale", "GoldPickup", "Phozon", "PhozonLevel", "Arrows"};

typedef struct {
    uint64_t hits, applied, original, ignored, normal, charged;
    int logged_hit, logged_applied, logged_original, logged_ignored;
} Counts;

static Counts counts[OSL_CHEAT_PROBE_SITES];
static int quiet, classic_seen;

/* The entry state of the hit this thread is in, for the line its end prints. */
static __thread struct {
    int id;
    uint64_t object;
    int64_t a, b, c;
    float x, y;
    int charged;
} entry;

static const Watch *watch_of(const OslCheatProbeSite *site) {
    if (!site || site->engine != OSL_ENGINE_CLASSIC) return NULL;
    for (size_t w = 0; w < sizeof(watches) / sizeof(*watches); ++w)
        if (!strcmp(watches[w].id, site->site->id)) return &watches[w];
    return NULL;
}

static const char *cheat_label(int cheat) {
    return cheat >= 0 && cheat < OSL_CHEAT_COUNT ? osl_cheats[cheat].label : osl_experience_multiplier.label;
}

static uint64_t u64(uint64_t at) { return at ? *(const volatile uint64_t *)(uintptr_t)at : 0; }
static int32_t i32(uint64_t at) { return *(const volatile int32_t *)(uintptr_t)at; }
static uint16_t u16(uint64_t at) { return *(const volatile uint16_t *)(uintptr_t)at; }
static uint8_t u8(uint64_t at) { return *(const volatile uint8_t *)(uintptr_t)at; }
static float f32(uint64_t at) { return *(const volatile float *)(uintptr_t)at; }

/* Every Classic group reads the same root global; groups without one use another group's. */
static uint64_t classic_root_global(const OslCheatProbeSite *site) {
    if (site->global) return site->global;
    for (size_t i = 0; i < osl_cheat_probe_count(); ++i) {
        const OslCheatProbeSite *other = osl_cheat_probe_site(i);
        if (other->engine == OSL_ENGINE_CLASSIC && other->global) return other->global;
    }
    return 0;
}

static uint64_t controlled_of(const OslCheatProbeSite *site) {
    const uint64_t root = u64(classic_root_global(site));
    return root ? u64(root + 0x158) : 0;
}

static void purse_text(uint64_t purse, char *text, size_t size) {
    int used = snprintf(text, size, "[");
    const unsigned slots = u8(purse + 0x20);
    for (unsigned s = 0; s < slots && s < 8 && used > 0 && (size_t)used < size; ++s) {
        const uint64_t item = u64(purse + 0x28 + s * 8);
        if (item) used += snprintf(text + used, size - (size_t)used, "%sd%u=%u", s ? " " : "", u8(item + 0xd98), u16(item + 0xda4));
        else used += snprintf(text + used, size - (size_t)used, "%s-", s ? " " : "");
    }
    if (used > 0 && (size_t)used < size) snprintf(text + used, size - (size_t)used, "]");
}

static uint64_t object_of(Kind kind, const Frame *f) {
    switch (kind) {
    case K_DAMAGE: case K_STAMINA_RBX: case K_PP_FILL: case K_SHOT: return f->rbx;
    case K_HIT: return f->r13;
    case K_STAMINA_R12: return f->r12;
    case K_STAMINA_SET: case K_PP_SPEND: case K_POINTS: case K_EXP: return f->rdi;
    case K_PICKUP: return f->r14;
    default: return 0;
    }
}

static void say(const char *what, const OslCheatProbeSite *site, const char *details) {
    if (quiet) return;
    printf("DEBUG_SAULO ClassicCheat %s %s %s %s: %s\n", cheat_label(site->cheat), site->site->id, site->site->name, what, details);
}

static void at_entry(int id, const OslCheatProbeSite *site, const Watch *watch, const Frame *f, const float *xmm) {
    Counts *c = &counts[id];
    const uint64_t hits = __atomic_add_fetch(&c->hits, 1, __ATOMIC_RELAXED);
    const uint64_t object = object_of(watch->kind, f), controlled = controlled_of(site);
    entry.id = id;
    entry.object = object;
    entry.charged = 0;
    entry.a = entry.b = entry.c = 0;
    switch (watch->kind) {
    case K_DAMAGE: entry.a = (int32_t)f->rax, entry.b = (int32_t)f->r15; break;
    case K_HIT: entry.a = i32(object + 0x7c0), entry.b = (int32_t)f->rax; break;
    case K_STAMINA_RBX: case K_STAMINA_R12: case K_STAMINA_SET: entry.x = f32(object + 0xd7c), entry.y = xmm[0]; break;
    case K_PP_SPEND: entry.a = u16(object + 0x7c8), entry.b = (int32_t)f->rsi; break;
    case K_PP_FILL: entry.a = (int16_t)u16(object + 0x7c8); break;
    case K_POINTS: entry.a = i32(object + 0x7d4), entry.b = (int32_t)f->rsi, entry.c = u8(object + 0x805); break;
    case K_EXP: entry.a = i32(object + 0x7cc), entry.b = (uint32_t)f->rsi; break;
    case K_SHOT: entry.a = i32(object + 0xebc), entry.b = (int32_t)f->rcx, entry.charged = i32(object + 0xeb8) == 0x5a; break;
    default: break;
    }
    if (quiet) return;
    if (!__atomic_exchange_n(&classic_seen, 1, __ATOMIC_RELAXED)) {
        printf("DEBUG_SAULO ClassicCheat Classic gameplay detected: %s ran at image+0x%llx (Classic Os* code) on object 0x%llx; "
               "controlled character [[root]+0x158] = 0x%llx, its vtable at image+0x%llx\n",
               site->site->id, (unsigned long long)site->region, (unsigned long long)object, (unsigned long long)controlled,
               controlled ? (unsigned long long)(u64(controlled) - site->image) : 0ull);
    }
    if (hits != 1) return;
    char state[200] = "", purse[120];
    switch (watch->kind) {
    case K_DAMAGE: case K_HIT:
        snprintf(state, sizeof(state), "HP before %lld, damage %lld", (long long)entry.a, (long long)entry.b);
        break;
    case K_STAMINA_RBX: case K_STAMINA_R12: case K_STAMINA_SET:
        snprintf(state, sizeof(state), "POW before %.2f, requested %.2f, maximum %.2f", entry.x, entry.y, f32(object + 0xd80));
        break;
    case K_PP_SPEND:
        snprintf(state, sizeof(state), "PP before %lld, cost %lld, maximum %u", (long long)entry.a, (long long)entry.b, u16(object + 0x7ca));
        break;
    case K_PP_FILL:
        snprintf(state, sizeof(state), "PP with the phozon's amount added %lld, maximum %d", (long long)entry.a, (int16_t)u16(object + 0x7ca));
        break;
    case K_POINTS:
        snprintf(state, sizeof(state), "phozon total before %lld, absorbed %lld, Psypher level %lld (shown %lld)", (long long)entry.a,
                 (long long)entry.b, (long long)entry.c, (long long)entry.c + 1);
        break;
    case K_EXP:
        snprintf(state, sizeof(state), "EXP before %lld, delta %lld, multiplier switch %u", (long long)entry.a, (long long)entry.b, *site->flag);
        break;
    case K_SALE:
        purse_text(f->r13, purse, sizeof(purse));
        snprintf(state, sizeof(state), "sale done, purse 0x%llx stacks before %s", (unsigned long long)f->r13, purse);
        break;
    case K_PICKUP:
        purse_text(object + 0xe00, purse, sizeof(purse));
        snprintf(state, sizeof(state), "coin taken, stacks before %s", purse);
        break;
    case K_SHOT:
        snprintf(state, sizeof(state), "%s shot, ammunition before %lld, cost %lld", entry.charged ? "charged" : "normal", (long long)entry.a,
                 (long long)entry.b);
        break;
    default: break;
    }
    char line[400];
    snprintf(line, sizeof(line), "object 0x%llx, controlled 0x%llx (%s), switch %u, %s", (unsigned long long)object,
             (unsigned long long)controlled, object && object == controlled ? "match" : "mismatch", *site->flag, state);
    say("HIT", site, line);
}

static void describe_end(const Watch *watch, const Frame *f, const float *xmm, uint64_t object, char *result, size_t size) {
    char purse[120];
    switch (watch->kind) {
    case K_DAMAGE:
        snprintf(result, size, "HP %lld -> %d (damage %lld)", (long long)entry.a, (int32_t)f->rcx, (long long)entry.b);
        break;
    case K_HIT:
        snprintf(result, size, "HP %lld -> %d (damage %lld)", (long long)entry.a, i32(object + 0x7c0), (long long)entry.b);
        break;
    case K_STAMINA_RBX: case K_STAMINA_R12:
        snprintf(result, size, "POW %.2f -> %.2f (the game asked %.2f)", entry.x, f32(object + 0xd7c), entry.y);
        break;
    case K_STAMINA_SET:
        snprintf(result, size, "POW %.2f -> %.2f before the clamp (the game asked %.2f)", entry.x, xmm[0], entry.y);
        break;
    case K_PP_SPEND:
        snprintf(result, size, "PP %lld -> %lld (cost %lld)", (long long)entry.a, (long long)((int32_t)f->rax - (int32_t)f->rsi), (long long)entry.b);
        break;
    case K_PP_FILL:
        snprintf(result, size, "PP %lld -> %d", (long long)entry.a, (int16_t)u16(object + 0x7c8));
        break;
    case K_POINTS:
        snprintf(result, size, "absorbed %lld -> %d added to the total %lld at Psypher level %lld (the game levels up after)",
                 (long long)entry.b, (int32_t)f->rsi, (long long)entry.a, (long long)entry.c);
        break;
    case K_EXP:
        snprintf(result, size, "EXP %lld + delta %lld -> final delta %u", (long long)entry.a, (long long)entry.b, (uint32_t)f->r14);
        break;
    case K_SALE: case K_PICKUP:
        purse_text(watch->kind == K_SALE ? f->r13 : object + 0xe00, purse, sizeof(purse));
        snprintf(result, size, "stacks after %s", purse);
        break;
    case K_SHOT:
        snprintf(result, size, "%s shot, ammunition %lld -> %d (cost %lld)", entry.charged ? "charged" : "normal", (long long)entry.a,
                 i32(object + 0xebc), (long long)entry.b);
        break;
    default: result[0] = 0; break;
    }
}

static void at_end(int id, int changed, const OslCheatProbeSite *site, const Watch *watch, const Frame *f, const float *xmm) {
    Counts *c = &counts[id];
    if (entry.id != id) return;
    entry.id = -1;
    const uint64_t object = entry.object, controlled = controlled_of(site);
    const int match = object && object == controlled;
    int applied = changed && match;
    switch (watch->kind) {
    case K_DAMAGE: case K_HIT: applied = applied && entry.b > 0; break;
    case K_POINTS: case K_SALE: case K_PICKUP: applied = changed; break;
    case K_EXP: applied = changed && (uint32_t)f->r14 != (uint32_t)entry.b; break;
    default: break;
    }
    const char *what;
    int *logged;
    if (applied) {
        __atomic_add_fetch(&c->applied, 1, __ATOMIC_RELAXED);
        if (watch->kind == K_SHOT) __atomic_add_fetch(entry.charged ? &c->charged : &c->normal, 1, __ATOMIC_RELAXED);
        what = "APPLIED", logged = &c->logged_applied;
    } else if (!changed) {
        __atomic_add_fetch(&c->original, 1, __ATOMIC_RELAXED);
        what = "ORIGINAL (switch off)", logged = &c->logged_original;
    } else {
        __atomic_add_fetch(&c->ignored, 1, __ATOMIC_RELAXED);
        what = "IGNORED (not the controlled character, or nothing to change)", logged = &c->logged_ignored;
    }
    if (quiet || __atomic_exchange_n(logged, 1, __ATOMIC_RELAXED)) return;
    char result[240], line[400];
    describe_end(watch, f, xmm, object, result, sizeof(result));
    snprintf(line, sizeof(line), "object 0x%llx, controlled 0x%llx (%s): %s", (unsigned long long)object, (unsigned long long)controlled,
             match ? "match" : "mismatch", result);
    say(what, site, line);
}

__attribute__((sysv_abi, used)) void debug_saulo_probe_host(const Frame *f, const unsigned char *vectors) {
    const int id = (int)(f->id >> 2), phase = (int)(f->id & 3);
    const OslCheatProbeSite *site = osl_cheat_probe_site((size_t)id);
    if (!site) return;
    const Watch *watch = watch_of(site);
    if (!watch) {
        if (phase == 0 && __atomic_add_fetch(&counts[id].hits, 1, __ATOMIC_RELAXED) == 1 && !quiet)
            printf("DEBUG_SAULO ClassicCheat %s %s %s (%s) HIT #1\n", cheat_label(site->cheat), site->site->id, site->site->name,
                   osl_engine_name(site->engine));
        return;
    }
    float xmm[1];
    memcpy(xmm, vectors, sizeof(xmm));
    if (phase == 0) at_entry(id, site, watch, f, xmm);
    else at_end(id, phase == 2, site, watch, f, xmm);
}

const void *debug_saulo_cheat_probe_thunk(void) { return (const void *)debug_saulo_probe_thunk; }

void debug_saulo_cheat_probe_quiet(int on) { quiet = on; }

void debug_saulo_cheat_probe_clear(void) {
    memset(counts, 0, sizeof(counts));
    classic_seen = 0;
}

void debug_saulo_cheat_probe_totals(const char *site_id, uint64_t *hits, uint64_t *applied, uint64_t *original) {
    *hits = *applied = *original = 0;
    for (size_t i = 0; i < osl_cheat_probe_count(); ++i) {
        const OslCheatProbeSite *site = osl_cheat_probe_site(i);
        if (strcmp(site->site->id, site_id)) continue;
        *hits += counts[i].hits;
        *applied += counts[i].applied;
        *original += counts[i].original;
    }
}

void debug_saulo_cheat_probe_start(void) {
    osl_cheat_probe_reset();
    debug_saulo_cheat_probe_clear();
    osl_cheat_set_probe((const void *)debug_saulo_probe_thunk);
}

void debug_saulo_cheat_probe_report(void) {
    osl_cheat_set_probe(NULL);
    size_t refined = 0;
    for (size_t i = 0; i < osl_cheat_probe_count(); ++i) {
        const OslCheatProbeSite *site = osl_cheat_probe_site(i);
        if (site->engine != OSL_ENGINE_CLASSIC) {
            refined += site->installed;
            continue;
        }
        printf("DEBUG_SAULO ClassicCheat %s %s %s %s at image+0x%llx\n", cheat_label(site->cheat), site->site->id, site->site->name,
               site->installed ? "INSTALLED" : "NOT INSTALLED", (unsigned long long)site->region);
    }
    printf("DEBUG_SAULO ClassicCheat Refined hooks installed: %zu (probed too, for comparison)\n", refined);
}

void debug_saulo_cheat_probe_summary(void) {
    printf("DEBUG_SAULO ClassicCheat summary:\n");
    for (size_t o = 0; o < sizeof(summary_order) / sizeof(*summary_order); ++o) {
        uint64_t hits = 0, applied = 0, normal = 0, charged = 0;
        int installed = 0;
        for (size_t i = 0; i < osl_cheat_probe_count(); ++i) {
            const Watch *watch = watch_of(osl_cheat_probe_site(i));
            if (!watch || strcmp(watch->summary, summary_order[o])) continue;
            installed |= osl_cheat_probe_site(i)->installed;
            hits += counts[i].hits;
            applied += counts[i].applied;
            normal += counts[i].normal;
            charged += counts[i].charged;
        }
        if (!strcmp(summary_order[o], "Arrows"))
            printf("%s installed=%d hits=%llu applied=%llu (normal %llu, charged %llu)\n", summary_order[o], installed,
                   (unsigned long long)hits, (unsigned long long)applied, (unsigned long long)normal, (unsigned long long)charged);
        else
            printf("%s installed=%d hits=%llu applied=%llu\n", summary_order[o], installed, (unsigned long long)hits, (unsigned long long)applied);
    }
    uint64_t refined = 0;
    for (size_t i = 0; i < osl_cheat_probe_count(); ++i) {
        const OslCheatProbeSite *site = osl_cheat_probe_site(i);
        if (site->engine != OSL_ENGINE_CLASSIC) {
            refined += counts[i].hits;
            continue;
        }
        if (!watch_of(site)) continue;
        printf("DEBUG_SAULO ClassicCheat   %s %s: hits=%llu applied=%llu switch-off=%llu skipped=%llu\n", site->site->id, site->site->name,
               (unsigned long long)counts[i].hits, (unsigned long long)counts[i].applied, (unsigned long long)counts[i].original,
               (unsigned long long)counts[i].ignored);
    }
    printf("DEBUG_SAULO ClassicCheat Refined hooks hit in this session: %llu\n", (unsigned long long)refined);
    fflush(stdout);
}
