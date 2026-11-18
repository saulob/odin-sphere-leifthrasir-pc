"""Stage and location title cards (GUI/HD_StageNameNN.ftx, Classic NN.ftx) in Portuguese (Brazil).

A Refined card pack holds one title in three renderings in its first 1024x1024 texture: the lettering as a blue glow
on transparency (C), the same lettering cut out of a parchment (A, alpha only) and the same lettering turned a quarter
and painted brown on parchment (B). Only the blue one is complete art on its own, so the lettering is made once as an
alpha mask plus blue glyph pixels and the other two are derived from it. The letters come from the player's own cards of
every European language (calligraphy of one style, cut apart and reused at their own scale).
"""
import csv
import itertools
import re
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy import ndimage
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'prompts'))
import cpk  # noqa: E402
import ftex  # noqa: E402
import visual_font as VF  # noqa: E402
import visuals  # noqa: E402

ROOT = HERE.parent.parent
TABLE = ROOT / 'localization' / 'pt-BR' / 'visual' / 'stage-cards.tsv'
REGION_A = (1, 1, 644, 376)
REGION_C = (1, 378, 644, 753)
REGION_B = (646, 1, 1021, 644)
LANGUAGES = ('UK', 'ES', 'FR', 'DE', 'IT')
FLAT = set('aceimnorsuvwxzbdhklt')
XHEIGHT = set('aceomnrsuvwxz')
STYLE = 'stage-card'
PAD = 6
THRESHOLD = 64
DOT_AREA, DOT_SIZE = 400, 32


def read_titles():
    with open(TABLE, encoding='utf-8', newline='') as f:
        return list(csv.DictReader(f, delimiter='\t'))


def region(texture, box):
    x0, y0, x1, y1 = box
    return texture[y0:y1, x0:x1]


def read_cards(prepared, language, build='CUSA05262-1.01'):
    """{card: dict(A, B, C, fingerprint)} of a language's Refined cards, as the archive holds them."""
    pack = cpk.Cpk(Path(prepared) / build / f'Odin2_RE_{language}.CPK', original=True)
    out = {}
    try:
        for entry in pack.entries:
            m = re.match(r'(?i)^GUI/HD_StageName(\d\w*)\.ftx$', entry['path'])
            if not m:
                continue
            data = pack.read(entry)
            texture = visuals.decode(data, ftex.textures(data)[0])
            out[m.group(1)] = dict(A=region(texture, REGION_A), B=region(texture, REGION_B), C=region(texture, REGION_C),
                                   crc32=zlib.crc32(data), path=entry['path'])
    finally:
        pack.f.close()
    return out


@dataclass
class Piece:
    box: list
    labels: list
    area: int
    char: str = None
    marks: list = None
    base_box: list = None

    @property
    def width(self):
        return self.box[2] - self.box[0]

    @property
    def height(self):
        return self.box[3] - self.box[1]


def pieces_of(alpha):
    """(label image, glyph pieces): connected ink, with the dot or accent of a letter merged into it."""
    lab, _ = ndimage.label(alpha > THRESHOLD, structure=np.ones((3, 3)))
    found = []
    for i, o in enumerate(ndimage.find_objects(lab), 1):
        area = int((lab[o] == i).sum())
        found.append(Piece([o[1].start, o[0].start, o[1].stop, o[0].stop], [i], area))
    small = [p for p in found if p.area < DOT_AREA and p.width <= DOT_SIZE and p.height <= DOT_SIZE]
    big = [p for p in found if p not in small]
    for dot in small:
        cx = (dot.box[0] + dot.box[2]) / 2
        near = [b for b in big if b.box[0] - 10 <= cx <= b.box[2] + 10 and 0 <= b.box[1] - dot.box[3] + 6 <= 40]
        if not near:
            big.append(dot)
            continue
        target = min(near, key=lambda b: abs((b.box[0] + b.box[2]) / 2 - cx))
        if target.marks is None:
            target.marks, target.base_box = [], list(target.box)
        target.marks.append(dot)
        target.box = [min(target.box[0], dot.box[0]), min(target.box[1], dot.box[1]),
                      max(target.box[2], dot.box[2]), max(target.box[3], dot.box[3])]
        target.labels += dot.labels
        target.area += dot.area
    return lab, big


def chains(pieces):
    """Pieces grouped into words: neighbours on one baseline, left to right."""
    parent = list(range(len(pieces)))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for i, a in enumerate(pieces):
        for j in range(i + 1, len(pieces)):
            b = pieces[j]
            gap = max(a.box[0], b.box[0]) - min(a.box[2], b.box[2])
            overlap = min(a.box[3], b.box[3]) - max(a.box[1], b.box[1])
            if gap <= 14 and overlap >= 0.25 * min(a.height, b.height):
                parent[find(i)] = find(j)
    groups = {}
    for i in range(len(pieces)):
        groups.setdefault(find(i), []).append(pieces[i])
    return [sorted(g, key=lambda p: p.box[0]) for g in groups.values()]


def assign_words(words, groups, score=None):
    """Chains matched to the title's words by length, then by reading order or, when several words share a length, by the
    best shape score of `score(char, piece)`; None when the chains do not fit the words."""
    if sorted(len(g) for g in groups) != sorted(len(w) for w in words):
        return None
    by_length = {}
    for g in groups:
        by_length.setdefault(len(g), []).append(g)
    out = {}
    for length, chains_ in by_length.items():
        indexes = [i for i, w in enumerate(words) if len(w) == length]
        if len(indexes) == 1 or len({words[i] for i in indexes}) == 1:
            for i, g in zip(indexes, sorted(chains_, key=lambda g: (g[0].box[1], g[0].box[0]))):
                out[i] = g
            continue
        if score is None:
            return None
        best = None
        for order in itertools.permutations(chains_):
            total = sum(score(c, p) for i, g in zip(indexes, order) for c, p in zip(words[i], g))
            if best is None or total > best[0]:
                best = (total, order)
        for i, g in zip(indexes, best[1]):
            out[i] = g
    return [(words[i], out[i]) for i in range(len(words))]


