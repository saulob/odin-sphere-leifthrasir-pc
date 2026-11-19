"""Chapter and act titles of GUI/REHD_StageName.mbs (the L_* animations) in Portuguese (Brazil).

Each L_* animation spells a book title, Act, Prelude, Epilogue or "Ch." letter by letter from a glyph font every
language shares (the stage card packs' atlases). A word is one channel of three layers whose keys interpolate one to
one: the letters scattered, settled, and stretched three times as wide while they fade. Letters and accent marks are
taken from the settled lines of the five European layouts (named by the words they spell, OFFICIAL); a Portuguese line
the Spanish, French, German or Italian layout already spells is copied from it, any other is set from the letters with
the official spacing and placed on the English line's centre. The Classic Interface.mbs draws the same L_* animations
from the interface glyph font; make_visuals.py writes them with the rest of that layout (classic_design).
"""
import argparse
import struct
import sys
import unicodedata
import zlib
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from statistics import median

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'prompts'))
import cpk  # noqa: E402
import ftex  # noqa: E402
import layout_text as LT  # noqa: E402
import mbs as M  # noqa: E402
import oslv  # noqa: E402
import visuals  # noqa: E402

ROOT = HERE.parent.parent
DEFAULT_OUT = ROOT / 'localization' / 'pt-BR' / 'visual' / 'stage-acts-ptbr.oslv'
LAYOUT = 'GUI/REHD_StageName.mbs'
ATLAS = 'GUI/HD_StageName01.ftx'
EUROPE = 'CUSA05262-1.01'
LANGUAGES = ('UK', 'ES', 'FR', 'DE', 'IT')
EUROPE_ARCHIVES = ('Odin2_EU_PATCH1_RE_{}.CPK', 'Odin2_RE_{}.CPK')

# What each language's settled lines spell, channel by channel in the animation's order.
OFFICIAL = {
    'L_VALKYRIE': dict(UK=['VALKYRIE'], ES=['LA VALQUIRIA'], FR=['VALKYRIE'], DE=['WALKÜRE'], IT=['LA VALCHIRIA']),
    'L_POOKA': dict(UK=['THE POOKA PRINCE'], ES=['EL PRÍNCIPE PUKA'], FR=['LE PRINCE POOKA'], DE=['DER POOKAPRINZ'],
                    IT=['IL PRINCIPE PUKA']),
    'L_FAERIE': dict(UK=['FAIRY LAND'], ES=['TIERRA DE HADAS'], FR=['LE ROYAUME DES FÉES'], DE=['IM FEENLAND'],
                     IT=['LA TERRA FATATA']),
    'L_BLACK': dict(UK=['THE BLACK SWORD'], ES=['LA ESPADA OSCURA'], FR=["L'ÉPÉE NOIRE"], DE=['DAS SCHWARZE SCHWERT'],
                    IT=['LA SPADA FUNESTA']),
    'L_FATE': dict(UK=['FATE'], ES=['EL DESTINO'], FR=['LE DESTIN'], DE=['DAS SCHICKSAL'], IT=['IL FATO']),
    'L_ENDING': dict(UK=['ARMAGEDDON'], ES=['APOCALIPSIS'], FR=['ARMAGEDDON'], DE=['AM ENDE DER ZEIT'], IT=['ARMAGEDDON']),
    'L_REEL': dict(UK=['WHEEL OF FATE'], ES=['LA RUEDA DEL DESTINO'], FR=['LA ROUE DU DESTIN'],
                   DE=['DAS RAD DES SCHICKSALS'], IT=['LA RUOTA DEL DESTINO']),
    'L_0_0': dict(UK=['Act'], ES=['Acto'], FR=['Acte'], DE=['Akt'], IT=['Atto']),
    'L_A_A': dict(UK=['Prelude'], ES=['Preludio'], FR=['Prêlude'], DE=['Prolog'], IT=['Prologo']),
    'L_A_B': dict(UK=['Epilogue'], ES=['Epílogo'], FR=['Épilogue'], DE=['Epilog'], IT=['Epilogo']),
}
for n in range(8):
    OFFICIAL[f'L_A_{n}'] = dict(UK=[str(n), 'Ch.'], ES=[str(n), 'Cap.'], FR=[str(n), 'Ch.'], DE=[str(n), 'Kap.'],
                                IT=[str(n), 'Cap.'])
