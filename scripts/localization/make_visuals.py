#!/usr/bin/env python3
"""Portuguese (Brazil) interface labels: localization/pt-BR/visual/interface-ptbr.oslv.

The interface layouts (GUI/REHD_Interface.mbs, Classic Interface.mbs) spell most labels letter by letter from a glyph
font every language shares, and the rest are word sprites of the language. Each English line or word sprite listed in
labels.tsv is rewritten in Portuguese from the shared glyphs (layout_text.py): the layout grows by the new keys and
quads, the atlas is not touched. The width a label may take is the widest of the official European languages for the
same slot; the alignment is the one those languages share. Every target is an English layout of a local build, bound
to its size and CRC32.

    make_visuals.py [--prepared game/prepared] [--out localization/pt-BR/visual/interface-ptbr.oslv] [--report out.tsv]
Needs numpy and Pillow (the Dragon's Crown .venv, or tools\\setup.ps1 -InstallPython).
"""
import argparse
import csv
import struct
import sys
import zlib
from collections import defaultdict
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'prompts'))
import cpk  # noqa: E402
import ftex  # noqa: E402
import arc_text as A  # noqa: E402
import layout_text as T  # noqa: E402
import mbs as M  # noqa: E402
import oslv  # noqa: E402
import visuals  # noqa: E402

ROOT = HERE.parent.parent
VISUAL = ROOT / 'localization' / 'pt-BR' / 'visual'
LABELS = VISUAL / 'labels.tsv'
EUROPE = 'CUSA05262-1.01'
LANGUAGES = ('UK', 'ES', 'DE', 'FR', 'IT')
LAYOUTS = (('GUI/REHD_Interface.mbs', 'GUI/HD_Interface.ftx'), ('Interface.mbs', 'Interface.ftx'))
EUROPE_ARCHIVES = ('Odin2_EU_PATCH1_RE_{}.CPK', 'Odin2_EU_PATCH1_OR_{}.CPK', 'Odin2_RE_{}.CPK', 'Odin2_OR_{}.CPK')
ENGLISH = {
    'CUSA05083-1.00': ('Odin2_RE_US.CPK', 'Odin2_OR_US.CPK'),
    'CUSA05262-1.00': ('Odin2_RE_UK.CPK', 'Odin2_OR_UK.CPK'),
    'CUSA05262-1.01': ('Odin2_RE_UK.CPK', 'Odin2_OR_UK.CPK', 'Odin2_EU_PATCH1_RE_UK.CPK', 'Odin2_EU_PATCH1_OR_UK.CPK'),
    'CUSA01290-1.00': ('Odin2_RE.CPK', 'Odin2_ORHD.CPK'),
    'CUSA01290-1.02': ('Odin2_RE.CPK', 'Odin2_ORHD.CPK'),
}