def vector(alpha, box):
    x0, y0, x1, y1 = box
    crop = Image.fromarray(alpha[y0:y1, x0:x1]).resize((32, 32), Image.BILINEAR)
    v = np.asarray(crop, np.float32).ravel()
    return v / (np.linalg.norm(v) + 1e-6)


def card_geometry(labelled):
    """x-height of a card from its labelled pieces, or None when it shows too few letters of x-height."""
    heights = [p.height for _, group in labelled for p in group if p.char in XHEIGHT]
    return float(np.median(heights)) if len(heights) >= 3 else None


def glyph_of(rgba, lab, piece, char, scale, drop, source):
    x0, y0, x1, y1 = piece.box
    px0, py0 = max(0, x0 - PAD), max(0, y0 - PAD)
    px1, py1 = min(rgba.shape[1], x1 + PAD), min(rgba.shape[0], y1 + PAD)
    mask = np.isin(lab[py0:py1, px0:px1], piece.labels)
    keep = ndimage.binary_dilation(mask, iterations=PAD - 2)
    patch = rgba[py0:py1, px0:px1].copy()
    patch[~keep, 3] = 0
    return VF.VisualGlyph(char, patch, (x0 - px0, y0 - py0, x1 - px0, y1 - py0), scale, drop, source)


TILDE = dict(card='12', crc32=0xcdb813ad, piece=[460, 136, 500, 199], cut=152)


def mark_kind(lab, mark):
    """'acute', 'grave' (None: unused) or 'circumflex' from how a small piece leans: its upper half against its lower."""
    x0, y0, x1, y1 = mark.box
    mask = np.isin(lab[y0:y1, x0:x1], mark.labels)
    ys, xs = np.nonzero(mask)
    if len(xs) < 20:
        return None
    mid = (ys.min() + ys.max()) / 2
    lean = (xs[ys <= mid].mean() - xs[ys > mid].mean()) / max(1, mark.width)
    if abs(lean) < 0.08 and mark.width >= 22:
        return 'circumflex'
    return 'acute' if lean > 0.08 else None


def mark_orientation(lab, mark):
    """Where the weight of a small piece lies, 0 (top) to 1 (bottom): a dot hangs from its top, an acute rests on its base."""
    x0, y0, x1, y1 = mark.box
    mask = np.isin(lab[y0:y1, x0:x1], mark.labels)
    rows = np.nonzero(mask)[0]
    return float(rows.mean() / max(1, mask.shape[0] - 1)) if len(rows) else 0.5


def is_circumflex(piece):
    return 24 <= piece.width <= 44 and 14 <= piece.height <= 30 and 150 <= piece.area <= 900 and piece.width / piece.height >= 1.2


def base_below(found, piece):
    cx = (piece.box[0] + piece.box[2]) / 2
    options = [b for b in found if b is not piece and b.box[1] >= piece.box[3] - 4 and b.box[0] <= cx <= b.box[2]]
    return min(options, key=lambda b: b.box[1] - piece.box[3]) if options else None


def word_baseline(group):
    flat = [p.box[3] for p in group if p.char in FLAT]
    return float(np.median(flat)) if flat else None


PINS = (('DE', '01', 0x85e5dd3e, 'U', [43, 52, 150, 203], 62.0), ('ES', '16', 0xd52d9dbc, 'J', [53, 20, 119, 173], 62.0),
        ('DE', '36', 0x2e679162, 'é', [254, 108, 308, 192], 62.0))


