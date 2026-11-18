#!/usr/bin/env python3
"""The Classic character screen (Butterfly.mbs) in Portuguese (Brazil): each hero's book title under the name.

The title is one word channel of three layers (scattered, settled, stretched) and every layer draws the word in two
passes of the same keys (an outline pass, then the lettering) between quote pieces. The letters and the acute come from
the settled lines of the five European layouts, named by what each language spells; the Portuguese is set with the
official spacing and every layer and pass is derived from the English one letter by letter.

    butterfly_titles.py [--prepared game/prepared] [--out file] [--report file]
"""
import argparse
import struct
import sys
import zlib
from collections import defaultdict
from pathlib import Path
from statistics import median

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'prompts'))
import layout_text as LT  # noqa: E402
import mbs as M  # noqa: E402
import oslv  # noqa: E402
import stage_acts as S  # noqa: E402
import visuals  # noqa: E402

ROOT = HERE.parent.parent
DEFAULT_OUT = ROOT / 'localization' / 'pt-BR' / 'visual' / 'butterfly-ptbr.oslv'
LAYOUT = 'Butterfly.mbs'
ARCHIVES = ('Odin2_OR_{}.CPK',)
QUOTE = (0, 494, 102, 512, 130)
SUBTITLE_CHANNEL = 2

TITLES = {
    'A': dict(UK='VALKYRIE', ES='LA VALQUIRIA', FR='VALKYRIE', DE='WALKÜRE', IT='LA VALCHIRIA'),
    'B': dict(UK='THE POOKA PRINCE', ES='EL PRÍNCIPE PUKA', FR='LE PRINCE POOKA', DE='DER POOKAPRINZ',
              IT='IL PRINCIPE PUKA'),
    'C': dict(UK='FAIRY LAND', ES='TIERRA DE HADAS', FR='LE ROYAUME DES FÉES', DE='IM FEENLAND', IT='LA TERRA FATATA'),
    'D': dict(UK='THE BLACK SWORD', ES='LA ESPADA OSCURA', FR="L'ÉPÉE NOIRE", DE='DAS SCHWARZE SCHWERT',
              IT='LA SPADA FUNESTA'),
    'E': dict(UK='FATE', ES='EL DESTINO', FR='LE DESTIN', DE='DAS SCHICKSAL', IT='IL FATO'),
}
PORTUGUESE = {'A': 'VALQUÍRIA', 'B': 'O PRÍNCIPE POOKA', 'C': 'TERRA DAS FADAS', 'D': 'A ESPADA NEGRA', 'E': 'DESTINO'}
SOURCES = (('CUSA05262-1.01', 'Odin2_OR_UK.CPK'), ('CUSA05262-1.00', 'Odin2_OR_UK.CPK'),
           ('CUSA05083-1.00', 'Odin2_OR_US.CPK'))


def channel_of(layout, anim):
    first = next(f for n, f, _ in layout.anims() if n == anim)
    return first + SUBTITLE_CHANNEL


def layers_of(layout, channel):
    seen = []
    for segment in layout.channel_segments(channel):
        layer = layout.segment_layer(segment)
        if layer not in seen:
            seen.append(layer)
    return seen[0], seen[1], seen[-1]


def passes(layout, layer):
    keys = layout.layer_keys(layer)
    half = len(keys) // 2
    return keys[:half], keys[half:]


def box_of(layout, key):
    return LT.quad_box(layout.geom(key['geom']))


def is_quote(layout, key):
    return (key['texture'],) + layout.part_rect(key['part']) == QUOTE


def split(layout, keys):
    """(letters and marks as [(index, box)], quote keys) of one pass."""
    body = [(i, box_of(layout, k)) for i, k in enumerate(keys) if not is_quote(layout, k)]
    letters, marks = S.split_marks([(i, b) for i, b in body])
    return sorted(letters, key=lambda ib: ib[1][0]), marks