# The interface glyph font in the European atlas: (texture, x0, y0, x1, y1) -> letter. Pieces whose meaning depends on
# how they are turned start with '#' and are read through VARIANTS only.
GLYPHS = {
    (0, 2008, 152, 2041, 168): '#stroke', (0, 1356, 158, 1403, 172): 'l', (0, 1946, 354, 1968, 402): 'f',
    (0, 1894, 854, 1948, 870): 'j', (0, 1948, 854, 1996, 869): 'i', (1, 1992, 152, 2010, 170): '.',
    (1, 2030, 186, 2045, 234): '!', (1, 1954, 210, 1970, 234): '#tick', (1, 950, 418, 964, 464): 'I',
    (1, 1024, 846, 1048, 862): '#dots', (1, 2027, 942, 2048, 986): 't', (2, 599, 0, 631, 47): 'D',
    (2, 691, 0, 722, 46): '9', (2, 796, 0, 824, 46): 'F', (2, 984, 0, 1012, 36): 'c', (2, 581, 54, 615, 100): '4',
    (2, 707, 54, 738, 100): 'R', (2, 804, 54, 831, 101): 'k', (2, 867, 54, 895, 96): 'g', (2, 647, 78, 686, 114): 'm',
    (2, 686, 78, 707, 114): 'r', (2, 594, 120, 626, 167): 'U', (2, 686, 120, 717, 166): 'N',
    (2, 717, 120, 746, 167): 'T', (2, 782, 120, 809, 167): 'h', (2, 654, 167, 685, 213): 'H',
    (2, 920, 167, 950, 202): 'u', (2, 950, 167, 978, 203): 'n', (2, 528, 170, 561, 218): 'V',
    (2, 562, 170, 594, 217): 'X', (2, 502, 190, 528, 225): 'z', (2, 719, 232, 749, 278): 'P',
    (2, 841, 232, 870, 274): 'p', (2, 942, 232, 972, 268): 'o', (2, 1002, 232, 1024, 271): '1',
    (2, 508, 280, 544, 328): 'A', (2, 658, 280, 688, 328): 'B', (2, 688, 280, 718, 327): 'd',
    (2, 718, 280, 746, 327): 'b', (2, 949, 292, 977, 328): 'x', (2, 580, 312, 612, 358): 'K',
    (2, 688, 330, 718, 377): '2', (2, 975, 330, 1002, 366): 's', (2, 502, 364, 539, 410): 'M',
    (2, 628, 364, 658, 412): 'S', (2, 658, 364, 688, 410): '3', (2, 883, 372, 913, 408): 'a',
    (2, 756, 376, 783, 422): 'L', (2, 688, 377, 717, 423): 'Z', (2, 537, 414, 571, 462): 'G',
    (2, 571, 414, 603, 462): 'C', (2, 867, 423, 896, 459): 'e', (2, 896, 423, 924, 459): 'v',
    (2, 248, 426, 283, 481): 'Q', (2, 665, 462, 703, 498): 'w', (2, 703, 462, 731, 509): 'y',
    (2, 731, 462, 759, 508): 'E', (2, 795, 462, 824, 504): 'q', (2, 503, 464, 537, 512): 'O',
    (2, 338, 352, 361, 399): 'J', (2, 460, 414, 503, 461): 'W', (2, 615, 54, 647, 101): 'Y',
    (2, 718, 330, 747, 376): '5',
}
VARIANTS = {
    ('#stroke', ((-1, -1), (1, -1))): 'acute', ('#stroke', ((-1, 1), (1, 1))): 'acute',
    ('#stroke', ((1, 1), (1, -1))): 'grave', ('#stroke', ((1, -1), (1, 1))): 'grave',
    ('#tick', T.UPRIGHT): "'", ('#tick', ((-1, 0), (0, -1))): ',', ('#dots', T.UPRIGHT): 'diaeresis',
}
# Word sprites of the English interface, as the European atlas holds them.
SPRITES = {
    (1, 468, 478, 630, 502): 'EQUIPMENT', (1, 1480, 838, 1616, 862): 'COMMAND', (1, 1836, 314, 1992, 338): 'MATERIALS',
    (1, 1836, 482, 1972, 506): 'ITEM BOX', (1, 1842, 152, 1992, 176): 'SHORTCUT', (1, 1913, 986, 2045, 1010): 'SUPPORT',
    (2, 0, 194, 122, 218): 'SPECIAL', (2, 0, 242, 96, 266): 'SEEDS', (2, 122, 218, 238, 242): 'SETTING',
    (2, 122, 242, 218, 266): 'STATS', (2, 122, 352, 210, 376): 'MAGIC', (2, 122, 474, 200, 498): 'DATA',
    (2, 248, 248, 344, 272): 'FOODS', (2, 302, 146, 412, 170): 'RESULT', (2, 362, 350, 450, 374): 'ITEMS',
    (2, 571, 462, 631, 486): 'BAG', (3, 249, 28, 313, 52): 'MAX',
}
# The decorative initial of the English REST AREA card.
DECORATIVE = {(1, 246, 750, 352, 878), (1, 642, 0, 782, 166)}


class Reader(T.Reader):
    def char(self, layout, k):
        ch = super().char(layout, k)
        return None if ch is None or ch.startswith('#') else ch


def compact(text):
    return ''.join(text.split())


def read_labels():
    out = {}
    with open(LABELS, encoding='utf-8', newline='') as f:
        for row in csv.DictReader(f, delimiter='\t'):
            out[' | '.join(compact(part) for part in row['source'].split('|'))] = row
    return out


class Archives:
    def __init__(self, prepared):
        self.prepared = Path(prepared)
        self.open = {}

    def get(self, build, name):
        key = (build, name)
        if key not in self.open:
            path = self.prepared / build / name
            self.open[key] = cpk.Cpk(path, original=True) if path.is_file() else None
        return self.open[key]

    def read(self, build, archives, entry):
        for name in archives:
            c = self.get(build, name)
            if c is None:
                continue
            hit = next((e for e in c.entries if e['path'].lower() == entry.lower()), None)
            if hit:
                return c.read(hit)
        return None