class PoolBuilder:
    def __init__(self, titles):
        self.titles = {r['card']: r['english'].split() for r in titles}
        self.pool = VF.VisualGlyphPool(STYLE)
        self.bank = []
        self.report = []
        self.gap_samples = {}
        self.scales = {}
        self.mark_samples = {}

    def label_known(self, language, cards):
        """Cards whose title is known (the English ones) labelled by alignment to their words; cards that need shapes to
        tell equal-length words apart are done after the others."""
        later = []
        for score in (None, self.shape_score):
            for card, art in (list(cards.items()) if score is None else later):
                words = self.titles.get(card)
                if not words:
                    continue
                lab, found = pieces_of(art['C'][..., 3])
                assigned = assign_words(words, chains(found), None if score is None else lambda c, p, a=art: score(a['C'][..., 3], c, p))
                if assigned is None:
                    if score is None:
                        later.append((card, art))
                    else:
                        self.report.append(f'{language} {card}: pieces do not align to the title')
                    continue
                for word, group in assigned:
                    for char, piece in zip(word, group):
                        piece.char = char
                self.take(language, card, art, lab, assigned)

    def shape_score(self, alpha, char, piece):
        v = vector(alpha, piece.box)
        return max((float(v @ vec) for c, vec, _ in self.bank if c == char), default=0.0)

    def label_matched(self, language, cards, minimum=0.93):
        """Cards of other languages: pieces named by the shape of the labelled English ones."""
        for card, art in cards.items():
            alpha = art['C'][..., 3]
            lab, found = pieces_of(alpha)
            for piece in found:
                piece.char = self.match(alpha, piece, minimum)
            assigned = [([p.char for p in g], g) for g in chains(found) if all(p.char for p in g)]
            if assigned:
                self.take(language, card, art, lab, assigned)

    def match(self, alpha, piece, minimum):
        if not self.bank or piece.width < 6 or piece.height < 8:
            return None
        v = vector(alpha, piece.box)
        aspect = piece.width / piece.height
        best = {}
        for char, vec, asp in self.bank:
            if abs(asp / aspect - 1) > 0.09:
                continue
            s = float(v @ vec)
            if s > best.get(char, 0):
                best[char] = s
        if not best:
            return None
        ranked = sorted(best.items(), key=lambda kv: -kv[1])
        if ranked[0][1] < minimum or (len(ranked) > 1 and ranked[0][1] - ranked[1][1] < 0.015):
            return None
        return ranked[0][0]

    def take(self, language, card, art, lab, assigned):
        scale = card_geometry(assigned)
        self.scales[(language, card)] = scale
        if scale is None:
            self.report.append(f'{language} {card}: too few letters of x-height to scale')
            return
        for word, group in assigned:
            base = word_baseline(group)
            if base is None:
                continue
            for char, piece in zip(word, group):
                if piece.char is None:
                    continue
                self.pool.add(glyph_of(art['C'], lab, piece, piece.char, scale, (piece.box[3] - base) / scale,
                                       f'{language} {card} {piece.box}'))
                if language == 'UK':
                    self.bank.append((piece.char, vector(art['C'][..., 3], piece.box), piece.width / piece.height))
            for a, b in zip(group, group[1:]):
                if a.char and b.char:
                    key = (VF.glyph_class(a.char), VF.glyph_class(b.char))
                    self.gap_samples.setdefault(key, []).append((b.box[0] - a.box[2]) / scale)
            for p in group:
                if p.char and p.char.isupper():
                    self.cap_samples.append(p.height / scale)


    def pin_letters(self, cards):
        """Letters English lacks, cut from pieces named by card fingerprint and rectangle."""
        for language, card, crc32, char, box, scale in PINS:
            art = cards.get(language, {}).get(card)
            if art is None or art['crc32'] != crc32:
                self.report.append(f'{char}: the {language} card {card} it is cut from is not the expected one')
                continue
            lab, found = pieces_of(art['C'][..., 3])
            piece = next((p for p in found if p.box == box), None)
            if piece is None:
                self.report.append(f'{char}: no piece {box} in the {language} card {card}')
                continue
            glyph = glyph_of(art['C'], lab, piece, char, scale, None if char.isupper() else 0.0, f'{language} {card} {box}')
            self.pool.add(glyph)

    def extract_marks(self, cards):
        """Accent marks of the other languages' cards: told from the i-dots by shape against the English dots, the acute
        by its size, the circumflex that stands apart, and the tilde cut from the ES ñ."""
        dots = []
        for art in cards['UK'].values():
            alpha = art['C'][..., 3]
            lab, found = pieces_of(alpha)
            dots += [vector(alpha, m.box) for p in found for m in (p.marks or [])]
        for language, arts in cards.items():
            if language == 'UK':
                continue
            for card, art in arts.items():
                scale = self.scales.get((language, card))
                if not scale:
                    continue
                alpha = art['C'][..., 3]
                lab, found = pieces_of(alpha)
                for piece in found:
                    for mark in piece.marks or []:
                        if max((float(vector(alpha, mark.box) @ d) for d in dots), default=0) > 0.88:
                            continue
                        name = mark_kind(lab, mark)
                        if name:
                            self.add_mark(name, art, lab, mark, piece.base_box, scale, f'{language} {card} {mark.box}')
                    if piece.marks is None and is_circumflex(piece) and mark_kind(lab, piece) == 'circumflex':
                        base = base_below(found, piece)
                        if base is not None:
                            self.add_mark('circumflex', art, lab, piece, base.box, scale, f'{language} {card} {piece.box}')
        self.add_tilde(cards)
        self.add_cedilla()

    def add_mark(self, name, art, lab, mark, base_box, scale, source):
        glyph = glyph_of(art['C'], lab, mark, name, scale, 0.0, source)
        dx = ((mark.box[0] + mark.box[2]) - (base_box[0] + base_box[2])) / 2 / scale
        gap = (base_box[1] - mark.box[3]) / scale
        self.mark_samples.setdefault(name, []).append((glyph, dx, gap))

    def add_tilde(self, cards):
        """The tilde of the Spanish ñ: the rows of that piece above the line the other letters of the card start on."""
        art = cards.get('ES', {}).get(TILDE['card'])
        if art is None or art['crc32'] != TILDE['crc32']:
            self.report.append('tilde: the Spanish card it is cut from is not the expected one')
            return
        lab, found = pieces_of(art['C'][..., 3])
        for piece in found:
            if piece.box == TILDE['piece']:
                same_line = [q.height for q in found if q is not piece and abs(q.box[3] - piece.box[3]) <= 3 and q.box[1] >= TILDE['cut'] - 2]
                scale = float(np.median(same_line))
                x0, y0, x1, y1 = piece.box
                cut = Piece([x0, y0, x1, TILDE['cut']], piece.labels, piece.area)
                cols = np.nonzero((art['C'][y0:TILDE['cut'], x0:x1, 3] > THRESHOLD).any(axis=0))[0]
                cut.box = [x0 + int(cols[0]), y0, x0 + int(cols[-1]) + 1, TILDE['cut']]
                glyph = glyph_of(art['C'], lab, cut, 'tilde', scale, 0.0, f"ES {TILDE['card']} {piece.box}")
                glyph.rgba[glyph.ink[3]:, :, 3] = 0
                self.mark_samples['tilde'] = [(glyph, 0.0, 0.0)]
                return
        self.report.append('tilde: the Spanish n with a tilde was not found')

    def add_cedilla(self):
        commas = self.pool.glyphs.get(',')
        if commas:
            best = max(commas, key=lambda g: g.scale)
            self.mark_samples['cedilla'] = [(VF.VisualGlyph('cedilla', best.rgba, best.ink, best.scale, 0.0, best.source), 0.0, -0.05)]

    cap_samples = []

    def finish(self):
        for key, values in self.gap_samples.items():
            self.pool.gaps[key] = float(np.median(values))
        if self.cap_samples:
            self.pool.cap_ratio = float(np.median(self.cap_samples))
        caps = [g.drop for gs in self.pool.glyphs.values() for g in gs if g.char.isupper() and g.drop is not None]
        for gs in self.pool.glyphs.values():
            for g in gs:
                if g.drop is None:
                    g.drop = float(np.median(caps)) if caps else 0.0
        for name, samples in self.mark_samples.items():
            glyph = max((g for g, _, _ in samples), key=lambda g: g.scale)
            self.pool.marks[name] = VF.VisualMark(glyph, float(np.median([d for _, d, _ in samples])),
                                                  float(np.median([g for _, _, g in samples])))
        return self.pool


