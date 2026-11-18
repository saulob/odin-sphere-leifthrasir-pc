"""Portuguese (Brazil) PRESS ANY BUTTON and legal line of the title atlas, on top of the menu rows.

The English layouts draw PRESS ANY BUTTON as two sprites; the Spanish layouts draw one row. The Refined layout draws the
copyright as one sprite and the Classic layout adds a trademark line. The Portuguese is painted into the Spanish slots
(letters of the Spanish, French and English rows of the same face) and the English keys are edited in place to draw
those slots with the Spanish quads, so the layouts keep their size and the result stays a block patch.
"""
import struct
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'localization'))
sys.path.insert(0, str(HERE.parent / 'prompts'))
import ftex  # noqa: E402
import ink_font as K  # noqa: E402
import make_visuals as V  # noqa: E402
import mbs as M  # noqa: E402
import patch_ftx  # noqa: E402
import press_start as P  # noqa: E402
import visuals  # noqa: E402

COPYRIGHT = (0, 0, 1872, 1056, 1908)
TRADEMARK = (5, 408, 762, 834, 800)
TRADEMARK_LEFT = (5, 362, 968, 878, 1010)
PRESS_LEFT = (3, 1244, 464, 1544, 498)
PRESS_RIGHT = (3, 940, 642, 1164, 676)
TARGETS = {'refined': {'copyright': {COPYRIGHT}, 'press': {PRESS_LEFT}, 'hidden': {PRESS_RIGHT}},
           'classic': {'copyright': {TRADEMARK}, 'press': {PRESS_LEFT}, 'hidden': {PRESS_RIGHT}}}
PREFIX_END = 296
BASELINE = 26
BODY_ENGLISH = 'All rights reserved. Published by NIS America, Inc.'
BODY_SPANISH = 'Todos los derechos reservados. Publicado por NIS America, Inc.'
BODY_PORTUGUESE = 'Todos os direitos reservados. Publicado pela NIS America, Inc.'
TRADEMARK_ENGLISH = 'is a trademark of ATLUS USA.'
TRADEMARK_SPANISH = 'es una marca comercial de ATLUS USA.'
TRADEMARK_PORTUGUESE = 'é uma marca comercial da ATLUS USA.'


class SecondaryError(Exception):
    pass


def rect_of(layout, key):
    return (key['texture'],) + layout.part_rect(key['part'])


def span(layout, layer):
    first, count = layout.layer_range(layer)
    return range(first, min(first + count, layout.count(M.KEYS)))


def drawn_rects(layout, layer):
    return {rect_of(layout, layout.key(k)) for k in span(layout, layer) if layout.key(k)['part'] < layout.count(M.PARTS)}


def is_copyright(rect):
    return rect[0] == 5 or rect[3] - rect[1] > 1000


def slots_of(kind, uk, other, language):
    """{'press', 'copyright'}: the rectangles `other` draws where `uk` draws the English ones."""
    peers = V.layer_peers(uk, [other])
    wanted = TARGETS[kind]['copyright'] | TARGETS[kind]['press']
    found = {}
    for layer in range(uk.count(M.LAYERS)):
        mine = drawn_rects(uk, layer)
        if not mine & wanted:
            continue
        for _, other_layer in peers.get(layer, ()):
            for rect in drawn_rects(other, other_layer) - mine:
                name = 'copyright' if is_copyright(rect) else 'press'
                if not mine & TARGETS[kind][name]:
                    continue
                if language == 'ES' and found.setdefault(name, rect) != rect:
                    raise SecondaryError(f'{kind}: the {language} layout draws two {name} slots')
                found.setdefault(name + 's', set()).add(rect)
                found.setdefault(name, rect)
    return found


def refined_copyright(atlas, slot):
    fill = K.bright_fill(170)
    spanish = atlas[slot[2]:slot[4], slot[1]:slot[3]]
    english = atlas[COPYRIGHT[2]:COPYRIGHT[4], COPYRIGHT[1]:COPYRIGHT[3]]
    face = K.Face()
    K.extract(spanish[:, PREFIX_END:], [BODY_SPANISH], fill, face)
    K.extract(english[:, PREFIX_END:], [BODY_ENGLISH], fill, face)
    line, up = K.set_line(face, BODY_PORTUGUESE, gap=face.gap)
    out = np.zeros(spanish.shape, np.uint8)
    out[:, :PREFIX_END] = spanish[:, :PREFIX_END]
    left, top = PREFIX_END + 8, BASELINE - up
    if left + line.shape[1] > out.shape[1] or top < 0 or top + line.shape[0] > out.shape[0]:
        raise SecondaryError('the Portuguese copyright line does not fit the Spanish slot')
    out[top:top + line.shape[0], left:left + line.shape[1]] = line
    return out


