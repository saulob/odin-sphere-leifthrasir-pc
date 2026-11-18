"""Classic stage and location title cards (NN.ftx + NN.mbs of Odin2_OR) in Portuguese (Brazil).

A Classic card is an atlas of single-letter sprites and a layout of three key layers over them: the letters scattered
(centres doubled), settled, and stretched three times as wide while they fade. The English slot's cards hold Japanese
lettering; the Spanish, French, German and Italian cards hold one Western face, which is the glyph pool. Its sprites are
named by aligning each language's own location names (story/sysmsg, SystemMsg) to the cards that spell them, then by
shape for the cards no name matches. A Portuguese card is a new atlas packed into the player's card texture and a new
layout with the same three layers.
"""
import argparse
import csv
import re
import sys
import zlib
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'prompts'))
import cpk  # noqa: E402
import ftex  # noqa: E402
import layout_text as LT  # noqa: E402
import mbs as M  # noqa: E402
import mbs_render as R  # noqa: E402
import odin_text as ot  # noqa: E402
import oslv  # noqa: E402
import patch_ftx  # noqa: E402
import visuals  # noqa: E402

ROOT = HERE.parent.parent
TABLE = ROOT / 'localization' / 'pt-BR' / 'visual' / 'stage-cards.tsv'
DEFAULT_OUT = ROOT / 'localization' / 'pt-BR' / 'visual' / 'classic-cards-ptbr.oslv'
POOL_BUILD = 'CUSA05262-1.01'
LANGUAGES = ('ES', 'FR', 'DE', 'IT')
CARD = re.compile(r'^(\d\d(?:_\w+)?)\.(ftx|mbs)$', re.I)
NAME_RESOURCES = ('story/sysmsg', 'msgsheet/SystemMsg.fms')
FLAT = set('acemnorsuvwxz')
INK = 64
SIMILAR = 0.975
SOURCES = (('CUSA05262-1.01', 'Odin2_OR_UK.CPK'), ('CUSA05262-1.00', 'Odin2_OR_UK.CPK'), ('CUSA05083-1.00', 'Odin2_OR_US.CPK'),
           ('CUSA01290-1.00', 'Odin2_ORHD.CPK'), ('CUSA01290-1.02', 'Odin2_ORHD.CPK'))
SETTLED, SCATTER, EXIT = 1, 0, 2
PINS = (('Q', 'FR 13 8bb874eb (0, 0, 173, 120)'), ('Ú', 'ES 65 eb21dace (0, 0, 193, 106)'),
        ('ô', 'FR 41 9c7b8e18 (146, 151, 204, 239)'), ('í', 'ES 50 ddf5ea3a (315, 151, 345, 235)'),
        ('y', 'ES 43 c88950be (211, 148, 265, 233)'))
FADE = bytes.fromhex('ffffffffffffff00ffffff00ffffff00ffffff00ffffff00')
MAX_LINES = 3
PAD = 2


def read_titles():
    with open(TABLE, encoding='utf-8', newline='') as f:
        return list(csv.DictReader(f, delimiter='\t'))


def read_pack(path):
    """{card: (ftx bytes, mbs bytes)} of an archive's Classic cards."""
    pack = cpk.Cpk(path, original=True)
    found = defaultdict(dict)
    try:
        for entry in pack.entries:
            m = CARD.match(entry['path'])
            if m:
                found[m.group(1)][m.group(2).lower()] = (entry['path'], pack.read(entry))
    finally:
        pack.f.close()
    return {card: parts for card, parts in found.items() if 'ftx' in parts and 'mbs' in parts}


# ---------------------------------------------------------------- glyph pool


@dataclass
class Instance:
    language: str
    card: str
    key: int
    part: int
    alpha: np.ndarray            # upright sprite
    quad: tuple                  # x0, y0, x1, y1 in layout units
    source: str
    char: str = None
    word_start: bool = None      # known only for sprites named from the language's text

    @property
    def scale(self):
        return (self.quad[3] - self.quad[1]) / self.alpha.shape[0]

    def ink(self):
        ys, xs = np.nonzero(self.alpha > INK)
        if not len(xs):
            return 0, 0, self.alpha.shape[1], self.alpha.shape[0]
        return int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1

    def ink_quad(self):
        x0, y0, x1, y1 = self.ink()
        s = self.scale
        return self.quad[0] + x0 * s, self.quad[1] + y0 * s, self.quad[0] + x1 * s, self.quad[1] + y1 * s