def build_pool(prepared, titles=None, languages=LANGUAGES):
    builder = PoolBuilder(titles or read_titles())
    PoolBuilder.cap_samples = []
    cards = {language: read_cards(prepared, language) for language in languages}
    builder.label_known('UK', cards['UK'])
    for language in languages:
        if language != 'UK':
            builder.label_matched(language, cards[language])
    builder.extract_marks(cards)
    builder.pin_letters(cards)
    pool = builder.finish()
    if 'j' not in pool.glyphs:
        j = compose_j(pool)
        if j is None:
            builder.report.append('j: no i and g of one size to build it from')
        else:
            pool.add(j)
    return pool, cards, builder.report


def stem_centre(alpha, rows):
    cols = np.nonzero((alpha[rows] > THRESHOLD).any(axis=0))[0]
    return float(cols.mean()) if len(cols) else None


def compose_j(pool):
    """No card draws a lowercase j: the face's own i (dot and stem) carried below the baseline by the hooked tail of its
    g, the tail's stroke joined to the stem."""
    if 'i' not in pool.glyphs or 'g' not in pool.glyphs:
        return None
    named = {c: [g for g in pool.glyphs[c] if g.source.startswith('UK ')] or pool.glyphs[c] for c in 'ig'}
    i = max(named['i'], key=lambda g: (g.scale, g.source))
    g = min(named['g'], key=lambda g: (abs(np.log(g.scale / i.scale)), g.source))
    gi, gg = VF.resample(g, i.scale)
    ix0, iy0, ix1, iy1 = i.ink
    base_g = gg[3] - g.drop * i.scale
    overlap = max(2, int(round(0.06 * i.scale)))
    top = int(round(base_g)) - overlap
    tail = gi[top:gg[3] + PAD].copy()
    lab, n = ndimage.label(tail[..., 3] > THRESHOLD, structure=np.ones((3, 3)))
    if not n:
        return None
    biggest = 1 + int(np.argmax(ndimage.sum(np.ones_like(lab), lab, range(1, n + 1))))
    tail[..., 3][ndimage.binary_dilation(lab == biggest, iterations=PAD - 2) == 0] = 0
    centre_tail = stem_centre(tail[..., 3], slice(0, 2 * overlap))
    centre_i = stem_centre(i.rgba[..., 3], slice(iy1 - 2 * overlap, iy1))
    if centre_tail is None or centre_i is None:
        return None
    dx = int(round(centre_i - centre_tail))
    left = min(0, dx)
    width = max(i.rgba.shape[1] - left, dx - left + tail.shape[1])
    canvas = np.zeros((max(i.rgba.shape[0], iy1 - overlap + tail.shape[0]), width, 4), np.float32)
    place(canvas, i.rgba, -left, 0)
    place(canvas, tail, dx - left, iy1 - overlap)
    rgba = np.clip(np.rint(canvas), 0, 255).astype(np.uint8)
    ys, xs = np.nonzero(rgba[..., 3] > THRESHOLD)
    ink = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
    baseline = iy1 - i.drop * i.scale
    return VF.VisualGlyph('j', rgba, ink, i.scale, (ink[3] - baseline) / i.scale, f'{i.source} + tail of {g.source}')


MARGIN = 8
PITCHES = (1.9, 2.1, 2.35)
STEPS = (0.6, 0.3, 0.0)
SCALE_RANGE = (32.0, 53.0)
REGION_SIZE = (REGION_C[2] - REGION_C[0], REGION_C[3] - REGION_C[1])
ACCENTS = {'á': ('a', 'acute'), 'é': ('e', 'acute'), 'í': ('i', 'acute'), 'ó': ('o', 'acute'), 'ú': ('u', 'acute'),
           'Á': ('A', 'acute'), 'É': ('E', 'acute'), 'Í': ('I', 'acute'), 'Ó': ('O', 'acute'), 'Ú': ('U', 'acute'),
           'ã': ('a', 'tilde'), 'õ': ('o', 'tilde'), 'â': ('a', 'circumflex'), 'ê': ('e', 'circumflex'),
           'ô': ('o', 'circumflex'), 'ç': ('c', 'cedilla')}


class FitError(Exception):
    pass