def atlas(data):
    return [visuals.decode(data, t) for t in ftex.textures(data)]


def matches(table, source, layout, target):
    """{European key: (target key, quarter turns)} for every European sprite of `table` the layout draws in the
    target atlas: same pixels, possibly stored turned."""
    wanted = {}
    for key in table:
        t, x0, y0, x1, y1 = key
        img = source[t][y0:y1, x0:x1, 3] > 128
        for q in range(4):
            turned = np.rot90(img, q)
            wanted.setdefault(turned.shape, []).append((turned, key, q))
    out = {}
    for key in table:
        t, x0, y0, x1, y1 = key
        if t < len(target) and target[t].shape == source[t].shape:
            same = (target[t][y0:y1, x0:x1, 3] > 128) == (source[t][y0:y1, x0:x1, 3] > 128)
            if same.size and same.mean() > 0.97:
                out[key] = (key, 0)
    for p in range(layout.count(M.PARTS)):
        rect = layout.part_rect(p)
        for t in range(len(target)):
            img = target[t][rect[1]:rect[3], rect[0]:rect[2], 3] > 128
            for ref, key, q in wanted.get(img.shape, []):
                if img.size and key not in out and (ref == img).mean() > 0.97:
                    out[key] = ((t,) + rect, q)
                    break
    return out


