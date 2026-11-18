/* The six chapter prologue cards as a translation map: the Spanish record of a story container and its Portuguese
 * record, keyed by resource and record index. */
#ifndef OSL_PROLOGUE_TRANSLATION_H
#define OSL_PROLOGUE_TRANSLATION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 only for Portuguese (Brazil) on the European build (the Spanish slot). */
void osl_prologue_set_active(int active);
int osl_prologue_active(void);

/* 1 when `resource` is one of the six chapter prologue story containers. */
int osl_prologue_is_resource(const char *resource);

size_t osl_prologue_record_count(void);
size_t osl_prologue_book_count(void);

/* The Spanish record `text` (NUL-terminated when length is (size_t)-1) of `resource` (a story key or a .txt name)
 * and record `index`, replaced by its Portuguese record. The source must match exactly, or the text is returned
 * unchanged. `resource` may be "a0_2_1", "A0_2_1.txt" or "story/a0_2_1". */
const char *osl_prologue_translate(const char *resource, size_t index, const char *text, size_t length);

#ifdef __cplusplus
}
#endif

#endif