def split_lines(words, count):
    """Every way to break the words into `count` lines."""
    if count == 1:
        yield [words]
        return
    for cut in range(1, len(words) - count + 2):
        for rest in split_lines(words[cut:], count - 1):
            yield [words[:cut]] + rest


@dataclass
class Placed:
    glyph: object
    rgba: np.ndarray
    ink: tuple
    box: tuple          # x0, y0, x1, y1 of the ink on the card
    bottom: float
    word: int


def accented(pool, char, scale):
    """A letter with its mark, built from the pool's own letter and mark of the same style."""
    base_char, name = ACCENTS[char]
    if base_char not in pool.glyphs or name not in pool.marks:
        raise FitError(f'no {name} mark or {base_char} for {char}')
    base, mark = pool.pick(base_char, scale), pool.marks[name]
    mrgba, mink = VF.resample(mark.glyph, base.scale)
    mw, mh = mink[2] - mink[0], mink[3] - mink[1]
    cx = (base.ink[0] + base.ink[2]) / 2 + mark.dx * base.scale
    mx = cx - mw / 2
    my = base.ink[3] + mark.gap * base.scale if name == 'cedilla' else base.ink[1] - mark.gap * base.scale - mh
    ox, oy = mx - mink[0], my - mink[1]
    left, top = min(0, int(np.floor(ox))), min(0, int(np.floor(oy)))
    right = max(base.rgba.shape[1], int(np.ceil(ox)) + mrgba.shape[1])
    bottom = max(base.rgba.shape[0], int(np.ceil(oy)) + mrgba.shape[0])
    canvas = np.zeros((bottom - top, right - left, 4), np.float32)
    place(canvas, base.rgba, -left, -top)
    place(canvas, mrgba, int(round(ox)) - left, int(round(oy)) - top)
    ink = (min(base.ink[0], int(round(mx))) - left, min(base.ink[1], int(round(my))) - top,
           max(base.ink[2], int(round(mx)) + mw) - left, max(base.ink[3], int(round(my)) + mh) - top)
    return VF.VisualGlyph(char, np.clip(np.rint(canvas), 0, 255).astype(np.uint8), ink, base.scale, base.drop,
                          f'{base.source} + {mark.glyph.source}')


def place(canvas, rgba, ox, oy):
    h, w = rgba.shape[:2]
    x0, y0, x1, y1 = max(0, ox), max(0, oy), min(canvas.shape[1], ox + w), min(canvas.shape[0], oy + h)
    if x0 >= x1 or y0 >= y1:
        return
    src = rgba[y0 - oy:y1 - oy, x0 - ox:x1 - ox].astype(np.float32)
    dst = canvas[y0:y1, x0:x1]
    sa, da = src[..., 3:] / 255, dst[..., 3:] / 255
    out_a = sa + da * (1 - sa)
    dst[..., :3] = np.divide(src[..., :3] * sa + dst[..., :3] * da * (1 - sa), np.maximum(out_a, 1e-6))
    dst[..., 3:] = out_a * 255


def glyph_run(pool, text, scale, tracking):
    """[(glyph, rgba, ink, x of the ink's left, drop in px)] of one word at `scale`, and its width."""
    placed, x, previous = [], 0.0, None
    for char in text:
        glyph = accented(pool, char, scale) if char in ACCENTS and char not in pool.glyphs else pool.pick(char, scale)
        rgba, ink = VF.resample(glyph, scale)
        if previous is not None:
            x += pool.gap(previous, char) * scale * tracking
        placed.append((glyph, rgba, ink, x, glyph.drop * scale))
        x += ink[2] - ink[0]
        previous = char
    return placed, x


def arrange(pool, lines, scale, pitch, step, tracking):
    """The title's glyphs placed on the card, centred as one block: ([Placed], ink box)."""
    runs, widths = [], []
    for line in lines:
        words, x = [], 0.0
        for i, word in enumerate(line):
            placed, width = glyph_run(pool, word, scale, tracking)
            x += pool.word_gap * scale if i else 0
            words.append((x, placed))
            x += width
        runs.append(words)
        widths.append(x)
    out, word_index = [], 0
    for k, words in enumerate(runs):
        shift = (-1 if k % 2 == 0 else 1) * 0.5 * scale if len(lines) > 1 else 0.0
        left = (REGION_SIZE[0] - widths[k]) / 2 + shift
        for w, (x, placed) in enumerate(words):
            baseline = k * pitch * scale + w * step * scale
            for glyph, rgba, ink, gx, drop in placed:
                bottom = baseline + drop
                gx0 = left + x + gx
                out.append(Placed(glyph, rgba, ink, (gx0, bottom - (ink[3] - ink[1]), gx0 + ink[2] - ink[0], bottom), bottom, word_index))
            word_index += 1
    x0, y0 = min(p.box[0] for p in out), min(p.box[1] for p in out)
    x1, y1 = max(p.box[2] for p in out), max(p.box[3] for p in out)
    dx, dy = (REGION_SIZE[0] - (x0 + x1)) / 2, (REGION_SIZE[1] - (y0 + y1)) / 2
    for p in out:
        p.box = (p.box[0] + dx, p.box[1] + dy, p.box[2] + dx, p.box[3] + dy)
        p.bottom += dy
    return out, (x0 + dx, y0 + dy, x1 + dx, y1 + dy)


