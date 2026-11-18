#!/usr/bin/env python3
"""The chapter prologue pages (the Installation layouts) in Portuguese (Brazil), for the Spanish slot of Europe.

Every book's prologue is an animation of Installation.mbs (Classic) and GUI/REHD_Installation.mbs (HD): the title and
the poem lines, each a channel whose three layers (drawn in, settled, drawn out) letter the line four times as a glow,
every letter a glyph part of the shared atlas (Installation.ftx, GUI/HD_Installation.ftx). Lines that start within a
few frames of each other share a screen. The Portuguese lines come from the canonical prologue map
(launcher/prologue_translation_table.inc) and fill the screens of the records they translate. Letters the Spanish atlas
lacks come from the French, German and Italian atlases (the same rasterization), or are composed from the atlas' own
letters and marks into its empty rows.

    installation_poems.py write [--prepared game/prepared]     localization/pt-BR/visual/prologues-ptbr.oslv
    installation_poems.py check [--prepared game/prepared]     the delta is current
"""
import argparse
import re
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
import mbs as M  # noqa: E402
import odin_text as OT  # noqa: E402
import oslv  # noqa: E402
import patch_ftx  # noqa: E402
import stage_acts as S  # noqa: E402
import visuals  # noqa: E402

ROOT = HERE.parent.parent
DEFAULT_OUT = ROOT / 'localization' / 'pt-BR' / 'visual' / 'prologues-ptbr.oslv'
TABLE = ROOT / 'launcher' / 'prologue_translation_table.inc'
BUILDS = ('CUSA05262-1.01', 'CUSA05262-1.00')
SLOT = 'ES'
SOURCES = ('ES', 'FR', 'DE', 'IT')  # one rasterization of the face; the English atlas is cut differently
LANGUAGES = SOURCES + ('UK',)
PAGES = (('Installation.mbs', 'Installation.ftx', 'Odin2_OR_{}.CPK'),
         ('GUI/REHD_Installation.mbs', 'GUI/HD_Installation.ftx', 'Odin2_RE_{}.CPK'))
BOOKS = {'VALKYRIE': 'a0_2_1', 'POOKA': 'b0_2_1', 'FAIRY': 'c0_2_1', 'BLACK': 'd0_2_1', 'FATE': 'e0_2_1',
         'ENDING': 'f0_1_0', 'REEL': None}
TITLES = {'VALKYRIE': 'L_VALKYRIE', 'POOKA': 'L_POOKA', 'FAIRY': 'L_FAERIE', 'BLACK': 'L_BLACK', 'FATE': 'L_FATE',
          'ENDING': 'L_ENDING', 'REEL': 'L_REEL'}
# The page title is the book name: d0_2_1#0 is the longer card title, #1 the book.
TITLE_RECORD, POEM_FROM = 1, 2
# Lines of the official pages that differ from their story text (None: not read).
LAYOUT_TEXT = {
    ('FR', 'VALKYRIE'): ['VALKYRIE', "L'âme exprime sa fierté", 'après être tombée au combat.', None,
                         "C'est la seule méthode", "pour rendre l'âme immortelle."],
}
COPIES = 4
GROUP_GAP = 100            # frames between the starts of two lines of one screen
LETTER = 1                 # key flags byte of a glyph; the wipe masks of a line use another
UPRIGHT = ((0, 0), (0, 1), (1, 1), (1, 0))
MARKS = {'́': 'acute', '̀': 'grave', '̂': 'circumflex', '̃': 'tilde', '̈': 'diaeresis'}
DESCENDERS = set('gjpqyQ,;¿çß')
MIN_SQUEEZE = 0.85
ATLAS_TOP = 560            # first row of the atlas the Spanish pages leave empty, block aligned with a margin
CELL_GAP = 8


class PageError(Exception):
    pass


# ------------------------------------------------------------------ data

class Prepared:
    def __init__(self, root):
        self.root = Path(root)
        self.packs = {}

    def read(self, build, archive, entry):
        path = self.root / build / archive
        if path not in self.packs:
            self.packs[path] = cpk.Cpk(path, original=True) if path.is_file() else None
        pack = self.packs[path]
        if pack is None:
            return None
        hit = next((e for e in pack.entries if e['path'].lower() == entry.lower()), None)
        return pack.read(hit) if hit else None

    def story(self, build, language, container):
        data = self.read(build, f'Odin2_OR_{language}.CPK', container.upper() + '.txt')
        return OT.parse_story(data).records if data else None

    def close(self):
        for pack in self.packs.values():
            if pack:
                pack.f.close()


def read_table(path=TABLE):
    """{container: {index: Portuguese record}} of the canonical prologue map."""
    row = re.compile(r'^\{"([a-z0-9_]+)", (\d+), "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)"\},$')
    out = defaultdict(dict)
    for line in path.read_text(encoding='utf-8').splitlines():
        m = row.match(line)
        if not m:
            raise PageError(f'{path.name}: unreadable row {line!r}')
        out[m.group(1)][int(m.group(2))] = re.sub(r'\\(.)', lambda e: '\n' if e.group(1) == 'n' else e.group(1),
                                                  m.group(4))
    return out


def portuguese_page(table, anim):
    """(title, [(record index, line)]) of a page in Portuguese: the book title in capitals, as every official page
    spells it, then the poem records split at their line breaks."""
    container = BOOKS[anim]
    if container is None:
        return S.PORTUGUESE[S.OFFICIAL[TITLES[anim]]['UK'][0]], []
    records = table[container]
    title = OT.visible(records[TITLE_RECORD]).strip().upper()
    return title, record_lines(records)


def record_lines(records):
    out = []
    for index in sorted(records):
        if index >= POEM_FROM:
            out += [(index, OT.visible(line).strip()) for line in OT.lines(records[index])]
    return out


def official_page(prepared, build, language, anim):
    """(title, [(record index, line)]) the official page of `language` spells."""
    title = S.OFFICIAL[TITLES[anim]][language][0]
    container = BOOKS[anim]
    if container is None:
        return title, []
    records = prepared.story(build, language, container)
    if records is None:
        raise PageError(f'{language}: {container} not found')
    return title, record_lines(dict(enumerate(records)))


# ------------------------------------------------------------------ page structure

@dataclass
class Line:
    channel: int
    layers: tuple              # (drawn in, settled, drawn out)
    start: int                 # frames before the line is drawn
    glow: int = -1             # the light channel sweeping the line


def is_letter(key):
    return key['raw'][5] == LETTER


def letter_keys(layout, layer):
    return [k for k in layout.layer_keys(layer) if is_letter(k)]


def channel_layers(layout, channel):
    out = []
    for s in layout.channel_segments(channel):
        layer = layout.segment_layer(s)
        if layer < layout.count(M.LAYERS) and layer not in out:
            out.append(layer)
    return out


def segment_frames(layout, segment):
    return struct.unpack_from('<H', layout.record(M.SEGMENTS, segment), 6)[0]


def anim_channels(layout, anim):
    first, count = next((f, c) for n, f, c in layout.anims() if n == anim)
    return list(range(first, first + count))