for n in range(1, 7):
    OFFICIAL[f'L_B_{n}'] = {language: [str(n)] for language in LANGUAGES}

# Portuguese of each English line (corpus SystemMsg book titles, glossary Act/Prelude/Epilogue/Chapter).
PORTUGUESE = {
    'VALKYRIE': 'VALQUÍRIA', 'THE POOKA PRINCE': 'O PRÍNCIPE POOKA', 'FAIRY LAND': 'TERRA DAS FADAS',
    'THE BLACK SWORD': 'A ESPADA NEGRA', 'FATE': 'DESTINO', 'ARMAGEDDON': 'ARMAGEDOM', 'WHEEL OF FATE': 'RODA DO DESTINO',
    'Act': 'Ato', 'Prelude': 'Prelúdio', 'Epilogue': 'Epílogo', 'Ch.': 'Cap.',
}
MARKS = {'́': 'acute', '̀': 'grave', '̂': 'circumflex', '̈': 'diaeresis'}
SETTLED = 1


class ActError(Exception):
    pass


def base_name(anim):
    for suffix in ('_1', '_00'):
        if anim.endswith(suffix) and anim[:-len(suffix)] in OFFICIAL:
            return anim[:-len(suffix)]
    return anim


def channel_layers(layout, channel):
    """(scatter, settled, exit) layers of a word channel."""
    segments = list(layout.channel_segments(channel))
    layers = [layout.segment_layer(s) for s in segments]
    if len(layers) < 3:
        raise ActError(f'channel {channel} has {len(layers)} segments')
    return layers[0], layers[1], layers[-1]


def word_channels(layout, anim):
    first, count = next((f, c) for n, f, c in layout.anims() if n == anim)
    return list(range(first, first + count))


def boxes(layout, layer):
    return [(k, LT.quad_box(layout.geom(k['geom']))) for k in layout.layer_keys(layer)]


def split_marks(items):
    """(letters, marks) of a settled line: a mark is a key that sits wholly above the line's letter tops."""
    if not items:
        return [], []
    tops = median(b[1] for _, b in items)
    heights = median(b[3] - b[1] for _, b in items)
    marks = [(k, b) for k, b in items if b[3] <= tops + 0.25 * heights and b[3] - b[1] < 0.75 * heights]
    letters = sorted([(k, b) for k, b in items if (k, b) not in marks], key=lambda kb: kb[1][0])
    return letters, marks


def decompose(text):
    """[(base char, [mark names])] of a word, spaces dropped."""
    out = []
    for ch in unicodedata.normalize('NFD', text):
        if ch in MARKS:
            out[-1][1].append(MARKS[ch])
        elif not ch.isspace():
            out.append([ch, []])
    return out


@dataclass
class Piece:
    part: bytes
    raw: bytes                 # a key drawing it (sprite id, texture)
    w: float
    h: float
    bottom: float              # below the baseline
    kind: str                  # 'cap' or 'low': the scale class of the line it came from
    anim: str
    unit: float                # height of the flat letters of its line