def collides(placed, shrink=0.12):
    """True when letters of different words overlap deeply: the cores of their ink boxes cross."""
    cores = []
    for p in placed:
        w, h = p.box[2] - p.box[0], p.box[3] - p.box[1]
        cores.append((p.box[0] + shrink * w, p.box[1] + shrink * h, p.box[2] - shrink * w, p.box[3] - shrink * h, p.word))
    for i, a in enumerate(cores):
        for b in cores[i + 1:]:
            if a[4] != b[4] and a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]:
                return True
    return False


def words_touch(placed, limit=40):
    """True when the ink of two different words overlaps by more than `limit` pixels."""
    masks = {}
    for p in placed:
        canvas = masks.setdefault(p.word, np.zeros((REGION_SIZE[1], REGION_SIZE[0]), np.float32))
        paste_mask(canvas, p)
    ink = {w: m > 90 for w, m in masks.items()}
    words = sorted(ink)
    return any(int((ink[a] & ink[b]).sum()) > limit for i, a in enumerate(words) for b in words[i + 1:])


def paste_mask(canvas, p):
    h, w = p.rgba.shape[:2]
    ox, oy = int(round(p.box[0] - p.ink[0])), int(round(p.bottom - p.ink[3]))
    x0, y0, x1, y1 = max(0, ox), max(0, oy), min(canvas.shape[1], ox + w), min(canvas.shape[0], oy + h)
    if x0 < x1 and y0 < y1:
        region = canvas[y0:y1, x0:x1]
        np.maximum(region, p.rgba[y0 - oy:y1 - oy, x0 - ox:x1 - ox, 3], out=region)


def fit(pool, text, tracking=1.0):
    """The best arrangement of `text` on a card: the largest scale at which it stays inside the card and clear of itself,
    then the line count nearest two (three for long titles), balanced lines and the tightest pitch."""
    words = text.split()
    target = 2 if len(words) < 5 else 3
    scale = SCALE_RANGE[1]
    while scale >= SCALE_RANGE[0]:
        if max(glyph_run(pool, word, scale, tracking)[1] for word in words) > REGION_SIZE[0] - 2 * MARGIN:
            scale -= 1.0
            continue
        options = []
        for count in range(1, min(len(words), 4) + 1):
            for lines in split_lines(words, count):
                for pitch in PITCHES:
                    for step in STEPS:
                        placed, box = arrange(pool, lines, scale, pitch, step, tracking)
                        if box[0] < MARGIN or box[2] > REGION_SIZE[0] - MARGIN or box[1] < MARGIN or box[3] > REGION_SIZE[1] - MARGIN:
                            continue
                        if collides(placed):
                            continue
                        widths = [sum(len(w) for w in line) + len(line) for line in lines]
                        options.append(((abs(count - target), (max(widths) - min(widths)) / max(widths), pitch, -step), placed))
        for _, placed in sorted(options, key=lambda o: o[0]):
            if not words_touch(placed):
                return scale, placed
        scale -= 1.0
    raise FitError(f'"{text}" does not fit a card at the smallest scale')


def compose(pool, text, tracking=1.0):
    """The title as blue glow letters on a transparent card-sized canvas (uint8 RGBA), and its scale."""
    scale, placed = fit(pool, text, tracking)
    canvas = np.zeros((REGION_SIZE[1], REGION_SIZE[0], 4), np.float32)
    for p in placed:
        paste(canvas, p.rgba, p.ink, p.box[0], p.bottom)
    return np.clip(np.rint(canvas), 0, 255).astype(np.uint8), scale


def paste(canvas, rgba, ink, x, bottom):
    """Alpha-over of a glyph whose ink's left edge is at x and ink's bottom at `bottom`."""
    h, w = rgba.shape[:2]
    ox, oy = int(round(x - ink[0])), int(round(bottom - ink[3]))
    x0, y0, x1, y1 = max(0, ox), max(0, oy), min(canvas.shape[1], ox + w), min(canvas.shape[0], oy + h)
    if x0 >= x1 or y0 >= y1:
        return
    src = rgba[y0 - oy:y1 - oy, x0 - ox:x1 - ox].astype(np.float32)
    dst = canvas[y0:y1, x0:x1]
    sa, da = src[..., 3:] / 255, dst[..., 3:] / 255
    out_a = sa + da * (1 - sa)
    rgb = np.divide(src[..., :3] * sa + dst[..., :3] * da * (1 - sa), np.maximum(out_a, 1e-6))
    dst[..., :3] = rgb
    dst[..., 3:] = out_a * 255


@dataclass
class Fields:
    parchment_a: np.ndarray    # 375x643x3 parchment of region A
    parchment_b: np.ndarray    # 643x375x3 parchment of region B
    brown_b: np.ndarray        # 643x375x3 the brown lettering is painted with
    hole: np.ndarray           # colour stored under the lettering cut-out of A
    lut_a: np.ndarray          # blue mask alpha -> A alpha
    lut_b: np.ndarray          # blue mask alpha -> brown cover of B (0..1)


def parchment_cards(cards):
    """Cards whose A and B hold the title (those that differ between languages), not the shared filler art."""
    uk, es = cards['UK'], cards['ES']
    return [c for c in uk if np.abs(uk[c]['A'].astype(int) - es[c]['A'].astype(int)).max() > 40]


def fill(values, valid, sigma=18.0):
    """`values` where valid, elsewhere the smoothed value of the nearby valid ones."""
    weight = ndimage.gaussian_filter(valid.astype(np.float32), sigma)
    out = np.zeros_like(values, dtype=np.float32)
    for k in range(values.shape[-1]):
        out[..., k] = ndimage.gaussian_filter(np.where(valid, values[..., k], 0), sigma) / np.maximum(weight, 1e-4)
    return np.where(valid[..., None], values, out)


