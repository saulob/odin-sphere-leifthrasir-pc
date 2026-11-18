"""Headless drawing of a layout's settled frame: every key of an animation's layers, atlas sprite warped onto its quad.

Only what the visual checks need: the last segment of each channel, its translation and scale, alpha-over in key order.
"""
import numpy as np
from PIL import Image, ImageDraw

import mbs as M


def _affine(src, dst):
    """Coefficients of the affine map taking three destination points to their source points (PIL's inverse map)."""
    a = np.array([[x, y, 1] for x, y in dst[:3]], np.float64)
    sx = np.linalg.solve(a, np.array([p[0] for p in src[:3]], np.float64))
    sy = np.linalg.solve(a, np.array([p[1] for p in src[:3]], np.float64))
    return tuple(sx) + tuple(sy)


def key_quads(layout, anim=None, layers=None):
    """[(key, quad in layout units after the channel's transform)] of an animation's settled frame (or of given layers,
    untransformed), in draw order."""
    if layers is not None:
        placed = [(layer, (0.0, 0.0, 1.0, 1.0)) for layer in layers]
    else:
        name_to = {n: (first, count) for n, first, count in layout.anims()}
        first, count = name_to[anim]
        placed = [(layer, t) for _, layer, t in layout.anim_layers(first, count)]
    out = []
    for layer, (x, y, sx, sy) in placed:
        for key in layout.layer_keys(layer):
            if key['part'] >= layout.count(M.PARTS) or key['geom'] >= layout.count(M.GEOMS):
                continue
            quad = [(x + px * sx, y + py * sy) for px, py in layout.geom(key['geom'])]
            out.append((key, quad))
    return out


def draw(layout, atlases, anim=None, scale=1.0, size=None, origin=None, layers=None):
    """RGBA uint8 of an animation's settled frame (or of `layers`). `atlases` maps a texture index to its decoded RGBA."""
    items = key_quads(layout, anim, layers)
    if not items:
        return np.zeros((1, 1, 4), np.uint8), (0.0, 0.0)
    xs = [p[0] for _, q in items for p in q]
    ys = [p[1] for _, q in items for p in q]
    ox, oy = origin if origin is not None else (min(xs), min(ys))
    w, h = size if size is not None else (int(np.ceil((max(xs) - ox) * scale)) + 1, int(np.ceil((max(ys) - oy) * scale)) + 1)
    canvas = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    for key, quad in items:
        atlas = atlases.get(key['texture'])
        if atlas is None:
            continue
        uv = layout.part_uv(key['part'])
        dst = [((px - ox) * scale, (py - oy) * scale) for px, py in quad]
        try:
            coeffs = _affine(uv, dst)
        except np.linalg.LinAlgError:
            continue
        sprite = Image.fromarray(atlas).transform((w, h), Image.AFFINE, coeffs, Image.BILINEAR)
        mask = Image.new('L', (w, h), 0)
        ImageDraw.Draw(mask).polygon(dst, fill=255)
        sprite.putalpha(Image.fromarray(np.minimum(np.asarray(sprite)[..., 3], np.asarray(mask))))
        canvas.alpha_composite(sprite)
    return np.asarray(canvas), (ox, oy)


def on_background(rgba, colour=(60, 30, 60)):
    a = rgba[..., 3:4].astype(np.float32) / 255
    return (rgba[..., :3] * a + np.array(colour, np.float32) * (1 - a)).astype(np.uint8)