class Pool:
    def __init__(self):
        self.letters = defaultdict(list)       # (char, kind) -> [Piece]
        self.marks = defaultdict(list)         # (mark, capital base) -> [(part, raw, dx, lift, w, h)]
        self.gaps = defaultdict(list)          # kind or animation -> letter gaps, in flat-letter heights
        self.spaces = defaultdict(list)
        self.lines = defaultdict(list)         # (anim, channel index) -> [(text, box)]
        self.skipped = []

    def letter(self, char, kind, anim):
        """The letter as the animation draws it, else as another line of its kind does (to be scaled)."""
        options = self.letters.get((char, kind)) or self.letters.get((char, 'cap' if kind == 'low' else 'low'))
        if not options:
            raise ActError(f'no {char!r} in the glyph font')
        return next((p for p in options if p.anim == anim), options[0])

    def spacing(self, table, kind, anim):
        values = table.get(anim) or table.get(kind)
        return median(values) if values else 0.0


def line_kind(text):
    return 'cap' if text.isupper() or text.isdigit() else 'low'


def learn(layouts):
    """The pool of every settled line of the five European layouts, labelled by OFFICIAL."""
    pool = Pool()
    for language, layout in layouts.items():
        for anim, by_language in OFFICIAL.items():
            texts = by_language[language]
            channels = word_channels(layout, anim)
            if len(channels) != len(texts):
                raise ActError(f'{language} {anim}: {len(channels)} channels for {len(texts)} words')
            for index, (channel, text) in enumerate(zip(channels, texts)):
                _, settled, _ = channel_layers(layout, channel)
                letters, marks = split_marks(boxes(layout, settled))
                spelled = decompose(text)
                if len(letters) != len(spelled) or len(marks) != sum(len(m) for _, m in spelled):
                    pool.skipped.append(f'{language} {anim}: the keys do not spell {text!r}')
                    continue
                kind = line_kind(text)
                flat = [b for (k, b), (c, _) in zip(letters, spelled) if c not in ',.gjpqy']
                baseline = median(b[3] for b in flat)
                unit = median(b[3] - b[1] for b in flat)
                pool.lines[(anim, index)].append((text, (min(b[0] for _, b in letters), min(b[1] for _, b in letters),
                                                         max(b[2] for _, b in letters), max(b[3] for _, b in letters))))
                for (k, b), (c, ms) in zip(letters, spelled):
                    c = 'ı' if c == 'i' and ms else c
                    piece = Piece(bytes(layout.record(M.PARTS, k['part'])), k['raw'], b[2] - b[0], b[3] - b[1],
                                  b[3] - baseline, kind, anim, unit)
                    if not any(p.part == piece.part and p.anim == anim for p in pool.letters[(c, kind)]):
                        pool.letters[(c, kind)].append(piece)
                words = text.split()
                starts, n = set(), 0
                for w in words:
                    starts.add(n)
                    n += len(decompose(w))
                for i in range(1, len(letters)):
                    gap = (letters[i][1][0] - letters[i - 1][1][2]) / unit
                    table = pool.spaces if i in starts else pool.gaps
                    table[kind].append(gap)
                    table[anim].append(gap)
                queue = [(c, m) for c, ms in spelled for m in ms]
                bases = [(c, b) for (k, b), (c, ms) in zip(letters, spelled) for _ in ms]
                for (mk, mb), (c, m), (_, bb) in zip(sorted(marks, key=lambda kb: kb[1][0]), queue, bases):
                    dx = ((mb[0] + mb[2]) / 2 - (bb[0] + bb[2]) / 2) / unit
                    lift = (bb[1] - mb[3]) / unit
                    pool.marks[(m, c.isupper())].append((bytes(layout.record(M.PARTS, mk['part'])), mk['raw'], dx, lift,
                                                         (mb[2] - mb[0]) / unit, (mb[3] - mb[1]) / unit))
    return pool


def official_line(layouts, anim, index, text):
    """(language, settled keys) of an official line that already spells `text`."""
    for language, layout in layouts.items():
        if OFFICIAL[anim][language][index] == text:
            channel = word_channels(layout, anim)[index]
            return language, layout, channel
    return None


