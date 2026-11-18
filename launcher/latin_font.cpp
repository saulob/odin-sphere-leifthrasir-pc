#include "latin_font.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace osl {
namespace {

constexpr uint32_t kDfmtBc3 = 37, kTilingThin1d = 13;
constexpr int kInk = 24;     // alpha above which a pixel is part of a glyph
constexpr int kBlank = 8;    // a free block's highest alpha

uint32_t ReadU32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

struct Texture {
    size_t data = 0; // of the texels in the pack
    int width = 0;
};

std::vector<Texture> Textures(const std::vector<uint8_t>& pack) {
    std::vector<Texture> out;
    if (pack.size() < 0x20 || std::memcmp(pack.data(), "FTEX", 4)) return out;
    const uint32_t count = ReadU32(&pack[12]);
    size_t pos = ReadU32(&pack[8]);
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 12 > pack.size() || std::memcmp(&pack[pos], "FTX0", 4)) return {};
        const uint32_t size = ReadU32(&pack[pos + 4]), header = ReadU32(&pack[pos + 8]);
        const size_t gnf = pos + header;
        if (gnf + 0x100 > pack.size() || std::memcmp(&pack[gnf], "GNF ", 4)) return {};
        const uint8_t* t = &pack[gnf + 16];
        const uint32_t dfmt = ReadU32(t + 4) >> 20 & 0x3f, dims = ReadU32(t + 8), tiling = ReadU32(t + 12) >> 20 & 0x1f;
        const int width = int(dims & 0x3fff) + 1, height = int(dims >> 14 & 0x3fff) + 1;
        const int pitch = int(ReadU32(t + 16) >> 13 & 0x3fff) + 1;
        if (dfmt != kDfmtBc3 || tiling != kTilingThin1d || width != height || pitch != width ||
            gnf + 0x100 + size_t(width) * width > pack.size()) {
            return {};
        }
        out.push_back({gnf + 0x100, width});
        pos += header + size;
    }
    return out;
}