def learn(layouts):
    pool = S.Pool()
    raws = {}
    for language, layout in layouts.items():
        for anim, names in TITLES.items():
            text = names[language]
            settled = layers_of(layout, channel_of(layout, anim))[1]
            outline, lettering = passes(layout, settled)
            letters, marks = split(layout, lettering)
            spelled = S.decompose(text)
            if language == 'FR':
                spelled = [s for s in spelled if s[0] not in "«»"]
            if len(letters) != len(spelled) or len(marks) != sum(len(m) for _, m in spelled):
                pool.skipped.append(f'{language} {anim}: the keys do not spell {text!r}')
                continue
            flat = [b for (_, b), (c, _) in zip(letters, spelled) if c not in ',.gjpqy']
            baseline = median(b[3] for b in flat)
            unit = median(b[3] - b[1] for b in flat)
            kind = S.line_kind(text)
            pool.lines[(anim, 0)].append((text, (min(b[0] for _, b in letters), min(b[1] for _, b in letters),
                                                 max(b[2] for _, b in letters), max(b[3] for _, b in letters))))
            for (i, b), (c, ms) in zip(letters, spelled):
                c = 'ı' if c == 'i' and ms else c
                part = bytes(layout.record(M.PARTS, lettering[i]['part']))
                piece = S.Piece(part, lettering[i]['raw'], b[2] - b[0], b[3] - b[1], b[3] - baseline, kind, anim, unit)
                raws[(part, lettering[i]['raw'])] = outline[i]['raw']
                if not any(p.part == piece.part for p in pool.letters[(c, kind)]):
                    pool.letters[(c, kind)].append(piece)
            for k in range(1, len(letters)):
                gap = (letters[k][1][0] - letters[k - 1][1][2]) / unit
                words, n, starts = text.split(), 0, set()
                for w in words:
                    starts.add(n)
                    n += len([s for s in S.decompose(w) if s[0] not in "«»"])
                (pool.spaces if k in starts else pool.gaps)[kind].append(gap)
            queue = [(c, m) for c, ms in spelled for m in ms]
            bases = [(c, b) for (_, b), (c, ms) in zip(letters, spelled) for _ in ms]
            for (i, mb), (c, m), (_, bb) in zip(sorted(marks, key=lambda ib: ib[1][0]), queue, bases):
                part = bytes(layout.record(M.PARTS, lettering[i]['part']))
                raws[(part, lettering[i]['raw'])] = outline[i]['raw']
                pool.marks[(m, c.isupper())].append((part, lettering[i]['raw'], ((mb[0] + mb[2]) / 2 - (bb[0] + bb[2]) / 2) / unit,
                                                     (bb[1] - mb[3]) / unit, (mb[2] - mb[0]) / unit, (mb[3] - mb[1]) / unit))
    return pool, raws


class Frame:
    """What the English layout does to a letter between two layers or passes: its move and size change."""

    def __init__(self, layout, anim):
        self.layout = layout
        self.layers = layers_of(layout, channel_of(layout, anim))
        self.info = {}
        for name, layer in zip(('scatter', 'settled', 'exit'), self.layers):
            outline, lettering = passes(layout, layer)
            for label, keys in (('outline', outline), ('lettering', lettering)):
                letters, marks = split(layout, keys)
                quotes = [k for k in keys if is_quote(layout, k)]
                self.info[(name, label)] = (keys, letters + marks, quotes)

    def boxes(self, name, label):
        keys, body, _ = self.info[(name, label)]
        return [b for _, b in sorted(body, key=lambda ib: ib[1][0])]

    def colour(self, name, label):
        keys, body, _ = self.info[(name, label)]
        return keys[body[0][0]]['colour'], [k for k in keys if is_quote(self.layout, k)]


