#include "text_glyphs.h"

#include <stdio.h>
#include <string.h>

#define PAGE_ENTRY 8
#define PAGE_BYTES (256 * PAGE_ENTRY)

static uint64_t read_u64(const uint8_t *p) {
    uint64_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

/* A page pointer is an image offset until the loader relocates it, and an address after. */
static uint8_t *page_at(uint8_t *image, size_t size, uint64_t value, size_t bytes) {
    const uint64_t start = (uint64_t)(uintptr_t)image;
    const uint64_t offset = value >= start ? value - start : value;
    if (!value || size < bytes || offset > size - bytes) return NULL;
    return image + offset;
}

static uint8_t *glyph_record(uint8_t *image, size_t size, uint64_t descriptor, uint32_t code) {
    if (descriptor > size || size - descriptor < 16 + 256 * 8 || code > 0xffff) return NULL;
    uint8_t *page = page_at(image, size, read_u64(image + descriptor + 16 + (size_t)(code >> 8) * 8), PAGE_BYTES);
    return page ? page + (size_t)(code & 0xff) * PAGE_ENTRY : NULL;
}

static uint8_t *sjis_entry(uint8_t *image, size_t size, uint64_t table, uint8_t lead, uint8_t trail) {
    if (table > size || size - table < 256 * 8) return NULL;
    uint8_t *page = page_at(image, size, read_u64(image + table + (size_t)lead * 8), 512);
    return page ? page + (size_t)trail * 2 : NULL;
}

static int is_zero(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (p[i]) return 0;
    return 1;
}

int text_glyphs_install(uint8_t *image, size_t size, const OslGlyphPatch *patches, size_t count, const OslSjisPatch *sjis,
                        size_t sjis_count, char *detail, size_t detail_size) {
    for (size_t i = 0; i < count; ++i) {
        const OslGlyphPatch *p = &patches[i];
        const uint8_t *record = glyph_record(image, size, p->descriptor, p->code);
        const uint8_t *from = glyph_record(image, size, p->descriptor, p->template_code);
        if (p->descriptor > size - 16 || memcmp(image + p->descriptor, p->header, 16)) {
            snprintf(detail, detail_size, "font descriptor at %#llx is not the one expected", (unsigned long long)p->descriptor);
            return 0;
        }
        if (!record || !from || !is_zero(record, PAGE_ENTRY) || is_zero(from, PAGE_ENTRY)) {
            snprintf(detail, detail_size, "glyph record U+%04X of the font at %#llx is taken or its base U+%04X is empty",
                     (unsigned)p->code, (unsigned long long)p->descriptor, (unsigned)p->template_code);
            return 0;
        }
    }
    for (size_t i = 0; i < sjis_count; ++i) {
        const uint8_t *entry = sjis_entry(image, size, sjis[i].table, sjis[i].lead, sjis[i].trail);
        if (!entry || !is_zero(entry, 2)) {
            snprintf(detail, detail_size, "Shift-JIS code %02X%02X is assigned", sjis[i].lead, sjis[i].trail);
            return 0;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const OslGlyphPatch *p = &patches[i];
        uint8_t *record = glyph_record(image, size, p->descriptor, p->code);
        memcpy(record, glyph_record(image, size, p->descriptor, p->template_code), PAGE_ENTRY);
        record[0] = (uint8_t)p->cell;
        record[1] = (uint8_t)(p->cell >> 8);
    }
    for (size_t i = 0; i < sjis_count; ++i) {
        uint8_t *entry = sjis_entry(image, size, sjis[i].table, sjis[i].lead, sjis[i].trail);
        entry[0] = (uint8_t)sjis[i].code;
        entry[1] = (uint8_t)(sjis[i].code >> 8);
    }
    snprintf(detail, detail_size, "%zu glyph records and %zu Shift-JIS codes added", count, sjis_count);
    return 1;
}
