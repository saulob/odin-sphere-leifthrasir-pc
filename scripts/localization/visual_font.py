"""Glyph pools for baked calligraphy: letters cut out of the player's own art, kept with their scale and metrics.

A pool belongs to one visual style. Glyphs are only ever reused inside their style; every instance remembers the asset
it was cut from (source fingerprint and rectangle) so a pool can be rebuilt, audited, and refused when the asset is not
the expected one.
"""
from dataclasses import dataclass, field
from statistics import median

import numpy as np
from PIL import Image


@dataclass
class VisualGlyph:
    char: str
    rgba: np.ndarray          # the glyph's pixels, with margin
    ink: tuple                # x0, y0, x1, y1 of the ink inside rgba
    scale: float              # x-height of the art it was cut from
    drop: float               # ink bottom below the word baseline, in x-heights
    source: str               # asset and rectangle it came from

    @property
    def width(self):
        return self.ink[2] - self.ink[0]

    @property
    def height(self):
        return self.ink[3] - self.ink[1]


@dataclass
class VisualMark:
    glyph: VisualGlyph
    dx: float                 # centre of the mark from the centre of its letter, in x-heights
    gap: float                # letter top to mark bottom, in x-heights (negative: overlaps)


@dataclass
class VisualGlyphPool:
    style: str
    glyphs: dict = field(default_factory=dict)       # char -> [VisualGlyph]
    marks: dict = field(default_factory=dict)        # name -> VisualMark
    gaps: dict = field(default_factory=dict)         # (class, class) -> gap in x-heights
    word_gap: float = 0.7
    cap_ratio: float = 2.4

    def add(self, glyph):
        self.glyphs.setdefault(glyph.char, []).append(glyph)

    def has(self, char):
        return char in self.glyphs

    def missing(self, text):
        return sorted({c for c in text if c != ' ' and c not in self.glyphs})

    def pick(self, char, scale):
        """The instance of `char` closest to `scale`, preferring one at least as large (shrinking blurs less)."""
        options = self.glyphs[char]
        larger = [g for g in options if g.scale >= scale * 0.97]
        pool = larger or options
        return min(pool, key=lambda g: abs(np.log(g.scale / scale)))

    def gap(self, left, right):
        return self.gaps.get((glyph_class(left), glyph_class(right)), self.gaps.get(('low', 'low'), 0.12))


def glyph_class(char):
    return 'cap' if char.isupper() else 'low' if char.isalpha() else 'mark'


def resample(glyph, scale):
    """The glyph's pixels resized uniformly to `scale`; returns (rgba, ink box)."""
    factor = scale / glyph.scale
    if abs(factor - 1) < 0.02:
        return glyph.rgba, glyph.ink
    h, w = glyph.rgba.shape[:2]
    size = (max(1, round(w * factor)), max(1, round(h * factor)))
    alpha = glyph.rgba[..., 3:].astype(np.float32) / 255
    planes = np.concatenate([glyph.rgba[..., :3].astype(np.float32) * alpha, alpha * 255], axis=-1)
    out = np.stack([np.asarray(Image.fromarray(planes[..., i], 'F').resize(size, Image.LANCZOS)) for i in range(4)], axis=-1)
    a = np.clip(out[..., 3], 0, 255)
    rgb = np.divide(out[..., :3], np.maximum(a[..., None] / 255, 1e-6))
    rgba = np.zeros((size[1], size[0], 4), np.uint8)
    rgba[..., :3] = np.clip(np.rint(rgb), 0, 255)
    rgba[..., 3] = np.rint(a)
    box = tuple(int(round(v * factor)) for v in glyph.ink)
    return rgba, box