def word_spans(profile, gap=6):
    ink = np.nonzero(profile > 0)[0]
    return [(int(g[0]), int(g[-1]) + 1) for g in np.split(ink, np.nonzero(np.diff(ink) > gap)[0] + 1)]


LETTER_WIDTH = {'i': 0.5, 'l': 0.5, 'j': 0.5, 'm': 1.5, 'w': 1.5, 'r': 0.7, 't': 0.7, 'f': 0.7, 'I': 0.6, 'M': 1.5}


def letter_cuts(profile, start, stop, word):
    """The letters of a known word as column spans: each cut falls on a thinly inked column near the place the letters'
    usual widths put it, no letter narrower than 4."""
    weights = [LETTER_WIDTH.get(c, 1.0) for c in word]
    total = sum(weights)
    expected = [start + (stop - start) * sum(weights[:i]) / total for i in range(len(word) + 1)]
    best = {}

    def solve(index, left):
        if index == len(word) - 1:
            return (0.0, ())
        if (index, left) in best:
            return best[index, left]
        out = (float('inf'), ())
        for cut in range(left + 4, stop - 4 * (len(word) - 1 - index) + 1):
            tail = solve(index + 1, cut)
            cost = profile[cut] * 3 + abs(cut - expected[index + 1]) * 1.5 + tail[0]
            if cost < out[0]:
                out = (cost, (cut,) + tail[1])
        best[index, left] = out
        return out

    cuts = (start,) + solve(0, start)[1] + (stop,)
    return list(zip(cuts, cuts[1:]))


def trademark_letters(image, words):
    """{char: columns of the sprite} of a line whose words are known; each word is cut by its column profile."""
    profile = (image[..., 3] > 128).sum(axis=0)
    spans = word_spans(profile)
    if len(spans) != len(words):
        raise SecondaryError(f'{len(spans)} words in the trademark sprite, expected {len(words)}')
    cells = {}
    for (start, stop), word in zip(spans, words):
        for (a, b), char in zip(letter_cuts(profile, start, stop, word), word):
            cells.setdefault(char, image[:, a:b])
    return cells


def acute_over(cell):
    """The letter with an acute above it, drawn with the width of the face's strokes."""
    ink = cell[..., 3] > 128
    rows, columns = np.nonzero(ink)
    top, centre = int(rows.min()), (int(columns.min()) + int(columns.max())) / 2
    out = cell.copy()
    yy, xx = np.mgrid[0:cell.shape[0], 0:cell.shape[1]]
    for t in np.linspace(0, 1, 30):
        x, y = centre - 2 + 5 * t, top - 3 - 7 * t
        out[(xx - x) ** 2 + (yy - y) ** 2 <= 1.6 ** 2] = (255, 255, 255, 255)
    return out


def trademark_image(atlas, slot):
    spanish = atlas[slot[2]:slot[4], slot[1]:slot[3]]
    cells = trademark_letters(spanish, TRADEMARK_SPANISH.split())
    english = atlas[TRADEMARK[2]:TRADEMARK[4], TRADEMARK[1]:TRADEMARK[3]]
    for char, cell in trademark_letters(english, TRADEMARK_ENGLISH.split()).items():
        cells.setdefault(char, cell)
    cells['é'] = acute_over(cells['e'])
    space = np.zeros_like(spanish[:, :8])
    pieces = []
    for char in TRADEMARK_PORTUGUESE:
        pieces.append(space if char == ' ' else cells[char])
    line = np.concatenate(pieces, axis=1)
    out = np.zeros(spanish.shape, np.uint8)
    if line.shape[1] > out.shape[1]:
        raise SecondaryError(f'the Portuguese trademark line is {line.shape[1]} px, the Spanish slot {out.shape[1]}')
    out[:, :line.shape[1]] = line
    return out


def press_image(face, slot):
    return P.fitted(P.row_image(face), (slot[4] - slot[2], slot[3] - slot[1], 4))


