/* Title menu Exit Game: the Refined and Classic title menus get EXIT GAME as their last item, in the slot of the
 * game's unused INSTALL row. The sites are found by their code, so every game build is covered the same way. */
#ifndef OSL_TITLE_MENU_EXIT_H
#define OSL_TITLE_MENU_EXIT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OSL_TITLE_EXIT_STUB_BYTES = 0x1000,
    OSL_TITLE_EXIT_MAX_PATCHES = 16,
    OSL_TITLE_EXIT_PATCH_BYTES = 24,
    OSL_TITLE_EXIT_PLAN_STUB_BYTES = 256
};

typedef struct {
    uint64_t at;
    uint8_t length;
    uint8_t expected[OSL_TITLE_EXIT_PATCH_BYTES];
    uint8_t bytes[OSL_TITLE_EXIT_PATCH_BYTES];
} OslTitleExitPatch;

/* Every change of one game image, built before anything is written: in-place patches, and the stub bytes that go at
 * the stub offset. */
typedef struct {
    OslTitleExitPatch patches[OSL_TITLE_EXIT_MAX_PATCHES];
    size_t count;
    unsigned char stub[OSL_TITLE_EXIT_PLAN_STUB_BYTES];
    size_t stub_used;
} OslTitleExitPlan;

/* The code signatures the sites are found by; -1 matches any byte. */
typedef struct {
    const char *name;
    const short *bytes;
    size_t count;
} OslTitleExitSignature;

const OslTitleExitSignature *osl_title_exit_signatures(size_t *count);

/* Finds each menu site once in the image (code signatures) and plans the changes; stubs call exit_request. Returns 0
 * and a message in `error` when a site is missing or ambiguous, or its surroundings differ from what was expected. */
int osl_title_exit_plan(const unsigned char *image, uint64_t image_size, uint64_t stub_offset, uint64_t exit_request,
                        OslTitleExitPlan *plan, char *error, size_t error_size);

/* Checks a plan against the image and its stub space: patches inside the image, holding their expected bytes, not
 * overlapping each other or the stub space; the stub space unused and big enough; rel32 jumps in range. */
int osl_title_exit_validate(const OslTitleExitPlan *plan, const unsigned char *image, uint64_t image_size,
                            uint64_t stub_offset, size_t stub_bytes, char *error, size_t error_size);

/* Plans, validates and then writes; writes nothing and returns 0 with a message on any failure. The stub space
 * (stub_bytes after stub_offset) must be inside the image buffer and executable once written. */
int osl_apply_title_menu_exit(unsigned char *image, uint64_t image_size, uint64_t stub_offset, size_t stub_bytes,
                              uint64_t exit_request, char *error, size_t error_size);

/* The host's clean shutdown, called by the request below. */
void osl_title_menu_exit_set_host(void (*host)(void));

/* From guest code (System V): ends the process through the host's clean shutdown, once. */
__attribute__((sysv_abi)) void osl_title_menu_exit_request(void);

/* For tests: allows one more request. */
void osl_title_menu_exit_rearm(void);

#ifdef __cplusplus
}
#endif

#endif
