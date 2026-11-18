"""Portuguese (Brazil) title menu rows: NOVO JOGO, CARREGAR, MODO CLÁSSICO and MODO REFINADO in the title face.

The Refined title labels of every European language (and the English Classic ones) share one face. Its letters are cut
out of the player's own title atlases: each upright label sprite whose glyph pieces match its known text one to one
gives its letters (an accent is merged with the letter under it). English art is preferred; the other languages only
fill letters English lacks (V, Á, F, U...). No other language's word is copied; only single letters are.
"""
import struct

import numpy as np

import make_title_exit as X

# The text of each Refined label sprite (and English Classic), by title language and row.
TEXTS = {
    'en': {'NEWGAME': ['NEW GAME'], 'LOAD': ['LOAD'], 'INSTALL': ['INSTALL'], 'MODE': ['CLASSIC MODE', 'REFINED MODE'],
           'NEWGAME2': ['XTRA']},
    'es': {'NEWGAME': ['NUEVA PARTIDA'], 'LOAD': ['CARGAR'], 'INSTALL': ['INSTALAR'], 'MODE': ['MODO CLÁSICO']},
    'fr': {'NEWGAME': ['NOUVELLE PARTIE'], 'LOAD': ['CHARGER'], 'INSTALL': ['INSTALLER'], 'MODE': ['MODE CLASSIQUE']},
    'de': {'NEWGAME': ['NEUES SPIEL'], 'LOAD': ['LADEN'], 'INSTALL': ['INSTALLIEREN'], 'MODE': ['KLASSIK-MODUS']},
    'it': {'NEWGAME': ['NUOVA PARTITA'], 'LOAD': ['CARICA'], 'INSTALL': ['INSTALLA'], 'MODE': ['MODALITÀ CLASSICA']},
}
PREFERENCE = ('en', 'es', 'fr', 'it', 'de')
ROWS = {'NEWGAME': 'NOVO JOGO', 'LOAD': 'CARREGAR', 'MODE': {'CLASSIC MODE': 'MODO CLÁSSICO', 'REFINED MODE': 'MODO REFINADO'}}
FACE_ROWS = 38


class Glyph:
    def __init__(self, image, source):
        rows = np.nonzero((image[..., 3] > X.ALPHA_FLOOR).any(axis=1))[0]
        self.image = image[:rows[-1] + 1]
        self.source = source


def letter_pieces(image):
    """Glyph pieces of an upright sprite, an accent merged with the letter under it; left to right."""
    pieces = X.components(image)
    merged = []
    for p in sorted(pieces, key=lambda p: (p[3] - p[2]) * -1):
        for m in merged:
            overlap = min(p[1], m[1]) - max(p[0], m[0])
            if overlap > 0.6 * min(p[1] - p[0], m[1] - m[0]) and (p[3] <= m[2] + 3 or m[3] <= p[2] + 3):
                x0, x1 = min(p[0], m[0]), max(p[1], m[1])
                canvas = np.zeros((image.shape[0], x1 - x0, 4), np.uint8)
                canvas[..., :3] = 255
                for q in (m, p):
                    region = canvas[:, q[0] - x0:q[1] - x0]
                    take = q[4][..., 3] > region[..., 3]
                    region[take] = q[4][take]
                merged[merged.index(m)] = (x0, x1, min(p[2], m[2]), max(p[3], m[3]), canvas)
                break
        else:
            merged.append(p)
    return sorted(merged, key=lambda p: p[0])


def pool(arts):
    """{letter: Glyph} over every title atlas given as {language: NativeArt}, English first."""
    found = {}
    for language in PREFERENCE:
        art = arts.get(language)
        if art is None:
            continue
        kinds = ('refined', 'classic') if language == 'en' else ('refined',)
        for kind in kinds:
            for role, texts in TEXTS[language].items():
                if role not in X.KINDS[kind]['roles']:
                    continue
                for part in art.role_parts(kind, role):
                    image = X.upright_part(art, kind, part)
                    pieces = letter_pieces(image)
                    for text in texts:
                        letters = [c for c in text if c != ' ']
                        if len(pieces) != len(letters):
                            continue
                        bottom = [int(np.nonzero((p[4][..., 3] > X.ALPHA_FLOOR).any(axis=1))[0][-1]) for p in pieces]
                        base = int(np.median(bottom))
                        for letter, piece, b in zip(letters, pieces, bottom):
                            if letter in found or letter == '-' or abs(b - base) > 2:
                                continue
                            found[letter] = Glyph(_trim_to_baseline(piece[4], base), f'{language} {text}')
                        break
    if 'E' in found and 'F' not in found:
        found['F'] = Glyph(_f_from_e(found['E'].image), 'E without its lower arm')
    if 'O' in found and 'I' in found and 'J' not in found:
        found['J'] = Glyph(X.compose_j(_pad(found['O'].image), _pad(found['I'].image)), 'I stem and O bowl')
    return found


