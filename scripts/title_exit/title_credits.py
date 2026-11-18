"""Portuguese (Brazil) credit captions of the title atlas: Apresentado por, Produtor, Diretor.

The credit page draws four texture-0 sprites: 1550/1551 hold Presented by and 1560/1561 hold the Producer caption,
a personal name, the Director caption and the other name. Only the caption rows of 1560/1561 and the two Presented
by sprites are written; the name rows keep their pixels.

Apresentado por comes from the Spanish credit line of the same atlas (same face, size and gold): the capital A is
built from cross-sections of the face's strokes and the lowercase p is taken from its own "por". Produtor and
Diretor are the Spanish caption words with the c removed.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'localization'))
sys.path.insert(0, str(HERE.parent / 'prompts'))

import ftex  # noqa: E402
import mbs as M  # noqa: E402
import patch_ftx  # noqa: E402
import visuals  # noqa: E402

GOLD = np.array([134, 114, 79], np.uint8)
QUADS = (
    ((-37.0, -114.0), (-37.0, -98.0), (9.0, -98.0), (9.0, -114.0)),
    ((7.0, -116.0), (7.0, -100.0), (56.0, -100.0), (56.0, -116.0)),
    ((-28.65625, 22.3125), (-28.65625, 92.3125), (15.3125, 92.3125), (15.3125, 22.3125)),
    ((13.3125, 22.3125), (13.3125, 90.3125), (53.3125, 90.3125), (53.3125, 22.3125)),
)
# Columns of the c in the ink-cropped Spanish "Productor" and "Director".
C_COLUMNS = {'Productor': (44, 51), 'Director': (32, 39)}
PRESENTED_CENTER, PRESENTED_TOP, PRESENTED_WIDTH = 504.5, 208.0, 91.0
CAPTION_CENTERS = {'Produtor': 508.0, 'Diretor': 509.0}
CAPTION_BOTTOMS = {'Produtor': 354.0, 'Diretor': 397.0}
SOURCE_SCALE = (0.5113, 0.5073)
PRESENTED_SCALE = (0.5113, 0.5333)
CANVAS_OFFSET = (494.84, 323.0)


class CreditError(Exception):
    pass


def _same_quad(a, b):
    return all(abs(x - y) < 1e-3 for p, q in zip(a, b) for x, y in zip(p, q))


def credit_keys(layout):
    """{quad index: key} of the credit layer, found by the four caption quads."""
    for layer in range(layout.count(M.LAYERS)):
        found = {}
        for key in layout.layer_keys(layer):
            if key['part'] >= layout.count(M.PARTS):
                continue
            quad = layout.geom(key['geom'])
            for i, wanted in enumerate(QUADS):
                if i not in found and _same_quad(quad, wanted):
                    found[i] = key
        if len(found) == len(QUADS):
            return found
    raise CreditError('the credit caption quads are not in this layout')


def _affine(points_from, points_to):
    a = np.array([[p[0], p[1], 1.0] for p in points_from[:3]])
    return (np.linalg.solve(a, [q[0] for q in points_to[:3]]),
            np.linalg.solve(a, [q[1] for q in points_to[:3]]))


def uv_from_dest(layout, key):
    return _affine(layout.geom(key['geom']), layout.part_uv(key['part']))


def dest_from_uv(layout, key):
    return _affine(layout.part_uv(key['part']), layout.geom(key['geom']))


def upright(atlas, layout, key):
    """The sprite as seen on screen, at source resolution and turned upright."""
    uv, quad = layout.part_uv(key['part']), layout.geom(key['geom'])
    ax, ay = uv_from_dest(layout, key)
    bx, _ = dest_from_uv(layout, key)
    du = max(u for u, _ in uv) - min(u for u, _ in uv)
    dv = max(v for _, v in uv) - min(v for _, v in uv)
    xs, ys = [p[0] for p in quad], [p[1] for p in quad]
    qx0, qx1, qy0, qy1 = min(xs), max(xs), min(ys), max(ys)
    w, h = (du, dv) if abs(bx[0]) > abs(bx[1]) else (dv, du)
    w, h = int(round(w)), int(round(h))
    out = np.zeros((h, w, 4), np.uint8)
    for by in range(h):
        dy = qy0 + (by + 0.5) * (qy1 - qy0) / h
        for bx_ in range(w):
            dx = qx0 + (bx_ + 0.5) * (qx1 - qx0) / w
            u = ax[0] * dx + ax[1] * dy + ax[2]
            v = ay[0] * dx + ay[1] * dy + ay[2]
            out[by, bx_] = atlas[key['texture']][int(np.floor(v)), int(np.floor(u))]
    return out


def _over(base, top, x):
    h, w = min(base.shape[0], top.shape[0]), top.shape[1]
    s = top[:h].astype(np.float32) / 255.0
    d = base[:h, x:x + w].astype(np.float32) / 255.0
    a = s[..., 3:4]
    out = np.empty_like(s)
    out[..., :3] = s[..., :3] * a + d[..., :3] * (1 - a)
    out[..., 3] = a[..., 0] + d[..., 3] * (1 - a[..., 0])
    base[:h, x:x + w] = np.clip(np.rint(out * 255.0), 0, 255).astype(np.uint8)


def _join_shift(layout, left_key, right_key, left_width):
    ql, qr = layout.geom(left_key['geom']), layout.geom(right_key['geom'])
    x0, x1 = min(p[0] for p in ql), max(p[0] for p in ql)
    return int(round((min(p[0] for p in qr) - x0) * left_width / (x1 - x0)))


def _strip(atlas, layout, left_key, right_key):
    left = upright(atlas, layout, left_key)
    right = upright(atlas, layout, right_key)
    out = np.zeros((left.shape[0], _join_shift(layout, left_key, right_key, left.shape[1]) + right.shape[1], 4),
                   np.uint8)
    out[:, :left.shape[1]] = left
    _over(out, right, _join_shift(layout, left_key, right_key, left.shape[1]))
    return out


def _bands(image):
    rows = (image[..., 3] > 0).any(axis=1)
    runs, start = [], None
    for i, v in enumerate(rows):
        if v and start is None:
            start = i
        if not v and start is not None:
            runs.append((start, i))
            start = None
    if start is not None:
        runs.append((start, len(rows)))
    return runs


def _crop(image):
    ys, xs = np.nonzero(image[..., 3] > 0)
    return image[ys.min():ys.max() + 1, xs.min():xs.max() + 1]


def _gaps(profile):
    gaps, start = [], None
    for i, v in enumerate(profile):
        if not v and start is None:
            start = i
        if v and start is not None:
            gaps.append((start, i))
            start = None
    if start is not None:
        gaps.append((start, len(profile)))
    return gaps


def spanish_block(atlas, layout):
    """The four Spanish credit lines, upright and joined."""
    keys = credit_keys(layout)
    return _strip(atlas, layout, keys[2], keys[3])


def spanish_word(block, text):
    """A Spanish caption word with its c removed, cropped to its ink."""
    bands = _bands(block)
    if len(bands) != 4:
        raise CreditError(f'{len(bands)} credit lines, expected 4')
    band = bands[0] if text == 'Productor' else bands[2]
    line = block[band[0]:band[1]]
    c0, c1 = C_COLUMNS[text]
    if not 0 < c0 < c1 < line.shape[1]:
        raise CreditError(f'{text}: the c columns are outside the line')
    return _crop(np.concatenate([line[:, :c0], line[:, c1:]], axis=1))


def _stroke(canvas, p0, p1, sigma, peak):
    h, w = canvas.shape
    y0, y1 = max(0, int(min(p0[1], p1[1])) - 4), min(h, int(max(p0[1], p1[1])) + 5)
    x0, x1 = max(0, int(min(p0[0], p1[0])) - 4), min(w, int(max(p0[0], p1[0])) + 5)
    yy, xx = np.mgrid[y0:y1, x0:x1]
    px, py = p1[0] - p0[0], p1[1] - p0[1]
    t = np.clip(((xx - p0[0]) * px + (yy - p0[1]) * py) / (px * px + py * py), 0, 1)
    dx, dy = xx - (p0[0] + t * px), yy - (p0[1] + t * py)
    v = peak * np.exp(-(dx * dx + dy * dy) / (2 * sigma * sigma))
    np.maximum(canvas[y0:y1, x0:x1], v, out=canvas[y0:y1, x0:x1])


def capital_a(rows=30):
    """A capital A on the credit line's baseline, in the face's stroke weight."""
    canvas = np.zeros((rows, 18), np.float32)
    _stroke(canvas, (2.6, 24.0), (8.5, 6.0), 0.98, 250)
    _stroke(canvas, (8.5, 6.0), (14.4, 24.0), 0.98, 250)
    _stroke(canvas, (5.0, 17.0), (12.0, 17.0), 0.92, 250)
    out = np.zeros((rows, 18, 4), np.uint8)
    out[..., :3] = GOLD
    out[..., 3] = np.clip(canvas, 0, 255).astype(np.uint8)
    return out


def presented_line(atlas, layout):
    """Apresentado por in source pixels: A, the face's p, then the Spanish line after its P."""
    keys = credit_keys(layout)
    line = _strip(atlas, layout, keys[0], keys[1])
    profile = (line[..., 3] > 0).any(axis=0)
    ink = np.nonzero(profile)[0]
    gaps = [g for g in _gaps(profile) if ink.min() <= g[0] and g[1] <= ink.max()]
    if len(gaps) < 3:
        raise CreditError(f'{len(gaps)} gaps in the Spanish credit line')
    p_end = gaps[0][0]
    spaces = [g for g in gaps if g[1] - g[0] > 8]
    if len(spaces) != 1:
        raise CreditError(f'{len(spaces)} word spaces in the Spanish credit line')
    por = spaces[0][1]
    p_end2 = next(g[0] for g in gaps if g[0] >= por)
    p = line[:, por:p_end2]
    if not 8 <= p.shape[1] <= 14:
        raise CreditError(f'the p of "por" is {p.shape[1]} wide')
    return np.concatenate([capital_a(line.shape[0]), p, line[:, p_end:]], axis=1)