def upright(atlas, uv, quad):
    pattern = LT.corner_pattern(quad)
    w, h = LT.drawn_size(uv, pattern)
    w, h = int(round(w)), int(round(h))
    coeffs = R._affine(uv, [(px * w, py * h) for px, py in pattern])
    return np.asarray(Image.fromarray(atlas).transform((w, h), Image.AFFINE, coeffs, Image.NEAREST))


def card_instances(language, card, ftx_bytes, mbs_bytes):
    tex = ftex.textures(ftx_bytes)[0]
    atlas = visuals.decode(ftx_bytes, tex)
    layout = M.Mbs(mbs_bytes)
    crc = zlib.crc32(ftx_bytes)
    out = []
    for key in layout.layer_keys(SETTLED):
        uv, quad = layout.part_uv(key['part']), layout.geom(key['geom'])
        xs, ys = [p[0] for p in quad], [p[1] for p in quad]
        out.append(Instance(language, card, key['index'], key['part'], upright(atlas, uv, quad)[..., 3].copy(),
                            (min(xs), min(ys), max(xs), max(ys)),
                            f'{language} {card} {crc:08x} {layout.part_rect(key["part"])}'))
    return out


def vector(alpha):
    ys, xs = np.nonzero(alpha > INK)
    if not len(xs):
        return np.zeros(1024, np.float32), 1.0, 1
    crop = alpha[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    v = np.asarray(Image.fromarray(crop).resize((32, 32), Image.BILINEAR), np.float32).ravel()
    return v / (np.linalg.norm(v) + 1e-6), crop.shape[1] / crop.shape[0], crop.shape[0]


def names_of(prepared, language, build=POOL_BUILD):
    """{English name: [the language's names of the same record]} from the language's own text."""
    profile = dict(ot.build_profile(build), slot=language)
    english = dict(ot.build_profile(build), slot='UK')
    folder = Path(prepared) / build
    out = defaultdict(set)
    mine, theirs = ot.slot_resources(profile, folder), ot.slot_resources(english, folder)
    for resource in NAME_RESOURCES:
        if resource not in mine or resource not in theirs:
            continue
        units, _ = ot.parse_resource(resource, ot.read_resource(mine[resource]))
        en_units, _ = ot.parse_resource(resource, ot.read_resource(theirs[resource]))
        for en, text in zip(en_units, units):
            out[en].add(text.replace('’', "'"))
    return out


class Pool:
    def __init__(self):
        self.glyphs = {}          # char -> Instance (the representative)
        self.metrics = {}
        self.report = []


def label_by_names(cards, titles, names):
    """Instances of the cards whose official name has exactly one letter per sprite, labelled in reading order."""
    labelled = 0
    for row in titles:
        options = sorted(names.get(row['english'], ()))
        for card_key in (row['card'],):
            inst = cards.get(card_key)
            if not inst:
                continue
            fits = [t for t in options if len([c for c in t if not c.isspace()]) == len(inst)]
            if len({re.sub(r'\s', '', t) for t in fits}) != 1:
                continue
            letters = [(c, k == 0 or fits[0][k - 1].isspace()) for k, c in enumerate(fits[0]) if not c.isspace()]
            for i, (c, start) in zip(inst, letters):
                i.char, i.word_start = c, start
            labelled += 1
    return labelled


def build_pool(prepared, titles):
    """The Western Classic face: every sprite of the European cards, named, and the face's metrics."""
    pool = Pool()
    by_card, everything = {}, []
    for language in LANGUAGES:
        names = names_of(prepared, language)
        cards = {}
        for card, parts in read_pack(Path(prepared) / POOL_BUILD / f'Odin2_OR_{language}.CPK').items():
            cards[card] = card_instances(language, card, parts['ftx'][1], parts['mbs'][1])
        n = label_by_names(cards, titles, names)
        pool.report.append(f'{language}: {n} cards named from the language\'s own text')
        for card, inst in cards.items():
            by_card[(language, card)] = inst
            everything += inst
    for i in everything:
        i.vec, i.aspect, i.height = vector(i.alpha)
    _drop_inconsistent(everything, pool.report)
    _pin(everything, pool.report)
    _label_by_shape(everything, pool.report)
    for i in everything:
        if i.char:
            pool.glyphs.setdefault(i.char, []).append(i)
    pool.metrics = _metrics(by_card)
    pool.lines = _official_extents(by_card)
    pool.instances = by_card
    return pool


def _drop_inconsistent(instances, report):
    """A card whose name-labelled sprites include one unlike the other cards' sprites of its letter is unlabelled
    entirely (the name does not align to its sprites). Support is counted over distinct cards."""
    by_char = defaultdict(list)
    for i in instances:
        if i.char:
            by_char[i.char].append(i)
    bad = set()
    for char, group in by_char.items():
        cards = [(g.language, g.card) for g in group]
        if len(set(cards)) < 3:
            continue
        similar = (np.stack([g.vec for g in group]) @ np.stack([g.vec for g in group]).T) > SIMILAR
        for k, g in enumerate(group):
            others = {cards[j] for j in np.flatnonzero(similar[k]) if cards[j] != cards[k]}
            if len(others) < max(1, (len(set(cards)) - 1) // 6):
                bad.add(cards[k])
    for language, card in sorted(bad):
        report.append(f'{language} {card}: its name does not align to its sprites; unnamed')
    for i in instances:
        if (i.language, i.card) in bad:
            i.char = i.word_start = None


def _pin(instances, report):
    """Letters no official name spells where its sprites can be aligned, named by card fingerprint and atlas rectangle."""
    for char, source in PINS:
        found = [i for i in instances if i.source == source]
        if not found:
            report.append(f'{char}: the sprite {source} it is cut from is not in the player cards')
        for i in found:
            i.char = char


def _label_by_shape(instances, report):
    named = [i for i in instances if i.char]
    vecs = np.stack([i.vec for i in named])
    left = 0
    for i in instances:
        if i.char:
            continue
        sims = vecs @ i.vec
        best, votes = {}, Counter()
        for k in np.argsort(-sims)[:40]:
            n = named[k]
            if abs(n.aspect / i.aspect - 1) > 0.12 or abs(n.height / i.height - 1) > 0.25:
                continue
            best[n.char] = max(best.get(n.char, 0.0), float(sims[k]))
            votes[n.char] += sims[k] >= SIMILAR
        ranked = sorted(best.items(), key=lambda kv: (-kv[1], -votes[kv[0]]))
        if len(ranked) > 1 and ranked[0][0].lower() == ranked[1][0].lower() and ranked[0][1] - ranked[1][1] <= 0.005:
            ranked = sorted(ranked[:2], key=lambda kv: -votes[kv[0]]) + ranked[2:]
            ranked = ranked[:1] + ranked[2:]
        if ranked and ranked[0][1] >= SIMILAR and (len(ranked) == 1 or ranked[0][1] - ranked[1][1] > 0.005):
            i.char = ranked[0][0]
        else:
            left += 1
    report.append(f'{left} sprites left unnamed (shape matches no named letter)')


def split_lines(inst):
    """Reading lines of a card: a new line where the next letter starts left of the previous one."""
    lines, current = [], []
    for i in inst:
        if current and i.quad[0] < current[-1].quad[0] - 4:
            lines.append(current)
            current = []
        current.append(i)
    if current:
        lines.append(current)
    return lines


def line_frame(line):
    """(baseline, x-height) of an official line in layout units, from its flat lowercase letters."""
    flat = [i.ink_quad() for i in line if i.char in FLAT]
    if len(flat) < 2:
        return None
    return float(np.median([q[3] for q in flat])), float(np.median([q[3] - q[1] for q in flat]))


def _metrics(by_card):
    """Per letter: ink bottom below the baseline (x-heights); spacing between letters and words; line pitch; letter scale."""
    drops, gaps, pitches, scales, xheights, words = defaultdict(list), defaultdict(list), [], [], [], []
    for (language, card), inst in by_card.items():
        if not all(i.char for i in inst):
            continue
        frames = []
        for line in split_lines(inst):
            frame = line_frame(line)
            frames.append(frame)
            if frame is None:
                continue
            base, xh = frame
            px = float(np.median([i.scale for i in line]))
            scales.append(px)
            xheights.append(xh / px)
            for i in line:
                q = i.ink_quad()
                drops[i.char].append((q[3] - base) / xh)
            for a, b in zip(line, line[1:]):
                if b.word_start is None:
                    continue
                qa, qb = a.ink_quad(), b.ink_quad()
                (words if b.word_start else gaps[(cls(a.char), cls(b.char))]).append((qb[0] - qa[2]) / xh)
        for f0, f1 in zip(frames, frames[1:]):
            if f0 and f1:
                pitches.append((f1[0] - f0[0]) / ((f0[1] + f1[1]) / 2))
    return dict(drop={c: float(np.median(v)) for c, v in drops.items()},
                gap={k: float(np.median(v)) for k, v in gaps.items() if len(v) >= 5},
                pitch=float(np.median(pitches)), scale=float(np.median(scales)), xheight_px=float(np.median(xheights)),
                word=float(np.median(words)))


def cls(char):
    return 'cap' if char.isupper() else 'low' if char.isalpha() else 'mark'


def _official_extents(by_card):
    """{card: [(x0, y0, x1, y1, lines)]} of the European settled layers."""
    out = defaultdict(list)
    for (language, card), inst in by_card.items():
        xs = [v for i in inst for v in (i.quad[0], i.quad[2])]
        ys = [v for i in inst for v in (i.quad[1], i.quad[3])]
        out[card].append((min(xs), min(ys), max(xs), max(ys), len(split_lines(inst))))
    return out


# ---------------------------------------------------------------- glyphs


@dataclass
class Glyph:
    char: str
    alpha: np.ndarray
    drop: float                  # ink bottom below the baseline, x-heights
    source: str

    def ink(self):
        ys, xs = np.nonzero(self.alpha > INK)
        return int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1


def representative(pool, char):
    """The letter's most typical sprite: the one most like the others, the largest of those."""
    group = pool.glyphs[char]
    vecs = np.stack([g.vec for g in group])
    support = (vecs @ vecs.T > SIMILAR).sum(axis=1)
    best = max(range(len(group)), key=lambda k: (support[k], group[k].alpha.size, -k))
    g = group[best]
    return Glyph(char, g.alpha, pool.metrics['drop'].get(char, 0.0), g.source)


def components(alpha):
    lab, n = ndimage.label(alpha > INK, structure=np.ones((3, 3)))
    boxes = ndimage.find_objects(lab)
    return lab, [(k + 1, b) for k, b in enumerate(boxes)]


def tilde_of(pool):
    """The tilde of the face's ñ: its topmost ink component."""
    n_tilde = representative(pool, 'ñ')
    lab, comps = components(n_tilde.alpha)
    top = min(comps, key=lambda c: c[1][0].start)
    body = [c for c in comps if c is not top]
    mask = ndimage.binary_dilation(lab == top[0], iterations=2)
    mark = np.where(mask, n_tilde.alpha, 0).astype(np.uint8)
    body_top = min(c[1][0].start for c in body)
    body_x = (min(c[1][1].start for c in body) + max(c[1][1].stop for c in body)) / 2
    ys, xs = np.nonzero(mark > INK)
    gap = body_top - (ys.max() + 1)
    dx = (xs.min() + xs.max() + 1) / 2 - body_x
    return mark[ys.min():ys.max() + 1, xs.min():xs.max() + 1], gap, dx, n_tilde.source


def comma_of(pool):
    g = representative(pool, ',')
    x0, y0, x1, y1 = g.ink()
    return g.alpha[y0:y1, x0:x1], g.source


def compose_mark(base, mark, above, gap, dx, char, source):
    """The base letter with a mark centred over (or under) its ink, `gap` pixels clear of it."""
    bx0, by0, bx1, by1 = base.ink()
    mh, mw = mark.shape
    cx = (bx0 + bx1) / 2 + dx
    mx = int(round(cx - mw / 2))
    my = by0 - gap - mh if above else by1 + gap
    left, top = min(0, mx), min(0, my)
    right, bottom = max(base.alpha.shape[1], mx + mw), max(base.alpha.shape[0], my + mh)
    canvas = np.zeros((bottom - top + 2 * PAD, right - left + 2 * PAD), np.uint8)
    oy, ox = PAD - top, PAD - left
    canvas[oy:oy + base.alpha.shape[0], ox:ox + base.alpha.shape[1]] = base.alpha
    region = canvas[oy + my:oy + my + mh, ox + mx:ox + mx + mw]
    np.maximum(region, mark, out=region)
    return Glyph(char, canvas, base.drop, f'{base.source} + {source}')


def glyph_set(pool, text):
    """{char: Glyph} for every letter of `text`, composing ã õ ç from the face's own ñ tilde and comma."""
    out = {}
    for char in sorted(set(text) - {' '}):
        if char in pool.glyphs:
            out[char] = representative(pool, char)
        elif char in 'ãõ':
            base = representative(pool, {'ã': 'a', 'õ': 'o'}[char])
            mark, gap, dx, source = tilde_of(pool)
            out[char] = compose_mark(base, mark, True, gap, dx, char, source)
        elif char == 'ç':
            base = representative(pool, 'c')
            mark, source = comma_of(pool)
            out[char] = compose_mark(base, mark, False, -max(1, mark.shape[0] // 8), 0.0, char, source)
        else:
            raise FitError(f'the Classic face has no {char!r}')
    return out


# ---------------------------------------------------------------- layout


class FitError(Exception):
    pass


@dataclass
class Letter:
    char: str
    glyph: Glyph
    quad: tuple = None           # settled quad, layout units
    line: int = 0


def word_runs(glyphs, metrics, word):
    """[(char, x of the sprite's left edge in atlas pixels)], and the word's ink width in atlas pixels."""
    xh = metrics['xheight_px']
    out, pen, previous = [], 0.0, None
    for char in word:
        g = glyphs[char]
        x0, _, x1, _ = g.ink()
        if previous is not None:
            pen += metrics['gap'].get((cls(previous), cls(char)), metrics['gap'].get(('low', 'low'), 0.0)) * xh
        out.append((char, pen - x0))
        pen += x1 - x0
        previous = char
    return out, pen


def split_options(words, count):
    if count == 1:
        yield [words]
        return
    for cut in range(1, len(words) - count + 2):
        for rest in split_options(words[cut:], count - 1):
            yield [words[:cut]] + rest


def arrange(glyphs, metrics, lines, s, pitch):
    """Letters with settled quads (layout units, block centred on 0, 0), stair-stepped: first line left, last line right."""
    xh = metrics['xheight_px']
    rows = []
    for words in lines:
        row, pen = [], 0.0
        for k, word in enumerate(words):
            if k:
                pen += metrics['word'] * xh
            runs, width = word_runs(glyphs, metrics, word)
            row += [(c, pen + x) for c, x in runs]
            pen += width
        rows.append((row, pen))
    block = max(w for _, w in rows)
    letters = []
    for n, (row, width) in enumerate(rows):
        indent = 0.0 if n == 0 else block - width if n == len(rows) - 1 else (block - width) / 2
        baseline = n * pitch * xh
        for char, x in row:
            g = glyphs[char]
            _, iy0, _, iy1 = g.ink()
            bottom = baseline + g.drop * xh
            top = bottom - iy1
            h, w = g.alpha.shape
            letters.append(Letter(char, g, ((indent + x) * s, top * s, (indent + x + w) * s, (top + h) * s), n))
    return letters


def ink_box(letter):
    g, q = letter.glyph, letter.quad
    s = (q[3] - q[1]) / g.alpha.shape[0]
    x0, y0, x1, y1 = g.ink()
    return q[0] + x0 * s, q[1] + y0 * s, q[0] + x1 * s, q[1] + y1 * s


def lines_collide(letters, shrink=0.15):
    boxes = []
    for letter in letters:
        x0, y0, x1, y1 = ink_box(letter)
        w, h = x1 - x0, y1 - y0
        boxes.append((x0 + shrink * w, y0 + shrink * h, x1 - shrink * w, y1 - shrink * h, letter.line))
    for i, a in enumerate(boxes):
        for b in boxes[i + 1:]:
            if a[4] != b[4] and a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]:
                return True
    return False


def extent(letters):
    boxes = [ink_box(l) for l in letters]
    return (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))


def envelope(pool, card):
    """(max width, max height, preferred width, centre) for a card from the European layouts of every card."""
    every = [e for es in pool.lines.values() for e in es]
    widths = [e[2] - e[0] for e in every]
    heights = [e[3] - e[1] for e in every]
    mine = pool.lines.get(card) or every
    centre = (float(np.mean([(e[0] + e[2]) / 2 for e in mine])), float(np.mean([(e[1] + e[3]) / 2 for e in mine])))
    return max(widths), max(heights), float(np.percentile(widths, 75)), centre


def fit(pool, glyphs, text, card):
    """Settled letters of `text`: the largest letter scale (up to the face's own) at which some line break fits the
    European cards' envelope with no line overlapping another; wide single lines give way to stacked ones."""
    metrics = pool.metrics
    words = text.split()
    wmax, hmax, wpref, centre = envelope(pool, card)
    best = None
    for count in range(1, min(len(words), MAX_LINES) + 1):
        for lines in split_options(words, count):
            for pitch in (metrics['pitch'], metrics['pitch'] * 1.15, metrics['pitch'] * 1.3):
                s = metrics['scale']
                while s >= metrics['scale'] * 0.6:
                    letters = arrange(glyphs, metrics, lines, s, pitch)
                    x0, y0, x1, y1 = extent(letters)
                    if x1 - x0 <= wmax and y1 - y0 <= hmax and not lines_collide(letters):
                        break
                    s -= 0.005
                else:
                    continue
                widths = [sum(len(w) for w in line) for line in lines]
                score = (-round(s, 2), x1 - x0 > wpref, count, (max(widths) - min(widths)) / max(widths), pitch)
                if best is None or score < best[0]:
                    best = (score, letters)
                break
    if best is None:
        raise FitError(f'"{text}" does not fit a Classic card')
    letters = best[1]
    x0, y0, x1, y1 = extent(letters)
    dx, dy = centre[0] - (x0 + x1) / 2, centre[1] - (y0 + y1) / 2
    for letter in letters:
        q = letter.quad
        letter.quad = (q[0] + dx, q[1] + dy, q[2] + dx, q[3] + dy)
    return letters


# ---------------------------------------------------------------- atlas and layout


def pack(glyphs, width, height):
    """{char: (x, y, w, h)} of the sprites shelf-packed into the atlas, and the factor they were shrunk by to fit."""
    chars = sorted(glyphs, key=lambda c: (-glyphs[c].alpha.shape[0], c))
    for factor in np.arange(1.0, 0.59, -0.05):
        placed, x, y, shelf = {}, 0, 0, 0
        ok = True
        for c in chars:
            h, w = (int(np.ceil(v * factor)) + 2 * PAD for v in glyphs[c].alpha.shape)
            if x + w > width:
                x, y, shelf = 0, y + shelf, 0
            if y + h > height or w > width:
                ok = False
                break
            placed[c] = (x, y, w, h)
            x += w
            shelf = max(shelf, h)
        if ok:
            return placed, float(factor)
    raise FitError('the letters do not fit the card atlas')


def atlas_image(glyphs, placed, factor, width, height):
    rgba = np.zeros((height, width, 4), np.uint8)
    rgba[..., :3] = 255
    for c, (x, y, w, h) in placed.items():
        alpha = glyphs[c].alpha
        if factor < 1:
            alpha = np.asarray(Image.fromarray(alpha).resize((w - 2 * PAD, h - 2 * PAD), Image.LANCZOS))
        rgba[y + PAD:y + PAD + alpha.shape[0], x + PAD:x + PAD + alpha.shape[1], 3] = alpha
    return rgba


def write_atlas(ftx_bytes, rgba):
    tex = ftex.textures(ftx_bytes)[0]
    patched, _ = patch_ftx.patch(ftx_bytes, tex, rgba, np.ones(rgba.shape[:2], bool))
    return patched


def corners(x0, y0, x1, y1):
    return [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]


def part_record(x, y, w, h):
    c = corners(x + PAD, y + PAD, x + w - PAD, y + h - PAD)
    flat = [v for p in c + c[:1] for v in p]
    return np.array([x + w / 2, y + h / 2] + flat, '<f4').tobytes()


def geom_record(q):
    c = corners(*q)
    flat = [v for p in c + c[:1] for v in p]
    return np.array([(q[0] + q[2]) / 2, (q[1] + q[3]) / 2] + flat, '<f4').tobytes()


def build_layout(mbs_bytes, letters, placed, glyph_order):
    """The card's layout over the new atlas: one part per letter shape, three layers of one key per letter."""
    layout = M.Mbs(mbs_bytes)
    template = layout.key(layout.layer_range(SETTLED)[0])['raw']
    exit_colour = max(1, max((k['colour'] for k in layout.layer_keys(EXIT)), default=1))
    if layout.count(M.COLOURS) <= exit_colour:
        colours = [bytes(layout.record(M.COLOURS, c)) for c in range(layout.count(M.COLOURS))]
        layout.replace(M.COLOURS, colours + [FADE] * (exit_colour + 1 - len(colours)))
    parts = [part_record(*placed[c]) for c in glyph_order]
    part_of = {c: i for i, c in enumerate(glyph_order)}
    settled = [l.quad for l in letters]
    scatter = []
    for q in settled:
        cx, cy = (q[0] + q[2]) / 2, (q[1] + q[3]) / 2
        scatter.append((q[0] + cx, q[1] + cy, q[2] + cx, q[3] + cy))
    stretched = []
    for q in settled:
        cx, w = (q[0] + q[2]) / 2, q[2] - q[0]
        stretched.append((cx - 1.5 * w, q[1], cx + 1.5 * w, q[3]))
    geoms, keys = [], []
    for layer_index, (quads, colour) in enumerate(((scatter, 0), (settled, 0), (stretched, exit_colour))):
        for n, (letter, q) in enumerate(zip(letters, quads)):
            raw = bytearray(template)
            raw[0:4] = n.to_bytes(4, 'little')
            raw[10:12] = part_of[letter.char].to_bytes(2, 'little')
            raw[12:14] = colour.to_bytes(2, 'little')
            raw[14:16] = len(geoms).to_bytes(2, 'little')
            geoms.append(geom_record(q))
            keys.append(bytes(raw))
    layout.replace(M.PARTS, parts)
    layout.replace(M.GEOMS, geoms)
    layout.replace(M.KEYS, keys)
    n = len(letters)
    boxes = []
    for layer_index, quads in ((SCATTER, scatter), (SETTLED, settled), (EXIT, stretched)):
        bbox = (min(q[0] for q in quads), min(q[1] for q in quads), max(q[2] for q in quads), max(q[3] for q in quads))
        layout.set_layer(layer_index, layer_index * n, n, bbox)
        boxes.append(bbox)
    _grow_anim_box(layout, boxes)
    return layout.bytes()


def _grow_anim_box(layout, boxes):
    raw = bytearray(layout.record(M.ANIMS, 0))
    old = np.frombuffer(bytes(raw[:16]), '<f4')
    reach = max(max(abs(v) for b in boxes for v in b) * 2.0, float(np.abs(old).max()))
    raw[:16] = np.array([-reach, -reach, reach, reach], '<f4').tobytes()
    layout.put(M.ANIMS, 0, bytes(raw))


def make_card(pool, text, card, ftx_bytes, mbs_bytes):
    """(Portuguese ftx, Portuguese mbs, note) of one card from the player's own card."""
    glyphs = glyph_set(pool, text)
    letters = fit(pool, glyphs, text, card)
    tex = ftex.textures(ftx_bytes)[0]
    placed, factor = pack(glyphs, tex['width'], tex['height'])
    order = sorted(glyphs)
    rgba = atlas_image(glyphs, placed, factor, tex['width'], tex['height'])
    new_ftx = write_atlas(ftx_bytes, rgba)
    new_mbs = build_layout(mbs_bytes, letters, placed, order)
    s = (letters[0].quad[3] - letters[0].quad[1]) / letters[0].glyph.alpha.shape[0]
    lines = max(l.line for l in letters) + 1
    return new_ftx, new_mbs, f'scale {s:.3f}, {lines} line(s), {len(order)} sprites, atlas factor {factor:.2f}'


def generate(prepared, titles=None, sources=SOURCES):
    titles = titles or read_titles()
    pool = build_pool(prepared, titles)
    targets, notes, designs = {}, [], {}
    for build, archive in sources:
        path = Path(prepared) / build / archive
        if not path.is_file():
            notes.append((build, f'DATA UNAVAILABLE: {archive}'))
            continue
        cards = read_pack(path)
        for row in titles:
            card, text = row['card'], row['pt_br']
            if card not in cards:
                notes.append((f'{build} {card}', 'BLOCKED: no Classic card in the archive'))
                continue
            (ftx_path, ftx_bytes), (mbs_path, mbs_bytes) = cards[card]['ftx'], cards[card]['mbs']
            if (ftx_path, zlib.crc32(ftx_bytes)) in targets and (mbs_path, zlib.crc32(mbs_bytes)) in targets:
                notes.append((f'{build} {card}', 'ok: same card as an earlier build'))
                continue
            try:
                new_ftx, new_mbs, note = make_card(pool, text, card, ftx_bytes, mbs_bytes)
            except FitError as e:
                notes.append((f'{build} {card}', f'BLOCKED: {e}'))
                continue
            targets[(ftx_path, zlib.crc32(ftx_bytes))] = (ftx_bytes, new_ftx, [])
            targets[(mbs_path, zlib.crc32(mbs_bytes))] = (mbs_bytes, new_mbs, None)
            designs[card] = (new_ftx, new_mbs)
            notes.append((f'{build} {card}', 'ok: ' + note))
    return targets, notes, pool, designs


def preview(designs, directory):
    """One contact sheet of the generated settled layers."""
    directory.mkdir(parents=True, exist_ok=True)
    tiles = []
    for card in sorted(designs):
        ftx_bytes, mbs_bytes = designs[card]
        atlas = visuals.decode(ftx_bytes, ftex.textures(ftx_bytes)[0])
        image, _ = R.draw(M.Mbs(mbs_bytes), {0: atlas}, layers=[SETTLED], origin=(-230, -130), size=(460, 260))
        tiles.append((card, R.on_background(image)))
    cols = 4
    sheet = Image.new('RGB', (cols * 464, ((len(tiles) + cols - 1) // cols) * 278), (20, 10, 20))
    from PIL import ImageDraw
    draw = ImageDraw.Draw(sheet)
    for n, (card, tile) in enumerate(tiles):
        x, y = (n % cols) * 464, (n // cols) * 278
        sheet.paste(Image.fromarray(tile), (x + 2, y + 16))
        draw.text((x + 4, y + 2), card, fill=(255, 255, 0))
    sheet.save(directory / 'classic-cards.png')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--prepared', default=str(ROOT / 'game' / 'prepared'))
    ap.add_argument('--out', default=str(DEFAULT_OUT))
    ap.add_argument('--report')
    ap.add_argument('--preview')
    args = ap.parse_args()
    visuals.use_native_reader()
    targets, notes, pool, designs = generate(args.prepared)
    blob = oslv.encode([(name, old, new, inserts) for (name, _), (old, new, inserts) in targets.items()])
    Path(args.out).write_bytes(blob)
    print(f'{len(targets)} Classic card entries -> {args.out} ({len(blob)} bytes)')
    for line in pool.report:
        print('pool:', line)
    if args.report:
        with open(args.report, 'w', encoding='utf-8', newline='\n') as f:
            f.write('card\tresult\n' + ''.join(f'{c}\t{r}\n' for c, r in notes))
    if args.preview:
        preview(designs, Path(args.preview))
    blocked = [(c, r) for c, r in notes if 'BLOCKED' in r]
    for c, r in blocked:
        print(c, r)
    return 1 if blocked else 0


if __name__ == '__main__':
    sys.exit(main())
