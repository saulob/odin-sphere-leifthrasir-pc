/* Cheats: optional gameplay modifiers. Each one hooks a few instructions of the game's code in memory, at sites found
 * by their code, and the hook asks a one-byte switch per cheat which behaviour to run; the game files are never
 * changed. The executable holds two gameplay engines, the Refined one (Os2 classes) and the Classic one (Os classes),
 * so a cheat has a group of sites per engine, both behind the same switch. One table serves the launcher, the
 * settings file, the loader, the in-game menu, the build scan and the diagnostics. */
#ifndef OSL_CHEATS_H
#define OSL_CHEATS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OSL_CHEAT_MAX_GOLD_GAIN,
    OSL_CHEAT_MAX_PHOZON_GAIN,
    OSL_CHEAT_NO_AP_CONSUMPTION,
    OSL_CHEAT_INFINITE_PP,
    OSL_CHEAT_INFINITE_POW,
    OSL_CHEAT_INFINITE_HEALTH,
    OSL_CHEAT_INFINITE_ARROWS,
    OSL_CHEAT_COUNT
} OslCheat;

typedef enum {
    OSL_ENGINE_REFINED,
    OSL_ENGINE_CLASSIC,
    OSL_ENGINE_COUNT
} OslEngine;

enum {
    OSL_CHEAT_MAX_SITES = 8,
    OSL_CHEAT_MAX_HITS = 8,
    OSL_CHEAT_REASON_BYTES = 192,
    OSL_CHEAT_STUB_BYTES = 128,
    OSL_CHEAT_STUBS_BYTES = 0x2000,
    OSL_CHEAT_REGION_MAX = 16,
    OSL_CHEAT_CHANGED_MAX = 64
};

typedef enum {
    OSL_CHEAT_SITE_HOOK,            /* its region is hooked */
    OSL_CHEAT_SITE_PLAYER_ACCESSOR, /* only located: a call to the game's player accessor, which the hook reads */
    OSL_CHEAT_SITE_SAME_FUNCTION,   /* only located: code that must follow the hook site closely, in the same function */
    OSL_CHEAT_SITE_GLOBAL           /* only located: a `lea reg,[rip+disp32]` of the root-object pointer close to the hook site,
                                       before or after it, which the hook reads; the disp32 is the site's patch */
} OslCheatSiteKind;

typedef struct {
    const char *id;   /* P1 to P7 for the Issue #20 patches, H for Infinite Health, E for the Experience Multiplier */
    const char *name;
    OslCheatSiteKind kind;
    const unsigned char *prefix; /* code just before the signature that the hooked region needs; may be NULL */
    size_t prefix_size;
    const unsigned char *signature; /* holds the original instruction bytes at patch_at */
    size_t signature_size;
    size_t patch_at;
    const unsigned char *replacement;
    size_t patch_size;
    int region_at;      /* the whole instructions the hook replaces, from the signature's start (may be negative) */
    size_t region_size; /* at least a jmp rel32 long */
    const unsigned char *changed; /* the cheat's code for the region instead of the region with `replacement`; may be NULL */
    size_t changed_size;
    int changed_player_at; /* where `changed` holds the rel32 of the player accessor's global; -1 when it has none */
    int changed_flag_at;   /* where `changed` holds the rel32 of its own switch byte; -1 when it has none */
    const char *mask;      /* 'x' per compared signature byte, '?' per byte that differs between builds; NULL: all compared */
    int call_from;         /* where the signature holds a `call rel32` to the game function the group's hooks call; -1 */
    int changed_call_at;   /* where `changed` holds the rel32 of a call to that function; -1 when it has none */
} OslCheatSite;

typedef struct {
    OslCheat cheat;
    const char *key; /* user\osl.ini, 0 or 1 */
    const char *label;
    const char *description;
    const char *section;
    const OslCheatSite *sites; /* the Refined engine's */
    size_t site_count;
    const OslCheatSite *classic_sites; /* the Classic engine's */
    size_t classic_site_count;
    const char *classic_inherent; /* when there are no Classic sites: why the Classic engine already behaves as the cheat */
} OslCheatInfo;

extern const OslCheatInfo osl_cheats[OSL_CHEAT_COUNT];

const char *osl_engine_name(OslEngine engine);
/* The cheat's sites in `engine`. */
const OslCheatSite *osl_cheat_engine_sites(const OslCheatInfo *info, OslEngine engine, size_t *count);

typedef enum {
    OSL_CHEAT_SITE_UNIQUE,
    OSL_CHEAT_SITE_MISSING,
    OSL_CHEAT_SITE_AMBIGUOUS,
    OSL_CHEAT_SITE_PATCHED, /* the code is there, already holding the replacement */
    OSL_CHEAT_SITE_MODIFIED /* the code is there, with other bytes where the original instruction should be */
} OslCheatSiteState;

typedef struct {
    OslCheatSiteState state;
    size_t hits;                       /* signature matches, the patch bytes not compared */
    uint64_t at[OSL_CHEAT_MAX_HITS];   /* where the signatures start, the first OSL_CHEAT_MAX_HITS */
    uint64_t patch;                    /* image offset of the patch when there is exactly one match */
    uint64_t global;                   /* a player accessor's global, when it was recognised; 0 otherwise */
    uint64_t callee;                   /* the target of the site's call_from call, inside the code; 0 otherwise */
} OslCheatSiteScan;

typedef enum {
    OSL_CHEAT_COMPLETE,
    OSL_CHEAT_PARTIAL,
    OSL_CHEAT_MISSING,
    OSL_CHEAT_AMBIGUOUS,
    OSL_CHEAT_INVALID,
    OSL_CHEAT_INHERENT /* the engine already behaves as the cheat, nothing to change (OslCheatInfo.classic_inherent says why) */
} OslCheatState;