def build_fields(cards):
    names = parchment_cards(cards)
    samples = [(cards[language][c]) for language in cards for c in names if c in cards[language]]
    masks = [a['C'][..., 3] for a in samples]
    a_free = np.stack([ndimage.binary_erosion(a['A'][..., 3] == 255, iterations=2) for a in samples])
    a_rgb = np.stack([a['A'][..., :3].astype(np.float32) for a in samples])
    pa = np.nanmedian(np.where(a_free[..., None], a_rgb, np.nan), axis=0)
    pa = fill(np.nan_to_num(pa), ~np.isnan(pa).any(-1))
    rot = [np.rot90(m, 1) for m in masks]
    b_rgb = np.stack([a['B'][..., :3].astype(np.float32) for a in samples])
    b_free = np.stack([ndimage.binary_dilation(r > 10, iterations=14) == 0 for r in rot])
    b_ink = np.stack([ndimage.binary_erosion(r > 250, iterations=2) for r in rot])
    pb = np.nanmedian(np.where(b_free[..., None], b_rgb, np.nan), axis=0)
    pb = fill(np.nan_to_num(pb), ~np.isnan(pb).any(-1))
    brown = np.nanmedian(np.where(b_ink[..., None], b_rgb, np.nan), axis=0)
    brown = fill(np.nan_to_num(brown), ~np.isnan(brown).any(-1), sigma=30.0)
    holes = np.concatenate([a['A'][..., :3][a['A'][..., 3] == 0] for a in samples[:10]])
    hole = np.median(holes, axis=0)
    levels = np.arange(256)
    a_alpha = np.concatenate([a['A'][..., 3].ravel() for a in samples]).astype(np.float32)
    m_all = np.concatenate([m.ravel() for m in masks])
    lut_a = lut_of(m_all, a_alpha, levels)
    cover = np.concatenate([(((pb - a['B'][..., :3].astype(np.float32)) * (pb - brown)).sum(-1) /
                             np.maximum(((pb - brown) ** 2).sum(-1), 1.0)).ravel() for a in samples])
    r_all = np.concatenate([r.ravel() for r in rot])
    lut_b = np.clip(lut_of(r_all, cover, levels), 0, 1)
    return Fields(pa, pb, brown, hole, lut_a, lut_b)


def lut_of(keys, values, levels):
    sums = np.bincount(keys.astype(int), weights=values, minlength=256)
    counts = np.bincount(keys.astype(int), minlength=256)
    out = np.where(counts > 0, sums / np.maximum(counts, 1), np.nan)
    valid = ~np.isnan(out)
    out = np.interp(levels, levels[valid], out[valid])
    return np.maximum.accumulate(out) if out[-1] >= out[0] else np.minimum.accumulate(out)


def render_a(fields, mask):
    alpha = np.clip(np.rint(fields.lut_a[mask]), 0, 255).astype(np.uint8)
    rgb = np.where((alpha > 0)[..., None], fields.parchment_a, fields.hole)
    return np.dstack([np.clip(np.rint(rgb), 0, 255).astype(np.uint8), alpha])


def render_b(fields, mask):
    s = fields.lut_b[np.rot90(mask, 1)][..., None]
    rgb = fields.parchment_b * (1 - s) + fields.brown_b * s
    return np.dstack([np.clip(np.rint(rgb), 0, 255).astype(np.uint8), np.full(rgb.shape[:2], 255, np.uint8)])


import make_dcpx  # noqa: E402
import patch_ftx  # noqa: E402

NEAR = 4
TOLERANCE = 48


def near_text(old_mask, new_mask, rotate=False):
    mask = (old_mask > 0) | (new_mask > 0)
    if rotate:
        mask = np.rot90(mask, 1)
    return ndimage.binary_dilation(mask, iterations=NEAR)


def apply_title(blob, new_c, fields, parchment):
    """(patched pack, blocks re-encoded, intended texture, original texture): the card's three regions carry `new_c`'s
    lettering. Only pixels near the old or the new lettering change; every other pixel is left as the pack stores it."""
    tex = ftex.textures(blob)[0]
    full = visuals.decode(blob, tex)
    out = full.copy()
    old_mask, new_mask = region(full, REGION_C)[..., 3], new_c[..., 3]
    near = near_text(old_mask, new_mask)
    region(out, REGION_C)[near] = new_c[near]
    if parchment:
        region(out, REGION_A)[near] = render_a(fields, new_mask)[near]
        near_b = near_text(old_mask, new_mask, rotate=True)
        region(out, REGION_B)[near_b] = render_b(fields, new_mask)[near_b]
    changed = (out != full).any(axis=-1)
    patched, blocks = patch_ftx.patch(blob, tex, out, changed)
    return patched, blocks, out, full