uint8_t* Block(std::vector<uint8_t>& pack, const Texture& t, int bx, int by) {
    const int pitch = t.width / 4;
    const size_t tile = size_t(by / 8) * size_t(pitch / 8) + size_t(bx / 8);
    const int x = bx & 7, y = by & 7;
    const int micro = (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 | (x & 4) << 2 | (y & 4) << 3;
    return &pack[t.data + tile * 64 * 16 + size_t(micro) * 16];
}

using Texels = std::array<std::array<uint8_t, 4>, 16>;

void DecodeBlock(const uint8_t* b, Texels& out) {
    std::array<int, 8> a{};
    a[0] = b[0];
    a[1] = b[1];
    if (a[0] > a[1]) {
        for (int i = 1; i < 7; ++i) a[1 + i] = ((7 - i) * a[0] + i * a[1]) / 7;
    } else {
        for (int i = 1; i < 5; ++i) a[1 + i] = ((5 - i) * a[0] + i * a[1]) / 5;
        a[6] = 0;
        a[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
    const uint16_t c0 = uint16_t(b[8] | b[9] << 8), c1 = uint16_t(b[10] | b[11] << 8);
    const auto rgb = [](uint16_t c) {
        return std::array<int, 3>{(c >> 11 & 31) * 255 / 31, (c >> 5 & 63) * 255 / 63, (c & 31) * 255 / 31};
    };
    std::array<std::array<int, 3>, 4> palette{rgb(c0), rgb(c1), {}, {}};
    for (int k = 0; k < 3; ++k) {
        palette[2][k] = (2 * palette[0][k] + palette[1][k]) / 3;
        palette[3][k] = (palette[0][k] + 2 * palette[1][k]) / 3;
    }
    const uint32_t indices = ReadU32(b + 12);
    for (int i = 0; i < 16; ++i) {
        const auto& c = palette[indices >> (2 * i) & 3];
        out[i] = {uint8_t(c[0]), uint8_t(c[1]), uint8_t(c[2]), uint8_t(a[bits >> (3 * i) & 7])};
    }
}

// Glyphs are grey with alpha: the colour endpoints are the darkest and lightest inked texels.
void EncodeBlock(const Texels& t, uint8_t* b) {
    int lo_a = 255, hi_a = 0;
    for (const auto& p : t) {
        lo_a = std::min<int>(lo_a, p[3]);
        hi_a = std::max<int>(hi_a, p[3]);
    }
    std::memset(b, 0, 16);
    b[0] = uint8_t(hi_a);
    b[1] = uint8_t(lo_a);
    uint64_t abits = 0;
    if (hi_a > lo_a) {
        for (int i = 0; i < 16; ++i) {
            int best = 0, error = 1 << 30;
            for (int k = 0; k < 8; ++k) {
                const int v = k == 0 ? hi_a : k == 1 ? lo_a : ((8 - k) * hi_a + (k - 1) * lo_a) / 7;
                if (std::abs(v - t[i][3]) < error) {
                    error = std::abs(v - t[i][3]);
                    best = k;
                }
            }
            abits |= uint64_t(best) << (3 * i);
        }
    }
    for (int i = 0; i < 6; ++i) b[2 + i] = uint8_t(abits >> (8 * i));
    const auto luma = [](const std::array<uint8_t, 4>& p) { return (p[0] * 3 + p[1] * 6 + p[2]) / 10; };
    int light = -1, dark = -1;
    for (int i = 0; i < 16; ++i) {
        if (t[i][3] <= kBlank) continue;
        if (light < 0 || luma(t[i]) > luma(t[light])) light = i;
        if (dark < 0 || luma(t[i]) < luma(t[dark])) dark = i;
    }
    const std::array<uint8_t, 4> white{255, 255, 255, 255};
    const auto& c0 = light < 0 ? white : t[light];
    const auto& c1 = dark < 0 ? white : t[dark];
    const uint16_t e0 = uint16_t((c0[0] >> 3) << 11 | (c0[1] >> 2) << 5 | (c0[2] >> 3));
    const uint16_t e1 = uint16_t((c1[0] >> 3) << 11 | (c1[1] >> 2) << 5 | (c1[2] >> 3));
    b[8] = uint8_t(e0);
    b[9] = uint8_t(e0 >> 8);
    b[10] = uint8_t(e1);
    b[11] = uint8_t(e1 >> 8);
    const int l0 = luma(c0), l1 = luma(c1);
    uint32_t cbits = 0;
    for (int i = 0; i < 16; ++i) {
        int index = 0;
        if (l0 != l1 && t[i][3] > kBlank) {
            const int f = std::clamp((luma(t[i]) - l1) * 3 / std::max(1, l0 - l1), 0, 3); // 0 dark .. 3 light
            index = f == 3 ? 0 : f == 0 ? 1 : f == 2 ? 2 : 3;
        }
        cbits |= uint32_t(index) << (2 * i);
    }
    for (int i = 0; i < 4; ++i) b[12 + i] = uint8_t(cbits >> (8 * i));
}

struct Place {
    int texture, x, y;
};

std::optional<Place> PlaceOf(const AtlasGeometry& g, const std::vector<Texture>& textures, int cell) {
    const int per = g.cols * g.rows;
    if (cell < 0 || per <= 0 || cell >= per * g.textures || size_t(cell / per) >= textures.size()) return std::nullopt;
    const int local = cell % per;
    const Place p{cell / per, (local % g.cols) * g.cell, (local / g.cols) * g.cell};
    if (p.x + g.cell > textures[size_t(p.texture)].width || p.y + g.cell > textures[size_t(p.texture)].width) return std::nullopt;
    return p;
}

GlyphCell Read(std::vector<uint8_t>& pack, const Texture& t, const Place& p, int size) {
    GlyphCell c{size, std::vector<uint8_t>(size_t(size) * size * 4)};
    Texels texels;
    for (int by = p.y / 4; by * 4 < p.y + size; ++by) {
        for (int bx = p.x / 4; bx * 4 < p.x + size; ++bx) {
            DecodeBlock(Block(pack, t, bx, by), texels);
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + (i & 3) - p.x, y = by * 4 + (i >> 2) - p.y;
                if (x >= 0 && y >= 0 && x < size && y < size) std::memcpy(&c.rgba[(size_t(y) * size + x) * 4], texels[i].data(), 4);
            }
        }
    }
    return c;
}

struct Box {
    int x0, y0, x1, y1; // inclusive; empty when x1 < x0
    bool empty() const { return x1 < x0; }
    int width() const { return x1 - x0 + 1; }
    int height() const { return y1 - y0 + 1; }
};

Box InkOf(const GlyphCell& c, int y_from = 0, int y_to = 1 << 20) {
    Box b{c.size, c.size, -1, -1};
    for (int y = std::max(0, y_from); y < std::min(c.size, y_to); ++y) {
        for (int x = 0; x < c.size; ++x) {
            if (c.Alpha(x, y) > kInk) {
                b.x0 = std::min(b.x0, x);
                b.x1 = std::max(b.x1, x);
                b.y0 = std::min(b.y0, y);
                b.y1 = std::max(b.y1, y);
            }
        }
    }
    return b;
}

// `source`'s pixels inside `box` (all alpha: those over the ink threshold and their soft edges) copied onto `target`
// at (left, top), the more opaque pixel winning.
void Stamp(GlyphCell& target, const GlyphCell& source, const Box& box, int left, int top) {
    for (int y = box.y0; y <= box.y1; ++y) {
        for (int x = box.x0; x <= box.x1; ++x) {
            const int tx = left + x - box.x0, ty = top + y - box.y0;
            if (tx < 0 || ty < 0 || tx >= target.size || ty >= target.size) continue;
            const uint8_t* s = &source.rgba[(size_t(y) * source.size + x) * 4];
            uint8_t* d = &target.rgba[(size_t(ty) * target.size + tx) * 4];
            if (s[3] > d[3]) std::memcpy(d, s, 4);
        }
    }
}

GlyphCell Resampled(const GlyphCell& source, const Box& box, int width, int height) {
    GlyphCell out{std::max(width, height), std::vector<uint8_t>(size_t(std::max(width, height)) * std::max(width, height) * 4)};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::array<int, 4> sum{};
            int weight = 0;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    const int px = box.x0 + (x * 4 + sx) * box.width() / (width * 4);
                    const int py = box.y0 + (y * 4 + sy) * box.height() / (height * 4);
                    const uint8_t* s = &source.rgba[(size_t(py) * source.size + px) * 4];
                    for (int k = 0; k < 3; ++k) sum[k] += s[k] * s[3];
                    sum[3] += s[3];
                    weight += s[3];
                }
            }
            uint8_t* d = &out.rgba[(size_t(y) * out.size + x) * 4];
            for (int k = 0; k < 3; ++k) d[k] = uint8_t(weight ? sum[k] / weight : 255);
            d[3] = uint8_t(sum[3] / 16);
        }
    }
    return out;
}