typedef struct {
    OslCheatSiteScan sites[OSL_CHEAT_MAX_SITES];
    OslCheatState state;
    int related; /* every site found once, the sites apart from each other and from every other cheat's */
    char reason[OSL_CHEAT_REASON_BYTES]; /* why it is not COMPLETE; empty when it is */
} OslCheatScan;

typedef struct {
    uint64_t begin, end;
} OslCodeRange;

const char *osl_cheat_state_name(OslCheatState state);

/* Bit `cheat` of a cheat mask. */
static inline unsigned osl_cheat_bit(OslCheat cheat) { return 1u << (unsigned)cheat; }

#define OSL_CHEAT_ALL ((1u << OSL_CHEAT_COUNT) - 1u)

/* Finds every site of every cheat inside `ranges` (the executable's code) and judges each cheat's sites in `engine`
 * against every other group of sites of both engines. Reads only. osl_cheat_scan is the Refined engine's. */
void osl_cheat_scan_engine(const unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                           OslEngine engine, OslCheatScan scans[OSL_CHEAT_COUNT]);
void osl_cheat_scan(const unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                    OslCheatScan scans[OSL_CHEAT_COUNT]);

/* Scans, then hooks every COMPLETE group of sites of both engines, the Refined engine's first: each site's region
 * becomes a jump to a stub in `stubs` (OSL_CHEAT_STUBS_BYTES, executable and within 2 GiB of the image) that runs the
 * original instructions while the cheat's byte in `flags` (OSL_CHEAT_COUNT bytes, writable, within 2 GiB) is 0 and the
 * changed ones otherwise. A group with any doubt, or whose stubs do not fit, is not hooked at all, and the other
 * engine's group is judged on its own. Returns the mask of the cheats hooked in at least one engine; `hooked` (may be
 * NULL) receives each engine's own mask. The code is never written again: turning a cheat on or off writes its flag
 * byte alone. `scans` (may be NULL) is the Refined engine's, `classic_scans` (may be NULL) the Classic engine's. */
unsigned osl_install_cheats_engines(unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                                    unsigned char *stubs, size_t stubs_size, unsigned char *flags,
                                    OslCheatScan scans[OSL_CHEAT_COUNT], OslCheatScan classic_scans[OSL_CHEAT_COUNT],
                                    unsigned hooked[OSL_ENGINE_COUNT]);
unsigned osl_install_cheats(unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                            unsigned char *stubs, size_t stubs_size, unsigned char *flags, OslCheatScan *scans);

/* "Max Gold Gain on, Infinite PP on, 4 off", or "all off". */
void osl_cheat_summary(unsigned enabled, char *text, size_t size);

/* Experience Multiplier: a setting with choices, not an on/off cheat, hooked the same way. Its switch byte holds the
 * multiplier (0 for Off). Saved in user\osl.ini as one of the choices' values; anything else is Off. */
enum {
    OSL_EXPERIENCE_OFF = 1,
    OSL_EXPERIENCE_CHOICE_COUNT = 5,
    OSL_EXPERIENCE_CAP = 2500000, /* the total the game's last level needs: no single award is worth more */
    OSL_EXPERIENCE_STUBS_BYTES = 2 * OSL_CHEAT_STUB_BYTES /* one stub per engine */
};

typedef struct {
    int value;
    const char *saved;
    const char *label;
} OslChoice;

typedef struct {
    const char *key;
    const char *label;
    const char *description;
    const OslCheatSite *sites;
    size_t site_count;
    const OslCheatSite *classic_sites;
    size_t classic_site_count;
} OslSettingInfo;

extern const OslChoice osl_experience_choices[OSL_EXPERIENCE_CHOICE_COUNT];
extern const OslSettingInfo osl_experience_multiplier;

/* The value when it is one of the choices, else Off. */
int osl_experience_listed(int value);
/* The choice a saved text names, else 0. */
int osl_experience_from_saved(const char *saved);
/* "Off", "2x", "5x", "10x" or "100x"; "Off" for anything else. */
const char *osl_experience_label(int value);
/* The next choice, wrapping from 100x to Off. */
int osl_experience_next(int value);

/* Experience Multiplier's sites in `engine`, judged against every cheat's so that none overlap. Reads only. */
void osl_experience_scan_engine(const unsigned char *image, uint64_t image_size, const OslCodeRange *ranges,
                                size_t range_count, OslEngine engine, OslCheatScan *scan);
void osl_experience_scan(const unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                         OslCheatScan *scan);

/* Scans and hooks the Experience Multiplier in both engines like osl_install_cheats_engines: their stubs in `stubs`
 * (OSL_EXPERIENCE_STUBS_BYTES), the one switch byte at `flag`. Returns the mask of the engines hooked (a bit per
 * OslEngine); nothing is written for the others. `scan` is the Refined engine's and `classic_scan` the Classic
 * engine's; either may be NULL. */
unsigned osl_install_experience_engines(unsigned char *image, uint64_t image_size, const OslCodeRange *ranges,
                                        size_t range_count, unsigned char *stubs, size_t stubs_size, unsigned char *flag,
                                        OslCheatScan *scan, OslCheatScan *classic_scan);
/* 1 when the Experience Multiplier is hooked in at least one engine. */
int osl_install_experience(unsigned char *image, uint64_t image_size, const OslCodeRange *ranges, size_t range_count,
                           unsigned char *stubs, size_t stubs_size, unsigned char *flag, OslCheatScan *scan);

#ifdef __cplusplus
}
#endif

#endif