def check_patched(patched, expected, full, tex):
    """The patched pack decodes to the intended pixels within BC3's tolerance, and no block the patch did not touch moved;
    returns the drift of the untouched pixels inside touched blocks."""
    back = visuals.decode(patched, tex).astype(int)
    changed = (expected != full).any(axis=-1)
    h, w = changed.shape
    touched = changed.reshape(h // 4, 4, w // 4, 4).any(axis=(1, 3)).repeat(4, 0).repeat(4, 1)
    assert (back[~touched] == full.astype(int)[~touched]).all(), 'pixels outside the touched blocks changed'
    worst = int(np.abs(back[..., 3] - expected.astype(int)[..., 3]).max())
    assert worst <= TOLERANCE + 40, f'the patched card differs from the lettering by {worst}'
    near = touched & ~changed
    if not near.any():
        return 0.0
    old = full.astype(int)
    seen = near & (old[..., 3] > 16) & (back[..., 3] > 16)
    colour = float(np.abs(back[seen][:, :3] - old[seen][:, :3]).mean()) if seen.any() else 0.0
    return max(colour, float(np.abs(back[near][:, 3] - old[near][:, 3]).mean()))


SOURCES = (('CUSA05262-1.01', 'Odin2_RE_UK.CPK'), ('CUSA05262-1.00', 'Odin2_RE_UK.CPK'), ('CUSA05083-1.00', 'Odin2_RE_US.CPK'),
           ('CUSA01290-1.00', 'Odin2_RE.CPK'), ('CUSA01290-1.02', 'Odin2_RE.CPK'))
ENGLISH_BUILDS = ('CUSA05262', 'CUSA05083')


def generate(prepared, titles=None, sources=SOURCES):
    """{(card path, CRC32 of the pack): (original pack, patched pack)} for every translated Refined card of every local
    build, plus per-card notes and the pool report. The lettering comes from the pool built out of the player's own European
    cards; a build whose cards hold other lettering (Japanese) is repainted the same way."""
    titles = titles or read_titles()
    pool, cards, report = build_pool(prepared, titles)
    fields = build_fields(cards)
    parchment = set(parchment_cards(cards))
    composed, targets, notes = {}, {}, []
    for build, archive in sources:
        path = Path(prepared) / build / archive
        if not path.is_file():
            notes.append((build, 'DATA UNAVAILABLE: ' + archive))
            continue
        pack = cpk.Cpk(path, original=True)
        try:
            for row in titles:
                card, english, portuguese = row['card'], row['english'], row['pt_br']
                entry = f'GUI/HD_StageName{card}.ftx'
                blob = pack.read(pack.find(entry))
                key = (entry, zlib.crc32(blob))
                if english == portuguese and build.startswith(ENGLISH_BUILDS):
                    targets[key] = (blob, blob)
                    notes.append((f'{build} {card}', 'not needed: the Portuguese title is the English one'))
                    continue
                if key in targets:
                    notes.append((f'{build} {card}', 'ok: same pack as an earlier build'))
                    continue
                if card not in composed:
                    try:
                        composed[card] = compose(pool, portuguese)
                    except (FitError, KeyError) as e:
                        composed[card] = e
                if isinstance(composed[card], Exception):
                    notes.append((f'{build} {card}', f'BLOCKED: {composed[card]}'))
                    continue
                canvas, scale = composed[card]
                patched, blocks, out, full = apply_title(blob, canvas, fields, card in parchment)
                drift = check_patched(patched, out, full, ftex.textures(blob)[0])
                targets[key] = (blob, patched)
                notes.append((f'{build} {card}', f'ok: scale {scale:.0f}, {blocks} blocks, edge drift {drift:.2f}'))
        finally:
            pack.f.close()
    return targets, notes, report, pool


import argparse  # noqa: E402
import oslv  # noqa: E402

DEFAULT_OUT = ROOT / 'localization' / 'pt-BR' / 'visual' / 'stage-cards-ptbr.oslv'


def preview_sheets(targets, directory):
    """Contact sheets of the patched cards' blue titles, as headless PNG files."""
    from PIL import ImageDraw
    directory.mkdir(parents=True, exist_ok=True)
    names = sorted(targets, key=lambda k: (k[0], k[1]))
    cols, cw, ch = 4, 330, 195
    for part in range(0, len(names), 16):
        chunk = names[part:part + 16]
        sheet = Image.new('RGB', (cols * cw, ((len(chunk) + cols - 1) // cols) * ch), (40, 40, 60))
        draw = ImageDraw.Draw(sheet)
        for i, name in enumerate(chunk):
            blob = targets[name][1]
            tex = ftex.textures(blob)[0]
            image = Image.fromarray(visuals.decode(blob, tex)[REGION_C[1]:REGION_C[3], REGION_C[0]:REGION_C[2]])
            image = image.resize((cw - 6, ch - 20))
            bg = Image.new('RGBA', image.size, (40, 40, 60, 255))
            bg.alpha_composite(image)
            sheet.paste(bg.convert('RGB'), ((i % cols) * cw + 3, (i // cols) * ch + 16))
            draw.text(((i % cols) * cw + 3, (i // cols) * ch + 2), f'{name[0][4:]} {name[1]:08x}', fill=(255, 255, 0))
        sheet.save(directory / f'stage-cards-{part // 16}.png')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--prepared', default=str(ROOT / 'game' / 'prepared'))
    ap.add_argument('--out', default=str(DEFAULT_OUT))
    ap.add_argument('--report')
    ap.add_argument('--preview', help='folder for contact sheets of the generated cards')
    args = ap.parse_args()
    visuals.use_native_reader()
    targets, notes, report, pool = generate(args.prepared)
    blob = oslv.encode([(entry, old, new, []) for (entry, _), (old, new) in targets.items()])
    Path(args.out).write_bytes(blob)
    print(f'{len(targets)} card packs -> {args.out} ({len(blob)} bytes)')
    if args.report:
        with open(args.report, 'w', encoding='utf-8', newline='\n') as f:
            f.write('card\tresult\n' + ''.join(f'{c}\t{r}\n' for c, r in notes) + ''.join(f'pool\t{r}\n' for r in report))
    if args.preview:
        preview_sheets(targets, Path(args.preview))
    blocked = [(c, r) for c, r in notes if r.startswith('BLOCKED')]
    for c, r in blocked:
        print(c, r)
    return 1 if blocked else 0


if __name__ == '__main__':
    sys.exit(main())