def set_line(pool, text, centre_x, baseline, unit, kind, anim):
    """[(part, raw, (x0, y0, x1, y1))] of `text` on `baseline`, its flat letters `unit` tall, centred on `centre_x`."""
    pieces, x = [], 0.0
    gap = pool.spacing(pool.gaps, kind, anim) * unit
    space = pool.spacing(pool.spaces, kind, anim) * unit or unit * 0.5
    words = text.split(' ')
    for wi, word in enumerate(words):
        if wi:
            x += space
        for li, (char, marks) in enumerate(decompose(word)):
            base = ('ı' if (char == 'i' and marks and (('ı', kind) in pool.letters)) else 'I' if char == 'i' and marks
                    else char)
            p = pool.letter(base, kind, anim)
            k = unit / p.unit
            if li:
                x += gap
            box = (x, baseline + (p.bottom - p.h) * k, x + p.w * k, baseline + p.bottom * k)
            pieces.append((p.part, p.raw, box))
            for m in marks:
                options = pool.marks.get((m, char.isupper())) or pool.marks.get((m, not char.isupper()))
                if not options:
                    raise ActError(f'no {m} mark in the glyph font')
                part, raw, dx, lift, w, h = options[0]
                cx = (box[0] + box[2]) / 2 + dx * unit
                pieces.append((part, raw, (cx - w * unit / 2, box[1] - (lift + h) * unit, cx + w * unit / 2,
                                           box[1] - lift * unit)))
            x = box[2]
    shift = centre_x - x / 2
    return [(part, raw, (b[0] + shift, b[1], b[2] + shift, b[3])) for part, raw, b in pieces]


SQUEEZE = 0.9


def squeezed(items, centre, q):
    """Items narrowed horizontally about `centre` by q, as the game squeezes a long label."""
    return [(part, raw, (centre + (b[0] - centre) * q, b[1], centre + (b[2] - centre) * q, b[3])) for part, raw, b in items]


def corners(box):
    x0, y0, x1, y1 = box
    return [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]


def geom_record(box):
    c = corners(box)
    return struct.pack('<12f', (box[0] + box[2]) / 2, (box[1] + box[3]) / 2, *[v for p in c + c[:1] for v in p])


class Writer:
    """Appends keys, quads and parts to a layout and points layers at the new keys."""

    def __init__(self, layout):
        self.layout = layout
        self.parts = {bytes(layout.record(M.PARTS, i)): i for i in range(layout.count(M.PARTS))}
        self.keys, self.geoms, self.new_parts, self.layers = [], [], [], {}

    def part(self, record):
        if record not in self.parts:
            self.parts[record] = self.layout.count(M.PARTS) + len(self.new_parts)
            self.new_parts.append(record)
        return self.parts[record]

    def layer(self, layer, items, template, colour):
        """items: [(part record, raw key it was drawn with, box)]."""
        first = self.layout.count(M.KEYS) + len(self.keys)
        for part, raw, box in items:
            out = bytearray(template)
            out[0:4] = raw[0:4]
            out[6:8] = raw[6:8]
            struct.pack_into('<HHH', out, 10, self.part(part), colour, self.layout.count(M.GEOMS) + len(self.geoms))
            self.geoms.append(geom_record(box))
            self.keys.append(bytes(out))
        bbox = (min(b[0] for _, _, b in items), min(b[1] for _, _, b in items),
                max(b[2] for _, _, b in items), max(b[3] for _, _, b in items))
        self.layers[layer] = (first, len(items), bbox)

    def finish(self):
        if self.new_parts:
            self.layout.append(M.PARTS, self.new_parts)
        self.layout.append(M.GEOMS, self.geoms)
        self.layout.append(M.KEYS, self.keys)
        for layer, (first, count, bbox) in self.layers.items():
            self.layout.set_layer(layer, first, count, bbox)