def mapped(items, base, target):
    """Items moved the way the English letters at the same rank move from the boxes `base` to `target`."""
    out = []
    order = sorted(range(len(items)), key=lambda i: items[i][2][0])
    rank = {i: r for r, i in enumerate(order)}
    for i, (part, raw, b) in enumerate(items):
        j = round(rank[i] * (len(base) - 1) / max(1, len(items) - 1))
        a, t = base[j], target[j]
        sx = (t[2] - t[0]) / (a[2] - a[0])
        sy = (t[3] - t[1]) / (a[3] - a[1])
        cx, cy = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
        ncx = cx + ((t[0] + t[2]) - (a[0] + a[2])) / 2
        ncy = cy + ((t[1] + t[3]) - (a[1] + a[3])) / 2
        w, h = (b[2] - b[0]) * sx, (b[3] - b[1]) * sy
        out.append((part, raw, (ncx - w / 2, ncy - h / 2, ncx + w / 2, ncy + h / 2)))
    return out


def sub_part(record, source, window):
    """A copy of the part record `record` (covering the `source` rectangle) that shows only `window` of it."""
    values = list(struct.unpack('<12f', record))
    for i in range(1, 6):
        x, y = values[2 * i], values[2 * i + 1]
        values[2 * i] = window[0] if x <= source[0] else window[2]
        values[2 * i + 1] = window[1] if y <= source[1] else window[3]
    values[0], values[1] = (window[0] + window[2]) / 2, (window[1] + window[3]) / 2
    return struct.pack('<12f', *values)


def with_g(items, text, pool, raws):
    """The face has no G: each is its C with the middle arm of its F laid over the opening."""
    c = pool.letters[('C', 'cap')][0]
    f = pool.letters[('F', 'cap')][0]
    f_rect = S.part_rect(f.part)
    window = (f_rect[0] + 13, f_rect[1] + 22, f_rect[2] - 3, f_rect[3] - 16)
    bar = sub_part(f.part, f_rect, window)
    raws[(bar, f.raw)] = raws[(f.part, f.raw)]
    extra, at = [], 0
    for char, marks in S.decompose(text):
        if char == 'G':
            part, raw, (x0, y0, x1, y1) = items[at]
            k = (y1 - y0) / c.h
            w, h = (window[2] - window[0]) * k, (window[3] - window[1]) * k
            left = x0 + 0.42 * (x1 - x0)
            top = y0 + 0.40 * (y1 - y0)
            extra.append((bar, f.raw, (left, top, left + w, top + h)))
        at += 1 + len(marks)
    return items + extra


def design(layout, anim, text, pool, raws):
    frame = Frame(layout, anim)
    settled_boxes = frame.boxes('settled', 'lettering')
    letters, _ = split(layout, frame.info[('settled', 'lettering')][0])
    flat = [b for _, b in letters]
    centre = (min(b[0] for b in flat) + max(b[2] for b in flat)) / 2
    pool.letters[('G', S.line_kind(text))] = pool.letters[('C', S.line_kind(text))]
    items = S.set_line(pool, text, centre, median(b[3] for b in flat), median(b[3] - b[1] for b in flat),
                       S.line_kind(text), anim)
    budget = max(b[2] - b[0] for _, b in pool.lines[(anim, 0)])
    width = max(b[2] for _, _, b in items) - min(b[0] for _, _, b in items)
    if width > budget:
        items = S.squeezed(items, centre, budget / width)
    items = with_g(items, text, pool, raws)
    out = []
    for name in ('scatter', 'settled', 'exit'):
        for label in ('outline', 'lettering'):
            target = frame.boxes(name, label)
            placed = mapped(items, settled_boxes, target) if (name, label) != ('settled', 'lettering') else items
            if label == 'outline':
                placed = [(part, raws[(part, raw)], b) for part, raw, b in placed]
            colour, quotes = frame.colour(name, label)
            keys, body, _ = frame.info[(name, label)]
            old = [b for _, b in sorted(body, key=lambda ib: ib[1][0])]
            lo = min(b[0] for b in old)
            hi = max(b[2] for b in old)
            new_lo, new_hi = min(b[0] for _, _, b in placed), max(b[2] for _, _, b in placed)
            quote_items = []
            for q in quotes:
                qb = box_of(layout, q)
                shift = (new_lo - lo) if qb[0] < (lo + hi) / 2 else (new_hi - hi)
                quote_items.append((bytes(layout.record(M.PARTS, q['part'])), q['raw'],
                                    (qb[0] + shift, qb[1], qb[2] + shift, qb[3]), q['colour']))
            out.append((name, label, [(p, r, b, colour) for p, r, b in placed] + quote_items))
    return out


