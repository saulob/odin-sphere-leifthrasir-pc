/* Skip intro: the startup notice and the title intro, skipped through the game's own transitions. */
#ifndef OSL_SKIP_INTRO_H
#define OSL_SKIP_INTRO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Image offsets in one game build. */
typedef struct {
    uint32_t notice_scene;        /* `mov esi, 2`: the boot notice task moves on to its notice screen */
    uint32_t hd_title_check;      /* `call [rax+0x248]`: the first skip-input check of the Refined title intro */
    uint32_t classic_title_check; /* `call rel32`: the first button check of the Classic title intro */
    uint32_t classic_button;      /* the button read that call reaches */
    uint32_t stubs;               /* free bytes after the game's code, inside the last page of its mapping */
} OslSkipIntroSites;

/* The code around one kind of site, the same in every supported build; -1 matches any byte. */
typedef struct {
    const short *bytes;
    size_t count;
    size_t site; /* index of the site's first byte */
} OslSkipIntroCode;

extern const OslSkipIntroCode osl_skip_intro_notice_code;
extern const OslSkipIntroCode osl_skip_intro_hd_title_code;
extern const OslSkipIntroCode osl_skip_intro_classic_title_code;

#define OSL_SKIP_INTRO_STUB_BYTES 96

/* Sends the notice task from its checks straight to its end, and points the first input check of both title intros
 * at a stub that calls `claim`: while it returns 0 the original check runs, once it returns 1 the check reports a
 * press and the game takes its own skip to PRESS START. code_end is where the executable mapping holding the stubs
 * ends. Checks every site and the stub space first; returns 0 and a message in `error` and writes nothing when one
 * does not match. */
int osl_apply_skip_intro(unsigned char *image, uint64_t code_end, const OslSkipIntroSites *sites, uint64_t claim,
                         char *error, size_t error_size);

/* What the stubs call, from guest code (System V): 1 the first time after osl_skip_intro_arm, then 0. */
void osl_skip_intro_arm(void);
__attribute__((sysv_abi)) int osl_skip_intro_claim(void);

#ifdef __cplusplus
}
#endif

#endif