// The base letter, its dot removed when the mark replaces it: everything above the font's x-height (the dot's outline
// may touch the stem's).
GlyphCell Base(GlyphCell base, bool dotless, int x_height_top) {
    if (!dotless || x_height_top <= 0) return base;
    for (int y = 0; y < x_height_top; ++y) std::memset(&base.rgba[size_t(y) * base.size * 4], 0, size_t(base.size) * 4);
    return base;
}

std::optional<GlyphCell> FromDonor(const GlyphCell& base, const GlyphCell& donor, const GlyphCell& donor_base, bool cedilla) {
    const Box b = InkOf(donor_base), t = InkOf(base);
    if (b.empty() || t.empty()) return std::nullopt;
    const Box mark = cedilla ? InkOf(donor, b.y1 + 1) : InkOf(donor, 0, b.y0);
    if (mark.empty()) return std::nullopt;
    GlyphCell out = base;
    const int dx = (t.x0 + t.x1) / 2 - (b.x0 + b.x1) / 2;
    const int dy = cedilla ? t.y1 - b.y1 : t.y0 - b.y0;
    const Box region{std::max(0, mark.x0 - 1), cedilla ? mark.y0 : std::max(0, mark.y0 - 1), std::min(donor.size - 1, mark.x1 + 1),
                     cedilla ? std::min(donor.size - 1, mark.y1 + 1) : std::min(b.y0 - 1, mark.y1 + 1)};
    if (mark.y0 + dy < 0 || mark.y1 + dy >= base.size) return std::nullopt;
    Stamp(out, donor, region, region.x0 + dx, region.y0 + dy);
    return out;
}