def scatter_of(layout, scatter, settled, items):
    """Scattered boxes for new settled items: each takes the displacement of the English letter at the same place in
    the word, so the letters fly in the way the English ones do."""
    old_s = [LT.quad_box(layout.geom(k['geom'])) for k in layout.layer_keys(scatter)]
    old_t = [LT.quad_box(layout.geom(k['geom'])) for k in layout.layer_keys(settled)]
    moves = [((a[0] + a[2] - b[0] - b[2]) / 2, (a[1] + a[3] - b[1] - b[3]) / 2) for a, b in zip(old_s, old_t)]
    order = sorted(range(len(old_t)), key=lambda i: old_t[i][0])
    moves = [moves[i] for i in order] or [(0.0, 0.0)]
    out = []
    xs = sorted(range(len(items)), key=lambda i: items[i][2][0])
    rank = {i: r for r, i in enumerate(xs)}
    for i, (part, raw, b) in enumerate(items):
        r = rank[i]
        j = round(r * (len(moves) - 1) / max(1, len(items) - 1))
        dx, dy = moves[j]
        out.append((part, raw, (b[0] + dx, b[1] + dy, b[2] + dx, b[3] + dy)))
    return out


def stretched(items):
    out = []
    for part, raw, b in items:
        cx, w = (b[0] + b[2]) / 2, b[2] - b[0]
        out.append((part, raw, (cx - 1.5 * w, b[1], cx + 1.5 * w, b[3])))
    return out


def copied(layout, channel):
    """The three layers of an official channel as items."""
    scatter, settled, exit_ = channel_layers(layout, channel)
    return [[(bytes(layout.record(M.PARTS, k['part'])), k['raw'], LT.quad_box(layout.geom(k['geom'])))
             for k in layout.layer_keys(layer)] for layer in (scatter, settled, exit_)]


def designs(layouts, pool, report):
    """{(animation, channel index): (scatter, settled, exit) items} of every translated line, set over the European
    English layout's lines."""
    english = layouts['UK']
    out = {}
    for name, by_language in OFFICIAL.items():
        channels = word_channels(english, name)
        for index, (channel, text) in enumerate(zip(channels, by_language['UK'])):
            portuguese = PORTUGUESE.get(text, text)
            if portuguese == text:
                continue
            official = official_line(layouts, name, index, portuguese)
            if official:
                language, other, other_channel = official
                out[(name, index)] = copied(other, other_channel)
                report.append(('design', name, text, portuguese, f'ok: copied from {language}'))
                continue
            scatter, settled, _ = channel_layers(english, channel)
            letters, _ = split_marks(boxes(english, settled))
            kind = line_kind(text)
            flat = [b for (k, b), (c, _) in zip(letters, decompose(text)) if c not in ',.gjpqy']
            centre = (min(b[0] for _, b in letters) + max(b[2] for _, b in letters)) / 2
            try:
                settled_items = set_line(pool, portuguese, centre, median(b[3] for b in flat),
                                         median(b[3] - b[1] for b in flat), kind, name)
            except ActError as e:
                report.append(('design', name, text, portuguese, f'BLOCKED: {e}'))
                continue
            width = max(b[2] for _, _, b in settled_items) - min(b[0] for _, _, b in settled_items)
            budget = max(b[2] - b[0] for _, b in pool.lines[(name, index)])
            if width * SQUEEZE > budget:
                report.append(('design', name, text, portuguese, f'BLOCKED: {width:.0f} units, official widest {budget:.0f}'))
                continue
            if width > budget:
                settled_items = squeezed(settled_items, centre, budget / width)
                width = budget
            out[(name, index)] = (scatter_of(english, scatter, settled, settled_items), settled_items, stretched(settled_items))
            report.append(('design', name, text, portuguese, f'ok: set, {width:.0f} of {budget:.0f} units'))
    return out


def respell(data, plan, where):
    """A build's layout with every designed L_* channel written over its own layers; None when nothing changes."""
    layout = M.Mbs(data)
    if not respell_layout(layout, plan, where):
        return None, None
    return layout.bytes(), layout.inserts


