/* Unlock Bookshelf: the Bookshelf shows every real entry while the option is on, without touching the save data. */
#ifndef OSL_BOOKSHELF_H
#define OSL_BOOKSHELF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OSL_BOOKSHELF_SLOTS = 163,
    OSL_BOOKSHELF_SPECIAL_SLOT = 162,
    OSL_BOOKSHELF_READS = 14,
    OSL_BOOKSHELF_MARKS = 2,
    OSL_BOOKSHELF_SIGNATURE_BYTES = 9,
    OSL_BOOKSHELF_TOTAL_BYTES = 12,
    OSL_BOOKSHELF_STUB_BYTES = 0x1000
};

/* The code of one game build that the option changes, as image offsets. */
typedef struct {
    uint32_t accessor;                    /* the save data getter */
    uint32_t total;                       /* the entry count's last test: whether Special is unlocked */
    uint32_t reads[OSL_BOOKSHELF_READS];  /* the getter calls of the Bookshelf code that only read the entry arrays */
    uint32_t marks[OSL_BOOKSHELF_MARKS];  /* the getter calls that mark an opened entry as read, in rbx then r14 */
    int in_place_page_read;               /* 1 when reads[7] reads the page count itself, not through a helper */
} OslBookshelfSites;

/* The code the sites hold: the bytes after each getter call, the entry count's last term. */
extern const unsigned char osl_bookshelf_read_signatures[OSL_BOOKSHELF_READS][OSL_BOOKSHELF_SIGNATURE_BYTES];
extern const unsigned char osl_bookshelf_in_place_page_signature[OSL_BOOKSHELF_SIGNATURE_BYTES];
extern const unsigned char osl_bookshelf_mark_signatures[OSL_BOOKSHELF_MARKS][OSL_BOOKSHELF_SIGNATURE_BYTES];
extern const unsigned char osl_bookshelf_total_code[OSL_BOOKSHELF_TOTAL_BYTES];

/* Whether a slot holds content: slots 0-54 and 102-161 the 115 texts, 162 Special, the artwork gallery. 55-101 are
 * placeholders. */
int osl_bookshelf_entry(int slot);

/* The pages the Bookshelf shows for an entry once all are found. */
int osl_bookshelf_pages(int slot);

/* view = the unlock array then the page array (2 * OSL_BOOKSHELF_SLOTS bytes): the save's values, with every entry
 * unlocked and every page open. */
void osl_bookshelf_fill_view(unsigned char *view, const unsigned char *unlocked, const unsigned char *pages);

/* From guest code (System V). The view as a save data pointer for the arrays' offsets; 0 without save data. */
__attribute__((sysv_abi)) void *osl_bookshelf_view(const unsigned char *save);

/* From guest code (System V): the save data when the entry is unlocked in it, else 0, so opening an entry that only
 * the view shows does not mark it as found. */
__attribute__((sysv_abi)) void *osl_bookshelf_mark(unsigned char *save, long slot);

/* Rewrites the sites, with stubs placed in `stubs` (OSL_BOOKSHELF_STUB_BYTES, executable and within 2 GiB of the
 * image). Checks every site first and writes nothing when one differs. Returns 0 and a message in `error` then. */
int osl_apply_bookshelf(unsigned char *image, uint64_t image_size, const OslBookshelfSites *sites,
                        unsigned char *stubs, size_t stubs_size, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
