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

#ifdef __cplusplus
}
#endif

#endif