def _pad(image):
    out = np.zeros((FACE_ROWS, image.shape[1], 4), np.uint8)
    out[..., :3] = 255
    out[FACE_ROWS - image.shape[0]:] = image[-FACE_ROWS:]
    return out


def _trim_to_baseline(image, base):
    """The glyph cut at the sprite's baseline row: every glyph ends on the same row."""
    cols = np.nonzero((image[..., 3] > X.ALPHA_FLOOR).any(axis=0))[0]
    return image[:base + 1, cols[0]:cols[-1] + 1].copy()


def _f_from_e(e):
    alpha = e[..., 3] > X.ALPHA_FLOOR
    rows = np.nonzero(alpha.any(axis=1))[0]
    out = e.copy()
    stem = np.nonzero(alpha[(rows[0] + rows[-1]) // 2])[0]
    cut = int(stem[0] + (stem[-1] - stem[0] + 1) * 0.45) if len(stem) else e.shape[1] // 3
    low = rows[-1] - int((rows[-1] - rows[0]) * 0.3)
    out[low:, cut:, 3] = 0
    cols = np.nonzero((out[..., 3] > X.ALPHA_FLOOR).any(axis=0))[0]
    return out[:, cols[0]:cols[-1] + 1]


def compose(glyphs, phrase):
    """(canvas, baseline row): the phrase on one baseline with the face's spacing."""
    items, gap, previous = [], 0, None
    for character in phrase:
        if character == ' ':
            gap = X.WORD_GAP
            continue
        if character not in glyphs:
            raise X.Unsupported(f'no title glyph for {character} in {phrase}')
        if previous is not None:
            items[-1] = (items[-1][0], gap or X.PAIR_GAPS.get((previous, character), X.LETTER_GAP))
        items.append((character, 0))
        previous, gap = character, 0
    above = max(glyphs[c].image.shape[0] for c, _ in items)
    width = sum(glyphs[c].image.shape[1] + after for c, after in items)
    canvas = np.zeros((above, width, 4), np.uint8)
    canvas[..., :3] = 255
    x = 0
    for c, after in items:
        g = glyphs[c].image
        region = canvas[above - g.shape[0]:, x:x + g.shape[1]]
        take = g[..., 3] > region[..., 3]
        region[take] = g[take]
        x += g.shape[1] + after
    return canvas, above - 1


def keys_of_part(mbs, part):
    return [j for j in range(mbs.nkeys) if struct.unpack_from('<6H', mbs.data, mbs.keys + 16 * j + 4)[3] == part]


def geoms_of_part(mbs, part):
    return sorted({struct.unpack_from('<6H', mbs.data, mbs.keys + 16 * j + 4)[5] for j in keys_of_part(mbs, part)})


class Painter:
    """Paints new title labels into one atlas and widens the parts and quads that show them."""

    def __init__(self, art, mbs_files):
        self.art = art
        self.mbs = {kind: X.Mbs(data) for kind, data in mbs_files.items()}
        self.atlas = {}
        self.blob = art.blob
        self.painted = {}
        self.notes = []

    def texture(self, t):
        if t not in self.atlas:
            self.atlas[t] = self.art.texture(t).copy()
        return self.atlas[t]

    def other_rects(self, texture, own):
        out = []
        for mbs in self.mbs.values():
            for (part, t), rect in mbs.part_rects().items():
                if t == texture and rect != own:
                    out.append(rect)
        return out

    def paint(self, kind, part, canvas, base):
        mbs = self.mbs[kind]
        texture = self.art.part_texture(kind, part)
        rect = mbs.part_rect(part)
        rec = mbs.part_record(part)
        uv = [(rec[2 + 2 * i], rec[3 + 2 * i]) for i in range(4)]
        geoms = geoms_of_part(mbs, part)
        g0 = mbs.geom_record(geoms[0])
        turn, scale = X.orientation(X.Part(part, texture, geoms, uv, [(g0[2 + 2 * i], g0[3 + 2 * i]) for i in range(4)]))
        if not X.is_upright(turn):
            raise X.Unsupported(f'{kind} part {part} is not upright')
        key = (texture, rect)
        if key in self.painted:
            new_rect, deltas = self.painted[key]
        else:
            new_rect, deltas = self._place(texture, rect, canvas, base)
            self.painted[key] = (new_rect, deltas)
        sx, sy = float(scale[0, 0]), float(scale[1, 1])
        x0, y0, x1, y1 = new_rect
        rec = mbs.part_record(part)
        rec[2:10] = [x0, y0, x0, y1, x1, y1, x1, y0]
        rec[0], rec[1], rec[10], rec[11] = (x0 + x1) / 2, (y0 + y1) / 2, x0, y0
        mbs.set_part(part, rec)
        dl, dt, dr, db = deltas
        for g in geoms:
            grec = mbs.geom_record(g)
            xs, ys = grec[2:10:2], grec[3:10:2]
            gx0, gx1, gy0, gy1 = min(xs), max(xs), min(ys), max(ys)
            for i in range(4):
                grec[2 + 2 * i] = gx0 - dl * sx if grec[2 + 2 * i] == gx0 else gx1 + dr * sx
                grec[3 + 2 * i] = gy0 - dt * sy if grec[3 + 2 * i] == gy0 else gy1 + db * sy
            grec[0] = (grec[2] + grec[6]) / 2
            grec[1] = (grec[3] + grec[5]) / 2
            grec[10], grec[11] = grec[2], grec[3]
            mbs.set_geom(g, grec)

    def _place(self, texture, rect, canvas, base):
        """The atlas rectangle that shows `canvas`, the old one grown (or moved to free space), and how much each side of
        the label grew: (rect, (left, top, right, bottom))."""
        atlas = self.texture(texture)
        x0, y0, x1, y1 = rect
        old = atlas[y0:y1, x0:x1]
        ink = np.nonzero((old[..., 3] > X.ALPHA_FLOOR).any(axis=1))[0]
        old_base = int(ink[-1])
        ch, cw = canvas.shape[:2]
        width = max(x1 - x0, cw + 2 * X.MARGIN)
        top = min(0, old_base - base - X.MARGIN)
        bottom = max(y1 - y0, old_base - base + ch + X.MARGIN)
        grow_l = (width - (x1 - x0)) // 2
        grow_r = width - (x1 - x0) - grow_l
        want = (x0 - grow_l, y0 + top, x1 + grow_r, y0 + bottom)
        others = self.other_rects(texture, rect)
        if not self._free(atlas, want, rect, others):
            moved = X.find_free_rect(atlas, others, rect, want[2] - want[0], want[3] - want[1])
            if not moved:
                raise X.Unsupported(f'no free atlas space for a {want[2] - want[0]}x{want[3] - want[1]} label')
            self._write(texture, rect, np.zeros_like(old))
            want = moved
        region = np.zeros((want[3] - want[1], want[2] - want[0], 4), np.uint8)
        region[..., :3] = 255
        cx = (region.shape[1] - cw) // 2
        cy = old_base - top - base
        region[cy:cy + ch, cx:cx + cw] = canvas
        self._write(texture, want, region)
        return want, (grow_l, -top, grow_r, bottom - (y1 - y0))

    @staticmethod
    def _free(atlas, want, own, others):
        x0, y0, x1, y1 = want
        if x0 < 0 or y0 < 0 or x1 > atlas.shape[1] or y1 > atlas.shape[0]:
            return False
        if any(X.overlap(want, r) for r in others):
            return False
        alpha = atlas[y0:y1, x0:x1, 3].copy()
        ox0, oy0, ox1, oy1 = own
        alpha[max(0, oy0 - y0):max(0, oy1 - y0), max(0, ox0 - x0):max(0, ox1 - x0)] = 0
        return not alpha.any()

    def _write(self, texture, rect, rgba):
        atlas = self.texture(texture)
        x0, y0, x1, y1 = rect
        before = atlas.copy()
        atlas[y0:y1, x0:x1] = rgba
        changed = (atlas[..., 3] != before[..., 3]) | ((atlas[..., 3] > 0) & (atlas[..., :3] != before[..., :3]).any(axis=-1))
        self.blob, blocks = X.patch_ftx.patch(self.blob, self.art.textures[texture], atlas, changed)
        X.check_painted(self.blob, self.art.textures, X.Edit(texture, rect, rgba))
        self.notes.append(f'tex{texture} {rect}: {blocks} blocks')

    def files(self):
        return self.blob, {kind: bytes(m.data) for kind, m in self.mbs.items()}


ENGLISH = {('refined', 'NEWGAME'): 'NEW GAME', ('refined', 'LOAD'): 'LOAD', ('refined', 'MODE'): 'CLASSIC MODE',
           ('classic', 'NEWGAME'): 'NEW GAME', ('classic', 'LOAD'): 'LOAD', ('classic', 'MODE'): 'REFINED MODE'}


def paint_rows(art, mbs_files, glyphs):
    """(atlas blob, {kind: mbs}, notes) with every NEW GAME, LOAD and MODE row of an English group in Portuguese."""
    painter = Painter(art, mbs_files)
    done = []
    for kind in ('refined', 'classic'):
        for role in ('NEWGAME', 'LOAD', 'MODE'):
            for part in art.role_parts(kind, role):
                english = ENGLISH[(kind, role)]
                phrase = ROWS[role][english] if isinstance(ROWS[role], dict) else ROWS[role]
                canvas, base = compose(glyphs, phrase)
                painter.paint(kind, part, canvas, base)
                done.append(f'{kind} {english} -> {phrase}')
    blob, files = painter.files()
    return blob, files, done + painter.notes
