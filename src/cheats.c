#include "osl_cheats.h"

#include <stdio.h>
#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* vmovss xmm0,[r14+0x1698]; vminss xmm0,xmm0,xmm1; vmovss [r14+0x1694],xmm0: Mercedes's shot spends POW. */
static const unsigned char crossbow_shot[] = {0x0d, 0xc4, 0xc1, 0x7a, 0x10, 0x86, 0x98, 0x16, 0x00, 0x00, 0xc5, 0xfa, 0x5d,
                                              0xc1, 0xc4, 0xc1, 0x7a, 0x11, 0x86, 0x94, 0x16, 0x00, 0x00, 0xc5, 0xf8};
/* The same clamp on [r12+0x1694] after a skill's POW cost; the load of the cap starts four bytes before. */
static const unsigned char skill_pow_cost_prefix[] = {0xc4, 0xc1, 0x7a, 0x10};
static const unsigned char skill_pow_cost[] = {0x84, 0x24, 0x98, 0x16, 0x00, 0x00, 0xc5, 0xfa, 0x5d,
                                               0xc1, 0xc4, 0xc1, 0x7a, 0x11, 0x84, 0x24, 0x94};
/* mov eax,999999; cmovle eax,ebx; mov [r15+0x3ac],eax: gold picked up. */
static const unsigned char gold_pickup[] = {0x42, 0x0f, 0x00, 0xb8, 0x3f, 0x42, 0x0f, 0x00, 0x0f, 0x4e,
                                            0xc3, 0x41, 0x89, 0x87, 0xac, 0x03, 0x00, 0x00, 0xe8};
/* mov ecx,999999; cmovle ecx,edx; mov [rax+0x3ac],ecx: a shop sale's total paid out. */
static const unsigned char shop_sale[] = {0x42, 0x0f, 0x00, 0xb9, 0x3f, 0x42, 0x0f, 0x00, 0x0f, 0x4e,
                                          0xca, 0x89, 0x88, 0xac, 0x03, 0x00, 0x00, 0x41, 0xc7};
/* inc eax; cmp ecx,eax; cmovb eax,ecx; mov [rbx+0x2144],eax: one phozon added to the stock, capped. */
static const unsigned char phozon_absorbed[] = {0x00, 0x48, 0x89, 0xdf, 0xff, 0xc0, 0x39, 0xc1, 0x0f, 0x42,
                                                0xc1, 0x89, 0x83, 0x44, 0x21, 0x00, 0x00, 0xc4, 0xc1};
/* mov eax,[rax+0x20]; sub [rbx+0x214c],eax: the Ability screen spends AP. */
static const unsigned char ability_purchase[] = {0x84, 0x24, 0x03, 0x00, 0x00, 0x8b, 0x40, 0x20, 0x29, 0x83, 0x4c,
                                                 0x21, 0x00, 0x00, 0xe9, 0x32, 0x03, 0x00, 0x00, 0x41, 0x80, 0xbf};
/* call [rax+0xaa0]: the PP setter's maximum, the last of the clamp. */
static const unsigned char pp_setter[] = {0x77, 0x27, 0x48, 0x8b, 0x03, 0x48, 0x89, 0xdf, 0xff, 0x90, 0xa0,
                                          0x0a, 0x00, 0x00, 0xc5, 0xfa, 0x10, 0x4d, 0xf4, 0xc5, 0xf8, 0x2e};