def respell_layout(layout, plan, where):
    """Writes every designed L_* channel over its own layers of `layout`, in place; False when nothing changes."""
    writer = Writer(layout)
    done = set()
    for anim, _, _ in layout.anims():
        name = base_name(anim)
        if name not in OFFICIAL:
            continue
        channels = word_channels(layout, anim)
        if len(channels) != len(OFFICIAL[name]['UK']):
            raise ActError(f'{where} {anim}: {len(channels)} channels')
        for index, channel in enumerate(channels):
            if (name, index) not in plan:
                continue
            layers = channel_layers(layout, channel)
            if layers[1] in done:
                continue
            template = layout.key(layout.layer_range(layers[1])[0])['raw']
            for layer, items in zip(layers, plan[(name, index)]):
                writer.layer(layer, items, template, layout.key(layout.layer_range(layer)[0])['colour'])
            done.update(layers)
    if not writer.keys:
        return False
    writer.finish()
    _grow_anim_boxes(layout)
    return True


def _grow_anim_boxes(layout):
    """Each L_* animation's box covers its layers at the largest scale its segments use."""
    for i in range(layout.count(M.ANIMS)):
        raw = bytearray(layout.record(M.ANIMS, i))
        name = bytes(raw[16:40]).split(b'\0')[0].decode('latin1')
        if base_name(name) not in OFFICIAL:
            continue
        first, count = struct.unpack_from('<H', raw, 40)[0], raw[42]
        box = list(struct.unpack_from('<4f', raw, 0))
        for ch in range(first, first + count):
            for s in layout.channel_segments(ch):
                layer = layout.segment_layer(s)
                x, y, sx, sy = layout.segment_transform(s)
                lb = struct.unpack_from('<4f', layout.record(M.LAYERS, layer), 0)
                box = [min(box[0], x + lb[0] * sx), min(box[1], y + lb[1] * sy),
                       max(box[2], x + lb[2] * sx), max(box[3], y + lb[3] * sy)]
        struct.pack_into('<4f', raw, 0, *box)
        layout.put(M.ANIMS, i, bytes(raw))


def read_entry(prepared, build, archives, entry):
    for name in archives:
        path = Path(prepared) / build / name
        if not path.is_file():
            continue
        pack = cpk.Cpk(path, original=True)
        try:
            hit = next((e for e in pack.entries if e['path'].lower() == entry.lower()), None)
            if hit:
                return pack.read(hit)
        finally:
            pack.f.close()
    return None


def atlas_alpha(data):
    return [visuals.decode(data, t)[..., 3] for t in ftex.textures(data)]


def part_rect(record):
    f = struct.unpack('<12f', record)
    return int(min(f[2:10:2])), int(min(f[3:10:2])), int(max(f[2:10:2])), int(max(f[3:10:2]))


def moved_part(record, dx, dy):
    f = list(struct.unpack('<12f', record))
    for i in range(0, 12, 2):
        f[i] += dx
        f[i + 1] += dy
    return struct.pack('<12f', *f)


def same_pixels(a, b):
    return a.shape == b.shape and a.size and ((a > 128) == (b > 128)).mean() >= 0.99


def find_sprite(texture, sprite):
    """(x, y) where the alpha of `texture` holds `sprite` exactly, or None."""
    from scipy.signal import fftconvolve
    t = sprite.astype(np.float32) / 255
    image = texture.astype(np.float32) / 255
    h, w = t.shape
    corr = fftconvolve(image, t[::-1, ::-1], mode='valid')
    energy = fftconvolve(image * image, np.ones_like(t), mode='valid')
    ssd = energy - 2 * corr + float((t * t).sum())
    y, x = np.unravel_index(int(np.argmin(ssd)), ssd.shape)
    if not same_pixels(texture[y:y + h, x:x + w], sprite):
        return None
    return int(x), int(y)