std::optional<GlyphCell> FromPunctuation(GlyphCell base, const GlyphCell& punctuation, const std::string& kind, bool capital) {
    const Box p = InkOf(punctuation);
    Box t = InkOf(base);
    if (p.empty() || t.empty()) return std::nullopt;
    const int size = base.size;
    const int gap = std::max(1, size / 30);
    if (kind == "cedilla") {
        const int needed = std::max(4, size * 12 / 68), room = size - 1 - t.y1;
        if (room < needed) {
            const int bottom = t.y1 - (needed - room);
            if (bottom <= t.y0) return std::nullopt;
            const GlyphCell squeezed = Resampled(base, t, t.width(), bottom - t.y0 + 1);
            GlyphCell fresh{size, std::vector<uint8_t>(base.rgba.size())};
            Stamp(fresh, squeezed, {0, 0, t.width() - 1, bottom - t.y0}, t.x0, t.y0);
            base = fresh;
            t = InkOf(base);
        }
        const int height = std::min(std::max(3, size - 1 - t.y1 + gap), std::max(p.height(), size * 18 / 68));
        const int width = std::max(3, height * p.width() / std::max(1, p.height()));
        const GlyphCell mark = Resampled(punctuation, p, width, height);
        Stamp(base, mark, {0, 0, width - 1, height - 1}, (t.x0 + t.x1 + 1 - width) / 2, t.y1 + 1 - gap);
        return base;
    }
    const int height = capital ? size * 11 / 68 : size * 15 / 68;
    int mw = height * 6 / 5, mh = height;
    if (kind == "tilde") {
        mw = height * 8 / 5;
        mh = std::max(2, height * 3 / 5);
    } else if (kind == "circumflex") {
        mw = height * 8 / 5;
    } else if (kind == "diaeresis") {
        mw = height * 8 / 5;
        mh = std::max(2, height / 2);
    }
    int top = t.y0 - gap - mh;
    if (top < 0) {
        const int squeeze_top = mh + gap + 1;
        if (squeeze_top >= t.y1) return std::nullopt;
        const GlyphCell squeezed = Resampled(base, t, t.width(), t.y1 - squeeze_top + 1);
        GlyphCell fresh{size, std::vector<uint8_t>(base.rgba.size())};
        Stamp(fresh, squeezed, {0, 0, t.width() - 1, t.y1 - squeeze_top}, t.x0, squeeze_top);
        base = fresh;
        t = InkOf(base);
        top = t.y0 - gap - mh;
    }
    const GlyphCell mark = Resampled(punctuation, p, mw, mh);
    Stamp(base, mark, {0, 0, mw - 1, mh - 1}, (t.x0 + t.x1 + 1 - mw) / 2 + (kind == "acute" ? size / 34 : kind == "grave" ? -size / 34 : 0),
          std::max(0, top));
    return base;
}

} // namespace

std::optional<GlyphCell> ReadGlyphCell(const std::vector<uint8_t>& pack, const AtlasGeometry& geometry, int cell) {
    std::vector<uint8_t> copy = pack;
    const std::vector<Texture> textures = Textures(copy);
    const std::optional<Place> p = PlaceOf(geometry, textures, cell);
    if (!p) return std::nullopt;
    return Read(copy, textures[size_t(p->texture)], *p, geometry.cell);
}

std::vector<size_t> GlyphCellBlocks(const std::vector<uint8_t>& pack, const AtlasGeometry& geometry, int cell) {
    std::vector<uint8_t> copy = pack;
    const std::vector<Texture> textures = Textures(copy);
    const std::optional<Place> p = PlaceOf(geometry, textures, cell);
    std::vector<size_t> out;
    if (!p) return out;
    for (int by = p->y / 4; by * 4 < p->y + geometry.cell; ++by) {
        for (int bx = p->x / 4; bx * 4 < p->x + geometry.cell; ++bx) {
            out.push_back(size_t(Block(copy, textures[size_t(p->texture)], bx, by) - copy.data()));
        }
    }
    return out;
}