/* vminss becomes vmaxss: the clamp keeps the maximum. */
static const unsigned char vmaxss[] = {0x5f};
static const unsigned char nop3[] = {0x90, 0x90, 0x90};
/* mov eax,ecx; nop: the cap is always taken. */
static const unsigned char take_cap[] = {0x89, 0xc8, 0x90};
static const unsigned char nop6[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

/* The shared HP change (actor in r12, delta in r14d): lea rbx,[r12+0x350] ... mov r13d,[r12+0x35c];
 * mov rax,[r12+0x350]; add r13d,r14d; call [rax+0x10] (the maximum), then the clamp and the store of HP. */
static const unsigned char hp_change[] = {0x49, 0x8d, 0x9c, 0x24, 0x50, 0x03, 0x00, 0x00, 0x48, 0x89, 0xdf, 0x48, 0x8b,
                                          0x00, 0x48, 0x89, 0x45, 0xd0, 0x45, 0x8b, 0xac, 0x24, 0x5c, 0x03, 0x00, 0x00,
                                          0x49, 0x8b, 0x84, 0x24, 0x50, 0x03, 0x00, 0x00, 0x45, 0x01, 0xf5, 0xff, 0x50,
                                          0x10, 0x44, 0x89, 0xe9, 0x44, 0x39, 0xe8, 0x7f, 0x0b, 0x48, 0x8b};
/* The region again, with the add skipped when the delta is negative and the actor is the player the HUD shows:
 * [[[global]+0xf8]+0x170], read the way the game's player accessor reads it. rcx is free: the call after the
 * region clobbers it, and the flags are dead. */
static const unsigned char hp_change_protected[] = {
    0x49, 0x8b, 0x84, 0x24, 0x50, 0x03, 0x00, 0x00, /* mov rax,[r12+0x350] */
    0x45, 0x85, 0xf6,                               /* test r14d,r14d */
    0x79, 0x24,                                     /* jns add */
    0x48, 0x8b, 0x0d, 0x00, 0x00, 0x00, 0x00,       /* mov rcx,[rip+global] */
    0x48, 0x85, 0xc9,                               /* test rcx,rcx */
    0x74, 0x18,                                     /* jz add */
    0x48, 0x8b, 0x89, 0xf8, 0x00, 0x00, 0x00,       /* mov rcx,[rcx+0xf8] */
    0x48, 0x85, 0xc9,                               /* test rcx,rcx */
    0x74, 0x0c,                                     /* jz add */
    0x48, 0x8b, 0x89, 0x70, 0x01, 0x00, 0x00,       /* mov rcx,[rcx+0x170] */
    0x4c, 0x39, 0xe1,                               /* cmp rcx,r12 */
    0x74, 0x03,                                     /* je skip */
    0x45, 0x01, 0xf5,                               /* add: add r13d,r14d */
};
/* call <player accessor>; mov rbx,[rax+0x170]: the HUD reading the player it shows. */
static const unsigned char hud_player[] = {0xe8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x98, 0x70, 0x01, 0x00, 0x00, 0x0f,
                                           0xb7, 0x83, 0xcc, 0x04, 0x00, 0x00, 0x83, 0xc0, 0x85, 0x0f, 0xb7, 0xc0};
/* The accessor: lea rax,[rip+global]; mov rax,[rax]; mov rax,[rax+0xf8]; ret. */
static const unsigned char player_accessor[] = {0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8b,
                                                0x00, 0x48, 0x8b, 0x80, 0xf8, 0x00, 0x00, 0x00, 0xc3};
enum { ACCESSOR_DISP_AT = 3, ACCESSOR_DISP_END = 7, CHANGED_GLOBAL_AT = 16 };

#define SITE(id, name, code, at, bytes, region_at, region_size) \
    {id, name, OSL_CHEAT_SITE_HOOK, NULL, 0, code, sizeof(code), at, bytes, sizeof(bytes), region_at, region_size, NULL, 0, -1, -1, \
     NULL, -1, -1}

static const OslCheatSite gold_sites[] = {SITE("P3", "gold pickup", gold_pickup, 8, nop3, 3, 8),
                                          SITE("P6", "shop sale", shop_sale, 8, nop3, 3, 8)};
static const OslCheatSite phozon_sites[] = {SITE("P4", "phozon absorbed", phozon_absorbed, 8, take_cap, 6, 5)};
static const OslCheatSite ap_sites[] = {SITE("P5", "ability purchase", ability_purchase, 8, nop6, 8, 6)};
static const OslCheatSite pp_sites[] = {SITE("P7", "PP setter", pp_setter, 8, nop6, 8, 6)};
static const OslCheatSite pow_sites[] = {{"P2", "skill POW cost", OSL_CHEAT_SITE_HOOK, skill_pow_cost_prefix,
                                          sizeof(skill_pow_cost_prefix), skill_pow_cost, sizeof(skill_pow_cost), 8, vmaxss,
                                          sizeof(vmaxss), -4, 14, NULL, 0, -1, -1, NULL, -1, -1}};
static const OslCheatSite health_sites[] = {
    {"H1", "HP change", OSL_CHEAT_SITE_HOOK, NULL, 0, hp_change, sizeof(hp_change), 26, NULL, 11, 26, 11,
     hp_change_protected, sizeof(hp_change_protected), CHANGED_GLOBAL_AT, -1, NULL, -1, -1},
    {"H2", "player accessor call", OSL_CHEAT_SITE_PLAYER_ACCESSOR, NULL, 0, hud_player, sizeof(hud_player), 1, NULL, 4, 0, 0,
     NULL, 0, -1, -1, NULL, -1, -1}};
static const OslCheatSite arrow_sites[] = {SITE("P1", "crossbow shot", crossbow_shot, 12, vmaxss, 1, 13)};

/* Classic engine: HP at character+0x7c0, the controlled character at [[root]+0x158]. Damage reaches HP two ways: the
 * damage routine (status and scripted damage) and the hit routine every attack goes through. */
static const unsigned char classic_damage[] = {0x8b, 0x83, 0xc0, 0x07, 0x00, 0x00, 0x89, 0xc1, 0x44, 0x29, 0xf9, 0x89,
                                               0x8b, 0xc0, 0x07, 0x00, 0x00, 0x0f, 0xb7, 0x93, 0x8c, 0x07, 0x00, 0x00,
                                               0x83, 0xfa, 0x3b, 0x77, 0x1a};
/* The region without the subtraction for positive damage to the controlled character; rdx and the flags are dead after it. */
static const unsigned char classic_damage_protected[] = {
    0x89, 0xc1,                               /* mov ecx,eax */
    0x45, 0x85, 0xff,                         /* test r15d,r15d */
    0x7e, 0x15,                               /* jle subtract */
    0x48, 0x8b, 0x15, 0x00, 0x00, 0x00, 0x00, /* mov rdx,[rip+global] */
    0x48, 0x85, 0xd2,                         /* test rdx,rdx */
    0x74, 0x09,                               /* jz subtract */
    0x48, 0x39, 0x9a, 0x58, 0x01, 0x00, 0x00, /* cmp [rdx+0x158],rbx */
    0x74, 0x03,                               /* je skip */
    0x44, 0x29, 0xf9,                         /* subtract: sub ecx,r15d */
};
/* The damage routine's own read of the root pointer's global. */
static const unsigned char classic_root[] = {0x48, 0x8d, 0x15, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x12, 0x48, 0x39,
                                             0x9a, 0x58, 0x01, 0x00, 0x00, 0x75, 0x11, 0x29, 0xc8, 0x7e, 0x0d};
/* The hit routine: mov rcx,[rbp-0x50]; mov r14d,eax; sub [r13+0x7c0],r14d; test byte [rcx+0x25],1. */
static const unsigned char classic_hit[] = {0x48, 0x8b, 0x4d, 0xb0, 0x41, 0x89, 0xc6, 0x45, 0x29,
                                            0xb5, 0xc0, 0x07, 0x00, 0x00, 0xf6, 0x41, 0x25, 0x01};
/* The same protection for the hit's target in r13; r14d keeps the damage, rax and the flags are dead after it. */
static const unsigned char classic_hit_protected[] = {
    0x41, 0x89, 0xc6,                         /* mov r14d,eax */
    0x45, 0x85, 0xf6,                         /* test r14d,r14d */
    0x7e, 0x15,                               /* jle subtract */
    0x48, 0x8b, 0x05, 0x00, 0x00, 0x00, 0x00, /* mov rax,[rip+global] */
    0x48, 0x85, 0xc0,                         /* test rax,rax */
    0x74, 0x09,                               /* jz subtract */
    0x4c, 0x39, 0xa8, 0x58, 0x01, 0x00, 0x00, /* cmp [rax+0x158],r13 */
    0x74, 0x07,                               /* je skip */
    0x45, 0x29, 0xb5, 0xc0, 0x07, 0x00, 0x00, /* subtract: sub [r13+0x7c0],r14d */
};
enum { CLASSIC_DAMAGE_PLAYER_AT = 10, CLASSIC_HIT_PLAYER_AT = 11 };

static const OslCheatSite classic_health_sites[] = {
    {"C-H1", "damage taken", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_damage, sizeof(classic_damage), 6, NULL, 5, 6, 5,
     classic_damage_protected, sizeof(classic_damage_protected), CLASSIC_DAMAGE_PLAYER_AT, -1, NULL, -1, -1},
    {"C-H2", "root object global", OSL_CHEAT_SITE_GLOBAL, NULL, 0, classic_root, sizeof(classic_root), 3, NULL, 4, 0, 0, NULL, 0,
     -1, -1, NULL, -1, -1},
    {"C-H3", "hit taken", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_hit, sizeof(classic_hit), 7, NULL, 7, 4, 10,
     classic_hit_protected, sizeof(classic_hit_protected), CLASSIC_HIT_PLAYER_AT, -1, NULL, -1, -1}};

/* Classic POW is the stamina gauge (float +0xd7c, maximum +0xd80) that attacks, guarding and skills drain and time refills:
 * each drain stores the lowered value, then exhausts the character at 0. The HUD shows it, or Mercedes's ammunition. */
static const unsigned char classic_pow_drain[] = {0xc5, 0xfa, 0x58, 0x83, 0x7c, 0x0d, 0x00, 0x00, 0xeb, 0x0c, 0xc5, 0xfa, 0x10, 0x8b,
                                                  0x7c, 0x0d, 0x00, 0x00, 0xc5, 0xf2, 0x5c, 0xc0, 0xc5, 0xf0, 0x57, 0xc9, 0xc5, 0xfa,
                                                  0x11, 0x83, 0x7c, 0x0d, 0x00, 0x00, 0xc5, 0xf8, 0x2e, 0xc8, 0x72, 0x11};
static const unsigned char classic_pow_root[] = {0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x00, 0x48, 0x8b, 0xb8, 0xc8,
                                                 0x00, 0x00, 0x00, 0x48, 0x85, 0xff, 0x74, 0x33, 0x48, 0x83, 0xc7, 0x10, 0xbe, 0xaa,
                                                 0x01, 0x00, 0x00, 0xc5, 0xfa, 0x11, 0x45, 0xec, 0xe8, 0x00, 0x00, 0x00, 0x00, 0xc5,
                                                 0xfa, 0x10, 0x45, 0xec, 0x85, 0xc0, 0x74, 0x17, 0x45, 0x84, 0xf6};
static const char classic_pow_root_mask[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" "????" "xxxxxxxxxxxx";
/* The attack's drain, on the character in r12. */
static const unsigned char classic_pow_attack[] = {0xc4, 0xc1, 0x7a, 0x10, 0x84, 0x24, 0x7c, 0x0d, 0x00, 0x00, 0xc5, 0xfa, 0x5c, 0xc1,
                                                   0xc5, 0xf0, 0x57, 0xc9, 0xc4, 0xc1, 0x7a, 0x11, 0x84, 0x24, 0x7c, 0x0d, 0x00, 0x00,
                                                   0xc5, 0xf8, 0x2e, 0xc8, 0x72, 0x15, 0x41, 0x80, 0x8c, 0x24, 0xb8, 0x0b, 0x00, 0x00,
                                                   0x20};
/* The two drains while guarding (-0.1 or -0.025 a frame). */
static const unsigned char classic_pow_guard[] = {0xc5, 0xfa, 0x10, 0x83, 0x7c, 0x0d, 0x00, 0x00, 0xc5, 0xfa, 0x58, 0x05, 0x00, 0x00,
                                                  0x00, 0x00, 0xc5, 0xf0, 0x57, 0xc9, 0xc5, 0xfa, 0x11, 0x83, 0x7c, 0x0d, 0x00, 0x00,
                                                  0xc5, 0xf8, 0x2e, 0xc8, 0x72, 0x11, 0x80, 0x8b, 0xb8, 0x0b, 0x00, 0x00, 0x20, 0xc7,
                                                  0x83, 0x7c, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbe, 0x10, 0x00, 0x00, 0x00,
                                                  0x48, 0x89, 0xdf, 0xe8};
static const unsigned char classic_pow_guard_held[] = {0xc5, 0xfa, 0x10, 0x83, 0x7c, 0x0d, 0x00, 0x00, 0xc5, 0xfa, 0x58, 0x05, 0x00,
                                                       0x00, 0x00, 0x00, 0xc5, 0xf0, 0x57, 0xc9, 0xc5, 0xfa, 0x11, 0x83, 0x7c, 0x0d,
                                                       0x00, 0x00, 0xc5, 0xf8, 0x2e, 0xc8, 0x72, 0x11, 0x80, 0x8b, 0xb8, 0x0b, 0x00,
                                                       0x00, 0x20, 0xc7, 0x83, 0x7c, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44,
                                                       0x8b, 0x75, 0x18, 0x44, 0x8b, 0x7d, 0x10};
static const char classic_pow_guard_mask[] = "xxxxxxxxxxxx" "????" "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
static const char classic_pow_guard_held_mask[] = "xxxxxxxxxxxx" "????" "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
static const unsigned char classic_pow_spend[] = {0xc5, 0xfa, 0x10, 0x83, 0x7c, 0x0d, 0x00, 0x00, 0xc5, 0xfa, 0x5c, 0xc1, 0xc5,
                                                  0xf0, 0x57, 0xc9, 0xc5, 0xfa, 0x11, 0x83, 0x7c, 0x0d, 0x00, 0x00, 0xc5, 0xf8,
                                                  0x2e, 0xc8, 0x72, 0x11, 0x80, 0x8b, 0xb8, 0x0b, 0x00, 0x00, 0x20, 0xc7, 0x83,
                                                  0x7c, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41, 0xf6, 0x47, 0x24, 0x20};
/* The setter that clamps a new value to [0, maximum]; status effects call it to empty the controlled character's POW. */
static const unsigned char classic_pow_set[] = {0xc5, 0xfa, 0x10, 0x97, 0x80, 0x0d, 0x00, 0x00, 0xc5, 0xf0, 0x57, 0xc9, 0xc5, 0xf2, 0x5f,
                                                0xc0, 0xc5, 0xea, 0x5d, 0xc0, 0xc5, 0xfa, 0x11, 0x87, 0x7c, 0x0d, 0x00, 0x00, 0xc3};
/* For the controlled character the value stored is the maximum, so the gauge stays full and never exhausts; rax is saved
 * below the red zone, and the flags are redefined by the vucomiss that follows. */
static const unsigned char classic_pow_full[] = {
    0x48, 0x8d, 0x64, 0x24, 0x80,             /* lea rsp,[rsp-0x80] */
    0x50,                                     /* push rax */
    0x48, 0x8b, 0x05, 0x00, 0x00, 0x00, 0x00, /* mov rax,[rip+global] */
    0x48, 0x85, 0xc0,                         /* test rax,rax */
    0x74, 0x11,                               /* jz keep */
    0x48, 0x39, 0x98, 0x58, 0x01, 0x00, 0x00, /* cmp [rax+0x158],rbx */
    0x75, 0x08,                               /* jne keep */
    0xc5, 0xfa, 0x10, 0x83, 0x80, 0x0d, 0x00, 0x00, /* vmovss xmm0,[rbx+0xd80] */
    0x58,                                     /* keep: pop rax */
    0x48, 0x8d, 0xa4, 0x24, 0x80, 0x00, 0x00, 0x00, /* lea rsp,[rsp+0x80] */
    0xc5, 0xf0, 0x57, 0xc9,                   /* vxorps xmm1,xmm1,xmm1 */
    0xc5, 0xfa, 0x11, 0x83, 0x7c, 0x0d, 0x00, 0x00, /* vmovss [rbx+0xd7c],xmm0 */
};
static const unsigned char classic_pow_attack_full[] = {
    0x48, 0x8d, 0x64, 0x24, 0x80,                         /* lea rsp,[rsp-0x80] */
    0x50,                                                 /* push rax */
    0x48, 0x8b, 0x05, 0x00, 0x00, 0x00, 0x00,             /* mov rax,[rip+global] */
    0x48, 0x85, 0xc0,                                     /* test rax,rax */
    0x74, 0x13,                                           /* jz keep */
    0x4c, 0x39, 0xa0, 0x58, 0x01, 0x00, 0x00,             /* cmp [rax+0x158],r12 */
    0x75, 0x0a,                                           /* jne keep */
    0xc4, 0xc1, 0x7a, 0x10, 0x84, 0x24, 0x80, 0x0d, 0x00, 0x00, /* vmovss xmm0,[r12+0xd80] */
    0x58,                                                 /* keep: pop rax */
    0x48, 0x8d, 0xa4, 0x24, 0x80, 0x00, 0x00, 0x00,       /* lea rsp,[rsp+0x80] */
    0xc5, 0xf0, 0x57, 0xc9,                               /* vxorps xmm1,xmm1,xmm1 */
    0xc4, 0xc1, 0x7a, 0x11, 0x84, 0x24, 0x7c, 0x0d, 0x00, 0x00, /* vmovss [r12+0xd7c],xmm0 */
};
static const unsigned char classic_pow_set_full[] = {
    0xc5, 0xfa, 0x10, 0x97, 0x80, 0x0d, 0x00, 0x00, /* vmovss xmm2,[rdi+0xd80] */
    0x48, 0x8d, 0x64, 0x24, 0x80,                   /* lea rsp,[rsp-0x80] */
    0x50,                                           /* push rax */
    0x48, 0x8b, 0x05, 0x00, 0x00, 0x00, 0x00,       /* mov rax,[rip+global] */
    0x48, 0x85, 0xc0,                               /* test rax,rax */
    0x74, 0x0d,                                     /* jz keep */
    0x48, 0x39, 0xb8, 0x58, 0x01, 0x00, 0x00,       /* cmp [rax+0x158],rdi */
    0x75, 0x04,                                     /* jne keep */
    0xc5, 0xf8, 0x28, 0xc2,                         /* vmovaps xmm0,xmm2 */
    0x58,                                           /* keep: pop rax */
    0x48, 0x8d, 0xa4, 0x24, 0x80, 0x00, 0x00, 0x00, /* lea rsp,[rsp+0x80] */
    0xc5, 0xf0, 0x57, 0xc9,                         /* vxorps xmm1,xmm1,xmm1 */
};
enum { CLASSIC_POW_PLAYER_AT = 9, CLASSIC_POW_SET_PLAYER_AT = 17 };

static const OslCheatSite classic_pow_sites[] = {
    {"C-W1", "POW drained", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_pow_drain, sizeof(classic_pow_drain), 26, NULL, 8, 22, 12,
     classic_pow_full, sizeof(classic_pow_full), CLASSIC_POW_PLAYER_AT, -1, NULL, -1, -1},
    {"C-W2", "root object global", OSL_CHEAT_SITE_GLOBAL, NULL, 0, classic_pow_root, sizeof(classic_pow_root), 3, NULL, 4, 0, 0,
     NULL, 0, -1, -1, classic_pow_root_mask, -1, -1},
    {"C-W3", "POW spent by an attack", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_pow_attack, sizeof(classic_pow_attack), 18, NULL, 10,
     14, 14, classic_pow_attack_full, sizeof(classic_pow_attack_full), CLASSIC_POW_PLAYER_AT, -1, NULL, -1, -1},
    {"C-W4", "POW spent guarding", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_pow_guard, sizeof(classic_pow_guard), 20, NULL, 8, 16, 12,
     classic_pow_full, sizeof(classic_pow_full), CLASSIC_POW_PLAYER_AT, -1, classic_pow_guard_mask, -1, -1},
    {"C-W5", "POW spent holding a guard", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_pow_guard_held, sizeof(classic_pow_guard_held), 20,
     NULL, 8, 16, 12, classic_pow_full, sizeof(classic_pow_full), CLASSIC_POW_PLAYER_AT, -1, classic_pow_guard_held_mask, -1, -1},
    {"C-W6", "POW spent", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_pow_spend, sizeof(classic_pow_spend), 16, NULL, 8, 12, 12,
     classic_pow_full, sizeof(classic_pow_full), CLASSIC_POW_PLAYER_AT, -1, NULL, -1, -1},
    {"C-W7", "POW set", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_pow_set, sizeof(classic_pow_set), 0, NULL, 8, 0, 12,
     classic_pow_set_full, sizeof(classic_pow_set_full), CLASSIC_POW_SET_PLAYER_AT, -1, NULL, -1, -1}};

/* Classic PP is the word +0x7c8 (maximum +0x7ca) that absorbing phozons fills (AddPOW) and Psypher skills spend (SubPOW,
 * its only lowering). */
static const unsigned char classic_pp_spend[] = {0x0f, 0xb7, 0x87, 0xc8, 0x07, 0x00, 0x00, 0x31, 0xc9, 0x29, 0xf0, 0x66, 0x85,
                                                 0xc0, 0x66, 0x0f, 0x49, 0xc8, 0x66, 0x89, 0x8f, 0xc8, 0x07, 0x00, 0x00, 0xc3};
/* For the controlled character the function then stores the maximum minus nothing; rax and the flags are dead after it. */
static const unsigned char classic_pp_refill[] = {
    0x48, 0x8b, 0x05, 0x00, 0x00, 0x00, 0x00, /* mov rax,[rip+global] */
    0x48, 0x85, 0xc0,                         /* test rax,rax */
    0x74, 0x14,                               /* jz spend */
    0x48, 0x39, 0xb8, 0x58, 0x01, 0x00, 0x00, /* cmp [rax+0x158],rdi */
    0x75, 0x0b,                               /* jne spend */
    0x0f, 0xb7, 0x87, 0xca, 0x07, 0x00, 0x00, /* movzx eax,word [rdi+0x7ca] */
    0x31, 0xf6,                               /* xor esi,esi */
    0xeb, 0x07,                               /* jmp done */
    0x0f, 0xb7, 0x87, 0xc8, 0x07, 0x00, 0x00, /* spend: movzx eax,word [rdi+0x7c8] */
};
/* AddPOW, just before SubPOW, reading the root pointer's global. */
static const unsigned char classic_pp_root[] = {0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x00, 0x48, 0x39, 0x98, 0x58,
                                                0x01, 0x00, 0x00, 0x75, 0x18, 0x48, 0x8b, 0xb8, 0xb8, 0x00, 0x00, 0x00, 0xbe};
enum { CLASSIC_PP_PLAYER_AT = 3 };

static const OslCheatSite classic_pp_sites[] = {
    {"C-K1", "PP spent", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_pp_spend, sizeof(classic_pp_spend), 0, NULL, 7, 0, 7,
     classic_pp_refill, sizeof(classic_pp_refill), CLASSIC_PP_PLAYER_AT, -1, NULL, -1, -1},
    {"C-K2", "root object global", OSL_CHEAT_SITE_GLOBAL, NULL, 0, classic_pp_root, sizeof(classic_pp_root), 3, NULL, 4, 0, 0,
     NULL, 0, -1, -1, NULL, -1, -1}};

/* AddPOW's clamp of PP to its maximum, after the phozon's amount was added. */
static const unsigned char classic_phozon_absorbed[] = {0x0f, 0xbf, 0x83, 0xca, 0x07, 0x00, 0x00, 0x0f, 0xbf, 0xce,
                                                        0x39, 0xc1, 0x7e, 0x07, 0x66, 0x89, 0x83, 0xc8, 0x07, 0x00,
                                                        0x00, 0x48, 0x83, 0xc4, 0x08, 0x5b, 0x5d, 0xc3};
/* The controlled character's PP becomes its maximum; rdx and the flags are dead after the region. */
static const unsigned char classic_phozon_fill[] = {
    0x0f, 0xbf, 0x83, 0xca, 0x07, 0x00, 0x00, /* movsx eax,word [rbx+0x7ca] */
    0x0f, 0xbf, 0xce,                         /* movsx ecx,si */
    0x48, 0x8b, 0x15, 0x00, 0x00, 0x00, 0x00, /* mov rdx,[rip+global] */
    0x48, 0x85, 0xd2,                         /* test rdx,rdx */
    0x74, 0x10,                               /* jz done */
    0x48, 0x39, 0x9a, 0x58, 0x01, 0x00, 0x00, /* cmp [rdx+0x158],rbx */
    0x75, 0x07,                               /* jne done */
    0x66, 0x89, 0x83, 0xc8, 0x07, 0x00, 0x00, /* mov [rbx+0x7c8],ax */
};
/* AddPP's own read of the root pointer's global, in its level-up loop. */
static const unsigned char classic_points_root[] = {0x4c, 0x8d, 0x35, 0x00, 0x00, 0x00, 0x00, 0xeb, 0x08, 0x66, 0x90, 0x8a, 0x83,
                                                    0x05, 0x08, 0x00, 0x00, 0x0f, 0xb6, 0xc0, 0x83, 0xf8, 0x61, 0x77, 0x00};
static const char classic_points_root_mask[] = "xxxxxxxxxxxxxxxxxxxxxxxx?";
enum { CLASSIC_PHOZON_PLAYER_AT = 13 };

/* Classic AddPP: the Psypher level's running total +0x7d4, next level's total +0x7d8, level byte +0x805 (98 at most); only
 * the controlled character absorbing a phozon calls it. */
static const unsigned char classic_phozon_points[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48, 0x89, 0xfb, 0x0f, 0xb6, 0x83, 0x05, 0x08, 0x00, 0x00,
    0x83, 0xf8, 0x62, 0x75, 0x0e, 0x8b, 0x83, 0xd8, 0x07, 0x00, 0x00, 0x89, 0x83, 0xd4, 0x07, 0x00, 0x00,
    0xeb, 0x70, 0x8b, 0x8b, 0xd4, 0x07, 0x00, 0x00, 0xba, 0xff, 0xe0, 0xf5, 0x05, 0x29};
/* The amount raised to one below the function's 99,999,999 cap, whose path stores without levelling: the game's own loop
 * then levels up to 98, where its recompute pins the total to that level's. */
static const unsigned char classic_phozon_full[] = {
    0x48, 0x89, 0xfb,                         /* mov rbx,rdi */
    0xb9, 0xfe, 0xe0, 0xf5, 0x05,             /* mov ecx,99999998 */
    0x2b, 0x8b, 0xd4, 0x07, 0x00, 0x00,       /* sub ecx,[rbx+0x7d4] */
    0x39, 0xf1,                               /* cmp ecx,esi */
    0x0f, 0x4f, 0xf1,                         /* cmovg esi,ecx */
    0x0f, 0xb6, 0x83, 0x05, 0x08, 0x00, 0x00, /* movzx eax,[rbx+0x805] */
};

static const OslCheatSite classic_phozon_sites[] = {
    {"C-P1", "phozon absorbed", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_phozon_absorbed, sizeof(classic_phozon_absorbed), 0, NULL, 7,
     0, 10, classic_phozon_fill, sizeof(classic_phozon_fill), CLASSIC_PHOZON_PLAYER_AT, -1, NULL, -1, -1},
    {"C-P2", "phozon points", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_phozon_points, sizeof(classic_phozon_points), 7, NULL, 10, 7,
     10, classic_phozon_full, sizeof(classic_phozon_full), -1, -1, NULL, -1, -1},
    {"C-P3", "root object global", OSL_CHEAT_SITE_GLOBAL, NULL, 0, classic_points_root, sizeof(classic_points_root), 3, NULL, 4, 0,
     0, NULL, 0, -1, -1, classic_points_root_mask, -1, -1}};

/* Classic gold is the purse (character+0xe00): one stack per coin of 10, 5, 20, 10 and 1 G, 999 at most each, 45,954 G
 * in all. set_stack(purse, coin, count) clamps to 999 and creates a missing stack; buying pays with chosen coins. */
static const unsigned char classic_sale[] = {
    0x85, 0xdb, 0x74, 0x00, 0xbe, 0x04, 0x00, 0x00, 0x00, 0x4c, 0x89, 0xef, 0xe8, 0x00, 0x00, 0x00, 0x00,
    0x01, 0xc3, 0xbe, 0x04, 0x00, 0x00, 0x00, 0x4c, 0x89, 0xef, 0x89, 0xda, 0xe8, 0x00, 0x00, 0x00, 0x00,
    0xbf, 0x5a, 0x01, 0x00, 0x00, 0x31, 0xf6, 0xba, 0xff, 0xff, 0xff, 0xff, 0x31, 0xc9, 0xe8};
static const char classic_sale_mask[] = "xxx?" "xxxxxxxxx" "????" "xxxxxxxxxxxxx" "????" "xxxxxxxxxxxxxxx";
/* The pickup of a coin by the controlled character, once the purse took it. */
static const unsigned char classic_pickup[] = {
    0x48, 0x8b, 0x03, 0x48, 0x89, 0xdf, 0xff, 0x90, 0x00, 0x03, 0x00, 0x00, 0x83, 0xf8, 0x0a, 0x75, 0x00,
    0x48, 0x8d, 0x05, 0x00, 0x00, 0x00, 0x00, 0x45, 0x31, 0xff, 0x48, 0x8b, 0x00, 0x4c, 0x39, 0xb0, 0x58,
    0x01, 0x00, 0x00, 0x75, 0x00, 0x49, 0x8d, 0xbe, 0x00, 0x0e, 0x00, 0x00, 0x48, 0x89, 0xde, 0xe8, 0x00,
    0x00, 0x00, 0x00, 0x84, 0xc0, 0x74, 0x00, 0xbe, 0x23, 0x01, 0x00, 0x00, 0x4c, 0x89, 0xf7, 0xe8};
static const char classic_pickup_mask[] =
    "xxxxxxxx" "?" "xxxxxxx" "?" "xxx" "????" "xxxxxxxxxxxxxx" "?" "xxxxxxxxxxx" "????" "xxx" "?" "xxxxxxxxx";
/* Every stack set to 999, then the region; rbx is dead after it at both sites and the calls are aligned as the game's. */
static const unsigned char classic_sale_full[] = {
    0x31, 0xdb,                               /* xor ebx,ebx */
    0x4c, 0x89, 0xef,                         /* next: mov rdi,r13 */
    0x89, 0xde,                               /* mov esi,ebx */
    0xba, 0xe7, 0x03, 0x00, 0x00,             /* mov edx,999 */
    0xe8, 0x00, 0x00, 0x00, 0x00,             /* call set_stack */
    0xff, 0xc3,                               /* inc ebx */
    0x83, 0xfb, 0x05,                         /* cmp ebx,5 */
    0x75, 0xea,                               /* jne next */
    0xbf, 0x5a, 0x01, 0x00, 0x00, 0x31, 0xf6, /* the region */
};
static const unsigned char classic_pickup_full[] = {
    0x31, 0xdb,                                     /* xor ebx,ebx */
    0x49, 0x8d, 0xbe, 0x00, 0x0e, 0x00, 0x00,       /* next: lea rdi,[r14+0xe00] */
    0x89, 0xde,                                     /* mov esi,ebx */
    0xba, 0xe7, 0x03, 0x00, 0x00,                   /* mov edx,999 */
    0xe8, 0x00, 0x00, 0x00, 0x00,                   /* call set_stack */
    0xff, 0xc3,                                     /* inc ebx */
    0x83, 0xfb, 0x05,                               /* cmp ebx,5 */
    0x75, 0xe6,                                     /* jne next */
    0xbe, 0x23, 0x01, 0x00, 0x00, 0x4c, 0x89, 0xf7, /* the region */
};
enum { CLASSIC_SALE_SET_STACK_CALL = 29, CLASSIC_SALE_CALL_AT = 13, CLASSIC_PICKUP_CALL_AT = 17 };

static const OslCheatSite classic_gold_sites[] = {
    {"C-G1", "sale", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_sale, sizeof(classic_sale), 34, NULL, 7, 34, 7, classic_sale_full,
     sizeof(classic_sale_full), -1, -1, classic_sale_mask, CLASSIC_SALE_SET_STACK_CALL, CLASSIC_SALE_CALL_AT},
    {"C-G2", "coin pickup", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_pickup, sizeof(classic_pickup), 58, NULL, 8, 58, 8,
     classic_pickup_full, sizeof(classic_pickup_full), -1, -1, classic_pickup_mask, -1, CLASSIC_PICKUP_CALL_AT}};

/* Classic Mercedes's shot: the cost (-1 normal, -3, -10 charged) added to her ammunition (+0xebc, 25 at most). */
static const unsigned char classic_shot_cost[] = {
    0x83, 0xbb, 0xb8, 0x0e, 0x00, 0x00, 0x5a, 0xb9, 0xf6, 0xff, 0xff, 0xff, 0x0f, 0x94, 0xc0, 0x74, 0x0d,
    0x49, 0x63, 0xcc, 0x48, 0x8d, 0x15, 0x00, 0x00, 0x00, 0x00, 0x8b, 0x0c, 0x8a, 0x03, 0x8b, 0xbc, 0x0e,
    0x00, 0x00, 0x89, 0x8b, 0xbc, 0x0e, 0x00, 0x00, 0x7e, 0x15, 0x83, 0xf9, 0x1a, 0x7c, 0x21};
static const char classic_shot_cost_mask[] = "xxxxxxxxxxxxxxxxxxxxxxx" "????" "xxxxxxxxxxxxxxxxxxxxxx";
/* Full ammunition for the controlled character. al is live here, so rdx is saved below the red zone; the closing test
 * gives the following jle the sign the game's add would. */
static const unsigned char classic_shot_free[] = {
    0x48, 0x8d, 0x64, 0x24, 0x80,                   /* lea rsp,[rsp-0x80] */
    0x52,                                           /* push rdx */
    0x48, 0x8b, 0x15, 0x00, 0x00, 0x00, 0x00,       /* mov rdx,[rip+global] */
    0x48, 0x85, 0xd2,                               /* test rdx,rdx */
    0x74, 0x10,                                     /* jz spend */
    0x48, 0x39, 0x9a, 0x58, 0x01, 0x00, 0x00,       /* cmp [rdx+0x158],rbx */
    0x75, 0x07,                                     /* jne spend */
    0xb9, 0x19, 0x00, 0x00, 0x00,                   /* mov ecx,25 */
    0xeb, 0x06,                                     /* jmp store */
    0x03, 0x8b, 0xbc, 0x0e, 0x00, 0x00,             /* spend: add ecx,[rbx+0xebc] */
    0x5a,                                           /* store: pop rdx */
    0x48, 0x8d, 0xa4, 0x24, 0x80, 0x00, 0x00, 0x00, /* lea rsp,[rsp+0x80] */
    0x89, 0x8b, 0xbc, 0x0e, 0x00, 0x00,             /* mov [rbx+0xebc],ecx */
    0x85, 0xc9,                                     /* test ecx,ecx */
};
/* The fire function's own read of the root pointer's global. */
static const unsigned char classic_shot_root[] = {0x4c, 0x8d, 0x2d, 0x00, 0x00, 0x00, 0x00, 0xbe, 0x02, 0x00,
                                                  0x00, 0x00, 0xba, 0x01, 0x00, 0x00, 0x00, 0x49, 0x8b, 0x45,
                                                  0x00, 0x48, 0x8b, 0xb8, 0xf0, 0x00, 0x00, 0x00, 0xe8};
enum { CLASSIC_SHOT_PLAYER_AT = 9 };

static const OslCheatSite classic_arrow_sites[] = {
    {"C-A1", "shot ammunition", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_shot_cost, sizeof(classic_shot_cost), 30, NULL, 6, 30, 12,
     classic_shot_free, sizeof(classic_shot_free), CLASSIC_SHOT_PLAYER_AT, -1, classic_shot_cost_mask, -1, -1},
    {"C-A2", "root object global", OSL_CHEAT_SITE_GLOBAL, NULL, 0, classic_shot_root, sizeof(classic_shot_root), 3, NULL, 4, 0,
     0, NULL, 0, -1, -1, NULL, -1, -1}};

const OslCheatInfo osl_cheats[OSL_CHEAT_COUNT] = {
    {OSL_CHEAT_MAX_GOLD_GAIN, "cheat_max_gold_gain", "Max Gold Gain",
     "Picking up gold or selling at a shop sets your gold to 999,999. Buying still costs gold.", "Resources", gold_sites,
     COUNT(gold_sites), classic_gold_sites, COUNT(classic_gold_sites), NULL},
    {OSL_CHEAT_MAX_PHOZON_GAIN, "cheat_max_phozon_gain", "Max Phozon Gain",
     "Absorbing a phozon fills your phozon stock to its maximum.", "Resources", phozon_sites, COUNT(phozon_sites),
     classic_phozon_sites, COUNT(classic_phozon_sites), NULL},
    {OSL_CHEAT_NO_AP_CONSUMPTION, "cheat_no_ap_consumption", "No AP Consumption",
     "Learning an ability on the Ability screen does not spend AP.", "Resources", ap_sites, COUNT(ap_sites), NULL, 0,
     "Classic has no AP: its skills unlock by Psypher level at no cost"},
    {OSL_CHEAT_INFINITE_PP, "cheat_infinite_pp", "Infinite PP",
     "PP refills to its maximum whenever it changes, so skills never run out of PP.", "Combat", pp_sites,
     COUNT(pp_sites), classic_pp_sites, COUNT(classic_pp_sites), NULL},
    {OSL_CHEAT_INFINITE_POW, "cheat_infinite_pow", "Infinite POW",
     "Using a skill refills POW instead of spending it.", "Combat", pow_sites, COUNT(pow_sites), classic_pow_sites,
     COUNT(classic_pow_sites), NULL},
    {OSL_CHEAT_INFINITE_HEALTH, "cheat_infinite_health", "Infinite Health",
     "Damage does not lower the controlled character's HP. Healing still works; enemies are unaffected.", "Combat",
     health_sites, COUNT(health_sites), classic_health_sites, COUNT(classic_health_sites), NULL},
    {OSL_CHEAT_INFINITE_ARROWS, "cheat_infinite_arrows", "Infinite Arrows",
     "Mercedes's normal crossbow shots refill her POW instead of spending it.", "Combat", arrow_sites,
     COUNT(arrow_sites), classic_arrow_sites, COUNT(classic_arrow_sites), NULL},
};

const char *osl_engine_name(OslEngine engine) { return engine == OSL_ENGINE_CLASSIC ? "Classic" : "Refined"; }

const OslCheatSite *osl_cheat_engine_sites(const OslCheatInfo *info, OslEngine engine, size_t *count) {
    *count = engine == OSL_ENGINE_CLASSIC ? info->classic_site_count : info->site_count;
    return engine == OSL_ENGINE_CLASSIC ? info->classic_sites : info->sites;
}

/* The start of the function every experience award goes through (character in rdi, award in esi): mov rbx,rdi;
 * mov eax,esi at 11, then the HP test, the award as a float and the status bonus. */
static const unsigned char experience_award[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48, 0x83, 0xec, 0x10, 0x48, 0x89, 0xfb, 0x89, 0xf0,
    0x83, 0xbb, 0x5c, 0x03, 0x00, 0x00, 0x00, 0x0f, 0x84, 0x15, 0x02, 0x00, 0x00, 0xc4, 0xe1, 0xfa,
    0x2a, 0xc0, 0x85, 0xf6, 0x0f, 0x84, 0x65, 0x01, 0x00, 0x00, 0xbe, 0x1b, 0x00, 0x00, 0x00};
/* Its end: mov [rbx+0x2140],r14d (the new total, after any level-ups); add rsp,0x10; pop rbx; pop r14; pop rbp; ret. */
static const unsigned char experience_stored[] = {0x44, 0x89, 0xb3, 0x40, 0x21, 0x00, 0x00, 0x48,
                                                  0x83, 0xc4, 0x10, 0x5b, 0x41, 0x5e, 0x5d, 0xc3};
/* The region with the award multiplied by the switch byte (0 read as 1) and capped; rcx is free and the flags dead. */
static const unsigned char experience_multiplied[] = {
    0x48, 0x89, 0xfb,                         /* mov rbx,rdi */
    0x89, 0xf0,                               /* mov eax,esi */
    0x0f, 0xb6, 0x0d, 0x00, 0x00, 0x00, 0x00, /* movzx ecx,byte [rip+switch] */
    0x83, 0xf9, 0x01,                         /* cmp ecx,1 */
    0x83, 0xd1, 0x00,                         /* adc ecx,0 */
    0x48, 0x0f, 0xaf, 0xc1,                   /* imul rax,rcx */
    0xb9, 0xa0, 0x25, 0x26, 0x00,             /* mov ecx,OSL_EXPERIENCE_CAP */
    0x48, 0x39, 0xc8,                         /* cmp rax,rcx */
    0x0f, 0x47, 0xc1,                         /* cmova eax,ecx */
    0x89, 0xc6,                               /* mov esi,eax */
};
enum { EXPERIENCE_SWITCH_AT = 8, SAME_FUNCTION_SPAN = 0x400 };

static const OslCheatSite experience_sites[] = {
    {"E1", "experience award", OSL_CHEAT_SITE_HOOK, NULL, 0, experience_award, sizeof(experience_award), 11, NULL, 5, 11,
     5, experience_multiplied, sizeof(experience_multiplied), -1, EXPERIENCE_SWITCH_AT, NULL, -1, -1},
    {"E2", "experience stored", OSL_CHEAT_SITE_SAME_FUNCTION, NULL, 0, experience_stored, sizeof(experience_stored), 0, NULL,
     0, 0, 0, NULL, 0, -1, -1, NULL, -1, -1}};

/* Classic award (character in rdi, award in esi); the function itself caps the total at 99,999,999. */
static const unsigned char classic_experience_award[] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48, 0x89, 0xfb, 0x41, 0x89, 0xf6, 0x0f, 0xb7, 0x83, 0x4e,
    0x0c, 0x00, 0x00, 0x83, 0xf8, 0x02, 0x75, 0x2e, 0xbe, 0x16, 0x00, 0x00, 0x00, 0x48, 0x89, 0xdf, 0xe8};
/* The end of the function. */
static const unsigned char classic_experience_stored[] = {0xc7, 0x83, 0xcc, 0x07, 0x00, 0x00, 0xff, 0xe0,
                                                          0xf5, 0x05, 0x5b, 0x41, 0x5e, 0x5d, 0xc3};
/* The region with the award multiplied by the switch byte (0 read as 1) and capped; rcx is free and the flags dead. */
static const unsigned char classic_experience_multiplied[] = {
    0x48, 0x89, 0xfb,                         /* mov rbx,rdi */
    0x41, 0x89, 0xf6,                         /* mov r14d,esi */
    0x0f, 0xb6, 0x0d, 0x00, 0x00, 0x00, 0x00, /* movzx ecx,byte [rip+switch] */
    0x83, 0xf9, 0x01,                         /* cmp ecx,1 */
    0x83, 0xd1, 0x00,                         /* adc ecx,0 */
    0x4c, 0x0f, 0xaf, 0xf1,                   /* imul r14,rcx */
    0xb9, 0xff, 0xe0, 0xf5, 0x05,             /* mov ecx,99999999 */
    0x49, 0x39, 0xce,                         /* cmp r14,rcx */
    0x4c, 0x0f, 0x47, 0xf1,                   /* cmova r14,rcx */
};
enum { CLASSIC_EXPERIENCE_SWITCH_AT = 9 };

static const OslCheatSite classic_experience_sites[] = {
    {"C-E1", "experience award", OSL_CHEAT_SITE_HOOK, NULL, 0, classic_experience_award, sizeof(classic_experience_award), 7,
     NULL, 6, 7, 6, classic_experience_multiplied, sizeof(classic_experience_multiplied), -1, CLASSIC_EXPERIENCE_SWITCH_AT,
     NULL, -1, -1},
    {"C-E2", "experience stored", OSL_CHEAT_SITE_SAME_FUNCTION, NULL, 0, classic_experience_stored,
     sizeof(classic_experience_stored), 0, NULL, 0, 0, 0, NULL, 0, -1, -1, NULL, -1, -1}};

const OslChoice osl_experience_choices[OSL_EXPERIENCE_CHOICE_COUNT] = {
    {1, "1", "Off"}, {2, "2", "2x"}, {5, "5", "5x"}, {10, "10", "10x"}, {100, "100", "100x"}};

const OslSettingInfo osl_experience_multiplier = {
    "cheat_experience_multiplier", "Experience Multiplier",
    "Multiplies the experience each award adds", experience_sites,
    COUNT(experience_sites), classic_experience_sites, COUNT(classic_experience_sites)};

static int choice_index(int value) {
    for (int i = 0; i < OSL_EXPERIENCE_CHOICE_COUNT; ++i)
        if (osl_experience_choices[i].value == value) return i;
    return -1;
}

int osl_experience_listed(int value) { return choice_index(value) >= 0 ? value : OSL_EXPERIENCE_OFF; }

int osl_experience_from_saved(const char *saved) {
    if (!saved) return 0;
    for (int i = 0; i < OSL_EXPERIENCE_CHOICE_COUNT; ++i)
        if (!strcmp(saved, osl_experience_choices[i].saved)) return osl_experience_choices[i].value;
    return 0;
}

const char *osl_experience_label(int value) {
    const int i = choice_index(value);
    return osl_experience_choices[i >= 0 ? i : 0].label;
}

int osl_experience_next(int value) {
    const int i = choice_index(value);
    return osl_experience_choices[i >= 0 ? (i + 1) % OSL_EXPERIENCE_CHOICE_COUNT : 0].value;
}

const char *osl_cheat_state_name(OslCheatState state) {
    switch (state) {
    case OSL_CHEAT_COMPLETE: return "COMPLETE";
    case OSL_CHEAT_PARTIAL: return "PARTIAL";
    case OSL_CHEAT_MISSING: return "MISSING";
    case OSL_CHEAT_AMBIGUOUS: return "AMBIGUOUS";
    case OSL_CHEAT_INVALID: return "INVALID";
    case OSL_CHEAT_INHERENT: return "INHERENTLY SATISFIED";
    }
    return "?";
}

static int in_patch(const OslCheatSite *site, size_t k) {
    return k >= site->patch_at && k < site->patch_at + site->patch_size;
}

static int compared(const OslCheatSite *site, size_t k) {
    return !in_patch(site, k) && (!site->mask || site->mask[k] == 'x');
}

/* The prefix and the signature with its patch bytes and masked bytes left out, so a site that is already changed is
 * still found and reported. `at` is where the prefix starts. */
static int matches(const unsigned char *at, const OslCheatSite *site) {
    if (site->prefix_size && memcmp(at, site->prefix, site->prefix_size)) return 0;
    const unsigned char *code = at + site->prefix_size;
    for (size_t k = 0; k < site->signature_size; ++k)
        if (compared(site, k) && code[k] != site->signature[k]) return 0;
    return 1;
}

static int32_t get32(const unsigned char *at) {
    int32_t value;
    memcpy(&value, at, 4);
    return value;
}

/* The global the accessor that `call_at` calls reads, or 0 when the call does not reach the expected accessor. */
static uint64_t resolve_player_global(const unsigned char *image, uint64_t image_size, uint64_t call_at) {
    if (image[call_at] != 0xe8) return 0;
    const int64_t accessor = (int64_t)call_at + 5 + get32(image + call_at + 1);
    if (accessor < 0 || (uint64_t)accessor + sizeof(player_accessor) > image_size) return 0;
    for (size_t k = 0; k < sizeof(player_accessor); ++k)
        if ((k < ACCESSOR_DISP_AT || k >= ACCESSOR_DISP_END) && image[accessor + k] != player_accessor[k]) return 0;
    const int64_t global = accessor + ACCESSOR_DISP_END + get32(image + accessor + ACCESSOR_DISP_AT);
    return global > 0 && (uint64_t)global + 8 <= image_size ? (uint64_t)global : 0;
}

/* Where the `call rel32` at `call_at` goes, when that is inside the code; 0 otherwise. */
static uint64_t call_target(const unsigned char *image, const OslCodeRange *ranges, size_t range_count, uint64_t call_at) {
    if (image[call_at] != 0xe8) return 0;
    const int64_t target = (int64_t)call_at + 5 + get32(image + call_at + 1);
    for (size_t r = 0; r < range_count; ++r)
        if (target >= 0 && (uint64_t)target >= ranges[r].begin && (uint64_t)target < ranges[r].end) return (uint64_t)target;
    return 0;
}

static void find_site(const unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                      const OslCheatSite *site, OslCheatSiteScan *scan) {
    memset(scan, 0, sizeof(*scan));
    const size_t total = site->prefix_size + site->signature_size;
    const unsigned char first = site->prefix_size ? site->prefix[0] : site->signature[0];
    for (size_t r = 0; r < range_count; ++r) {
        const uint64_t end = ranges[r].end < image_size ? ranges[r].end : image_size;
        if (ranges[r].begin >= end || end - ranges[r].begin < total) continue;
        const uint64_t last = end - total;
        uint64_t position = ranges[r].begin;
        while (position <= last) {
            const unsigned char *found = memchr(image + position, first, (size_t)(last - position + 1));
            if (!found) break;
            position = (uint64_t)(found - image);
            if (matches(found, site)) {
                if (scan->hits < OSL_CHEAT_MAX_HITS) scan->at[scan->hits] = position + site->prefix_size;
                ++scan->hits;
            }
            ++position;
        }
    }
    if (scan->hits == 0) {
        scan->state = OSL_CHEAT_SITE_MISSING;
    } else if (scan->hits > 1) {
        scan->state = OSL_CHEAT_SITE_AMBIGUOUS;
    } else if (site->kind == OSL_CHEAT_SITE_PLAYER_ACCESSOR) {
        scan->patch = scan->at[0] + site->patch_at;
        scan->global = resolve_player_global(image, image_size, scan->at[0]);
        scan->state = scan->global ? OSL_CHEAT_SITE_UNIQUE : OSL_CHEAT_SITE_MODIFIED;
    } else if (site->kind == OSL_CHEAT_SITE_GLOBAL) {
        scan->patch = scan->at[0] + site->patch_at;
        const int64_t global = (int64_t)scan->patch + 4 + get32(image + scan->patch);
        scan->global = global > 0 && (uint64_t)global + 8 <= image_size ? (uint64_t)global : 0;
        scan->state = scan->global ? OSL_CHEAT_SITE_UNIQUE : OSL_CHEAT_SITE_MODIFIED;
    } else {
        scan->patch = scan->at[0] + site->patch_at;
        const unsigned char *bytes = image + scan->patch;
        scan->state = !memcmp(bytes, site->signature + site->patch_at, site->patch_size) ? OSL_CHEAT_SITE_UNIQUE
                      : site->replacement && !memcmp(bytes, site->replacement, site->patch_size) ? OSL_CHEAT_SITE_PATCHED
                                                                                                  : OSL_CHEAT_SITE_MODIFIED;
    }
    if (scan->state == OSL_CHEAT_SITE_UNIQUE && site->call_from >= 0) {
        scan->callee = call_target(image, ranges, range_count, scan->at[0] + (uint64_t)site->call_from);
        if (!scan->callee) scan->state = OSL_CHEAT_SITE_MODIFIED;
    }
}

static void add_reason(OslCheatScan *scan, const OslCheatSite *site, const char *what) {
    const size_t used = strlen(scan->reason);
    snprintf(scan->reason + used, sizeof(scan->reason) - used, "%s%s %s: %s", used ? "; " : "", site->id, site->name,
             what);
}

static int spans_overlap(uint64_t a, size_t a_size, uint64_t b, size_t b_size) {
    return a < b + b_size && b < a + a_size;
}

/* Every group of sites the scan judges together: the Refined engine's cheats then its Experience Multiplier, then the
 * Classic engine's. */
enum { EXPERIENCE_GROUP = OSL_CHEAT_COUNT, ENGINE_GROUPS, GROUP_COUNT = ENGINE_GROUPS * OSL_ENGINE_COUNT };

typedef struct {
    const OslCheatSite *sites;
    size_t site_count;
    const char *inherent; /* why a group without sites needs none */
} SiteGroup;

static SiteGroup group_of(int group) {
    const OslEngine engine = (OslEngine)(group / ENGINE_GROUPS);
    const int member = group % ENGINE_GROUPS;
    SiteGroup sites;
    sites.inherent = NULL;
    if (member < OSL_CHEAT_COUNT) {
        sites.sites = osl_cheat_engine_sites(&osl_cheats[member], engine, &sites.site_count);
        if (engine == OSL_ENGINE_CLASSIC) sites.inherent = osl_cheats[member].classic_inherent;
    } else {
        sites.sites = engine == OSL_ENGINE_CLASSIC ? osl_experience_multiplier.classic_sites : osl_experience_multiplier.sites;
        sites.site_count = engine == OSL_ENGINE_CLASSIC ? osl_experience_multiplier.classic_site_count
                                                        : osl_experience_multiplier.site_count;
    }
    return sites;
}

/* Another found site whose code (prefix and signature) shares bytes with this one's, in this group or another. */
static const OslCheatSite *overlapping(const OslCheatScan *scans, int group, size_t index) {
    const OslCheatSite *site = &group_of(group).sites[index];
    const uint64_t at = scans[group].sites[index].at[0] - site->prefix_size;
    for (int g = 0; g < GROUP_COUNT; ++g) {
        const SiteGroup others = group_of(g);
        for (size_t s = 0; s < others.site_count; ++s) {
            if ((g == group && s == index) || scans[g].sites[s].hits != 1) continue;
            const OslCheatSite *other = &others.sites[s];
            if (spans_overlap(at, site->prefix_size + site->signature_size, scans[g].sites[s].at[0] - other->prefix_size,
                              other->prefix_size + other->signature_size))
                return other;
        }
    }
    return NULL;
}

/* A same-function site must start after the group's first hooked site and close to it; a global's lea close to it on
 * either side. */
static int near_hook(const OslCheatScan *scan, SiteGroup sites, size_t index) {
    for (size_t s = 0; s < sites.site_count; ++s) {
        if (sites.sites[s].kind != OSL_CHEAT_SITE_HOOK) continue;
        const uint64_t hook = scan->sites[s].at[0], anchor = scan->sites[index].at[0];
        if (anchor > hook) return anchor - hook < SAME_FUNCTION_SPAN;
        return sites.sites[index].kind == OSL_CHEAT_SITE_GLOBAL && hook - anchor < SAME_FUNCTION_SPAN;
    }
    return 0;
}

static void judge(OslCheatScan *scans, int group) {
    const SiteGroup sites = group_of(group);
    OslCheatScan *scan = &scans[group];
    size_t once = 0, missing = 0, ambiguous = 0;
    int bytes_ok = 1;
    scan->reason[0] = 0;
    char what[64];
    for (size_t s = 0; s < sites.site_count; ++s) {
        const OslCheatSite *site = &sites.sites[s];
        const OslCheatSiteScan *found = &scan->sites[s];
        switch (found->state) {
        case OSL_CHEAT_SITE_MISSING:
            ++missing;
            add_reason(scan, site, "not found");
            break;
        case OSL_CHEAT_SITE_AMBIGUOUS:
            ++ambiguous;
            snprintf(what, sizeof(what), "%zu matches", found->hits);
            add_reason(scan, site, what);
            break;
        case OSL_CHEAT_SITE_PATCHED:
            ++once;
            bytes_ok = 0;
            add_reason(scan, site, "already changed");
            break;
        case OSL_CHEAT_SITE_MODIFIED:
            ++once;
            bytes_ok = 0;
            add_reason(scan, site,
                       site->kind == OSL_CHEAT_SITE_PLAYER_ACCESSOR ? "the call does not reach the player accessor"
                       : site->kind == OSL_CHEAT_SITE_GLOBAL        ? "the lea does not give a global in the image"
                       : site->call_from >= 0 ? "other bytes than the original instruction, or a call outside the code"
                                              : "other bytes than the original instruction");
            break;
        case OSL_CHEAT_SITE_UNIQUE: ++once; break;
        }
    }
    scan->related = once == sites.site_count;
    for (size_t s = 0; scan->related && s < sites.site_count; ++s) {
        const OslCheatSite *other = overlapping(scans, group, s);
        if (other) {
            scan->related = 0;
            snprintf(what, sizeof(what), "overlaps %s %s", other->id, other->name);
            add_reason(scan, &sites.sites[s], what);
        } else if ((sites.sites[s].kind == OSL_CHEAT_SITE_SAME_FUNCTION || sites.sites[s].kind == OSL_CHEAT_SITE_GLOBAL) &&
                   !near_hook(scan, sites, s)) {
            scan->related = 0;
            add_reason(scan, &sites.sites[s], sites.sites[s].kind == OSL_CHEAT_SITE_GLOBAL ? "not next to the hooked code"
                                                                                         : "not in the hooked function");
        }
    }
    if (sites.site_count == 0) {
        snprintf(scan->reason, sizeof(scan->reason), "%s", sites.inherent ? sites.inherent : "no site known in this engine");
        scan->state = sites.inherent ? OSL_CHEAT_INHERENT : OSL_CHEAT_MISSING;
    } else if (once == sites.site_count) {
        scan->state = bytes_ok && scan->related ? OSL_CHEAT_COMPLETE : OSL_CHEAT_INVALID;
    } else if (once + ambiguous == 0) {
        scan->state = OSL_CHEAT_MISSING;
    } else {
        scan->state = missing ? OSL_CHEAT_PARTIAL : OSL_CHEAT_AMBIGUOUS;
    }
}

static void scan_groups(const unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                        OslCheatScan scans[GROUP_COUNT]) {
    memset(scans, 0, sizeof(OslCheatScan) * GROUP_COUNT);
    for (int g = 0; g < GROUP_COUNT; ++g) {
        const SiteGroup sites = group_of(g);
        for (size_t s = 0; s < sites.site_count; ++s)
            find_site(image, image_size, ranges, range_count, &sites.sites[s], &scans[g].sites[s]);
    }
    for (int g = 0; g < GROUP_COUNT; ++g) judge(scans, g);
}

void osl_cheat_scan_engine(const unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                           OslEngine engine, OslCheatScan scans[OSL_CHEAT_COUNT]) {
    OslCheatScan all[GROUP_COUNT];
    scan_groups(image, image_size, ranges, range_count, all);
    memcpy(scans, all + (int)engine * ENGINE_GROUPS, sizeof(OslCheatScan) * OSL_CHEAT_COUNT);
}

void osl_cheat_scan(const unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                    OslCheatScan scans[OSL_CHEAT_COUNT]) {
    osl_cheat_scan_engine(image, image_size, ranges, range_count, OSL_ENGINE_REFINED, scans);
}

void osl_experience_scan_engine(const unsigned char *image, uint64_t image_size, const OslCodeRange *ranges,
                                size_t range_count, OslEngine engine, OslCheatScan *scan) {
    OslCheatScan all[GROUP_COUNT];
    scan_groups(image, image_size, ranges, range_count, all);
    *scan = all[(int)engine * ENGINE_GROUPS + EXPERIENCE_GROUP];
}

void osl_experience_scan(const unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                         OslCheatScan *scan) {
    osl_experience_scan_engine(image, image_size, ranges, range_count, OSL_ENGINE_REFINED, scan);
}

/* The hook: the flag byte decides between the original instructions and the changed ones. Flags are saved around the
 * test, and the stack pointer moves below the red zone first, so the hooked code sees neither change. */
enum {
    LEA_DOWN_BYTES = 5,
    LEA_UP_BYTES = 8,
    CMP_BYTES = 7,
    JMP_BYTES = 5
};

static int fits_rel32(int64_t delta) { return delta >= INT32_MIN && delta <= INT32_MAX; }

static void put32(unsigned char *at, int32_t value) { memcpy(at, &value, 4); }

/* DEBUG_SAULO: the telemetry probes. A probe is lea rsp,[rsp-0x80]; push id; call [rip+slot]; lea rsp,[rsp+0x88], the
 * slot being the stub's last 8 bytes, which hold the thunk's address. */
enum { PROBE_BYTES = 24, PROBE_SLOT_BYTES = 8 };
static const void *probe_thunk;
static OslCheatProbeSite probe_sites[OSL_CHEAT_PROBE_SITES];
static size_t probe_count;

void osl_cheat_set_probe(const void *thunk) { probe_thunk = thunk; }

void osl_cheat_probe_reset(void) {
    memset(probe_sites, 0, sizeof(probe_sites));
    probe_count = 0;
}

size_t osl_cheat_probe_count(void) { return probe_count; }

const OslCheatProbeSite *osl_cheat_probe_site(size_t id) { return id < probe_count ? &probe_sites[id] : NULL; }

static void emit_probe(unsigned char *code, size_t *n, const unsigned char *address, int id, int phase) {
    static const unsigned char lea_down[] = {0x48, 0x8d, 0x64, 0x24, 0x80};
    static const unsigned char lea_up[] = {0x48, 0x8d, 0xa4, 0x24, 0x88, 0x00, 0x00, 0x00};
    memcpy(code + *n, lea_down, sizeof(lea_down));
    *n += sizeof(lea_down);
    code[(*n)++] = 0x68; /* push imm32 */
    put32(code + *n, id * 4 + phase);
    *n += 4;
    code[(*n)++] = 0xff; /* call [rip+disp32] */
    code[(*n)++] = 0x15;
    const unsigned char *slot = address + OSL_CHEAT_STUB_BYTES - PROBE_SLOT_BYTES;
    put32(code + *n, (int32_t)(slot - (address + *n + 4)));
    *n += 4;
    memcpy(code + *n, lea_up, sizeof(lea_up));
    *n += sizeof(lea_up);
}

/* Writes the stub at `code` (which will live at `address`); 0 when a jump or the flag is out of reach. `probe` is the
 * DEBUG_SAULO telemetry id, or -1. */
static int emit_stub(unsigned char *code, const unsigned char *address, const unsigned char *original, size_t size,
                     const unsigned char *changed, size_t changed_size, const unsigned char *back, const unsigned char *flag,
                     int probe) {
    static const unsigned char lea_down[LEA_DOWN_BYTES] = {0x48, 0x8d, 0x64, 0x24, 0x80};
    static const unsigned char lea_up[LEA_UP_BYTES] = {0x48, 0x8d, 0xa4, 0x24, 0x80, 0x00, 0x00, 0x00};
    size_t n = 0;
    if (probe >= 0) {
        emit_probe(code, &n, address, probe, 0);
        memcpy(code + OSL_CHEAT_STUB_BYTES - PROBE_SLOT_BYTES, &probe_thunk, PROBE_SLOT_BYTES);
    }
    memcpy(code + n, lea_down, sizeof(lea_down));
    n += sizeof(lea_down);
    code[n++] = 0x9c; /* pushfq */
    const int64_t flag_delta = (int64_t)(intptr_t)flag - (int64_t)(intptr_t)(address + n + CMP_BYTES);
    if (!fits_rel32(flag_delta)) return 0;
    code[n++] = 0x80; /* cmp byte [rip+disp],0 */
    code[n++] = 0x3d;
    put32(code + n, (int32_t)flag_delta);
    n += 4;
    code[n++] = 0x00;
    code[n++] = 0x75; /* jne changed */
    code[n++] = (unsigned char)(1 + LEA_UP_BYTES + size + (probe >= 0 ? PROBE_BYTES : 0) + JMP_BYTES);
    for (int pass = 0; pass < 2; ++pass) {
        code[n++] = 0x9d; /* popfq */
        memcpy(code + n, lea_up, sizeof(lea_up));
        n += sizeof(lea_up);
        memcpy(code + n, pass ? changed : original, pass ? changed_size : size);
        n += pass ? changed_size : size;
        if (probe >= 0) emit_probe(code, &n, address, probe, 1 + pass);
        const int64_t back_delta = (int64_t)(intptr_t)back - (int64_t)(intptr_t)(address + n + JMP_BYTES);
        if (!fits_rel32(back_delta)) return 0;
        code[n++] = 0xe9;
        put32(code + n, (int32_t)back_delta);
        n += 4;
    }
    return n <= OSL_CHEAT_STUB_BYTES - (probe >= 0 ? PROBE_SLOT_BYTES : 0);
}

typedef struct {
    unsigned char stub[OSL_CHEAT_STUB_BYTES];
    uint64_t region;
    size_t region_size;
    int32_t jump;
    int probe; /* DEBUG_SAULO */
} PlannedHook;

/* Where the changed block starts in a stub: after the flag test and the original block's return jump. */
static size_t changed_block_at(size_t region_size, int probe) {
    return LEA_DOWN_BYTES + 1 + CMP_BYTES + 2 + 1 + LEA_UP_BYTES + region_size + JMP_BYTES + 1 + LEA_UP_BYTES +
           (probe >= 0 ? 2 * PROBE_BYTES : 0);
}

/* Points the rel32 at `at` in the changed block (which will run at `block`) to `target`. */
static int link_rel32(unsigned char *changed, size_t changed_size, int at, const unsigned char *block,
                      const unsigned char *target) {
    if (at < 0 || (size_t)at + 4 > changed_size) return 0;
    const int64_t delta = (int64_t)(intptr_t)target - (int64_t)(intptr_t)(block + at + 4);
    if (!fits_rel32(delta)) return 0;
    put32(changed + at, (int32_t)delta);
    return 1;
}

/* DEBUG_SAULO: a telemetry id for a hook about to be planned, or -1 when no probe is set. */
static int reserve_probe(const OslCheatSite *site, int cheat, OslEngine engine, const unsigned char *image, uint64_t player_global,
                         const unsigned char *flag) {
    if (!probe_thunk || probe_count >= OSL_CHEAT_PROBE_SITES) return -1;
    OslCheatProbeSite *entry = &probe_sites[probe_count];
    memset(entry, 0, sizeof(*entry));
    entry->site = site;
    entry->cheat = cheat;
    entry->engine = engine;
    entry->image = (uint64_t)(uintptr_t)image;
    entry->global = player_global ? (uint64_t)(uintptr_t)(image + player_global) : 0;
    entry->flag = flag;
    return (int)probe_count++;
}

static int plan_hook(PlannedHook *hook, unsigned char *image, uint64_t image_size, const OslCheatSite *site,
                     const OslCheatSiteScan *found, uint64_t player_global, uint64_t callee, unsigned char *stub_at,
                     const unsigned char *flag, int probe) {
    hook->probe = probe;
    const int64_t region = (int64_t)found->at[0] + site->region_at;
    const size_t size = site->region_size;
    if (size < JMP_BYTES || size > OSL_CHEAT_REGION_MAX || region < 0 || (uint64_t)region + size > image_size) return 0;
    const uint64_t patch_in_region = found->patch - (uint64_t)region;
    if (found->patch < (uint64_t)region || patch_in_region + site->patch_size > size) return 0;
    unsigned char changed[OSL_CHEAT_CHANGED_MAX];
    size_t changed_size = size;
    if (site->changed) {
        if (site->changed_size > sizeof(changed)) return 0;
        changed_size = site->changed_size;
        memcpy(changed, site->changed, changed_size);
        const unsigned char *block = stub_at + changed_block_at(size, probe);
        if (site->changed_player_at >= 0 &&
            (!player_global || !link_rel32(changed, changed_size, site->changed_player_at, block, image + player_global)))
            return 0;
        if (site->changed_flag_at >= 0 && !link_rel32(changed, changed_size, site->changed_flag_at, block, flag)) return 0;
        if (site->changed_call_at >= 0 &&
            (!callee || !link_rel32(changed, changed_size, site->changed_call_at, block, image + callee)))
            return 0;
    } else {
        memcpy(changed, image + region, size);
        memcpy(changed + patch_in_region, site->replacement, site->patch_size);
    }
    memset(hook->stub, 0xcc, sizeof(hook->stub));
    if (!emit_stub(hook->stub, stub_at, image + region, size, changed, changed_size, image + region + size, flag, probe)) return 0;
    if (probe >= 0) probe_sites[probe].region = (uint64_t)region;
    const int64_t jump = (int64_t)(intptr_t)stub_at - (int64_t)(intptr_t)(image + region + JMP_BYTES);
    if (!fits_rel32(jump)) return 0;
    hook->region = (uint64_t)region;
    hook->region_size = size;
    hook->jump = (int32_t)jump;
    return 1;
}

/* Hooks a COMPLETE group with its stubs from `*used` on, all or nothing; 0 (and the scan INVALID) when it cannot. */
static int install_group(unsigned char *image, uint64_t image_size, OslCheatScan *scan, SiteGroup sites,
                         unsigned char *stubs, size_t stubs_size, size_t *used, const unsigned char *flag, int cheat,
                         OslEngine engine) {
    if (scan->state != OSL_CHEAT_COMPLETE) return 0;
    PlannedHook hooks[OSL_CHEAT_MAX_SITES];
    uint64_t player_global = 0, callee = 0;
    size_t hook_count = 0;
    for (size_t s = 0; s < sites.site_count; ++s) {
        if (sites.sites[s].kind == OSL_CHEAT_SITE_PLAYER_ACCESSOR || sites.sites[s].kind == OSL_CHEAT_SITE_GLOBAL)
            player_global = scan->sites[s].global;
        else if (sites.sites[s].kind == OSL_CHEAT_SITE_HOOK) ++hook_count;
        if (sites.sites[s].call_from >= 0) callee = scan->sites[s].callee;
    }
    int ok = *used + hook_count * OSL_CHEAT_STUB_BYTES <= stubs_size;
    size_t planned = 0;
    for (size_t s = 0; ok && s < sites.site_count; ++s) {
        if (sites.sites[s].kind != OSL_CHEAT_SITE_HOOK) continue;
        const int probe = reserve_probe(&sites.sites[s], cheat, engine, image, player_global, flag);
        ok = plan_hook(&hooks[planned], image, image_size, &sites.sites[s], &scan->sites[s], player_global, callee,
                       stubs + *used + planned * OSL_CHEAT_STUB_BYTES, flag, probe);
        ++planned;
    }
    if (!ok) {
        scan->state = OSL_CHEAT_INVALID;
        snprintf(scan->reason, sizeof(scan->reason), "no room or reach for its hook");
        return 0;
    }
    for (size_t h = 0; h < hook_count; ++h) {
        memcpy(stubs + *used + h * OSL_CHEAT_STUB_BYTES, hooks[h].stub, OSL_CHEAT_STUB_BYTES);
        unsigned char *at = image + hooks[h].region;
        at[0] = 0xe9;
        put32(at + 1, hooks[h].jump);
        memset(at + JMP_BYTES, 0x90, hooks[h].region_size - JMP_BYTES);
        if (hooks[h].probe >= 0) probe_sites[hooks[h].probe].installed = 1;
    }
    *used += hook_count * OSL_CHEAT_STUB_BYTES;
    return 1;
}

unsigned osl_install_cheats_engines(unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                                    unsigned char *stubs, size_t stubs_size, unsigned char *flags,
                                    OslCheatScan scans[OSL_CHEAT_COUNT], OslCheatScan classic_scans[OSL_CHEAT_COUNT],
                                    unsigned hooked[OSL_ENGINE_COUNT]) {
    OslCheatScan all[GROUP_COUNT];
    scan_groups(image, image_size, ranges, range_count, all);
    unsigned installed = 0, by_engine[OSL_ENGINE_COUNT] = {0};
    size_t used = 0;
    for (int e = 0; e < OSL_ENGINE_COUNT; ++e) {
        for (int c = 0; c < OSL_CHEAT_COUNT; ++c) {
            const int group = e * ENGINE_GROUPS + c;
            if (install_group(image, image_size, &all[group], group_of(group), stubs, stubs_size, &used, flags + c, c,
                              (OslEngine)e))
                by_engine[e] |= osl_cheat_bit((OslCheat)c);
        }
        installed |= by_engine[e];
    }
    if (scans) memcpy(scans, all, sizeof(OslCheatScan) * OSL_CHEAT_COUNT);
    if (classic_scans) memcpy(classic_scans, all + ENGINE_GROUPS, sizeof(OslCheatScan) * OSL_CHEAT_COUNT);
    if (hooked) memcpy(hooked, by_engine, sizeof(by_engine));
    return installed;
}

unsigned osl_install_cheats(unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                            unsigned char *stubs, size_t stubs_size, unsigned char *flags, OslCheatScan *scans) {
    return osl_install_cheats_engines(image, image_size, ranges, range_count, stubs, stubs_size, flags, scans, NULL, NULL);
}

unsigned osl_install_experience_engines(unsigned char *image, uint64_t image_size, const OslCodeRange *ranges,
                                        size_t range_count, unsigned char *stubs, size_t stubs_size, unsigned char *flag,
                                        OslCheatScan *scan, OslCheatScan *classic_scan) {
    OslCheatScan all[GROUP_COUNT];
    scan_groups(image, image_size, ranges, range_count, all);
    size_t used = 0;
    unsigned installed = 0;
    for (int e = 0; e < OSL_ENGINE_COUNT; ++e) {
        const int group = e * ENGINE_GROUPS + EXPERIENCE_GROUP;
        if (install_group(image, image_size, &all[group], group_of(group), stubs, stubs_size, &used, flag, OSL_CHEAT_COUNT,
                          (OslEngine)e))
            installed |= 1u << e;
    }
    if (scan) *scan = all[EXPERIENCE_GROUP];
    if (classic_scan) *classic_scan = all[ENGINE_GROUPS + EXPERIENCE_GROUP];
    return installed;
}

int osl_install_experience(unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                           unsigned char *stubs, size_t stubs_size, unsigned char *flag, OslCheatScan *scan) {
    return osl_install_experience_engines(image, image_size, ranges, range_count, stubs, stubs_size, flag, scan, NULL) != 0;
}

void osl_cheat_summary(unsigned enabled, char *text, size_t size) {
    if (!size) return;
    text[0] = 0;
    size_t used = 0, off = 0;
    for (int c = 0; c < OSL_CHEAT_COUNT; ++c) {
        if (!(enabled & osl_cheat_bit((OslCheat)c))) {
            ++off;
            continue;
        }
        const int n = snprintf(text + used, size - used, "%s%s on", used ? ", " : "", osl_cheats[c].label);
        if (n < 0 || (size_t)n >= size - used) return;
        used += (size_t)n;
    }
    if (!used) {
        snprintf(text, size, "all off");
    } else if (off) {
        snprintf(text + used, size - used, ", %zu off", off);
    }
}