def page_lines(layout, anim):
    """[Line] of a page's lettered channels in order: the title, then the poem."""
    lines, channels = [], anim_channels(layout, anim)
    for channel in channels:
        layers = [l for l in channel_layers(layout, channel) if len(letter_keys(layout, l)) > COPIES]
        if len(layers) == 3:
            segments = list(layout.channel_segments(channel))
            lines.append(Line(channel, tuple(layers), segment_frames(layout, segments[0])))
    taken = {l.channel for l in lines}
    for line in lines:
        ys = [layout.segment_transform(s)[1] for s in layout.channel_segments(line.channel)]
        for other in channels:
            theirs = list(layout.channel_segments(other))
            if other in taken or segment_frames(layout, theirs[0]) != line.start:
                continue
            if all(any(abs(layout.segment_transform(s)[1] - y) < 1e-3 for y in ys) for s in theirs):
                line.glow = other
                taken.add(other)
                break
    return lines


def screens(lines):
    """The poem lines grouped by screen."""
    out = []
    for line in lines:
        if out and line.start - out[-1][-1].start <= GROUP_GAP:
            out[-1].append(line)
        else:
            out.append([line])
    return out


def copies(layout, layer):
    """(copies, extras) of a layer's glyph keys: the four glow copies of the line ([[key] * n] * 4), then glyphs added
    after them, each drawn four times in a row ([[key] * 4] * m): accent marks, or letters a translator appended."""
    keys = letter_keys(layout, layer)
    parts = [k['part'] for k in keys]
    for n in range(len(keys) // COPIES, 0, -1):
        rest = parts[COPIES * n:]
        if any(parts[i * n:(i + 1) * n] != parts[:n] for i in range(COPIES)) or len(rest) % COPIES:
            continue
        if all(len(set(rest[j:j + COPIES])) == 1 for j in range(0, len(rest), COPIES)):
            return ([keys[i * n:(i + 1) * n] for i in range(COPIES)],
                    [keys[COPIES * n + j:COPIES * n + j + COPIES] for j in range(0, len(rest), COPIES)])
    raise PageError(f'layer {layer}: {len(keys)} glyph keys are not {COPIES} copies')


def box(layout, key):
    q = layout.geom(key['geom'])
    xs, ys = [p[0] for p in q], [p[1] for p in q]
    return min(xs), min(ys), max(xs), max(ys)


def orientation(layout, key):
    q = layout.geom(key['geom'])
    xs, ys = [p[0] for p in q], [p[1] for p in q]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    return tuple((int(round((p[0] - x0) / max(x1 - x0, 1e-6))), int(round((p[1] - y0) / max(y1 - y0, 1e-6))))
                 for p in q[:4])


def glyph_runs(layout, layer, copy=0):
    """The glyphs of one glow copy in reading order: [(key, every copy of the key)], marks apart."""
    letters, extras = copies(layout, layer)
    runs = [(letters[copy][i], [c[i] for c in letters]) for i in range(len(letters[0]))]
    runs += [(group[copy], group) for group in extras]
    glyphs, marks = split_marks([(k, box(layout, k), every) for k, every in runs])
    return sorted(glyphs, key=lambda g: g[1][0]), marks


def split_marks(items):
    """(glyphs, marks): a mark sits wholly above the tops of the line's glyphs and is much shorter than they are."""
    if not items:
        return [], []
    top = median(b[1] for _, b, _ in items)
    height = median(b[3] - b[1] for _, b, _ in items)
    marks = [it for it in items if it[1][3] <= top + 0.25 * height and it[1][3] - it[1][1] < 0.75 * height]
    return [it for it in items if it not in marks], marks


def letters_of(text):
    return [ch for ch in unicodedata.normalize('NFC', text) if not ch.isspace()]


@dataclass
class Spelled:
    text: str
    glyphs: list               # [(key, box, every copy, character)] in reading order
    word_starts: set           # indexes of the glyphs that begin a word


def spell(layout, layer, text):
    """A settled layer read as `text`: every glyph named by the letter it draws (an accent drawn apart names its letter
    by the base letter)."""
    glyphs, marks = glyph_runs(layout, layer)
    chars = letters_of(text)
    if len(glyphs) != len(chars):
        raise PageError(f'{len(glyphs)} glyphs do not spell {text!r}')
    on = defaultdict(int)
    for _, b, _ in marks:
        at = min(range(len(glyphs)), key=lambda i: abs((glyphs[i][1][0] + glyphs[i][1][2]) / 2 - (b[0] + b[2]) / 2))
        on[at] += 1
    out = []
    for i, ((key, b, every), ch) in enumerate(zip(glyphs, chars)):
        if on.get(i):
            decomposed = unicodedata.normalize('NFD', ch)
            if len(decomposed) - 1 != on[i] or any(m not in MARKS for m in decomposed[1:]):
                raise PageError(f'{text!r}: the marks over {ch!r} do not match')
            ch = decomposed[0]
        out.append((key, b, every, ch))
    starts, n = set(), 0
    for word in text.split():
        starts.add(n)
        n += len(letters_of(word))
    return Spelled(text, out, starts)


# ------------------------------------------------------------------ the face

@dataclass
class Glyph:
    char: str
    kind: str                  # 'title' or 'body'
    source: str                # language whose atlas holds it, or 'composed'
    part: bytes                # part record (atlas UV) in the source layout
    pattern: tuple             # corner order of the quads drawing it
    w: float                   # natural drawn width, layout units
    h: float
    bottom: float              # below the baseline (positive) or above it


@dataclass
class Face:
    glyphs: dict               # (kind, char) -> Glyph
    right: dict                # (kind, char) -> bearing after the glyph
    left: dict
    space: dict                # kind -> extra gap of a space
    budget: dict               # kind -> widest official line
    title_squeeze: dict        # anim -> horizontal scale of the Spanish title
    atlas: set                 # (kind, char) the Portuguese atlas holds

    def glyph(self, kind, char):
        g = self.glyphs.get((kind, char))
        if g is None:
            raise PageError(f'no {char!r} in the {kind} face')
        return g

    def bearing(self, table, kind, char):
        if (kind, char) in table:
            return table[(kind, char)]
        base = unicodedata.normalize('NFD', char)[0]
        if (kind, base) in table:
            return table[(kind, base)]
        return median(v for (k, _), v in table.items() if k == kind)


def kind_of(index):
    return 'title' if index == 0 else 'body'


def read_language(prepared, build, page, language):
    """[(anim, line index, Spelled)] of every line of an official page that spells its text."""
    data = prepared.read(build, page[2].format(language), page[0])
    if data is None:
        raise PageError(f'{build} {language}: {page[0]} not found')
    layout = M.Mbs(data)
    out = []
    for anim in BOOKS:
        lines = page_lines(layout, anim)
        title, poem = official_page(prepared, build, language, anim)
        texts = LAYOUT_TEXT.get((language, anim)) or [title] + [line for _, line in poem]
        if len(texts) != len(lines):
            texts = texts[:1]
        for index, (line, text) in enumerate(zip(lines, texts)):
            if text is None:
                continue
            try:
                out.append((anim, index, spell(layout, line.layers[1], text)))
            except PageError:
                continue
    return layout, out


def line_scale(spelled, natural, kind):
    return median((b[2] - b[0]) / natural[(kind, ch)] for _, b, _, ch in spelled.glyphs)


def baseline_of(boxes_chars):
    flat = [b[3] for b, ch in boxes_chars if ch not in DESCENDERS] or [b[3] for b, _ in boxes_chars]
    return median(flat)


def learn(prepared, build, page):
    """The face of a page: every glyph the Spanish, French, German and Italian layouts draw, with their metrics, and the
    bearings that space them, fitted to every official line."""
    layouts, lines = {}, []
    for language in LANGUAGES:
        layout, read = read_language(prepared, build, page, language)
        layouts[language] = layout
        lines += [(language, anim, index, sp) for anim, index, sp in read]
    natural = defaultdict(float)
    for language, _, index, sp in lines:
        if language in SOURCES:
            for _, b, _, ch in sp.glyphs:
                natural[(kind_of(index), ch)] = max(natural[(kind_of(index), ch)], b[2] - b[0])
    samples = defaultdict(lambda: defaultdict(list))
    gaps = {'title': [], 'body': []}
    budget = {'title': 0.0, 'body': 0.0}
    squeeze = {}
    for language, anim, index, sp in lines:
        kind = kind_of(index)
        width = sp.glyphs[-1][1][2] - sp.glyphs[0][1][0]
        budget[kind] = max(budget[kind], width)
        if language not in SOURCES:
            continue
        q = line_scale(sp, natural, kind)
        if language == SLOT and index == 0:
            squeeze[anim] = q
        base = baseline_of([(b, ch) for _, b, _, ch in sp.glyphs])
        for key, b, _, ch in sp.glyphs:
            samples[(kind, ch)][language].append((key, b, base))
        for i in range(1, len(sp.glyphs)):
            gap = (sp.glyphs[i][1][0] - sp.glyphs[i - 1][1][2]) / q
            gaps[kind].append((sp.glyphs[i - 1][3], sp.glyphs[i][3], i in sp.word_starts, gap))
    glyphs = {}
    for (kind, ch), by_language in samples.items():
        language = next(l for l in SOURCES if l in by_language)
        found = by_language[language]
        layout = layouts[language]
        key = found[0][0]
        glyphs[(kind, ch)] = Glyph(ch, kind, language, bytes(layout.record(M.PARTS, key['part'])),
                                   orientation(layout, key), natural[(kind, ch)],
                                   median(b[3] - b[1] for _, b, _ in found), median(b[3] - base for _, b, base in found))
    right, left, space = fit_bearings(gaps)
    atlas = {key for key, g in glyphs.items() if g.source == SLOT}
    return Face(glyphs, right, left, space, budget, squeeze, atlas), layouts


def fit_bearings(gaps, ridge=0.05):
    """Per glyph right and left bearings and a space width per kind, least squares over every official gap."""
    right, left, space = {}, {}, {}
    for kind, rows in gaps.items():
        chars = sorted({a for a, _, _, _ in rows} | {b for _, b, _, _ in rows})
        index = {c: i for i, c in enumerate(chars)}
        n = len(chars)
        a = np.zeros((len(rows) + 2 * n, 2 * n + 1))
        y = np.zeros(len(rows) + 2 * n)
        for r, (p, c, spaced, gap) in enumerate(rows):
            a[r, index[p]] = 1
            a[r, n + index[c]] = 1
            a[r, 2 * n] = 1 if spaced else 0
            y[r] = gap
        for i in range(2 * n):
            a[len(rows) + i, i] = ridge
        x = np.linalg.lstsq(a, y, rcond=None)[0]
        for c, i in index.items():
            right[(kind, c)] = float(x[i])
            left[(kind, c)] = float(x[n + i])
        space[kind] = float(x[2 * n])
    return right, left, space


# ------------------------------------------------------------------ atlas

def texture(blob):
    textures = ftex.textures(blob)
    if len(textures) != 1 or textures[0]['dfmt'] != patch_ftx.BC3:
        raise PageError('the prologue atlas is not one BC3 texture')
    return textures[0]


def corners(record):
    f = struct.unpack('<12f', record)
    return [(f[2 + 2 * i], f[3 + 2 * i]) for i in range(4)]


def upright(atlas, record, pattern):
    """The sprite of a part as drawn on screen, RGBA [h, w]."""
    uv = corners(record)
    drawn = {p: uv[i] for i, p in enumerate(pattern)}
    tl, tr, bl = np.array(drawn[(0, 0)]), np.array(drawn[(1, 0)]), np.array(drawn[(0, 1)])
    w, h = int(round(np.abs(tr - tl).sum())), int(round(np.abs(bl - tl).sum()))
    ux, vy = (tr - tl) / max(w, 1), (bl - tl) / max(h, 1)
    jj, ii = np.meshgrid(np.arange(w) + 0.5, np.arange(h) + 0.5)
    xs = np.floor(tl[0] + ux[0] * jj + vy[0] * ii).astype(int)
    ys = np.floor(tl[1] + ux[1] * jj + vy[1] * ii).astype(int)
    return atlas[ys, xs].copy()


def ink(alpha, level=24):
    rows, cols = np.flatnonzero((alpha > level).any(1)), np.flatnonzero((alpha > level).any(0))
    return int(rows[0]), int(rows[-1]) + 1, int(cols[0]), int(cols[-1]) + 1


def over(canvas, tile, x, y):
    """Straight-alpha `tile` composited over `canvas` at (x, y)."""
    h, w = tile.shape[:2]
    dst = canvas[y:y + h, x:x + w].astype(np.float64) / 255
    src = tile.astype(np.float64) / 255
    a = src[..., 3:] + dst[..., 3:] * (1 - src[..., 3:])
    rgb = np.where(a > 0, (src[..., :3] * src[..., 3:] + dst[..., :3] * dst[..., 3:] * (1 - src[..., 3:])) /
                   np.maximum(a, 1e-9), 0)
    canvas[y:y + h, x:x + w] = np.round(np.concatenate([rgb, a], -1) * 255).astype(np.uint8)


@dataclass
class Made:
    """A glyph drawn into the atlas: its upright pixels and how its quad relates to the letter it is built on."""
    char: str
    kind: str
    image: np.ndarray
    base: str                  # the letter whose metrics it extends
    left: int                  # base sprite's place in the image
    top: int
    how: str


def compose_mark(base_img, donor_img, plain_img, char, kind, base, how):
    """`base` with the mark of `donor` (a letter with the mark over its `plain` form): the mark keeps its distance above
    the letter and its offset from the letter's centre."""
    plain_a, donor_a = plain_img[..., 3].astype(int), donor_img[..., 3].astype(int)
    p_top, p_bottom, p_left, p_right = ink(plain_a)
    body = plain_a[p_top:p_bottom, p_left:p_right]
    fits = []
    for y in range(0, donor_a.shape[0] - body.shape[0] + 1):
        for x in range(0, donor_a.shape[1] - body.shape[1] + 1):
            fits.append((np.abs(donor_a[y:y + body.shape[0], x:x + body.shape[1]] - body).sum(), y, x))
    if not fits:
        raise PageError(f'{char}: the plain letter is larger than the accented one')
    _, cut, at_x = min(fits)
    p_left, p_right = at_x, at_x + (p_right - p_left)
    mark = donor_img[:cut].copy()
    if not (mark[..., 3] > 24).any():
        raise PageError(f'{char}: no mark above the letter')
    m_top, m_bottom, m_left, m_right = ink(mark[..., 3])
    lift = cut - m_bottom
    centre_offset = (m_left + m_right) / 2 - (p_left + p_right) / 2
    mark = mark[m_top:m_bottom, m_left:m_right]
    b_top, _, b_left, b_right = ink(base_img[..., 3])
    x = int(round((b_left + b_right) / 2 + centre_offset - mark.shape[1] / 2))
    y = b_top - lift - mark.shape[0]
    pad_left, pad_top = max(0, -x), max(0, -y)
    width = max(base_img.shape[1] + pad_left, x + pad_left + mark.shape[1])
    canvas = np.zeros((base_img.shape[0] + pad_top, width, 4), np.uint8)
    canvas[pad_top:, pad_left:pad_left + base_img.shape[1]] = base_img
    over(canvas, mark, x + pad_left, y + pad_top)
    return Made(char, kind, canvas, base, pad_left, pad_top, how)


def spans(alpha, level=24):
    """Ink width of every row."""
    out = []
    for row in alpha > level:
        cols = np.flatnonzero(row)
        out.append(int(cols[-1] - cols[0] + 1) if len(cols) else 0)
    return np.array(out)


def compose_cedilla(c_img, comma_img, kind):
    """ç: the hooked tail of the face's comma (below its head) hung from the foot of the c's bowl."""
    widths = spans(comma_img[..., 3])
    widest = int(np.argmax(widths))
    waist = next((r for r in range(widest, len(widths)) if 0 < widths[r] <= 0.6 * widths[widest]), None)
    if waist is None:
        raise PageError('ç: the comma has no tail')
    tail = comma_img[waist:].copy()
    t_top, t_bottom, t_left, t_right = ink(tail[..., 3])
    tail = tail[t_top:t_bottom, t_left:t_right]
    head = np.flatnonzero(tail[0, :, 3] > 24)
    c_top, c_bottom, c_left, c_right = ink(c_img[..., 3])
    x = int(round((c_left + c_right) / 2 - (head[0] + head[-1]) / 2))
    y = c_bottom - 1
    pad_left = max(0, -x)
    width = max(c_img.shape[1] + pad_left, x + pad_left + tail.shape[1])
    canvas = np.zeros((max(c_img.shape[0], y + tail.shape[0]), width, 4), np.uint8)
    canvas[:c_img.shape[0], pad_left:pad_left + c_img.shape[1]] = c_img
    over(canvas, tail, x + pad_left, y)
    return Made('ç', kind, canvas, 'c', pad_left, 0, "c with the hooked tail of the face's comma hung from its foot")


def compose_hyphen(z_img, n_img, kind):
    """-: the closing stroke of the face's z (its widest rows at the foot), centred on the height of the n's bowl."""
    alpha = z_img[..., 3]
    widths = spans(alpha)
    top, bottom, _, _ = ink(alpha)
    foot = top + int(np.argmax(widths[top:bottom]))
    if foot < (top + bottom) / 2:
        raise PageError('-: the z has no closing stroke at its foot')
    r0 = foot
    while r0 - 1 > top and widths[r0 - 1] >= 0.45 * widths[foot]:
        r0 -= 1
    r1 = foot + 1
    while r1 < bottom and widths[r1] >= 0.3 * widths[foot]:
        r1 += 1
    stroke = z_img[r0:r1].copy()
    s_top, s_bottom, s_left, s_right = ink(stroke[..., 3])
    stroke = stroke[s_top:s_bottom, s_left:s_right]
    n_top, n_bottom, _, _ = ink(n_img[..., 3])
    pad = 2
    canvas = np.zeros((n_img.shape[0], stroke.shape[1] + 2 * pad, 4), np.uint8)
    y = int(round((n_top + n_bottom) / 2 - stroke.shape[0] / 2))
    canvas[y:y + stroke.shape[0], pad:pad + stroke.shape[1]] = stroke
    return Made('-', kind, canvas, 'n', 0, 0, "the closing stroke of the face's z")


TILDED = {'ã': 'a', 'õ': 'o'}


def made_glyphs(face, atlases, needed):
    """The needed glyphs no official atlas holds, composed from the face's own letters and marks."""
    def img(ch):
        g = face.glyph('body', ch)
        return upright(atlases[g.source], g.part, g.pattern)
    out = []
    for kind, ch in sorted(needed):
        if (kind, ch) in face.glyphs:
            continue
        if kind != 'body':
            raise PageError(f'no {ch!r} in the {kind} face')
        if ch in TILDED:
            out.append(compose_mark(img(TILDED[ch]), img('ñ'), img('n'), ch, kind, TILDED[ch],
                                    f'{TILDED[ch]} with the tilde of the Spanish ñ'))
        elif ch == 'ç':
            out.append(compose_cedilla(img('c'), img(','), kind))
        elif ch == '-':
            out.append(compose_hyphen(img('z'), img('n'), kind))
        else:
            raise PageError(f'no {ch!r} in the {kind} face')
    return out


def drawn_px(record, pattern):
    """(w, h) in atlas pixels of a part as drawn."""
    uv = corners(record)
    drawn = {p: np.array(uv[i]) for i, p in enumerate(pattern)}
    return (float(np.abs(drawn[(1, 0)] - drawn[(0, 0)]).sum()), float(np.abs(drawn[(0, 1)] - drawn[(0, 0)]).sum()))


def add_made(face, made, record):
    """The face entry of a composed glyph, drawn at the scale of its base letter, its bearings the base letter's less
    what the composition adds on either side."""
    base = face.glyph(made.kind, made.base)
    px_w, px_h = drawn_px(base.part, base.pattern)
    sx, sy = base.w / px_w, base.h / px_h
    h, w = made.image.shape[:2]
    face.glyphs[(made.kind, made.char)] = Glyph(made.char, made.kind, 'composed', record, UPRIGHT, w * sx, h * sy,
                                                base.bottom + (h - made.top - px_h) * sy)
    if unicodedata.normalize('NFD', made.char)[0] == made.base:
        key = (made.kind, made.char)
        face.left[key] = face.bearing(face.left, made.kind, made.base) - made.left * sx
        face.right[key] = face.bearing(face.right, made.kind, made.base) - (w - made.left - px_w) * sx


def rect_of(record):
    uv = corners(record)
    return (int(min(u for u, _ in uv)), int(min(v for _, v in uv)), int(max(u for u, _ in uv)),
            int(max(v for _, v in uv)))


def part_record(x0, y0, x1, y1):
    c = [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]
    return struct.pack('<12f', (x0 + x1) / 2, (y0 + y1) / 2, *[v for p in c + c[:1] for v in p])


def moved(record, dx, dy):
    f = list(struct.unpack('<12f', record))
    for i in range(0, 12, 2):
        f[i] += dx
        f[i + 1] += dy
    return struct.pack('<12f', *f)


class Packer:
    """Shelf packing into the atlas rows below ATLAS_TOP, every cell on whole 4x4 blocks with a free block around it."""

    def __init__(self, width, height):
        self.width, self.height = width, height
        self.x, self.y, self.row = 0, ATLAS_TOP, 0

    def place(self, w, h):
        cw, ch = -(-w // 4) * 4, -(-h // 4) * 4
        if self.x + cw > self.width:
            self.x, self.y, self.row = 0, self.y + self.row + CELL_GAP, 0
        if self.y + ch > self.height:
            raise PageError('the atlas has no room left')
        at = (self.x, self.y)
        self.x += cw + CELL_GAP
        self.row = max(self.row, ch)
        return at


def build_atlas(face, blob, atlases, needed):
    """The Spanish atlas with every glyph the Portuguese pages need and it lacks; the face gains their entries."""
    tex = texture(blob)
    pixels = visuals.decode(blob, tex).copy()
    if (pixels[ATLAS_TOP - 4:, :, 3] > 0).any():
        raise PageError('the atlas rows the glyphs go to are not empty')
    changed = np.zeros(pixels.shape[:2], bool)
    packer = Packer(tex['width'], tex['height'])
    added = []
    for (kind, ch), g in sorted(face.glyphs.items()):
        if g.source == SLOT or (kind, ch) not in needed:
            continue
        x0, y0, x1, y1 = rect_of(g.part)
        sprite = atlases[g.source][y0:y1, x0:x1]
        x, y = packer.place(x1 - x0, y1 - y0)
        pixels[y:y + y1 - y0, x:x + x1 - x0] = sprite
        changed[y:y + y1 - y0, x:x + x1 - x0] = True
        g.part = moved(g.part, x - x0, y - y0)
        face.atlas.add((kind, ch))
        added.append((kind, ch, g.source, (x, y, x + x1 - x0, y + y1 - y0), f'{g.source} atlas'))
    for made in made_glyphs(face, atlases, needed):
        h, w = made.image.shape[:2]
        x, y = packer.place(w, h)
        pixels[y:y + h, x:x + w] = made.image
        changed[y:y + h, x:x + w] = True
        add_made(face, made, part_record(x, y, x + w, y + h))
        face.atlas.add((made.kind, made.char))
        added.append((made.kind, made.char, 'composed', (x, y, x + w, y + h), made.how))
    out, _ = patch_ftx.patch(blob, tex, pixels, changed)
    return out, added


# ------------------------------------------------------------------ setting a line

def set_line(face, kind, text, centre, baseline, q):
    """[(glyph, (x0, y0, x1, y1))] of `text` centred on `centre`, its glyphs and gaps scaled by q horizontally."""
    items, x, prev, spaced = [], 0.0, None, False
    for ch in unicodedata.normalize('NFC', text):
        if ch.isspace():
            spaced = True
            continue
        g = face.glyph(kind, ch)
        if prev is not None:
            x += face.bearing(face.right, kind, prev) + face.bearing(face.left, kind, ch) + (face.space[kind] if spaced else 0)
        items.append((g, (x, baseline + g.bottom - g.h, x + g.w, baseline + g.bottom)))
        x += g.w
        prev, spaced = ch, False
    width = items[-1][1][2] - items[0][1][0]
    shift = centre - width * q / 2
    return [(g, (shift + b[0] * q, b[1], shift + b[2] * q, b[3])) for g, b in items]


def width_of(items):
    return items[-1][1][2] - items[0][1][0]


@dataclass
class Plan:
    """A Portuguese line of a page: the Spanish channel it takes and its settled glyphs."""
    line: Line
    text: str
    items: list
    squeeze: float


def fit(face, kind, text, centre, baseline, q):
    """The line set at scale q, narrowed (not below MIN_SQUEEZE of q) to the widest official line of its kind."""
    items = set_line(face, kind, text, centre, baseline, q)
    width = width_of(items)
    if width > face.budget[kind]:
        narrower = q * face.budget[kind] / width
        if narrower < MIN_SQUEEZE * q:
            raise PageError(f'{text!r}: {width:.0f} units, the widest official line is {face.budget[kind]:.0f}')
        q = narrower
        items = set_line(face, kind, text, centre, baseline, q)
    return items, q


def line_frame(layout, line):
    """(centre, baseline) of a Spanish settled line."""
    glyphs, _ = glyph_runs(layout, line.layers[1])
    boxes = [b for _, b, _ in glyphs]
    return (min(b[0] for b in boxes) + max(b[2] for b in boxes)) / 2, median(b[3] for b in boxes)


def joined(texts, face, kind):
    """`texts` ([(record index, line)]) with one adjacent pair joined: a record's own line break first, then a line
    the next continues (it starts in lower case), then the narrowest; never wider than the widest official line."""
    best = None
    for i in range(len(texts) - 1):
        (r0, a), (r1, b) = texts[i], texts[i + 1]
        text = a + ' ' + b
        width = width_of(set_line(face, kind, text, 0.0, 0.0, 1.0))
        if width * MIN_SQUEEZE > face.budget[kind]:
            continue
        rank = (r0 != r1, not b[:1].islower(), width)
        if best is None or rank < best[0]:
            best = (rank, i, (r0, text))
    if best is None:
        raise PageError(f'{[t for _, t in texts]}: no two lines fit one line')
    _, i, item = best
    return texts[:i] + [item] + texts[i + 2:]


def plan_page(layout, face, anim, spanish, portuguese):
    """[(Spanish lines, [Plan])] of a page's screens: the title, then each screen's Portuguese lines over its Spanish
    channels."""
    lines = page_lines(layout, anim)
    title, poem = portuguese
    _, es_poem = spanish
    if len(lines) != 1 + len(es_poem):
        raise PageError(f'{anim}: {len(lines)} lettered channels for {1 + len(es_poem)} Spanish lines')
    centre, baseline = line_frame(layout, lines[0])
    items, q = fit(face, 'title', title, centre, baseline, face.title_squeeze[anim])
    out = [(lines[:1], [Plan(lines[0], title, items, q)])]
    record_of = {line.channel: index for line, (index, _) in zip(lines[1:], es_poem)}
    for screen in screens(lines[1:]):
        records = sorted({record_of[line.channel] for line in screen})
        texts = [(index, text) for index, text in poem if index in records]
        while len(texts) > len(screen):
            texts = joined(texts, face, 'body')
        plans = []
        for line, (_, text) in zip(screen, texts):
            centre, baseline = line_frame(layout, line)
            items, q = fit(face, 'body', text, centre, baseline, 1.0)
            plans.append(Plan(line, text, items, q))
        out.append((screen, plans))
    placed = sorted(record_of[l.channel] for screen in screens(lines[1:]) for l in screen)
    if placed and set(placed) != {index for index, _ in poem}:
        raise PageError(f'{anim}: the Spanish records do not match the Portuguese ones')
    return out


# ------------------------------------------------------------------ writing a page

def geom_record(b):
    c = [(b[0], b[1]), (b[0], b[3]), (b[2], b[3]), (b[2], b[1])]
    return struct.pack('<12f', (b[0] + b[2]) / 2, (b[1] + b[3]) / 2, *[v for p in c + c[:1] for v in p])


def oriented_geom(b, pattern):
    """A quad over box b whose corners follow `pattern` (the corner order the official keys draw the glyph with)."""
    x = (b[0], b[2])
    y = (b[1], b[3])
    c = [(x[i], y[j]) for i, j in pattern]
    return struct.pack('<12f', (b[0] + b[2]) / 2, (b[1] + b[3]) / 2, *[v for p in c + c[:1] for v in p])


@dataclass
class Pass:
    """How a layer draws each glow copy of a glyph relative to its settled box."""
    dx: list                   # per copy, horizontal offset of the centre
    dy: list                   # per copy, vertical offset
    k: float                   # width factor


SETTLED = Pass([0.0] * COPIES, [0.0] * COPIES, 1.0)


def layer_pass(layout, line, layer):
    settled, _ = copies(layout, line.layers[1])
    other, _ = copies(layout, layer)
    dx, dy, ks = [], [], []
    for c in range(COPIES):
        xs, ys = [], []
        for a, b in zip(other[c], settled[c]):
            ba, bb = box(layout, a), box(layout, b)
            xs.append((ba[0] + ba[2]) / 2 - (bb[0] + bb[2]) / 2)
            ys.append(ba[1] - bb[1])
            ks.append((ba[2] - ba[0]) / max(bb[2] - bb[0], 1e-6))
        dx.append(median(xs))
        dy.append(median(ys))
    return Pass(dx, dy, median(ks))


def passed(items, p, copy):
    out = []
    for g, b in items:
        cx, w = (b[0] + b[2]) / 2 + p.dx[copy], (b[2] - b[0]) * p.k
        out.append((g, (cx - w / 2, b[1] + p.dy[copy], cx + w / 2, b[3] + p.dy[copy])))
    return out


class Writer:
    """Writes keys and quads of rewritten layers over the Spanish ones where they fit, appends the rest (and parts,
    segments, transforms), and repoints layers and channels."""

    def __init__(self, layout, layers):
        self.layout = layout
        self.parts = {bytes(layout.record(M.PARTS, i)): i for i in range(layout.count(M.PARTS))}
        self.new_parts, self.geoms, self.geom_at, self.keys = [], [], {}, []
        self.segments, self.transforms, self.layers, self.channels = [], [], {}, {}
        self.key_writes, self.geom_writes = {}, {}
        mine, others = set(), set()
        for l in range(layout.count(M.LAYERS)):
            first, count = layout.layer_range(l)
            span = set(range(first, min(first + count, layout.count(M.KEYS))))
            (mine if l in layers else others).update(span)
        self.own_keys = mine - others

        def geoms_of(keys):
            return {struct.unpack_from('<H', layout.record(M.KEYS, k), 14)[0] for k in keys}
        self.free_geoms = sorted(g for g in geoms_of(mine) - geoms_of(others) if g < layout.count(M.GEOMS))

    def part(self, record):
        if record not in self.parts:
            self.parts[record] = self.layout.count(M.PARTS) + len(self.new_parts)
            self.new_parts.append(record)
        return self.parts[record]

    def geom(self, record):
        if record not in self.geom_at:
            if self.free_geoms:
                at = self.free_geoms.pop(0)
                self.geom_writes[at] = record
            else:
                at = self.layout.count(M.GEOMS) + len(self.geoms)
                self.geoms.append(record)
            self.geom_at[record] = at
        return self.geom_at[record]

    @staticmethod
    def key(template, sprite, part, colour, geom):
        out = bytearray(template)
        struct.pack_into('<I', out, 0, sprite)
        struct.pack_into('<HHH', out, 10, part, colour, geom)
        return bytes(out)

    def layer(self, layer, keys, bbox):
        first, count = self.layout.layer_range(layer)
        if len(keys) <= count and all(k in self.own_keys for k in range(first, first + count)):
            for i, raw in enumerate(keys):
                self.key_writes[first + i] = raw
        else:
            first = self.layout.count(M.KEYS) + len(self.keys)
            self.keys += keys
        self.layers[layer] = (first, len(keys), bbox)

    def channel(self, channel, segments):
        """Channel `channel` plays `segments` (raw records) from now on."""
        first = self.layout.count(M.SEGMENTS) + len(self.segments)
        self.segments += segments
        self.channels[channel] = (first, len(segments))

    def transform(self, record):
        self.transforms.append(record)
        return self.layout.count(M.TRANSFORMS) + len(self.transforms) - 1

    def finish(self):
        layout = self.layout
        for at, record in sorted(self.geom_writes.items()):
            layout.put(M.GEOMS, at, record)
        for at, raw in sorted(self.key_writes.items()):
            layout.put(M.KEYS, at, raw)
        for section, records in ((M.PARTS, self.new_parts), (M.GEOMS, self.geoms), (M.KEYS, self.keys),
                                 (M.TRANSFORMS, self.transforms), (M.SEGMENTS, self.segments)):
            if records:
                layout.append(section, records)
        for layer, (first, count, bbox) in sorted(self.layers.items()):
            layout.set_layer(layer, first, count, bbox)
        for channel, (first, count) in sorted(self.channels.items()):
            raw = bytearray(layout.record(M.CHANNELS, channel))
            struct.pack_into('<HH', raw, 0, first, count)
            layout.put(M.CHANNELS, channel, bytes(raw))


def write_line(writer, layout, plan, sprites):
    """The three layers of a line channel lettered with the plan's glyphs, four glow copies each, the line's wipe masks
    widened when the Portuguese line is wider than the Spanish one."""
    line, items = plan.line, plan.items
    settled_keys, _ = copies(layout, line.layers[1])
    es_extent = {}
    for layer in line.layers:
        boxes = [box(layout, k) for k in letter_keys(layout, layer)]
        es_extent[layer] = (min(b[0] for b in boxes), max(b[2] for b in boxes))
    centre = (es_extent[line.layers[1]][0] + es_extent[line.layers[1]][1]) / 2
    layered = {}
    for layer in line.layers:
        p = SETTLED if layer == line.layers[1] else layer_pass(layout, line, layer)
        layered[layer] = [passed(items, p, c) for c in range(COPIES)]
    stretch = 1.0
    for layer, per_copy in layered.items():
        lo = min(b[0] for copy in per_copy for _, b in copy)
        hi = max(b[2] for copy in per_copy for _, b in copy)
        es_lo, es_hi = es_extent[layer]
        stretch = max(stretch, (centre - lo) / max(centre - es_lo, 1e-6), (hi - centre) / max(es_hi - centre, 1e-6))
    for layer, per_copy in layered.items():
        template = letter_keys(layout, layer)[0]
        keys, boxes = [], []
        for copy in per_copy:
            for g, b in copy:
                keys.append(writer.key(template['raw'], sprites[(g.kind, g.char)], writer.part(g.part),
                                       template['colour'], writer.geom(oriented_geom(b, g.pattern))))
                boxes.append(b)
        for mask in [k for k in layout.layer_keys(layer) if not is_letter(k)]:
            q = layout.geom(mask['geom'])
            b = box(layout, mask)
            b = (centre + (b[0] - centre) * stretch, b[1], centre + (b[2] - centre) * stretch, b[3])
            pattern = tuple((int(px == max(p[0] for p in q)), int(py == max(p[1] for p in q))) for px, py in q[:4])
            keys.append(writer.key(mask['raw'], mask['sprite'], mask['part'], mask['colour'],
                                   writer.geom(oriented_geom(b, pattern))))
            boxes.append(b)
        bbox = (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))
        writer.layer(layer, keys, bbox)


def moved_segments(layout, writer, channel, layer_of=None, y_of=None):
    """The raw segment records of a channel with their layers replaced (layer_of: old -> new) and their transforms'
    y replaced (y_of: segment position -> y)."""
    out = []
    for i, s in enumerate(layout.channel_segments(channel)):
        raw = bytearray(layout.record(M.SEGMENTS, s))
        if layer_of is not None:
            struct.pack_into('<I', raw, 0, layer_of(struct.unpack_from('<I', raw, 0)[0]))
        if y_of is not None and y_of(i) is not None:
            t = struct.unpack_from('<H', raw, 4)[0]
            record = bytearray(layout.record(M.TRANSFORMS, t))
            struct.pack_into('<f', record, 4, y_of(i))
            struct.pack_into('<H', raw, 4, writer.transform(bytes(record)))
        out.append(bytes(raw))
    return out


def segment_ys(layout, channel):
    return [layout.segment_transform(s)[1] for s in layout.channel_segments(channel)]


def restage(layout, writer, screen, used):
    """A screen with fewer Portuguese lines than Spanish channels: the lines take the first channels, centred on the
    screen with its line spacing, and the channels left over draw nothing."""
    ys = [segment_ys(layout, line.channel) for line in screen]
    k = len(screen)
    for i, line in enumerate(screen):
        if i >= used:
            writer.channel(line.channel, moved_segments(layout, writer, line.channel, layer_of=lambda _: 0))
            if line.glow >= 0:
                writer.channel(line.glow, moved_segments(layout, writer, line.glow, layer_of=lambda _: 0))
            continue
        new_y = [sum(y[s] for y in ys) / k + (i - (used - 1) / 2) * (ys[-1][s] - ys[0][s]) / (k - 1)
                 for s in range(len(ys[0]))]
        writer.channel(line.channel, moved_segments(layout, writer, line.channel, y_of=lambda s: new_y[s]))
        if line.glow >= 0:
            old = ys[i]
            glow_ys = segment_ys(layout, line.glow)
            matched, at = [], 0
            for y in glow_ys:
                hit = next((s for s in range(at, len(old)) if abs(old[s] - y) < 1e-3), None)
                if hit is not None:
                    at = hit
                matched.append(new_y[at] - old[at] + y)
            writer.channel(line.glow, moved_segments(layout, writer, line.glow, y_of=lambda s: matched[s]))


def grow_anim_boxes(layout, names):
    for i in range(layout.count(M.ANIMS)):
        raw = bytearray(layout.record(M.ANIMS, i))
        name = bytes(raw[16:40]).split(b'\0')[0].decode('latin1')
        if name.removesuffix('_1') not in names:
            continue
        first, count = struct.unpack_from('<H', raw, 40)[0], raw[42]
        b = list(struct.unpack_from('<4f', raw, 0))
        for ch in range(first, first + count):
            for s in layout.channel_segments(ch):
                layer = layout.segment_layer(s)
                x, y, sx, sy = layout.segment_transform(s)
                lb = struct.unpack_from('<4f', layout.record(M.LAYERS, layer), 0)
                b = [min(b[0], x + lb[0] * sx), min(b[1], y + lb[1] * sy), max(b[2], x + lb[2] * sx),
                     max(b[3], y + lb[3] * sy)]
        struct.pack_into('<4f', raw, 0, *b)
        layout.put(M.ANIMS, i, bytes(raw))


def sprite_ids(layout, face):
    """The sprite id each glyph's keys carry: the official one, new ids after the layout's largest for added glyphs."""
    out, used = {}, set()
    for k in range(layout.count(M.KEYS)):
        used.add(struct.unpack_from('<I', layout.record(M.KEYS, k), 0)[0])
    official = {}
    for k in range(layout.count(M.KEYS)):
        key = layout.key(k)
        if is_letter(key) and key['part'] < layout.count(M.PARTS):
            official.setdefault(bytes(layout.record(M.PARTS, key['part'])), key['sprite'])
    following = max(used) + 1
    for (kind, ch), g in sorted(face.glyphs.items()):
        if g.source == SLOT and g.part in official:
            out[(kind, ch)] = official[g.part]
        else:
            out[(kind, ch)] = following
            following += 1
    return out


def respell(data, face, plans_by_anim):
    """A page layout with every planned line written over its Spanish channels, for the animation and its _1 twin."""
    layout = M.Mbs(data)
    rewritten = {layer for anim in plans_by_anim for _, plans in plans_by_anim[anim] for plan in plans
                 for layer in plan.line.layers}
    writer = Writer(layout, rewritten)
    sprites = sprite_ids(layout, face)
    written = set()
    for anim, _, _ in layout.anims():
        base = anim.removesuffix('_1')
        if base not in plans_by_anim:
            continue
        lines = page_lines(layout, anim)
        page = [lines[:1]] + screens(lines[1:])
        reference = plans_by_anim[base]
        if [len(s) for s in page] != [len(spanish) for spanish, _ in reference]:
            raise PageError(f'{anim}: the page does not match its plan')
        for (_, screen_plans), screen in zip(reference, page):
            for plan, line in zip(screen_plans, screen):
                if line.layers not in written:
                    write_line(writer, layout, Plan(line, plan.text, plan.items, plan.squeeze), sprites)
                    written.add(line.layers)
            if len(screen_plans) < len(screen):
                restage(layout, writer, screen, len(screen_plans))
    writer.finish()
    grow_anim_boxes(layout, set(plans_by_anim))
    return layout.bytes(), layout.inserts


# ------------------------------------------------------------------ reading a page back

def labels_of(face, layout, kinds=None):
    """{(part index, corner order): (kind, character)} of the glyphs of the face's atlas (or of `kinds`, a set of
    (kind, char)) the layout holds."""
    records = defaultdict(list)
    for i in range(layout.count(M.PARTS)):
        records[bytes(layout.record(M.PARTS, i))].append(i)
    out = {}
    for (kind, ch), g in sorted(face.glyphs.items()):
        if (kind, ch) in (kinds if kinds is not None else face.atlas):
            for i in records.get(g.part, ()):
                out[(i, g.pattern)] = (kind, ch)
    return out


def spanish_labels(face, layout):
    return labels_of(face, layout, {key for key, g in face.glyphs.items() if g.source == SLOT})


def decode_layer(layout, layer, labels, face, copy=0):
    """The text a settled layer's glow copy spells, spaces where the gap between two glyphs is a word space."""
    glyphs, _ = glyph_runs(layout, layer, copy)
    if not glyphs:
        return ''
    named = [(labels.get((k['part'], orientation(layout, k)), (None, '�'))[1], b,
              labels.get((k['part'], orientation(layout, k)), ('body', None))[0]) for k, b, _ in glyphs]
    kind = named[0][2] or 'body'
    q = median((b[2] - b[0]) / face.glyph(kind, ch).w for ch, b, _ in named if (kind, ch) in face.glyphs) \
        if any((kind, ch) in face.glyphs for ch, _, _ in named) else 1.0
    text = named[0][0]
    for (prev, pb, _), (ch, b, _) in zip(named, named[1:]):
        letter_gap = face.bearing(face.right, kind, prev) + face.bearing(face.left, kind, ch)
        gap = (b[0] - pb[2]) / q
        text += (' ' if gap > letter_gap + face.space[kind] / 2 else '') + ch
    return text


def decode_page(layout, channels, labels, face, copy=0):
    """[text of each of `channels`] (the lettered channels of the Spanish page); '' for a channel that draws nothing."""
    out = []
    for channel in channels:
        segments = list(layout.channel_segments(channel))
        layer = layout.segment_layer(segments[2]) if len(segments) > 2 else 0
        out.append(decode_layer(layout, layer, labels, face, copy) if len(letter_keys(layout, layer)) > COPIES else '')
    return out


def render_layer(layout, atlas, layer, copy=None, scale=1.0):
    """(RGBA uint8, (x, y) of its top left in layout units) of a layer's glyph keys (one glow copy, or all), the wipe
    masks left out: the sprite of each part mapped onto its quad."""
    from PIL import Image, ImageDraw
    import mbs_render as R
    glyphs, marks = glyph_runs(layout, layer, copy or 0)
    keys = [k for k, _, every in glyphs + marks for k in (every if copy is None else [k])]
    quads = [(k, layout.geom(k['geom'])) for k in keys]
    xs = [p[0] for _, q in quads for p in q]
    ys = [p[1] for _, q in quads for p in q]
    ox, oy = min(xs) - 2, min(ys) - 2
    w, h = int(np.ceil((max(xs) - ox + 2) * scale)), int(np.ceil((max(ys) - oy + 2) * scale))
    canvas = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    source = Image.fromarray(atlas)
    for key, quad in quads:
        dst = [((x - ox) * scale, (y - oy) * scale) for x, y in quad]
        sprite = source.transform((w, h), Image.AFFINE, R._affine(layout.part_uv(key['part']), dst), Image.BILINEAR)
        mask = Image.new('L', (w, h), 0)
        ImageDraw.Draw(mask).polygon(dst, fill=255)
        sprite.putalpha(Image.fromarray(np.minimum(np.asarray(sprite)[..., 3], np.asarray(mask))))
        canvas.alpha_composite(sprite)
    return np.asarray(canvas), (ox, oy)


# ------------------------------------------------------------------ the delta

@dataclass
class Result:
    targets: list              # [(entry name, source bytes, result bytes, inserts)]
    face: object
    added: list
    plans: dict                # page name -> {anim: [[Plan]]}
    layouts: dict              # page name -> (Spanish layout bytes, Portuguese layout bytes)
    atlas: tuple               # (Spanish atlas bytes, Portuguese atlas bytes)
    faces: dict


def needed_glyphs(portuguese):
    """{(kind, character)} the Portuguese pages draw."""
    out = set()
    for title, poem in portuguese.values():
        out |= {('title', ch) for ch in letters_of(title)}
        out |= {('body', ch) for _, line in poem for ch in letters_of(line)}
    return out


def spanish_pages(prepared, build):
    return {anim: official_page(prepared, build, SLOT, anim) for anim in BOOKS}


def generate(prepared, build=BUILDS[0], table=None):
    """The Portuguese pages and atlas of one build (Classic and HD), as delta targets over the Spanish entries."""
    table = table or read_table()
    portuguese = {anim: portuguese_page(table, anim) for anim in BOOKS}
    spanish = spanish_pages(prepared, build)
    targets, plans, pages, faces = [], {}, {}, {}
    atlas_source = atlas_result = added = None
    for page in PAGES:
        layout_name, atlas_name, archive = page
        face, _ = learn(prepared, build, page)
        data = prepared.read(build, archive.format(SLOT), layout_name)
        blob = prepared.read(build, archive.format(SLOT), atlas_name)
        if data is None or blob is None:
            raise PageError(f'{build}: {layout_name} or {atlas_name} not found')
        atlases = {}
        for language in SOURCES:
            source_blob = prepared.read(build, archive.format(language), atlas_name)
            atlases[language] = visuals.decode(source_blob, texture(source_blob))
        result, added = build_atlas(face, blob, atlases, needed_glyphs(portuguese))
        if atlas_source is None:
            atlas_source, atlas_result = blob, result
        elif blob != atlas_source or result != atlas_result:
            raise PageError(f'{atlas_name}: the Classic and HD atlases differ')
        targets.append((atlas_name, blob, result, []))
        layout = M.Mbs(data)
        page_plans = {anim: plan_page(layout, face, anim, spanish[anim], portuguese[anim]) for anim in BOOKS}
        new, inserts = respell(data, face, page_plans)
        targets.append((layout_name, data, new, inserts))
        plans[layout_name] = page_plans
        pages[layout_name] = (data, new)
        faces[layout_name] = face
    return Result(targets, faces[PAGES[0][0]], added, plans, pages, (atlas_source, atlas_result), faces)


def generate_all(prepared):
    """Delta targets of every Europe build: builds whose entries are byte-identical share them."""
    out, seen, results = [], set(), {}
    for build in BUILDS:
        if not (Path(prepared.root) / build / PAGES[0][2].format(SLOT)).is_file():
            continue
        result = generate(prepared, build)
        results[build] = result
        for name, source, new, inserts in result.targets:
            key = (name.lower(), len(source), zlib.crc32(source))
            if key in seen:
                continue
            seen.add(key)
            out.append((name, source, new, inserts))
    return out, results


def encode(targets):
    return oslv.encode(sorted(targets, key=lambda t: t[0].lower()))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=('write', 'check'))
    ap.add_argument('--prepared', default=str(ROOT / 'game' / 'prepared'))
    ap.add_argument('--out', default=str(DEFAULT_OUT))
    args = ap.parse_args()
    visuals.use_native_reader()
    prepared = Prepared(args.prepared)
    try:
        targets, results = generate_all(prepared)
    finally:
        prepared.close()
    if not targets:
        print('prologue pages: DATA UNAVAILABLE (no prepared Europe build)')
        return 0
    blob = encode(targets)
    out = Path(args.out)
    if args.cmd == 'write':
        out.write_bytes(blob)
        print(f'{len(targets)} entries ({", ".join(sorted({t[0] for t in targets}))}) -> {out} ({len(blob)} bytes)')
        return 0
    if not out.is_file() or out.read_bytes() != blob:
        print(f'{out.name}: stale (installation_poems.py write rebuilds it)')
        return 1
    print(f'{out.name}: current, {len(targets)} entries')
    return 0


if __name__ == '__main__':
    sys.exit(main())
