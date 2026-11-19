#!/usr/bin/env python3
"""Words a layout (.mbs) spells from glyph sprites: read, and respelled in another language.

Some interface layouts write labels letter by letter: each letter is a key drawing one sprite of a language-neutral
glyph font in the atlas, and each language's layout places its own letters. A glyph is told by its atlas rectangle and
the way the key's quad turns it (one stroke is a grave accent, mirrored an acute). Templates, baselines, spacing and
accent placement are learned from the layouts of every European language of the same screen, so a new word is laid out
the way the game lays out its own, from glyphs it already draws.
"""
import struct
import unicodedata
from collections import defaultdict
from dataclasses import dataclass, field
from statistics import median

import mbs as M

MARK_CHARS = {'acute': '́', 'grave': '̀', 'diaeresis': '̈', 'tilde': '̃',
              'circumflex': '̂', 'cedilla': '̧'}
DESCENDERS = set('gjpqy,')


@dataclass
class Glyph:
    char: str
    texture: int
    pattern: tuple          # per quad corner (0|1, 0|1): its place in the quad's box
    part: bytes             # part record
    key: bytes              # a key drawing it, for the sprite id and flags
    w: float                # drawn size, atlas pixels
    h: float
    bottom: float = 0.0     # drawn bottom below the baseline, atlas pixels


@dataclass
class Line:
    layer: int
    keys: list              # key indexes in draw order
    text: str
    box: tuple              # x0, y0, x1, y1, layout units
    scale: float            # layout units per atlas pixel, vertically
    squeeze: float          # horizontal scale relative to `scale`
    baseline: float
    colour: int


@dataclass
class Font:
    glyphs: dict = field(default_factory=dict)       # char -> Glyph
    gap: float = -4.0                                # between letters, atlas pixels
    space: float = 14.0
    marks: dict = field(default_factory=dict)        # (mark, capital base) -> (dx, lift, size)


def corner_pattern(quad):
    xs, ys = [p[0] for p in quad], [p[1] for p in quad]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    if x1 - x0 < 1e-3 or y1 - y0 < 1e-3:
        return None
    return tuple((int(round((p[0] - x0) / (x1 - x0))), int(round((p[1] - y0) / (y1 - y0)))) for p in quad)