def _ink_box(image):
    ys, xs = np.nonzero(image[..., 3] > 0)
    return int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1


def _fit(image, width):
    box = _ink_box(image)
    factor = width / ((box[2] - box[0]) * PRESENTED_SCALE[0])
    if factor >= 0.999:
        return image
    w = max(1, int(round(image.shape[1] * factor)))
    h = max(1, int(round(image.shape[0] * factor)))
    return np.asarray(Image.fromarray(image).resize((w, h), Image.BILINEAR))


def _paint(atlas0, layout, key, rows, line, origin, scale, written):
    x0, y0, x1, y1 = layout.part_rect(key['part'])
    ax, ay = dest_from_uv(layout, key)
    lo, hi = rows
    for v in range(lo, hi):
        for u in range(x0, x1):
            atlas0[v, u] = (0, 0, 0, 0)
            written[v, u] = True
            dx = ax[0] * (u + 0.5) + ax[1] * (v + 0.5) + ax[2] + CANVAS_OFFSET[0]
            dy = ay[0] * (u + 0.5) + ay[1] * (v + 0.5) + ay[2] + CANVAS_OFFSET[1]
            bx = (dx - origin[0]) / scale[0]
            by = (dy - origin[1]) / scale[1]
            if 0 <= bx < line.shape[1] and 0 <= by < line.shape[0]:
                pixel = line[int(by), int(bx)]
                if pixel[3] > 0:
                    atlas0[v, u] = pixel