def relocate(plan, data, reference, target, known=None, mover=None):
    """The plan with every glyph pointing at the same sprite in a build's own atlas: where the build's layout draws the
    same sprite id, else where its atlas holds the same pixels, else where `known` ({reference rect: (target rect,
    quarter turns)}) has found it, moved by `mover`. Raises ActError for a glyph the atlas lacks."""
    layout = M.Mbs(data)
    by_sprite = defaultdict(list)
    for k in range(layout.count(M.KEYS)):
        key = layout.key(k)
        if key['part'] < layout.count(M.PARTS):
            by_sprite[key['sprite']].append((key['texture'], bytes(layout.record(M.PARTS, key['part']))))
    moved = {}

    def place(record, raw):
        """(part, key) drawing the reference sprite in the target atlas; the sprite may sit in another texture there."""
        texture = raw[7]
        if (record, texture) in moved:
            return moved[(record, texture)]
        x0, y0, x1, y1 = part_rect(record)
        sprite = reference[texture][y0:y1, x0:x1]
        found = None
        if texture < len(target) and same_pixels(target[texture][y0:y1, x0:x1], sprite):
            found = record, texture
        candidates = sorted(by_sprite.get(struct.unpack_from('<I', raw)[0], []), key=lambda c: c[0] != texture)
        for t, candidate in candidates:
            cx0, cy0, cx1, cy1 = part_rect(candidate)
            if found is None and t < len(target) and same_pixels(target[t][cy0:cy1, cx0:cx1], sprite):
                found = moved_part(record, cx0 - x0, cy0 - y0), t
        for t in sorted(range(len(target)), key=lambda t: t != texture):
            if found is not None:
                break
            if target[t].shape[0] >= sprite.shape[0] and target[t].shape[1] >= sprite.shape[1]:
                at = find_sprite(target[t], sprite)
                if at is not None:
                    found = moved_part(record, at[0] - x0, at[1] - y0), t
        hit = known.get((texture, x0, y0, x1, y1)) if known else None
        if found is None and hit:
            (t, *rect), turns = hit
            found = mover(record, (texture, x0, y0, x1, y1), (t, *rect), turns), t
        if found is None:
            raise ActError(f'the atlas has no sprite {x0, y0, x1, y1} of texture {texture}')
        part, t = found
        key = bytearray(raw)
        key[7] = t
        moved[(record, texture)] = part, bytes(key)
        return moved[(record, texture)]

    return {key: tuple([(*place(part, raw), box) for part, raw, box in layer] for layer in layers)
            for key, layers in plan.items()}


SOURCES = (('CUSA05262-1.01', 'Odin2_EU_PATCH1_RE_UK.CPK'), ('CUSA05262-1.01', 'Odin2_RE_UK.CPK'),
           ('CUSA05262-1.00', 'Odin2_RE_UK.CPK'), ('CUSA05083-1.00', 'Odin2_RE_US.CPK'),
           ('CUSA01290-1.00', 'Odin2_RE.CPK'), ('CUSA01290-1.02', 'Odin2_RE.CPK'))


CLASSIC_LAYOUT, CLASSIC_ATLAS = 'Interface.mbs', 'Interface.ftx'
CLASSIC_ARCHIVES = ('Odin2_EU_PATCH1_OR_{}.CPK', 'Odin2_OR_{}.CPK')


def design(prepared, layout_name, archives, atlas_name, reference_archive):
    """(plan, reference atlas alpha, report) of the L_* titles of a layout family, from its five European layouts."""
    layouts = {L: M.Mbs(read_entry(prepared, EUROPE, [a.format(L) for a in archives], layout_name)) for L in LANGUAGES}
    pool = learn(layouts)
    report = [('pool', '', '', '', f'skipped: {line}') for line in pool.skipped]
    plan = designs(layouts, pool, report)
    return plan, atlas_alpha(read_entry(prepared, EUROPE, [reference_archive], atlas_name)), report, layouts