def moved_part(part, source_key, target_key, q):
    """A European part record pointing at the same pixels in the target atlas, stored `q` quarter turns away."""
    f = list(struct.unpack('<12f', part))
    _, sx0, sy0, sx1, sy1 = source_key
    _, tx0, ty0, tx1, ty1 = target_key
    corners = []
    for i in range(4):
        x, y = (f[2 + 2 * i] - sx0) / (sx1 - sx0), (f[3 + 2 * i] - sy0) / (sy1 - sy0)
        for _ in range(q):
            x, y = y, 1 - x
        corners.append((tx0 + x * (tx1 - tx0), ty0 + y * (ty1 - ty0)))
    xs, ys = [c[0] for c in corners], [c[1] for c in corners]
    flat = [v for c in corners for v in c]
    return struct.pack('<12f', (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, *flat, *corners[0])


def target_font(font, found):
    """The European font in a target atlas: every glyph the target draws, its part moved there."""
    out = T.Font(gap=font.gap, space=font.space, marks=dict(font.marks))
    for ch, g in font.glyphs.items():
        f = struct.unpack('<12f', g.part)
        rect = (int(min(f[2:10:2])), int(min(f[3:10:2])), int(max(f[2:10:2])), int(max(f[3:10:2])))
        hit = found.get((g.texture,) + rect)
        if hit is None:
            continue
        (t, *r), q = hit
        out.glyphs[ch] = T.Glyph(ch, t, g.pattern, moved_part(g.part, (g.texture,) + rect, (t,) + tuple(r), q),
                                 g.key, g.w, g.h, g.bottom)
    if '.' not in out.glyphs and '!' in out.glyphs and '.' in font.glyphs:
        out.glyphs['.'] = dot_of(out.glyphs['!'], font.glyphs['.'])
    return out


DOT_ROWS = 0.3


def dot_of(mark, period):
    """A period cut from the dot of the atlas's own exclamation mark: the bottom of its sprite, on the baseline."""
    f = list(struct.unpack('<12f', mark.part))
    corners = [(f[2 + 2 * k], f[3 + 2 * k]) for k in range(4)]
    top = {px: corners[k] for k, (px, py) in enumerate(mark.pattern) if py == 0}
    bottom = {px: corners[k] for k, (px, py) in enumerate(mark.pattern) if py == 1}
    cut = []
    for px, py in mark.pattern:
        a, b = top[px], bottom[px]
        cut.append(b if py == 1 else (b[0] + (a[0] - b[0]) * DOT_ROWS, b[1] + (a[1] - b[1]) * DOT_ROWS))
    xs, ys = [c[0] for c in cut], [c[1] for c in cut]
    part = struct.pack('<12f', (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, *[v for c in cut for v in c], *cut[0])
    return T.Glyph('.', mark.texture, mark.pattern, part, mark.key, mark.w, mark.h * DOT_ROWS, mark.bottom)

def layer_peers(uk, others):
    """UK layer -> [(layout, layer)] of the other languages drawn by the same animation channel and segment."""
    def by_anim(layout):
        out = {}
        for name, first, count in layout.anims():
            out[name] = [[layout.segment_layer(s) for s in layout.channel_segments(c)] for c in range(first, first + count)]
        return out
    mine = by_anim(uk)
    peers = defaultdict(set)
    for other in others:
        theirs = by_anim(other)
        for name, channels in mine.items():
            match = theirs.get(name)
            if not match or len(match) != len(channels):
                continue
            for a, b in zip(channels, match):
                if len(a) == len(b):
                    for la, lb in zip(a, b):
                        if la < uk.count(M.LAYERS) and lb < other.count(M.LAYERS):
                            peers[la].add((id(other), lb))
    return peers


def slot_lines(layout, reader, layer):
    found = T.lines(layout, reader, [layer])
    return sorted(found, key=lambda l: (round(l.box[1] / 4), l.box[0]))


def budget(line, index, peers, layouts, readers):
    """(width, align) of a line from its counterparts in the official European languages."""
    widths, aligns = [line.box[2] - line.box[0]], []
    for pid, layer in sorted(peers):
        layout, reader = layouts[pid], readers[pid]
        theirs = slot_lines(layout, reader, layer)
        if index < len(theirs):
            other = theirs[index]
            widths.append(other.box[2] - other.box[0])
            for name, a, b in (('left', line.box[0], other.box[0]), ('right', line.box[2], other.box[2]),
                               ('center', (line.box[0] + line.box[2]) / 2, (other.box[0] + other.box[2]) / 2)):
                if abs(a - b) < 1.5:
                    aligns.append(name)
    align = max(sorted(set(aligns)), key=aligns.count) if aligns else 'center'
    return max(widths), align, len(widths) > 1


def sprite_line(layout, layer, k, name, font):
    """A word sprite as a line to respell: the glyphs fill the sprite's box height, standing on its bottom."""
    key = layout.key(k)
    box = T.quad_box(layout.geom(key['geom']))
    letter = font.glyphs.get('A') or next(g for c, g in sorted(font.glyphs.items()) if c.isupper())
    scale = (box[3] - box[1]) / letter.h
    return T.Line(layer, [k], name, box, scale, 1.0, box[3] - letter.bottom * scale, key['colour'])


def sprite_layers(layout, k):
    return [layer for layer in range(layout.count(M.LAYERS))
            if layout.layer_range(layer)[0] <= k < sum(layout.layer_range(layer))]


def sprite_budget(layout, layer, k, peers, layouts, glyph_keys):
    """The widest word sprite the official languages draw at the slot of a UK sprite."""
    box = T.quad_box(layout.geom(layout.key(k)['geom']))
    cx, cy, h = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2, box[3] - box[1]
    widths = [box[2] - box[0]]
    for pid, other_layer in sorted(peers.get(layer, ())):
        other = layouts[pid]
        first, count = other.layer_range(other_layer)
        best = None
        for j in range(first, min(first + count, other.count(M.KEYS))):
            key = other.key(j)
            if key['part'] >= other.count(M.PARTS) or (key['texture'],) + other.part_rect(key['part']) in glyph_keys:
                continue
            b = T.quad_box(other.geom(key['geom']))
            if abs((b[1] + b[3]) / 2 - cy) < h and abs(b[3] - b[1] - h) < 0.35 * h:
                d = abs((b[0] + b[2]) / 2 - cx)
                if best is None or d < best[0]:
                    best = (d, b[2] - b[0])
        if best:
            widths.append(best[1])
    return max(widths)


def phrase_of(layout, reader, layer, labels):
    """The phrase row matching the layer's lines read left to right, with the lines in that order."""
    found = sorted(slot_lines(layout, reader, layer), key=lambda l: l.box[0])
    if len(found) < 2:
        return None, found
    key = ' | '.join(compact(T.line_text(layout, l, reader)) for l in found)
    return labels.get(key), found


def rest_area(layout, layer, lines, plans, font):
    """REST AREA: the decorative initial goes, and the channel drawing the bell between the two words moves so the bell
    and the new text are centred where the English stood (the Spanish card moves the same channel)."""
    first, count = layout.layer_range(layer)
    keys = range(first, min(first + count, layout.count(M.KEYS)))
    decor = [k for k in keys if (layout.key(k)['texture'],) + layout.part_rect(layout.key(k)['part']) in DECORATIVE]
    text = plans[-1]
    text.width = float('inf')
    for name, start, n in layout.anims():
        drawn = layout.anim_layers(start, n)
        mine = [x for x in drawn if x[1] == layer]
        if not mine:
            continue
        tx = mine[0][2][0]
        for channel, other, (bx, _, _, _) in drawn:
            if other == layer:
                continue
            o_first, o_count = layout.layer_range(other)
            boxes = [T.quad_box(layout.geom(layout.key(k)['geom'])) for k in range(o_first, o_first + o_count)]
            if not boxes:
                continue
            left, right = min(b[0] for b in boxes) + bx, max(b[2] for b in boxes) + bx
            if not lines[0].box[2] + tx < (left + right) / 2 < lines[-1].box[0] + tx:
                continue
            english_left = min([T.quad_box(layout.geom(layout.key(k)['geom']))[0] for k in decor] +
                               [l.box[0] for l in lines]) + tx
            english_right = lines[-1].box[2] + tx
            gap = lines[-1].box[0] + tx - right
            _, width, _ = T.layout_word(font, text.line, text.text, text.width)
            span = right - left + gap + width
            dx = (english_left + english_right) / 2 - span / 2 - left
            text.left = right + dx + gap - tx
            text.drop = tuple(decor)
            text.channels = {channel: dx}
            return
    text.left = lines[-1].box[0]
    text.drop = tuple(decor)


def _anim_line_boxes(layout, reader, first, count):
    """{channel: [(text, absolute box)]} of the glyph lines each channel of an animation draws in its first segment."""
    out = {}
    for channel in range(first, first + count):
        segments = list(layout.channel_segments(channel))
        if not segments:
            continue
        layer = layout.segment_layer(segments[0])
        if layer >= layout.count(M.LAYERS):
            continue
        x, y, sx, sy = layout.segment_transform(segments[0])
        found = [(compact(T.line_text(layout, line, reader)),
                  (x + line.box[0] * sx, y + line.box[1] * sy, x + line.box[2] * sx, y + line.box[3] * sy))
                 for line in T.lines(layout, reader, [layer])]
        if found:
            out[channel] = found
    return out


def overlaid_channels(layout, reader, slack=4.0):
    """{animation: [channel]} of English channels whose only line repeats, in the same place, part of a longer line
    another channel of the animation draws: the save slot's NO DATA draws a separate DATA over its own DATA. The
    official languages draw the longer line alone; respelled apart, the two words would no longer coincide."""
    out = {}
    for name, first, count in layout.anims():
        lines = _anim_line_boxes(layout, reader, first, count)
        for channel, mine in lines.items():
            if len(mine) != 1:
                continue
            text, box = mine[0]
            for other, theirs in lines.items():
                if other == channel:
                    continue
                if any(len(t) > len(text) and text in t and b[0] - slack <= box[0] and box[2] <= b[2] + slack and
                       b[1] - slack <= box[1] and box[3] <= b[3] + slack for t, b in theirs):
                    out.setdefault(name, []).append(channel)
                    break
    return out


def hide_channels(layout, hidden):
    """Points the hidden channels at copies of their segments that draw an empty layer (the game's own layouts draw
    empty layers too); other animations drawing the same layers are untouched."""
    if not hidden:
        return 0
    drawn = {layout.segment_layer(s) for c in range(layout.count(M.CHANNELS)) for s in layout.channel_segments(c)}
    empty = next((l for l in sorted(drawn) if l < layout.count(M.LAYERS) and layout.layer_range(l)[1] == 0), None)
    if empty is None:
        raw = bytearray(layout.record(M.LAYERS, 0))
        struct.pack_into('<4f', raw, 0, 0.0, 0.0, 0.0, 0.0)
        struct.pack_into('<H', raw, 16, 0)
        struct.pack_into('<H', raw, 22, 0)
        empty = layout.append(M.LAYERS, [bytes(raw)])
    done = 0
    for channels in hidden.values():
        for channel in channels:
            segments = list(layout.channel_segments(channel))
            copies = []
            for s in segments:
                raw = bytearray(layout.record(M.SEGMENTS, s))
                struct.pack_into('<I', raw, 0, empty)
                copies.append(bytes(raw))
            start = layout.append(M.SEGMENTS, copies)
            raw = bytearray(layout.record(M.CHANNELS, channel))
            struct.pack_into('<HH', raw, 0, start, len(copies))
            layout.put(M.CHANNELS, channel, bytes(raw))
            done += 1
    return done


def respell_arcs(layout, reader, font, labels, report, where):
    """Labels whose letters stand around a circle (the line reader cannot group them), set on the same circle."""
    done = 0
    for layer in range(layout.count(M.LAYERS)):
        keys = A.letters(layout, reader, layer)
        if not keys or len(keys) < 4:
            continue
        row = labels.get(''.join(ch for _, ch in keys))
        if row is None or sum(len(line.keys) for line in T.lines(layout, reader, [layer])) == len(keys):
            continue
        try:
            placed = A.place(font, row['pt_br'], A.geometry(layout, font, keys, row['source']))
        except (A.ArcError, KeyError) as e:
            report.append((where, 'arc', row['source'], row['pt_br'], f'BLOCKED {e}'))
            continue
        A.respell(layout, font, layer, keys, placed)
        report.append((where, 'arc', row['source'], row['pt_br'], f'layer {layer}'))
        done += 1
    return done


def plan_layout(layout, reader, font, labels, peers, layouts, readers, sprites, report, where):
    plans = []
    glyph_keys = set(reader.table)
    for layer in range(layout.count(M.LAYERS)):
        phrase, ordered = phrase_of(layout, reader, layer, labels)
        mine = []
        for index, line in enumerate(slot_lines(layout, reader, layer)):
            english = T.line_text(layout, line, reader)
            if phrase:
                text = phrase['pt_br'].split(' | ')[ordered.index(line)]
            else:
                row = labels.get(compact(english))
                if row is None:
                    report.append((where, 'line', english, '', 'no label'))
                    continue
                text = row['pt_br']
            if text == english:
                continue
            width, align, official = budget(line, index, peers.get(layer, ()), layouts, readers)
            try:
                T.compose(font, text)
            except KeyError as e:
                report.append((where, 'line', english, text, f'BLOCKED {e}'))
                continue
            mine.append(T.Plan(line, text, width if official else (line.box[2] - line.box[0]) * 1.25, align))
            report.append((where, 'line', english, text, f'{align} {width:.1f}'))
        if phrase and (phrase.get('notes') or '').startswith('rest area') and mine:
            rest_area(layout, layer, ordered, mine, font)
        plans.extend(mine)
    for k in range(layout.count(M.KEYS)):
        key = layout.key(k)
        if key['part'] >= layout.count(M.PARTS):
            continue
        name = sprites.get((key['texture'],) + layout.part_rect(key['part']))
        if not name:
            continue
        row = labels.get(compact(name))
        try:
            T.compose(font, row['pt_br'])
        except KeyError as e:
            report.append((where, 'sprite', name, row['pt_br'], f'BLOCKED {e}'))
            continue
        for layer in sprite_layers(layout, k):
            line = sprite_line(layout, layer, k, name, font)
            width = sprite_budget(layout, layer, k, peers, layouts, glyph_keys) * 1.05
            plans.append(T.Plan(line, row['pt_br'], width, 'center'))
            report.append((where, 'sprite', name, row['pt_br'], f'layer {layer} {width:.1f}'))
    return plans


def build(prepared, out, report_path):
    import interface_art
    import stage_acts
    labels = read_labels()
    archives = Archives(prepared)
    targets, report = [], []
    acts_plan, acts_reference, acts_report = stage_acts.classic_design(prepared)
    report.extend((f'{EUROPE} Classic act titles', 'act', english, pt_br, note)
                  for _, _, english, pt_br, note in acts_report)
    for mbs_name, ftx_name in LAYOUTS:
        europe = {L: M.Mbs(archives.read(EUROPE, [a.format(L) for a in EUROPE_ARCHIVES], mbs_name)) for L in LANGUAGES}
        base_europe = {L: M.Mbs(archives.read(EUROPE, [a.format(L) for a in EUROPE_ARCHIVES if 'PATCH1' not in a], mbs_name)) for L in LANGUAGES}
        eu_pixels = atlas(archives.read(EUROPE, [a.format('UK') for a in EUROPE_ARCHIVES], ftx_name))
        base_reader = Reader(GLYPHS, VARIANTS)
        all_eu = [M.Mbs(archives.read(EUROPE, [a.format(L) for a in EUROPE_ARCHIVES], name))
                  for L in LANGUAGES for name, _ in LAYOUTS]
        font = T.learn(all_eu, base_reader)
        others = [europe[L] for L in LANGUAGES[1:]]
        layouts = {id(m): m for m in others}
        readers = {id(m): base_reader for m in others}
        for build_name, names in ENGLISH.items():
            for archive in names:
                c = archives.get(build_name, archive)
                if c is None:
                    report.append((build_name, 'build', archive, '', 'DATA UNAVAILABLE'))
                    continue
                hit = next((e for e in c.entries if e['path'].lower() == mbs_name.lower()), None)
                if hit is None:
                    continue
                data = c.read(hit)
                pixels = atlas(archives.read(build_name, [archive] + list(names), ftx_name))
                layout = M.Mbs(data)
                found = matches({**GLYPHS, **SPRITES}, eu_pixels, layout, pixels)
                table = {found[k][0]: ch for k, ch in GLYPHS.items() if k in found}
                sprites = {found[k][0]: name for k, name in SPRITES.items() if k in found}
                reader = Reader(table, VARIANTS)
                peers = layer_peers(layout, others)
                drawn = target_font(font, found)
                where = f'{build_name} {archive} {mbs_name}'
                overlaid = overlaid_channels(layout, reader)
                plans = plan_layout(layout, reader, drawn, labels, peers, layouts, readers, sprites, report, where)
                if plans:
                    T.respell(layout, drawn, reader, plans)
                arcs = respell_arcs(layout, reader, drawn, labels, report, where)
                hidden = hide_channels(layout, overlaid)
                for name, channels in sorted(overlaid.items()):
                    report.append((where, 'overlay', name, '', f'channels {channels} hidden: their word is part of a longer line'))
                acted = False
                if mbs_name == stage_acts.CLASSIC_LAYOUT:
                    try:
                        moved = stage_acts.relocate(acts_plan, data, acts_reference, [p[..., 3] for p in pixels],
                                                    found, moved_part)
                        acted = stage_acts.respell_layout(layout, moved, where)
                        report.append((where, 'act', 'L_* titles', '', 'ok' if acted else 'nothing to respell'))
                    except stage_acts.ActError as e:
                        report.append((where, 'act', 'L_* titles', '', f'BLOCKED {e}'))
                where_rects = interface_art.resolve(eu_pixels, pixels)
                if where_rects is not None:
                    vintage = europe if 'PATCH1' in archive else base_europe
                    lang_of = {id(vintage[L]): L for L in LANGUAGES[1:]}
                    art_peers = layer_peers(layout, [vintage[L] for L in LANGUAGES[1:]])
                    painted = interface_art.edit_layout(layout, vintage, art_peers, lang_of, where_rects, report, where)
                else:
                    painted = 0
                    report.append((where, 'art', 'painted sprites', '', 'BLOCKED the atlas does not hold the European sprites'))
                if not plans and not painted and not hidden and not acted and not arcs:
                    report.append((where, 'layout', mbs_name, '', 'nothing to respell'))
                    continue
                new = layout.bytes()
                check = M.Mbs(new)
                written = {T.line_text(check, l, reader) for l in T.lines(check, reader)}
                missing = sorted({p.text for p in plans} - {t for t in written})
                if missing:
                    report.append((where, 'verify', ', '.join(missing[:8]), '', 'reads back differently'))
                targets.append((mbs_name, data, new, layout.inserts))
                print(f'{where}: {len(plans)} labels, {len(data)} -> {len(new)} bytes')
    unique = {}
    for name, data, new, inserts in targets:
        unique.setdefault((name.lower(), len(data), zlib.crc32(data)), (name, data, new, inserts))
    blob = oslv.encode([(n, d, w, i) for n, d, w, i in unique.values()])
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    Path(out).write_bytes(blob)
    print(f'-> {out} ({len(blob)} bytes, {len(unique)} targets)')
    if report_path:
        with open(report_path, 'w', encoding='utf-8', newline='') as f:
            f.write('where\tkind\tenglish\tpt_br\tnote\n')
            for row in report:
                f.write('\t'.join(row) + '\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--prepared', default=str(ROOT / 'game' / 'prepared'))
    ap.add_argument('--out', default=str(VISUAL / 'interface-ptbr.oslv'))
    ap.add_argument('--report')
    args = ap.parse_args()
    visuals.use_native_reader()
    build(args.prepared, args.out, args.report)


if __name__ == '__main__':
    main()