def _blocks(written):
    out = np.zeros_like(written)
    for v, u in zip(*np.nonzero(written)):
        out[v // 4 * 4:v // 4 * 4 + 4, u // 4 * 4:u // 4 * 4 + 4] = True
    return out


def paint(atlas, layouts, donors):
    """(texture 0 with the three credit captions, the written texel mask, statistics)."""
    atlas0 = atlas[0].copy()
    uk, es = M.Mbs(layouts['refined']), M.Mbs(donors['refined']['ES'])
    keys = credit_keys(uk)
    bands = {}
    for i in (2, 3):
        x0, y0, x1, y1 = uk.part_rect(keys[i]['part'])
        spans = _bands(atlas0[y0:y1, x0:x1])
        if len(spans) != 4:
            raise CreditError(f'sprite {keys[i]["sprite"]}: {len(spans)} credit lines, expected 4')
        bands[i] = (y0, y1, [(y0 + a, y0 + b) for a, b in spans])
    name_rows = {i: [bands[i][2][1], bands[i][2][3]] for i in bands}

    lines = spanish_block(atlas, es)
    written = np.zeros(atlas0.shape[:2], bool)

    line = _fit(presented_line(atlas, es), PRESENTED_WIDTH)
    ix0, iy0, ix1, _ = _ink_box(line)
    origin = (PRESENTED_CENTER - (ix0 + ix1) / 2 * PRESENTED_SCALE[0], PRESENTED_TOP - iy0 * PRESENTED_SCALE[1])
    for i in (0, 1):
        x0, y0, x1, y1 = uk.part_rect(keys[i]['part'])
        _paint(atlas0, uk, keys[i], (y0, y1), line, origin, PRESENTED_SCALE, written)

    words = {'Produtor': (spanish_word(lines, 'Productor'), 0), 'Diretor': (spanish_word(lines, 'Director'), 2)}
    for text, (image, index) in words.items():
        ix0, iy0, ix1, iy1 = _ink_box(image)
        origin = (CAPTION_CENTERS[text] - (ix0 + ix1) / 2 * SOURCE_SCALE[0],
                  CAPTION_BOTTOMS[text] - iy1 * SOURCE_SCALE[1])
        for i in (2, 3):
            y0, y1, spans = bands[i]
            lo, hi = spans[index]
            _paint(atlas0, uk, keys[i], (max(y0, lo - 4), min(y1, hi + 4)), image, origin, SOURCE_SCALE, written)

    for i, spans in name_rows.items():
        x0, y0, x1, y1 = uk.part_rect(keys[i]['part'])
        for lo, hi in spans:
            if written[lo:hi, x0:x1].any():
                raise CreditError(f'sprite {keys[i]["sprite"]}: a name row would be written')

    stats = dict(texels_written=int(written.sum()))
    for i, name in ((0, 'presented_left'), (1, 'presented_right'), (2, 'block_left'), (3, 'block_right')):
        x0, y0, x1, y1 = uk.part_rect(keys[i]['part'])
        stats[name] = int(written[y0:y1, x0:x1].sum())
    return atlas0, written, stats


def apply(blob, layouts, donors):
    """The atlas with the three credit captions in Portuguese, and the write statistics."""
    atlas = [visuals.decode(blob, t) for t in ftex.textures(blob)]
    atlas0, written, stats = paint(atlas, layouts, donors)
    out, blocks = patch_ftx.patch(blob, ftex.textures(blob)[0], atlas0, _blocks(written))
    stats['blocks_reencoded'] = int(blocks)
    return out, stats
