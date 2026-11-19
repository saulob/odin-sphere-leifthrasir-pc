/* The Portuguese (Brazil) letters' glyph records and Shift-JIS codes written into the loaded executable
 * (osl_text_glyph_patches). Every target is checked before anything is written. */
#ifndef OSL_TEXT_GLYPHS_H
#define OSL_TEXT_GLYPHS_H

#include <stddef.h>
#include <stdint.h>

#include "osl_launcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when every patch was applied. 0, with nothing written and `detail` saying why, when a descriptor's header differs, a
 * code page is missing, a record or code is already taken or its template is empty. */
int text_glyphs_install(uint8_t *image, size_t size, const OslGlyphPatch *patches, size_t count, const OslSjisPatch *sjis,
                        size_t sjis_count, char *detail, size_t detail_size);

/* 1 when the Spanish word of the executable's button prompt table (Press, drücken, Appuyez sur, Premi, Pulsa) now reads
 * `word`, written in that word's own slot and padding. 0, with nothing written, when the table is not found exactly once
 * or the word does not fit. */
int text_prompt_install(uint8_t *image, size_t size, const char *word, char *detail, size_t detail_size);

#ifdef __cplusplus
}
#endif

#endif