def respell(data, plan, where):
    layout = M.Mbs(data)
    parts = {bytes(layout.record(M.PARTS, i)): i for i in range(layout.count(M.PARTS))}
    new_parts, keys, geoms, layer_of = [], [], [], {}
    for anim, designed in plan.items():
        channel = channel_of(layout, anim)
        layers = dict(zip(('scatter', 'settled', 'exit'), layers_of(layout, channel)))
        per_layer = defaultdict(list)
        for name, label, items in designed:
            per_layer[name].extend(items)
        for name, items in per_layer.items():
            first = layout.count(M.KEYS) + len(keys)
            template = bytes(layout.record(M.KEYS, layout.layer_range(layers[name])[0]))
            for part, raw, box, colour in items:
                if part not in parts:
                    parts[part] = layout.count(M.PARTS) + len(new_parts)
                    new_parts.append(part)
                out = bytearray(template)
                out[0:4], out[6:8] = raw[0:4], raw[6:8]
                struct.pack_into('<HHH', out, 10, parts[part], colour, layout.count(M.GEOMS) + len(geoms))
                geoms.append(S.geom_record(box))
                keys.append(bytes(out))
            bbox = (min(b[0] for _, _, b, _ in items), min(b[1] for _, _, b, _ in items),
                    max(b[2] for _, _, b, _ in items), max(b[3] for _, _, b, _ in items))
            layer_of[layers[name]] = (first, len(items), bbox)
    if new_parts:
        layout.append(M.PARTS, new_parts)
    layout.append(M.GEOMS, geoms)
    layout.append(M.KEYS, keys)
    for layer, (first, count, bbox) in layer_of.items():
        layout.set_layer(layer, first, count, bbox)
    return layout.bytes(), layout.inserts


def generate(prepared):
    layouts = {L: M.Mbs(S.read_entry(prepared, S.EUROPE, [a.format(L) for a in ARCHIVES], LAYOUT)) for L in S.LANGUAGES}
    pool, raws = learn(layouts)
    report = [('pool', f'skipped: {line}') for line in pool.skipped]
    plan = {anim: design(layouts['UK'], anim, PORTUGUESE[anim], pool, raws) for anim in TITLES}
    targets = {}
    for build, archive in SOURCES:
        where = f'{build} {archive}'
        data = S.read_entry(prepared, build, [archive], LAYOUT)
        if data is None:
            report.append((where, 'DATA UNAVAILABLE'))
            continue
        key = (LAYOUT, zlib.crc32(data))
        if key in targets:
            continue
        mine = M.Mbs(data)
        if [n for n, _, _ in mine.anims()] != [n for n, _, _ in layouts['UK'].anims()] or mine.count(M.KEYS) != layouts['UK'].count(M.KEYS):
            report.append((where, 'BLOCKED: the layout is not the European one'))
            continue
        new, inserts = respell(data, plan, where)
        targets[key] = (data, new, inserts)
        report.append((where, f'ok: {len(data)} -> {len(new)} bytes'))
    return targets, report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--prepared', default=str(ROOT / 'game' / 'prepared'))
    ap.add_argument('--out', default=str(DEFAULT_OUT))
    args = ap.parse_args()
    visuals.use_native_reader()
    targets, report = generate(args.prepared)
    blob = oslv.encode([(name, old, new, inserts) for (name, _), (old, new, inserts) in targets.items()])
    Path(args.out).write_bytes(blob)
    print(f'{len(targets)} layouts -> {args.out} ({len(blob)} bytes)')
    for row in report:
        print(*row)
    return 1 if any('BLOCKED' in r[-1] for r in report) else 0


if __name__ == '__main__':
    sys.exit(main())
