// Portuguese letters drawn into the player's own font atlases (HD_font_*.ftx) for the port's Portuguese (Brazil) slot:
// a base letter of the atlas with a mark taken from the font's own accented letters or punctuation, written into a
// free cell. scripts/localization/glyphs.py records the cells per executable (localization/pt-BR/font/glyphs.json).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace osl {

struct GlyphRecipe {
    uint32_t code = 0;
    int cell = 0;        // where the letter is drawn
    int base_cell = 0;   // the letter without its mark
    std::string mark;    // acute, grave, circumflex, tilde, diaeresis, cedilla
    int donor_cell = -1, donor_base_cell = -1; // a letter of the font with the same mark, and its base
    int punctuation_cell = -1;                 // otherwise the font's own mark glyph
    uint32_t base = 0; // the base letter's code
    bool capital = false, dotless = false;
};

struct AtlasGeometry {
    int textures = 1, cell = 0, cols = 0, rows = 0;
};

// An RGBA cell, cell x cell pixels.
struct GlyphCell {
    int size = 0;
    std::vector<uint8_t> rgba;
    int Alpha(int x, int y) const { return x < 0 || y < 0 || x >= size || y >= size ? 0 : rgba[(size_t(y) * size + x) * 4 + 3]; }
};

// Draws every recipe into `pack`, an FTEX pack of BC3 Thin_1dThin textures laid out as `geometry`. Fails, leaving the
// pack unchanged, when a target cell holds ink or a block it touches belongs to another glyph, or a source is empty.
bool ComposeGlyphs(std::vector<uint8_t>& pack, const AtlasGeometry& geometry, const std::vector<GlyphRecipe>& recipes,
                   std::string* error);

std::optional<GlyphCell> ReadGlyphCell(const std::vector<uint8_t>& pack, const AtlasGeometry& geometry, int cell);
// Byte offsets in the pack of the 16-byte blocks a cell touches.
std::vector<size_t> GlyphCellBlocks(const std::vector<uint8_t>& pack, const AtlasGeometry& geometry, int cell);

} // namespace osl