def drawn_size(uv, pattern):
    """(w, h) of a sprite as drawn: its atlas extent along the quad's x and y."""
    a, b, d = uv[0], uv[1], uv[3]
    e01 = ((b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2) ** 0.5
    e03 = ((d[0] - a[0]) ** 2 + (d[1] - a[1]) ** 2) ** 0.5
    return (e03, e01) if pattern[0][0] == pattern[1][0] else (e01, e03)


def turn(uv, quad):
    """The sign matrix taking atlas directions to layout directions: identity for an upright sprite."""
    u = ((uv[1][0] - uv[0][0], uv[3][0] - uv[0][0]), (uv[1][1] - uv[0][1], uv[3][1] - uv[0][1]))
    g = ((quad[1][0] - quad[0][0], quad[3][0] - quad[0][0]), (quad[1][1] - quad[0][1], quad[3][1] - quad[0][1]))
    det = u[0][0] * u[1][1] - u[0][1] * u[1][0]
    if abs(det) < 1e-9:
        return None
    inv = ((u[1][1] / det, -u[0][1] / det), (-u[1][0] / det, u[0][0] / det))
    m = [[g[r][0] * inv[0][c] + g[r][1] * inv[1][c] for c in range(2)] for r in range(2)]
    top = max(abs(v) for row in m for v in row)
    return tuple(tuple(0 if abs(v) < 0.2 * top else (1 if v > 0 else -1) for v in row) for row in m)


UPRIGHT = ((1, 0), (0, 1))


def quad_box(quad):
    xs, ys = [p[0] for p in quad], [p[1] for p in quad]
    return min(xs), min(ys), max(xs), max(ys)


class Reader:
    """Glyph keys of a layout, from a table {(texture, x0, y0, x1, y1): char} and the turned variants
    {(char, pattern): char}."""

    def __init__(self, table, variants=None):
        self.table = table
        self.variants = variants or {}

    def char(self, layout, k):
        key = layout.key(k)
        if key['part'] >= layout.count(M.PARTS) or key['geom'] >= layout.count(M.GEOMS):
            return None
        base = self.table.get((key['texture'],) + layout.part_rect(key['part']))
        if base is None:
            return None
        t = turn(layout.part_uv(key['part']), layout.geom(key['geom']))
        return None if t is None else self.variants.get((base, t), base)


def _scale(layout, k):
    key = layout.key(k)
    quad = layout.geom(key['geom'])
    w, h = drawn_size(layout.part_uv(key['part']), corner_pattern(quad))
    box = quad_box(quad)
    return (box[3] - box[1]) / h


def shear(layout, keys):
    """Horizontal shift per layout unit of height of the line's letters (italic lines), 0 when upright."""
    values = []
    for k in keys:
        quad = layout.geom(layout.key(k)['geom'])
        top = min(quad, key=lambda p: (p[1], p[0]))
        bottom = min(quad, key=lambda p: (-p[1], p[0]))
        if bottom[1] - top[1] > 1e-3:
            values.append((top[0] - bottom[0]) / (bottom[1] - top[1]))
    return median(values) if values else 0.0


def is_mark(ch):
    return ch in MARK_CHARS


def lines(layout, reader, layers=None):
    """Every line of glyph keys in the layout's layers: letters on one baseline, close together, with their marks."""
    out = []
    for layer in (range(layout.count(M.LAYERS)) if layers is None else layers):
        first, count = layout.layer_range(layer)
        found = []
        for k in range(first, min(first + count, layout.count(M.KEYS))):
            ch = reader.char(layout, k)
            if ch is not None:
                found.append((k, ch, quad_box(layout.geom(layout.key(k)['geom']))))
        groups = []
        for f in sorted((f for f in found if not is_mark(f[1])), key=lambda f: f[2][0]):
            cy, h, sf = (f[2][1] + f[2][3]) / 2, f[2][3] - f[2][1], _scale(layout, f[0])
            for g in groups:
                gh = median(x[2][3] - x[2][1] for x in g)
                gcy = median((x[2][1] + x[2][3]) / 2 for x in g)
                gs = median(_scale(layout, x[0]) for x in g)
                near = f[2][0] - max(x[2][2] for x in g) < 1.3 * gh
                if near and abs(cy - gcy) < 0.45 * max(h, gh) and abs(sf - gs) < 0.12 * gs:
                    g.append(f)
                    break
            else:
                groups.append([f])
        for g in groups:
            if len(g) < 2:
                continue
            gx0, gx1 = min(x[2][0] for x in g), max(x[2][2] for x in g)
            gy0, gy1 = min(x[2][1] for x in g), max(x[2][3] for x in g)
            for f in found:
                if is_mark(f[1]) and gx0 - 4 <= (f[2][0] + f[2][2]) / 2 <= gx1 + 4 and gy0 - (gy1 - gy0) <= f[2][3] <= gy1:
                    g.append(f)
            out.append(_line(layout, layer, g))
    return out


def _line(layout, layer, group):
    scales, squeezes, bottoms = [], [], []
    for k, ch, box in group:
        if is_mark(ch):
            continue
        key = layout.key(k)
        w, h = drawn_size(layout.part_uv(key['part']), corner_pattern(layout.geom(key['geom'])))
        scales.append((box[3] - box[1]) / h)
        squeezes.append((box[2] - box[0]) / w / scales[-1])
        if ch not in DESCENDERS:
            bottoms.append(box[3])
    letters = sorted((f for f in group if not is_mark(f[1])), key=lambda f: f[2][0])
    marks = [f for f in group if is_mark(f[1])]
    text, prev, scale = '', None, median(scales)
    for k, ch, box in letters:
        if prev is not None and box[0] - prev > 0.3 * (box[3] - box[1]):
            text += ' '
        text += ch
        for _, mark, mbox in marks:
            if box[0] - 2 <= (mbox[0] + mbox[2]) / 2 <= box[2] + 2 and mbox[1] < box[1] + 0.2 * (box[3] - box[1]):
                text += MARK_CHARS[mark]
        prev = box[2]
    text = unicodedata.normalize('NFC', text.replace('ı́', 'í'))
    box = (min(b[0] for _, _, b in group), min(b[1] for _, _, b in group),
           max(b[2] for _, _, b in group), max(b[3] for _, _, b in group))
    return Line(layer, [k for k, _, _ in sorted(group, key=lambda f: f[0])], text, box, scale, median(squeezes),
                median(bottoms) if bottoms else box[3], layout.key(letters[0][0])['colour'])


def templates(layouts, reader):
    """{char: Glyph} as the layouts draw each glyph most often; the turn each is drawn with."""
    turns = defaultdict(lambda: defaultdict(list))
    for layout in layouts:
        for line in lines(layout, reader):
            for k in line.keys:
                key = layout.key(k)
                t = turn(layout.part_uv(key['part']), layout.geom(key['geom']))
                turns[reader.char(layout, k)][t].append((layout, k))
    glyphs, canonical = {}, {}
    for ch, by_turn in turns.items():
        canonical[ch] = max(by_turn, key=lambda t: len(by_turn[t]))
        layout, k = by_turn[canonical[ch]][0]
        key = layout.key(k)
        pattern = corner_pattern(layout.geom(key['geom']))
        w, h = drawn_size(layout.part_uv(key['part']), pattern)
        glyphs[ch] = Glyph(ch, key['texture'], pattern, bytes(layout.record(M.PARTS, key['part'])), key['raw'], w, h)
    return glyphs, canonical


def learn(layouts, reader):
    """A Font from the glyph keys of several layouts of the same screen. A glyph's template is taken from the way it is
    usually turned; accents keep the exact quad the game gives them over a letter."""
    font = Font()
    bottoms, gaps, spaces, marks = defaultdict(list), [], [], defaultdict(list)
    font.glyphs, canonical = templates(layouts, reader)
    for layout in layouts:
        for line in lines(layout, reader):
            natural = abs(line.squeeze - 1) < 0.04
            first_pass = set(passes(layout, line, reader)[0])
            letters, prev = [], None
            for k in sorted(first_pass, key=lambda k: quad_box(layout.geom(layout.key(k)['geom']))[0]):
                ch = reader.char(layout, k)
                key = layout.key(k)
                if is_mark(ch) or turn(layout.part_uv(key['part']), layout.geom(key['geom'])) != canonical[ch]:
                    continue
                box = quad_box(layout.geom(key['geom']))
                letters.append((ch, box))
                bottoms[ch].append((box[3] - line.baseline) / line.scale)
                if prev is not None and natural:
                    g = (box[0] - prev) / line.scale
                    (spaces if g > 0.3 * font.glyphs[ch].h else gaps).append(g)
                prev = box[2]
            for k in first_pass:
                ch = reader.char(layout, k)
                if not is_mark(ch):
                    continue
                quad = layout.geom(layout.key(k)['geom'])
                box = quad_box(quad)
                cx = (box[0] + box[2]) / 2
                under = [l for l in letters if l[1][0] - 2 <= cx <= l[1][2] + 2 and l[1][1] > box[1]]
                if not under:
                    continue
                base, bbox = min(under, key=lambda l: abs((l[1][0] + l[1][2]) / 2 - cx))
                bcx = (bbox[0] + bbox[2]) / 2
                marks[(ch, base.isupper() or base == 'l')].append(
                    [((x - bcx) / line.scale, (y - bbox[1]) / line.scale) for x, y in quad])
    for ch, values in bottoms.items():
        font.glyphs[ch].bottom = median(values)
    font.gap = median(gaps) if gaps else font.gap
    font.space = median(spaces) if spaces else font.space
    for key, values in marks.items():
        font.marks[key] = [(median(v[i][0] for v in values), median(v[i][1] for v in values)) for i in range(4)]
    return font


def passes(layout, line, reader):
    """The line's keys split into passes, in draw order: a layer may draw a word twice or more (a shadow, a glow, an
    animated copy), each pass a run of keys spelling the same letters. Returns [[key, ...] per pass]."""
    keys = sorted(line.keys)
    chars = [reader.char(layout, k) for k in keys]
    for depth in range(len(keys) // 2, 1, -1):
        if len(keys) % depth:
            continue
        period = len(keys) // depth
        if all(chars[i] == chars[i % period] for i in range(len(keys))):
            return [keys[i * period:(i + 1) * period] for i in range(depth)]
        if all(chars[i] == chars[i - i % depth] for i in range(len(keys))):
            return [keys[j::depth] for j in range(depth)]
    by_x = sorted(keys, key=lambda k: quad_box(layout.geom(layout.key(k)['geom']))[0])
    letters = [k for k in by_x if not is_mark(reader.char(layout, k))]
    for depth in (2, 3, 4):
        runs = [sorted(letters[i:i + depth]) for i in range(0, len(letters), depth)]
        if len(letters) % depth == 0 and all(len({reader.char(layout, k) for k in run}) == 1 for run in runs):
            out = [[run[j] for run in runs] for j in range(depth)]
            for k in keys:
                if is_mark(reader.char(layout, k)):
                    out[0].append(k)
            return out
    return [keys]


def line_text(layout, line, reader):
    """The line's text as one pass spells it."""
    first = passes(layout, line, reader)[0]
    if len(first) == len(line.keys):
        return line.text
    group = [(k, reader.char(layout, k), quad_box(layout.geom(layout.key(k)['geom']))) for k in first]
    return _line(layout, line.layer, group).text


@dataclass
class Plan:
    """A line's new text: the box it may fill and how it is aligned to the English one."""
    line: Line
    text: str
    width: float            # layout units available
    align: str = 'center'   # left, center or right
    left: float = None      # explicit left edge, overriding the alignment
    drop: tuple = ()        # other keys of the layer to remove
    move: dict = None       # other keys of the layer to shift: key -> dx
    channels: dict = None   # animation channels to shift: channel -> dx
    gap: float = None       # letter gap of this line, atlas pixels, instead of the font's
    lean: float = None      # slant of this line instead of the replaced keys'


TILDE, CIRCUMFLEX, CEDILLA = 'tilde', 'circumflex', 'cedilla'
TILDE_SIZE = (0.34, 0.58)
CIRCUMFLEX_SIZE = (1.3, 0.6)
CIRCUMFLEX_LIFT = 0.3


def compose(font, text):
    """[(char, [mark, ...])] of a text in the font's letters; raises KeyError naming what the font cannot draw."""
    out = []
    for ch in unicodedata.normalize('NFD', text):
        mark = next((m for m, c in MARK_CHARS.items() if c == ch), None)
        if mark:
            if not out:
                raise KeyError(f'{text!r}: a mark with no letter')
            out[-1][1].append(mark)
            continue
        out.append([ch, []])
    for item in out:
        if item[0] == 'i' and item[1]:
            item[0] = 'ı'
    for ch, marks in out:
        glyph = 'l' if ch == 'ı' else ch
        if ch != ' ' and glyph not in font.glyphs:
            raise KeyError(f'{text!r}: no glyph for {ch!r}')
        for m in marks:
            if m not in font.glyphs and m not in (TILDE, CIRCUMFLEX, CEDILLA):
                raise KeyError(f'{text!r}: no mark {m}')
    return out


def layout_word(font, line, text, width, gap=None):
    """[(glyph char, (x0, y0, x1, y1), turn override or None)] placing `text` on the line's baseline at the line's
    scale, squeezed horizontally to `width` when wider; x from 0."""
    s = line.scale
    items = compose(font, text)
    pieces, x = [], 0.0
    stem = font.glyphs.get('l')
    for i, (ch, marks) in enumerate(items):
        if ch == ' ':
            x += font.space * s
            continue
        if pieces and items[i - 1][0] != ' ':
            x += (font.gap if gap is None else gap) * s
        if ch == 'ı':
            short = font.glyphs['n']
            bottom = line.baseline + short.bottom * s
            box = (x, bottom - short.h * s, x + stem.w * s, bottom)
            pieces.append(('l', _box_corners(stem.pattern, box), None))
        else:
            g = font.glyphs[ch]
            bottom = line.baseline + g.bottom * s
            box = (x, bottom - g.h * s, x + g.w * s, bottom)
            pieces.append((ch, _box_corners(g.pattern, box), None))
        x = box[2]
        capital = ch.isupper() or ch in 'ldbfhkt'
        for m in marks:
            pieces.extend(mark_pieces(font, m, box, capital, s))
    natural = x
    q = min(1.0, width / natural) if natural > 0 and width > 0 else 1.0
    return [(ch, [(px * q, py) for px, py in c], t) for ch, c, t in pieces], natural * q, q


def _box_corners(pattern, box):
    return [(box[0] + px * (box[2] - box[0]), box[1] + py * (box[3] - box[1])) for px, py in pattern]


def mark_pieces(font, mark, box, capital, s):
    """[(glyph char, corners, None)] of an accent over (or a cedilla under) a letter placed in `box`."""
    cx, top = (box[0] + box[2]) / 2, box[1]

    def placed(corners, dx=0.0, k=1.0):
        mx = sum(c[0] for c in corners) / 4
        my = sum(c[1] for c in corners) / 4
        return [(cx + (mx + (x - mx) * k + dx) * s, top + (my + (y - my) * k) * s) for x, y in corners]

    if mark == CEDILLA:
        g = font.glyphs[',']
        w, h = g.w * s * 0.9, g.h * s * 0.9
        x0, y0 = cx - w / 2, box[3] - h * 0.3
        return [(',', [(x0 + px * w, y0 + py * h) for px, py in g.pattern], None)]
    acute = font.marks.get(('acute', capital)) or font.marks.get(('acute', not capital))
    if mark == TILDE:
        g = font.glyphs['s']
        my = sum(c[1] for c in acute) / 4
        h, w = g.w * s * TILDE_SIZE[0], g.h * s * TILDE_SIZE[1]
        y1 = top + (my + 2.5) * s
        return [('s', [(cx - w / 2 + px * w, y1 - h + py * h) for px, py in rotated(g.pattern)], True)]
    if mark == CIRCUMFLEX:
        # The face's v turned upside down, one outlined piece where the acute would stand: two outlined strokes would
        # cut into each other at the apex.
        x0, y0, x1, y1 = quad_box(placed(_centred(acute)))
        w, h = (x1 - x0) * CIRCUMFLEX_SIZE[0], (y1 - y0) * CIRCUMFLEX_SIZE[1]
        mx, bottom = (x0 + x1) / 2, y1 - (y1 - y0) * CIRCUMFLEX_LIFT
        corners = [(mx - w / 2 + (1 - px) * w, bottom - h + (1 - py) * h) for px, py in font.glyphs['v'].pattern]
        return [('v', corners, None)]
    corners = font.marks.get((mark, capital)) or font.marks.get((mark, not capital))
    return [(mark, placed(corners), None)]


def _centred(corners):
    mx = sum(c[0] for c in corners) / 4
    return [(x - mx, y) for x, y in corners]


def shift_channel(layout, channel, dx):
    """Moves every segment of an animation channel by dx, in place; its transforms must be its own."""
    mine = set(layout.channel_segments(channel))
    ids = {struct.unpack_from('<H', layout.record(M.SEGMENTS, s), 4)[0] for s in mine}
    for s in range(layout.count(M.SEGMENTS)):
        if s not in mine and struct.unpack_from('<H', layout.record(M.SEGMENTS, s), 4)[0] in ids:
            raise ValueError(f'channel {channel} shares a transform with segment {s}')
    for t in ids:
        raw = bytearray(layout.record(M.TRANSFORMS, t))
        struct.pack_into('<f', raw, 0, struct.unpack_from('<f', raw, 0)[0] + dx)
        layout.put(M.TRANSFORMS, t, bytes(raw))


def rotated(pattern):
    """The corner pattern of a quad turned a quarter clockwise in its box."""
    return tuple((y, 1 - x) for x, y in pattern)


def respell(layout, font, reader, plans):
    """Rewrites every planned line of `layout` in place: the line's keys are replaced by keys for the new text, the
    layer's other keys are kept in their order, and new keys, quads and parts are appended. Returns the number of keys
    written."""
    by_layer = defaultdict(list)
    for plan in plans:
        by_layer[plan.line.layer].append(plan)
    part_index = {bytes(layout.record(M.PARTS, i)): i for i in range(layout.count(M.PARTS))}
    written = 0
    for layer, mine in sorted(by_layer.items()):
        first, count = layout.layer_range(layer)
        replaced = {}
        for plan in mine:
            records = _line_keys(layout, font, reader, plan, part_index)
            keys = sorted(plan.line.keys)
            replaced[keys[0]] = records
            for k in keys[1:]:
                replaced[k] = []
            for k in plan.drop:
                replaced[k] = []
            for channel, dx in (plan.channels or {}).items():
                shift_channel(layout, channel, dx)
            for k, dx in (plan.move or {}).items():
                raw = bytearray(layout.record(M.KEYS, k))
                corners = [(x + dx, y) for x, y in layout.geom(struct.unpack_from('<H', raw, 14)[0])]
                box = quad_box(corners)
                flat = [v for c in corners for v in c]
                geom = layout.append(M.GEOMS, [struct.pack('<12f', (box[0] + box[2]) / 2, (box[1] + box[3]) / 2,
                                                           *flat, *corners[0])])
                struct.pack_into('<H', raw, 14, geom)
                replaced[k] = [bytes(raw)]
        out = []
        for k in range(first, first + count):
            out.extend(replaced[k] if k in replaced else [bytes(layout.record(M.KEYS, k))])
        start = layout.append(M.KEYS, out)
        x0, y0, x1, y1 = struct.unpack_from('<4f', layout.record(M.LAYERS, layer))
        for raw in out:
            g = struct.unpack_from('<H', raw, 14)[0]
            bx = quad_box(layout.geom(g))
            x0, y0, x1, y1 = min(x0, bx[0]), min(y0, bx[1]), max(x1, bx[2]), max(y1, bx[3])
        layout.set_layer(layer, start, len(out), (x0, y0, x1, y1))
        written += len(out)
    return written


def _line_keys(layout, font, reader, plan, part_index):
    line = plan.line
    groups = passes(layout, line, reader)
    boxes = {k: quad_box(layout.geom(layout.key(k)['geom'])) for k in line.keys}
    lead = groups[0][0]
    if not plan.text:
        return []
    pieces, width, _ = layout_word(font, line, plan.text, plan.width, plan.gap)
    if plan.left is not None:
        x0 = plan.left
    elif plan.align == 'left':
        x0 = line.box[0]
    elif plan.align == 'right':
        x0 = line.box[2] - width
    else:
        x0 = (line.box[0] + line.box[2]) / 2 - width / 2
    records = []
    for group in groups:
        first = group[0]
        dx = (boxes[first][0] + boxes[first][2] - boxes[lead][0] - boxes[lead][2]) / 2
        dy = (boxes[first][1] + boxes[first][3] - boxes[lead][1] - boxes[lead][3]) / 2
        template = layout.key(first)
        lean = shear(layout, group) if plan.lean is None else plan.lean
        for ch, placed, _ in pieces:
            glyph = font.glyphs[ch]
            corners = [(x0 + px + dx + lean * (line.baseline - py), py + dy) for px, py in placed]
            box = quad_box(corners)
            cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
            flat = [v for p in corners for v in p]
            geom = layout.append(M.GEOMS, [struct.pack('<12f', cx, cy, *flat, *corners[0])])
            part = part_index.get(glyph.part)
            if part is None:
                part = layout.append(M.PARTS, [glyph.part])
                part_index[glyph.part] = part
            raw = bytearray(template['raw'])
            struct.pack_into('<I', raw, 0, struct.unpack_from('<I', glyph.key, 0)[0])
            struct.pack_into('<H', raw, 6, (glyph.texture << 8) | (struct.unpack_from('<H', raw, 6)[0] & 0xff))
            struct.pack_into('<HHH', raw, 10, part, template['colour'], geom)
            records.append(bytes(raw))
    return records