def paint(blob, painted):
    textures = ftex.textures(blob)
    out = blob
    for (texture, x0, y0, x1, y1), image in painted:
        tex = textures[texture]
        full = visuals.decode(out, tex).copy()
        changed = np.zeros(full.shape[:2], bool)
        full[y0:y1, x0:x1] = image
        changed[y0:y1, x0:x1] = True
        out = patch_ftx.patch(out, tex, full, changed)[0]
    return out


def uses(layout, k, section, index):
    key = layout.key(k)
    return key['part'] == index if section == M.PARTS else key['geom'] == index


def redraw(layout, donor_layout, donor_key, targets, collapse):
    """Every key of `layout` drawing a rectangle of `targets` draws the donor key's sprite and quad instead (or nothing)."""
    done = set()
    for k in range(layout.count(M.KEYS)):
        key = layout.key(k)
        if key['part'] >= layout.count(M.PARTS) or rect_of(layout, key) not in targets or (key['part'], key['geom']) in done:
            continue
        part_users = [o for o in range(layout.count(M.KEYS)) if uses(layout, o, M.PARTS, key['part'])]
        geom_users = [o for o in range(layout.count(M.KEYS)) if uses(layout, o, M.GEOMS, key['geom'])]
        for o in part_users + geom_users:
            other = layout.key(o)
            if other['part'] >= layout.count(M.PARTS) or rect_of(layout, other) not in targets:
                raise SecondaryError(f'record {key["part"]}/{key["geom"]} is shared with another sprite')
        if collapse:
            quad = layout.geom(key['geom'])
            cx, cy = sum(p[0] for p in quad) / 4, sum(p[1] for p in quad) / 4
            layout.put(M.GEOMS, key['geom'], struct.pack('<12f', cx, cy, *([cx, cy] * 5)))
        else:
            layout.put(M.GEOMS, key['geom'], bytes(donor_layout.record(M.GEOMS, donor_key['geom'])))
        layout.put(M.PARTS, key['part'], bytes(donor_layout.record(M.PARTS, donor_key['part'])))
        for o in part_users:
            raw = bytearray(layout.record(M.KEYS, o))
            raw[7] = donor_key['raw'][7]
            layout.put(M.KEYS, o, bytes(raw))
        done.add((key['part'], key['geom']))


def donor_key(donor, rect):
    return next(donor.key(k) for k in range(donor.count(M.KEYS))
                if donor.key(k)['part'] < donor.count(M.PARTS) and rect_of(donor, donor.key(k)) == rect)


def apply(blob, layouts, donors):
    """(atlas, {kind: layout}) with the press line and legal lines in Portuguese. `layouts` maps 'refined' and 'classic'
    to the English layout bytes, `donors` to {'ES': bytes, 'FR': bytes}."""
    uk = {kind: M.Mbs(data) for kind, data in layouts.items()}
    spanish = {kind: M.Mbs(donors[kind]['ES']) for kind in layouts}
    slots = {kind: slots_of(kind, uk[kind], spanish[kind], 'ES') for kind in layouts}
    for kind, found in slots.items():
        if not {'copyright', 'press'} <= set(found):
            raise SecondaryError(f'{kind}: the Spanish layout lacks a press or copyright slot')
    atlas = [visuals.decode(blob, t) for t in ftex.textures(blob)]
    face = P.face_of(atlas, {'ES': slots['refined']['press'], 'UK': PRESS_LEFT})
    painted = {found['press']: press_image(face, found['press']) for found in slots.values()}
    painted[slots['refined']['copyright']] = refined_copyright(atlas[0], slots['refined']['copyright'])
    classic = slots['classic']['copyright']
    painted[classic] = trademark_image(atlas[classic[0]], classic)
    new_blob = paint(blob, list(painted.items()))
    out = {}
    for kind, layout in uk.items():
        for name in ('copyright', 'press'):
            redraw(layout, spanish[kind], donor_key(spanish[kind], slots[kind][name]), TARGETS[kind][name], False)
        redraw(layout, spanish[kind], donor_key(spanish[kind], slots[kind]['press']), TARGETS[kind]['hidden'], True)
        if kind == 'classic':
            redraw(layout, spanish[kind], donor_key(spanish[kind], TRADEMARK_LEFT), {TRADEMARK_LEFT}, False)
        out[kind] = layout.bytes()
    return new_blob, out
