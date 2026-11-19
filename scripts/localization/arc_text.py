"""Words a layout spells from glyph sprites set around a circle (the Classic book loading screen's NOW LOADING).

Such a word is a layer of glyph keys, each turned to face out from a common centre, that the line reader cannot group.
A new word is laid on the same circle: each letter centred on the English letters' radius, turned by the same angle
relative to its place on the circle, with the English letter gap measured along the arc, and centred on the arc the
English word covers.
"""
import math
import struct
from statistics import median

import layout_text as T
import mbs as M


class ArcError(Exception):
    pass


def letters(layout, reader, layer):
    """[(key, char)] of the layer's glyph keys in key order, or None when another key is mixed in."""
    first, count = layout.layer_range(layer)
    out = []
    for k in range(first, min(first + count, layout.count(M.KEYS))):
        ch = reader.char(layout, k)
        if ch is None or len(ch) != 1 or T.is_mark(ch):
            return None
        out.append((k, ch))
    return out


def _centre(quad):
    return sum(p[0] for p in quad) / 4, sum(p[1] for p in quad) / 4


def _circle(points):
    """Least-squares circle (cx, cy, r) through the points."""
    n = len(points)
    sx = sum(x for x, _ in points); sy = sum(y for _, y in points)
    sxx = sum(x * x for x, _ in points); syy = sum(y * y for _, y in points); sxy = sum(x * y for x, y in points)
    sz = [x * x + y * y for x, y in points]
    b = (sum(z * x for z, (x, _) in zip(sz, points)), sum(z * y for z, (_, y) in zip(sz, points)), sum(sz))
    a = ((sxx, sxy, sx), (sxy, syy, sy), (sx, sy, n))

    def det(m):
        return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))
    d = det(a)
    if abs(d) < 1e-9:
        raise ArcError('the letters are not on a circle')
    sol = []
    for i in range(3):
        m = [list(row) for row in a]
        for r in range(3):
            m[r][i] = b[r]
        sol.append(det(m) / d)
    cx, cy = sol[0] / 2, sol[1] / 2
    return cx, cy, math.sqrt(sol[2] + cx * cx + cy * cy)


def _wrap(a):
    return (a + math.pi) % (2 * math.pi) - math.pi


def _turn(quad, pattern):
    """The angle of the letter's upright x axis in the layout."""
    a = quad[pattern.index((0, 0))] if (0, 0) in pattern else quad[0]
    b = quad[pattern.index((1, 0))] if (1, 0) in pattern else quad[1]
    return math.atan2(b[1] - a[1], b[0] - a[0])


def geometry(layout, font, keys, english):
    """The circle, spacing and turn of the English word the keys spell (`english`, with its spaces), for `place`."""
    quads = [layout.geom(layout.key(k)['geom']) for k, _ in keys]
    centres = [_centre(q) for q in quads]
    cx, cy, r = _circle(centres)
    radii = [math.hypot(x - cx, y - cy) for x, y in centres]
    if max(abs(d - r) for d in radii) > 0.12 * r:
        raise ArcError('the letters are not on one circle')
    thetas = [math.atan2(y - cy, x - cx) for x, y in centres]
    steps = [_wrap(b - a) for a, b in zip(thetas, thetas[1:])]
    direction = 1 if sum(steps) > 0 else -1
    offsets, widths, heights = [], [], []
    for (k, ch), quad, theta in zip(keys, quads, thetas):
        glyph = font.glyphs[ch]
        offsets.append(_wrap(_turn(quad, glyph.pattern) - theta))
        side = [math.dist(quad[i], quad[(i + 1) % 4]) for i in range(4)]
        a, b = (side[0] + side[2]) / 2, (side[1] + side[3]) / 2
        w, h = (a, b) if abs(a / b - glyph.w / glyph.h) < abs(b / a - glyph.w / glyph.h) else (b, a)
        widths.append(w)
        heights.append(h)
    offset = median(offsets)
    unit = median(h / font.glyphs[ch].h for (_, ch), h in zip(keys, heights))
    words = english.split()
    space_after, n = set(), 0
    for w in words[:-1]:
        n += len(w)
        space_after.add(n - 1)
    gaps = [abs(s) * r - (widths[i] + widths[i + 1]) / 2 for i, s in enumerate(steps)]
    letter_gap = median([g for i, g in enumerate(gaps) if i not in space_after] or [0.0])
    word_gap = median([gaps[i] for i in space_after]) if space_after else letter_gap
    covered = abs(sum(steps)) * r + (widths[0] + widths[-1]) / 2
    mid = thetas[0] + direction * (covered / 2 - widths[0] / 2) / r
    return letter_gap, word_gap, (cx, cy, r), direction, offset, unit, mid


def place(font, text, geometry):
    """[(char, corners)] of `text` set on the English word's circle."""
    letter_gap, word_gap, (cx, cy, r), direction, offset, unit, mid = geometry
    items, along = [], 0.0
    previous = None
    for ch in text:
        if ch == ' ':
            previous = ' '
            continue
        if ch not in font.glyphs:
            raise ArcError(f'no glyph for {ch!r}')
        g = font.glyphs[ch]
        w, h = g.w * unit, g.h * unit
        if previous is not None:
            along += word_gap if previous == ' ' else letter_gap
        items.append((ch, along + w / 2, w, h))
        along += w
        previous = ch
    out = []
    for ch, at, w, h in items:
        theta = mid + direction * (at - along / 2) / r
        x, y = cx + r * math.cos(theta), cy + r * math.sin(theta)
        turn = theta + offset
        c, s = math.cos(turn), math.sin(turn)
        corners = []
        for px, py in font.glyphs[ch].pattern:
            lx, ly = (px - 0.5) * w, (py - 0.5) * h
            corners.append((x + lx * c - ly * s, y + lx * s + ly * c))
        out.append((ch, corners))
    return out


def respell(layout, font, layer, keys, placed):
    """Rewrites the layer's letter keys with the placed letters (template: the first English letter's key)."""
    template = layout.key(keys[0][0])
    part_index = {bytes(layout.record(M.PARTS, i)): i for i in range(layout.count(M.PARTS))}
    records = []
    for ch, corners in placed:
        glyph = font.glyphs[ch]
        box = T.quad_box(corners)
        flat = [v for p in corners for v in p]
        geom = layout.append(M.GEOMS, [struct.pack('<12f', (box[0] + box[2]) / 2, (box[1] + box[3]) / 2, *flat,
                                                   *corners[0])])
        part = part_index.get(glyph.part)
        if part is None:
            part = layout.append(M.PARTS, [glyph.part])
            part_index[glyph.part] = part
        raw = bytearray(template['raw'])
        struct.pack_into('<I', raw, 0, struct.unpack_from('<I', glyph.key, 0)[0])
        struct.pack_into('<H', raw, 6, (glyph.texture << 8) | (struct.unpack_from('<H', raw, 6)[0] & 0xff))
        struct.pack_into('<HHH', raw, 10, part, template['colour'], geom)
        records.append(bytes(raw))
    start = layout.append(M.KEYS, records)
    x0 = min(v for _, c in placed for v, _ in c); x1 = max(v for _, c in placed for v, _ in c)
    y0 = min(v for _, c in placed for _, v in c); y1 = max(v for _, c in placed for _, v in c)
    bx = struct.unpack_from('<4f', layout.record(M.LAYERS, layer))
    layout.set_layer(layer, start, len(records), (min(bx[0], x0), min(bx[1], y0), max(bx[2], x1), max(bx[3], y1)))
    return len(records)