def classic_design(prepared):
    """The Classic act titles (L_* of Interface.mbs), which the interface patch writes with its labels."""
    return design(prepared, CLASSIC_LAYOUT, CLASSIC_ARCHIVES, CLASSIC_ATLAS, CLASSIC_ARCHIVES[0].format('UK'))[:3]


def generate(prepared, sources=SOURCES):
    plan, reference, report, layouts = design(prepared, LAYOUT, EUROPE_ARCHIVES, ATLAS, 'Odin2_RE_UK.CPK')
    targets = {}
    for build, archive in sources:
        where = f'{build} {archive}'
        data = read_entry(prepared, build, [archive], LAYOUT)
        if data is None:
            report.append((where, '', '', '', 'DATA UNAVAILABLE'))
            continue
        key = (LAYOUT, zlib.crc32(data))
        if key in targets:
            report.append((where, '', '', '', 'ok: same layout as an earlier build'))
            continue
        pack_atlas = read_entry(prepared, build, [archive.replace('EU_PATCH1_', '')], ATLAS)
        try:
            if pack_atlas is None:
                raise ActError('no stage atlas next to the layout')
            new, inserts = respell(data, relocate(plan, data, reference, atlas_alpha(pack_atlas)), where)
        except ActError as e:
            report.append((where, '', '', '', f'BLOCKED: {e}'))
            continue
        if new is None:
            continue
        targets[key] = (data, new, inserts)
        report.append((where, '', '', '', f'ok: {len(data)} -> {len(new)} bytes'))
    return targets, report, layouts


def preview(targets, prepared, directory):
    import mbs_render as R
    from PIL import Image, ImageDraw
    atlas_data = read_entry(prepared, EUROPE, ['Odin2_RE_UK.CPK'], ATLAS)
    atlases = {i: visuals.decode(atlas_data, t) for i, t in enumerate(ftex.textures(atlas_data))}
    tiles = []
    for (name, crc), (_, new, _) in list(targets.items())[:1]:
        layout = M.Mbs(new)
        for anim, first, count in layout.anims():
            if base_name(anim) not in OFFICIAL or anim.endswith('_1'):
                continue
            layers = [channel_layers(layout, ch)[1] for ch in range(first, first + count)]
            image, _ = R.draw(layout, atlases, layers=layers, origin=(-160, -40), size=(320, 90))
            tiles.append((anim, R.on_background(image)))
    sheet = Image.new('RGB', (2 * 324, ((len(tiles) + 1) // 2) * 106), (20, 10, 20))
    draw = ImageDraw.Draw(sheet)
    for n, (anim, tile) in enumerate(tiles):
        x, y = (n % 2) * 324, (n // 2) * 106
        sheet.paste(Image.fromarray(tile), (x + 2, y + 14))
        draw.text((x + 4, y + 1), anim, fill=(255, 255, 0))
    directory.mkdir(parents=True, exist_ok=True)
    sheet.save(directory / 'stage-acts.png')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--prepared', default=str(ROOT / 'game' / 'prepared'))
    ap.add_argument('--out', default=str(DEFAULT_OUT))
    ap.add_argument('--report')
    ap.add_argument('--preview')
    args = ap.parse_args()
    visuals.use_native_reader()
    targets, report, _ = generate(args.prepared)
    blob = oslv.encode([(name, old, new, inserts) for (name, _), (old, new, inserts) in targets.items()])
    Path(args.out).write_bytes(blob)
    print(f'{len(targets)} layouts -> {args.out} ({len(blob)} bytes)')
    if args.report:
        with open(args.report, 'w', encoding='utf-8', newline='\n') as f:
            f.write('where\tanim\tenglish\tpt_br\tresult\n' + ''.join('\t'.join(r) + '\n' for r in report))
    if args.preview:
        preview(targets, args.prepared, Path(args.preview))
    blocked = [r for r in report if 'BLOCKED' in r[4]]
    for r in blocked:
        print(*r)
    return 1 if blocked else 0


if __name__ == '__main__':
    sys.exit(main())