bool ComposeGlyphs(std::vector<uint8_t>& pack, const AtlasGeometry& g, const std::vector<GlyphRecipe>& recipes,
                   std::string* error) {
    const auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    std::vector<uint8_t> work = pack;
    const std::vector<Texture> textures = Textures(work);
    if (textures.empty() || int(textures.size()) != g.textures) return fail("not a BC3 font pack of the expected layout");
    const auto cell_of = [&](int cell) -> std::optional<GlyphCell> {
        const std::optional<Place> p = PlaceOf(g, textures, cell);
        if (!p) return std::nullopt;
        return Read(work, textures[size_t(p->texture)], *p, g.cell);
    };
    int x_height_top = 0;
    for (const GlyphRecipe& r : recipes) {
        if (r.base == 'o' || r.base == 'a' || r.base == 'e') {
            if (const std::optional<GlyphCell> o = cell_of(r.base_cell)) x_height_top = InkOf(*o).y0;
            if (r.base == 'o') break;
        }
    }
    std::vector<std::pair<Place, GlyphCell>> letters;
    for (const GlyphRecipe& r : recipes) {
        const std::optional<Place> target = PlaceOf(g, textures, r.cell);
        const std::optional<GlyphCell> base = cell_of(r.base_cell);
        if (!target || !base) return fail("cell " + std::to_string(r.cell) + " or its base is outside the atlas");
        if (!InkOf(*cell_of(r.cell)).empty()) return fail("cell " + std::to_string(r.cell) + " is not free");
        std::optional<GlyphCell> letter;
        if (r.donor_cell >= 0) {
            const std::optional<GlyphCell> donor = cell_of(r.donor_cell), donor_base = cell_of(r.donor_base_cell);
            if (donor && donor_base) letter = FromDonor(Base(*base, r.dotless, x_height_top), *donor, *donor_base, r.mark == "cedilla");
        } else if (r.punctuation_cell >= 0) {
            const std::optional<GlyphCell> mark = cell_of(r.punctuation_cell);
            if (mark) letter = FromPunctuation(Base(*base, r.dotless, x_height_top), *mark, r.mark, r.capital);
        }
        if (!letter) return fail("U+" + std::to_string(r.code) + " could not be composed");
        letters.emplace_back(*target, std::move(*letter));
    }
    // Every block a new letter touches must be blank before any is written: neighbouring free cells share blocks.
    for (const auto& [p, letter] : letters) {
        const Texture& t = textures[size_t(p.texture)];
        for (int by = p.y / 4; by * 4 < p.y + g.cell; ++by) {
            for (int bx = p.x / 4; bx * 4 < p.x + g.cell; ++bx) {
                Texels texels;
                DecodeBlock(Block(work, t, bx, by), texels);
                for (const auto& texel : texels) {
                    if (texel[3] > kBlank) return fail("a block next to cell at " + std::to_string(p.x) + "," + std::to_string(p.y) + " holds ink");
                }
            }
        }
    }
    for (const auto& [p, letter] : letters) {
        const Texture& t = textures[size_t(p.texture)];
        for (int by = p.y / 4; by * 4 < p.y + g.cell; ++by) {
            for (int bx = p.x / 4; bx * 4 < p.x + g.cell; ++bx) {
                uint8_t* block = Block(work, t, bx, by);
                Texels texels;
                DecodeBlock(block, texels);
                for (int i = 0; i < 16; ++i) {
                    const int x = bx * 4 + (i & 3) - p.x, y = by * 4 + (i >> 2) - p.y;
                    if (x >= 0 && y >= 0 && x < g.cell && y < g.cell) std::memcpy(texels[i].data(), &letter.rgba[(size_t(y) * g.cell + x) * 4], 4);
                }
                EncodeBlock(texels, block);
            }
        }
    }
    pack = std::move(work);
    return true;
}

} // namespace osl
