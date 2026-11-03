#!/usr/bin/env python3
"""Builds launcher/title/title-exit.dcpx: the title menu's unused INSTALL label becomes EXIT GAME.

The Refined (GUI/HD_title.ftx, GUI/REHD_title.mbs) and Classic (title.ftx, title.mbs) title menus share one atlas per
build and language. EXIT GAME is composed from the game's own letters: E, G, A, M from NEW GAME, I and T from INSTALL
and X from XTRA, cut out of the English title sprites pixel for pixel (a language whose atlas holds an identical
instance of a letter uses its own). The composed label is scaled uniformly to the typical letter height of the
language's own NEW GAME, LOAD and MODE labels in that title variant, or to the largest scale its atlas room allows.
The label's atlas rectangles are cleared and the text is placed in them, widened into free atlas space when needed; only the touched 4x4 BC3 blocks are re-encoded and the label's part and geometry
rectangles follow. Each changed file becomes one DCPX2 target.

    make_title_exit.py game/prepared out.dcpx [--preview dir]
"""
import argparse
import re
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'prompts'))
import cpk
import ftex
import make_dcpx
import patch_ftx

TEXT = 'EXIT GAME'
KINDS = {
    'refined': dict(atlas='GUI/HD_title.ftx', mbs='GUI/REHD_title.mbs', layers=range(8, 23), install=(14, 15, 16),
                    roles={'NEWGAME': 8, 'LOAD': 11, 'INSTALL': 14, 'MODE': 17, 'NEWGAME2': 20}),
    'classic': dict(atlas='title.ftx', mbs='title.mbs', layers=range(79, 91), install=(85, 86, 87),
                    roles={'NEWGAME': 79, 'LOAD': 82, 'INSTALL': 85, 'MODE': 88}),
}
# Pixels between two native letters of a title word: the pairs the game's own words show, else the usual 2; and the space
# between words (NEW GAME).
PAIR_GAPS = {('X', 'I'): 3, ('I', 'T'): 4, ('G', 'A'): 0, ('A', 'M'): 1, ('M', 'E'): 5}
LETTER_GAP = 2
WORD_GAP = 14

# The title menus' languages, told apart by how many glyph pieces their LOAD, INSTALL and MODE labels have, with the
# labels' texts and the EXIT label to try, most natural first; English is the last resort. The face has no B and no J,
# which rules out SPIEL BEENDEN, QUITTER LE JEU and SALIR DEL JUEGO.
LANGUAGES = {
    'en': dict(counts=(4, 7, 11), texts={'LOAD': 'LOAD', 'INSTALL': 'INSTALL', 'MODE': 'CLASSIC MODE'},
               phrases=('EXIT GAME',)),
    'de': dict(counts=(5, 12, 13),
               texts={'NEWGAME': 'NEUES SPIEL', 'LOAD': 'LADEN', 'INSTALL': 'INSTALLIEREN', 'MODE': 'KLASSIK-MODUS'},
               phrases=('SPIEL VERLASSEN', 'VERLASSEN', 'ENDE', 'EXIT GAME')),
    'es': dict(counts=(6, 8, 12), texts={'NEWGAME': 'NUEVA PARTIDA', 'LOAD': 'CARGAR', 'INSTALL': 'INSTALAR'},
               phrases=('SALIR', 'EXIT GAME')),
    'fr': dict(counts=(7, 9, 13),
               texts={'NEWGAME': 'NOUVELLE PARTIE', 'LOAD': 'CHARGER', 'INSTALL': 'INSTALLER', 'MODE': 'MODE CLASSIQUE'},
               phrases=('QUITTER', 'SORTIR', 'EXIT GAME')),
    'it': dict(counts=(6, 8, 17), texts={'NEWGAME': 'NUOVA PARTITA', 'LOAD': 'CARICA', 'INSTALL': 'INSTALLA'},
               phrases=('ESCI DAL GIOCO', 'ESCI', 'EXIT GAME')),
}
ALPHA_FLOOR = 8
NATIVE_ROLES = ('NEWGAME', 'LOAD', 'MODE')
SCALE_DEADBAND = 0.03
SCALE_STEP = 0.98
MARGIN = 2
MATCH_TOLERANCE = 14.0
GLYPH_ROWS = (30, 35)
TOLERANCE = 96


class Unsupported(Exception):
    pass


class Mbs:
    def __init__(self, data):
        if data[:4] != b'FMBS':
            raise Unsupported('not an MBS')
        self.data = bytearray(data)
        u = struct.unpack_from('<11Q', data, 0xb0)
        self.parts, self.geoms, self.keys, self.layers, self.s7, self.labels = u[1], u[2], u[4], u[6], u[7], u[9]
        self.s8, self.s10 = u[8], u[10]
        self.nparts = (self.geoms - self.parts) // 48
        self.nlayers = (self.s7 - self.layers) // 28
        self.nkeys = (self.labels - self.keys) // 16

    def keyframes(self, layer):
        base = self.layers + 28 * layer + 16
        first, count = struct.unpack_from('<H', self.data, base)[0], struct.unpack_from('<H', self.data, base + 6)[0]
        out = []
        for j in range(first, first + count):
            w = struct.unpack_from('<6H', self.data, self.keys + 16 * j + 4)
            out.append(dict(index=j, texture=w[1] >> 8, part=w[3], geom=w[5]))
        return out

    def part_record(self, p):
        return list(struct.unpack_from('<12f', self.data, self.parts + 48 * p))

    def geom_record(self, g):
        return list(struct.unpack_from('<12f', self.data, self.geoms + 48 * g))

    def set_part(self, p, rec):
        struct.pack_into('<12f', self.data, self.parts + 48 * p, *rec)

    def set_geom(self, g, rec):
        struct.pack_into('<12f', self.data, self.geoms + 48 * g, *rec)

    def animations(self):
        """[(name, first channel, channel count)]: the 48-byte records of the animation table, empty slots skipped."""
        out = []
        for i in range((self.s10 - self.labels) // 48):
            at = self.labels + 48 * i + 16
            name = bytes(self.data[at:at + 24]).split(b'\0')[0].decode('latin1')
            if name.isprintable() and name:
                out.append((name, struct.unpack_from('<H', self.data, at + 24)[0], self.data[at + 26]))
        return out

    def channel_segments(self, channel):
        first, count = struct.unpack_from('<HH', self.data, self.s10 + 24 * channel)
        return range(first, first + count)

    def segment_layer(self, segment):
        return struct.unpack_from('<I', self.data, self.s8 + 32 * segment)[0]

    def set_segment_layer(self, segment, layer):
        struct.pack_into('<I', self.data, self.s8 + 32 * segment, layer)

    def segment_y(self, segment):
        transform = struct.unpack_from('<H', self.data, self.s8 + 32 * segment + 4)[0]
        return round(struct.unpack_from('<f', self.data, self.s7 + 36 * transform + 4)[0])

    def part_rect(self, p):
        rec = self.part_record(p)
        return (int(min(rec[2:10:2])), int(min(rec[3:10:2])), int(max(rec[2:10:2])), int(max(rec[3:10:2])))

    def part_rects(self):
        """(part, texture) -> atlas rectangle, for every part a keyframe draws."""
        out = {}
        for j in range(self.nkeys):
            w = struct.unpack_from('<6H', self.data, self.keys + 16 * j + 4)
            out[(w[3], w[1] >> 8)] = self.part_rect(w[3])
        return out

    def users(self, part=None, geom=None):
        found = set()
        for j in range(self.nkeys):
            w = struct.unpack_from('<6H', self.data, self.keys + 16 * j + 4)
            if (part is not None and w[3] == part) or (geom is not None and w[5] == geom):
                found.add(j)
        return found


@dataclass
class Part:
    part: int
    texture: int
    geoms: list           # geometry indexes, the first is the one the layout follows
    quad: list            # uv corners [(u, v)] x4
    gquad: list           # corners of the first geometry

    @property
    def rect(self):
        us, vs = [c[0] for c in self.quad], [c[1] for c in self.quad]
        return (int(min(us)), int(min(vs)), int(max(us)), int(max(vs)))


@dataclass
class Label:
    kind: str
    parts: list           # every part of the logical label, in the order the layers draw them
    owners: set = field(default_factory=set)


def find_label(mbs, kind):
    spec = KINDS[kind]
    if mbs.nlayers <= max(spec['layers']):
        raise Unsupported('too few layers')
    sets = {i: {k['part'] for k in mbs.keyframes(i)} for i in spec['layers']}
    common = set.intersection(*sets.values())
    install = [sets[i] - common for i in spec['install']]
    if not install[0] or any(s != install[0] for s in install):
        raise Unsupported('INSTALL layers differ')
    first_seen = {}
    for i in spec['install']:
        for k in mbs.keyframes(i):
            if k['part'] in install[0]:
                first_seen.setdefault(k['part'], k['index'])
    parts, owners = [], set()
    for number in sorted(install[0], key=first_seen.get):
        keyframes = [k for i in spec['install'] for k in mbs.keyframes(i) if k['part'] == number]
        geoms = list(dict.fromkeys(k['geom'] for k in keyframes))
        textures = {k['texture'] for k in keyframes}
        if len(textures) != 1:
            raise Unsupported('a label part spans several textures')
        mine = {k['index'] for k in keyframes}
        if mbs.users(part=number) != mine or any(not mbs.users(geom=g) <= mine for g in geoms):
            raise Unsupported('label part or geometry is shared')
        rec, grec = mbs.part_record(number), mbs.geom_record(geoms[0])
        parts.append(Part(number, textures.pop(), geoms, [(rec[2 + 2 * i], rec[3 + 2 * i]) for i in range(4)],
                          [(grec[2 + 2 * i], grec[3 + 2 * i]) for i in range(4)]))
        owners |= mine
    if len({p.rect for p in parts}) != len(parts):
        raise Unsupported('two label parts share a rectangle')
    return Label(kind, parts, owners)


def orientation(part):
    """(turn, scale): the 2x2 turn/flip taking atlas pixel offsets to the text's own axes (x along the words, y down),
    and the full atlas-to-geometry matrix, from the quad corners."""
    q, g = np.array(part.quad, float), np.array(part.gquad, float)
    uv = np.stack([q[1] - q[0], q[3] - q[0]], axis=1)
    gm = np.stack([g[1] - g[0], g[3] - g[0]], axis=1)
    m = gm @ np.linalg.inv(uv)
    return np.where(abs(m) > 1e-6 * abs(m).max(), np.sign(m), 0), m


def is_upright(turn):
    return np.allclose(turn, np.eye(2), atol=1e-6)


# ---------------------------------------------------------------- row order

SELECTION = re.compile(r'^(?:RE_)?(NEWGAME2|NEWGAME|LOAD|INSTALL|MODE)_(\d)(?:_(\d))?$')
FOUR_ROWS = {'refined': {1: ['NEWGAME', 'LOAD', 'MODE', 'INSTALL'], 3: ['NEWGAME', 'NEWGAME2', 'LOAD', 'MODE', 'INSTALL']},
             'classic': {1: ['NEWGAME', 'LOAD', 'MODE', 'INSTALL']}}


def row_layers(kind):
    """Role -> the three layers of its states (two selected, one resting), from the first layer of each role."""
    return {role: tuple(range(first, first + 3)) for role, first in KINDS[kind]['roles'].items()}


def selection_animations(mbs):
    return [(name, first, count, SELECTION.match(name)) for name, first, count in mbs.animations() if SELECTION.match(name)]


def visible_rows(mbs, kind, first, count):
    """The row roles an animation draws, top to bottom, with the Y each settles at; fails when a row is ambiguous."""
    roles = row_layers(kind)
    found = []
    for channel in range(first, first + count):
        drawn = {mbs.segment_layer(s) for s in mbs.channel_segments(channel)}
        owners = {role for role, layers in roles.items() if drawn & set(layers)}
        if not owners:
            continue
        if len(owners) != 1:
            raise Unsupported('a row slot draws two rows')
        found.append((mbs.segment_y(mbs.channel_segments(channel)[-1]), next(iter(owners))))
    return sorted(found)


def reorder_rows(mbs, kind):
    """Makes the third slot of the game's four-row layouts the MODE row and the fourth the INSTALL row (EXIT GAME), in
    every selection animation, by swapping the two groups' layer numbers in the animations' segments. Returns the
    animations changed; fails closed when a segment is shared with another animation or a layout is not what the swap
    expects."""
    roles = row_layers(kind)
    swap = {}
    for exit_layer, mode_layer in zip(roles['INSTALL'], roles['MODE']):
        swap[exit_layer], swap[mode_layer] = mode_layer, exit_layer
    chosen = selection_animations(mbs)
    if not chosen:
        raise Unsupported('no selection animations')
    mine = set()
    for _, first, count, _ in chosen:
        for channel in range(first, first + count):
            mine |= set(mbs.channel_segments(channel))
    chosen_names = {name for name, *_ in chosen}
    for name, first, count in mbs.animations():
        if name in chosen_names:
            continue
        for channel in range(first, first + count):
            if mine & set(mbs.channel_segments(channel)):
                raise Unsupported(f'{name} shares segments with the selection animations')
    for segment in sorted(mine):
        layer = mbs.segment_layer(segment)
        if layer in swap:
            mbs.set_segment_layer(segment, swap[layer])
    for name, first, count, match in chosen:
        variant = int(match.group(3) or 0)
        expected = FOUR_ROWS[kind].get(variant)
        if expected is None:
            continue
        rows = visible_rows(mbs, kind, first, count)
        if [role for _, role in rows] != expected or len({y for y, _ in rows}) != len(rows):
            raise Unsupported(f'{name} does not show {expected} after the swap: {rows}')
    return [name for name, *_ in chosen]


# ---------------------------------------------------------------- the game's own letters

def components(rgba, floor=ALPHA_FLOOR, min_area=20):
    """Glyph-like pieces of a sprite: [(x0, x1, y0, y1, rgba of that piece only, as tall as the sprite)], left to right."""
    labelled, count = ndimage.label(rgba[..., 3] > floor, structure=np.ones((3, 3)))
    pieces = []
    for index, box in enumerate(ndimage.find_objects(labelled), 1):
        mask = labelled[box] == index
        if mask.sum() < min_area:
            continue
        piece = np.zeros((rgba.shape[0], box[1].stop - box[1].start, 4), np.uint8)
        piece[..., :3] = 255
        inside = rgba[box].copy()
        inside[~mask] = (255, 255, 255, 0)
        piece[box[0]] = inside
        pieces.append((box[1].start, box[1].stop, box[0].start, box[0].stop, piece))
    return sorted(pieces, key=lambda p: (p[0], p[2]))


def cut_left(piece):
    """The left letter of two touching ones, cut where the ink between them is thinnest."""
    alpha = piece[..., 3].astype(int).sum(axis=0)
    low, high = int(len(alpha) * 0.35), int(len(alpha) * 0.65)
    cut = low + int(np.argmin(alpha[low:high]))
    out = piece[:, :cut].copy()
    return out


class NativeArt:
    """A group's title sprites, by role, as upright RGBA images."""

    def __init__(self, atlas_blob, refined, classic):
        self.blob = atlas_blob
        self.textures = ftex.textures(atlas_blob)
        self.mbs = {'refined': Mbs(refined), 'classic': Mbs(classic)}
        self._decoded = {}

    def texture(self, index):
        if index not in self._decoded:
            self._decoded[index] = np.array(ftex.decode(self.blob, self.textures[index]))
        return self._decoded[index]

    def part_texture(self, kind, part):
        mbs = self.mbs[kind]
        for j in range(mbs.nkeys):
            w = struct.unpack_from('<6H', mbs.data, mbs.keys + 16 * j + 4)
            if w[3] == part:
                return w[1] >> 8
        raise Unsupported(f'part {part} is not drawn')

    def crop(self, kind, part):
        x0, y0, x1, y1 = self.mbs[kind].part_rect(part)
        return self.texture(self.part_texture(kind, part))[y0:y1, x0:x1].copy()

    def role_parts(self, kind, role):
        """The label parts a title group draws that the other groups do not."""
        mbs, spec = self.mbs[kind], KINDS[kind]
        common = set.intersection(*[{k['part'] for k in mbs.keyframes(i)} for i in spec['layers']])
        mine = {k['part'] for k in mbs.keyframes(spec['roles'][role])} - common
        if role == 'NEWGAME2':
            mine -= {k['part'] for k in mbs.keyframes(spec['roles']['NEWGAME'])}
        return sorted(mine)

    def sprites(self, kind):
        out = []
        for role in KINDS[kind]['roles']:
            for part in self.role_parts(kind, role):
                out.append((role, part, self.crop(kind, part)))
        return out


def reference_glyphs(art):
    """E, G, A, M from NEW GAME, I and T from INSTALL, X from XTRA of an English group; None when it is not one."""
    try:
        new_game = components(art.crop('refined', art.role_parts('refined', 'NEWGAME')[0]))
        install = components(art.crop('refined', art.role_parts('refined', 'INSTALL')[0]))
        extra = components(art.crop('refined', art.role_parts('refined', 'NEWGAME2')[0]))
    except (Unsupported, IndexError, StopIteration):
        return None
    if len(new_game) != 6 or len(install) != 7 or len(extra) != 2:
        return None
    picks = {'E': new_game[5], 'G': new_game[2], 'A': new_game[3], 'M': new_game[4], 'I': install[0], 'T': install[3]}
    glyphs = {letter: piece[4] for letter, piece in picks.items()}
    glyphs['X'] = cut_left(extra[0][4])
    if any(piece.shape[0] != 38 or not GLYPH_ROWS[0] <= ink_rows(piece) <= GLYPH_ROWS[1] for piece in glyphs.values()):
        return None
    widths = {letter: piece.shape[1] for letter, piece in glyphs.items()}
    if not all(18 <= w <= 34 for l, w in widths.items() if l != 'I') or not 7 <= widths['I'] <= 12:
        return None
    return glyphs


def ink_rows(piece):
    rows = np.nonzero((piece[..., 3] > ALPHA_FLOOR).any(axis=1))[0]
    return int(rows[-1] - rows[0] + 1)


def part_of(art, kind, number):
    mbs = art.mbs[kind]
    geom = next(struct.unpack_from('<6H', mbs.data, mbs.keys + 16 * j + 4)[5] for j in range(mbs.nkeys)
                if struct.unpack_from('<6H', mbs.data, mbs.keys + 16 * j + 4)[3] == number)
    rec, grec = mbs.part_record(number), mbs.geom_record(geom)
    return Part(number, art.part_texture(kind, number), [geom], [(rec[2 + 2 * i], rec[3 + 2 * i]) for i in range(4)],
                [(grec[2 + 2 * i], grec[3 + 2 * i]) for i in range(4)])


def upright_part(art, kind, number):
    """A label sprite as the player shows it: turned upright through the label's own geometry."""
    turn = orientation(part_of(art, kind, number))[0]
    crop = art.crop(kind, number)
    h, w = crop.shape[:2]
    out_h, out_w = (w, h) if turn[0, 0] == 0 else (h, w)
    ys, xs = np.mgrid[0:out_h, 0:out_w]
    offset = np.stack([xs + 0.5 - out_w / 2, ys + 0.5 - out_h / 2], axis=-1) @ turn
    px, py = np.floor(offset[..., 0] + w / 2).astype(int), np.floor(offset[..., 1] + h / 2).astype(int)
    inside = (px >= 0) & (px < w) & (py >= 0) & (py < h)
    out = np.zeros((out_h, out_w, 4), np.uint8)
    out[..., :3] = 255
    out[inside] = crop[py[inside], px[inside]]
    return out


def upright_sprite(art, kind, role):
    return upright_part(art, kind, art.role_parts(kind, role)[0])


def text_scale(part):
    """(vertical, horizontal) screen units per atlas pixel along the text's own axes."""
    turn, m = orientation(part)
    return (abs(m[1, 1]), abs(m[0, 0])) if turn[0, 0] else (abs(m[1, 0]), abs(m[0, 1]))


def text_height(image):
    """The typical letter height of an upright sprite: the median height of its glyph pieces."""
    heights = [c[3] - c[2] for c in components(image)]
    return float(np.median(heights)) if heights else 0.0


def native_text_height(art, kind):
    """The typical on-screen letter height of the language's own NEW GAME, LOAD and MODE labels."""
    heights = []
    for role in NATIVE_ROLES:
        for number in art.role_parts(kind, role):
            vertical = text_scale(part_of(art, kind, number))[0]
            heights += [(c[3] - c[2]) * vertical for c in components(upright_part(art, kind, number))]
    if not heights:
        raise Unsupported('the native labels show no letters to measure')
    return float(np.median(heights))


def language_of(art):
    """The title language of a group, from its labels' glyph counts; None when they match none."""
    try:
        counts = tuple(len(components(upright_sprite(art, 'refined', role))) for role in ('LOAD', 'INSTALL', 'MODE'))
    except (Unsupported, IndexError, StopIteration, ValueError):
        return None
    return next((name for name, spec in LANGUAGES.items() if spec['counts'] == counts), None)


def labelled_glyphs(art):
    """letter -> a native glyph of this group's title face, from the labels whose pieces are one per letter."""
    found = {}
    language = language_of(art)
    if not language:
        return found
    for role, text in LANGUAGES[language]['texts'].items():
        image = upright_sprite(art, 'refined', role)
        pieces = components(image)
        letters = [c for c in text if c != ' ']
        if image.shape[0] != 38 or len(pieces) != len(letters):
            continue
        for letter, piece in zip(letters, pieces):
            found.setdefault(letter, piece[4])
    return found


def build_pool(arts, reference):
    """The letters of the title face over every group, the English reference first, then each group's own labels."""
    pool = dict(reference)
    for crc in sorted(arts):
        for letter, glyph in labelled_glyphs(arts[crc]).items():
            pool.setdefault(letter, glyph)
    return pool


def native_letters(art, phrase, pool):
    """Every letter of a phrase: the group's own glyph when its labels hold one, else the pool's. (letters, sources)."""
    own = labelled_glyphs(art)
    letters, sources = {}, {}
    for letter in {c for c in phrase if c != ' '}:
        if letter in own:
            letters[letter], sources[letter] = own[letter], 'own'
        elif letter in pool:
            letters[letter], sources[letter] = pool[letter], 'pool'
        else:
            raise Unsupported(f'no native {letter} for {phrase}')
    return letters, sources


def compose(letters, phrase):
    """A phrase set from native letters: an RGBA canvas 38 rows tall, and its ink's centre row."""
    placed, gap, previous = [], 0, None
    for character in phrase:
        if character == ' ':
            gap = WORD_GAP
            continue
        if previous is not None:
            placed.append((previous, gap or PAIR_GAPS.get((previous, character), LETTER_GAP)))
        previous, gap = character, 0
    placed.append((previous, 0))
    width = sum(letters[letter].shape[1] + after for letter, after in placed)
    canvas = np.zeros((38, width, 4), np.uint8)
    canvas[..., :3] = 255
    x = 0
    for letter, after in placed:
        glyph = letters[letter]
        region = canvas[:, x:x + glyph.shape[1]]
        take = glyph[..., 3] > region[..., 3]
        region[take] = glyph[take]
        x += glyph.shape[1] + after
    rows = np.nonzero((canvas[..., 3] > ALPHA_FLOOR).any(axis=1))[0]
    return canvas[:, :x], (rows[0] + rows[-1] + 1) / 2


def scale_canvas(canvas, factor):
    """The composed label resized uniformly on premultiplied alpha; (canvas, ink centre row)."""
    if abs(factor - 1.0) < 1e-9:
        rows = np.nonzero((canvas[..., 3] > ALPHA_FLOOR).any(axis=1))[0]
        return canvas, (rows[0] + rows[-1] + 1) / 2
    h, w = canvas.shape[:2]
    size = (max(1, round(w * factor)), max(1, round(h * factor)))
    alpha = canvas[..., 3:].astype(np.float32) / 255
    planes = np.concatenate([canvas[..., :3].astype(np.float32) * alpha, alpha * 255], axis=-1)
    resized = np.stack([np.asarray(Image.fromarray(planes[..., i], 'F').resize(size, Image.LANCZOS)) for i in range(4)], axis=-1)
    a = np.clip(resized[..., 3], 0, 255)
    out = np.zeros((size[1], size[0], 4), np.uint8)
    colour = np.divide(resized[..., :3], np.maximum(a[..., None] / 255, 1e-6))
    out[..., :3] = np.clip(np.rint(np.where(a[..., None] > 0, colour, 255)), 0, 255).astype(np.uint8)
    out[..., 3] = np.rint(a).astype(np.uint8)
    out[out[..., 3] == 0, :3] = 255
    rows = np.nonzero((out[..., 3] > ALPHA_FLOOR).any(axis=1))[0]
    return out, (rows[0] + rows[-1] + 1) / 2


# ---------------------------------------------------------------- planning

@dataclass
class Edit:
    texture: int
    rect: tuple
    rgba: np.ndarray      # the rectangle's new pixels


@dataclass
class Plan:
    label: Label
    new_quads: dict       # part -> uv corners
    ratio: float
    edits: list
    turn: np.ndarray

    def rect_of(self, part):
        quad = self.new_quads[part]
        us, vs = [c[0] for c in quad], [c[1] for c in quad]
        return (int(min(us)), int(min(vs)), int(max(us)), int(max(vs)))


def ink_box(rgba):
    ys, xs = np.nonzero(rgba[..., 3] > 40)
    if not len(xs):
        raise Unsupported('label rectangle is empty')
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def free_run(atlas, other_rects, rect, axis, direction, limit):
    """How many atlas pixels beyond a side of `rect`, along `axis` (0 across, 1 down), are transparent and belong to no
    other sprite, up to `limit`."""
    x0, y0, x1, y1 = rect
    run = 0
    for step in range(limit):
        if axis == 0:
            at = x1 + step if direction > 0 else x0 - 1 - step
            band = (at, y0, at + 1, y1)
            clear = 0 <= at < atlas.shape[1] and not atlas[y0:y1, at, 3].any()
        else:
            at = y1 + step if direction > 0 else y0 - 1 - step
            band = (x0, at, x1, at + 1)
            clear = 0 <= at < atlas.shape[0] and not atlas[at, x0:x1, 3].any()
        if not clear or any(not (band[2] <= r[0] or r[2] <= band[0] or band[3] <= r[1] or r[3] <= band[1]) for r in other_rects):
            break
        run += 1
    return run


def find_free_rect(atlas, other_rects, own_rect, width, height, margin=2):
    """The free atlas rectangle of this size nearest to the label's own, on a 4-pixel grid so no BC3 block is shared with
    neighbouring art: transparent, outside every sprite the title draws, with a margin around it. None when there is none."""
    h, w = atlas.shape[:2]
    taken = atlas[..., 3] > 0
    x0, y0, x1, y1 = own_rect
    taken[y0:y1, x0:x1] = False
    for r in other_rects:
        taken[r[1]:r[3], r[0]:r[2]] = True
    window_h, window_w = height + 2 * margin, width + 2 * margin
    if window_h > h or window_w > w:
        return None
    total = np.pad(np.cumsum(np.cumsum(taken.astype(np.int64), axis=0), axis=1), ((1, 0), (1, 0)))
    busy = total[window_h:, window_w:] - total[:-window_h, window_w:] - total[window_h:, :-window_w] + total[:-window_h, :-window_w]
    ys, xs = np.nonzero(busy == 0)
    ys, xs = ys + margin, xs + margin
    aligned = (xs % 4 == 0) & (ys % 4 == 0) & (xs + width <= w) & (ys + height <= h)
    if not aligned.any():
        return None
    xs, ys = xs[aligned], ys[aligned]
    centre = np.array([(x0 + x1) / 2, (y0 + y1) / 2])
    nearest = int(np.argmin((xs + width / 2 - centre[0]) ** 2 + (ys + height / 2 - centre[1]) ** 2))
    return int(xs[nearest]), int(ys[nearest]), int(xs[nearest]) + width, int(ys[nearest]) + height


def render(old, rect, canvas, ink_row, centre_x, turn, vertical_ink_centre):
    """The rectangle's new pixels: the canvas placed at `centre_x` (atlas columns) with its ink centred on
    `vertical_ink_centre` (atlas rows), whole pixels only."""
    x0, y0, x1, y1 = rect
    ch, cw = canvas.shape[:2]
    out = old.copy()
    out[..., 3] = 0
    ys, xs = np.mgrid[y0:y1, x0:x1]
    rect_c = np.array([(x0 + x1) / 2, (y0 + y1) / 2])
    offset = np.stack([xs + 0.5 - rect_c[0], ys + 0.5 - rect_c[1]], axis=-1) @ turn.T
    if is_upright(turn):
        shift = np.array([centre_x - rect_c[0], vertical_ink_centre - rect_c[1] - (ink_row - ch / 2)])
    else:
        shift = np.zeros(2)
    cx = np.floor(offset[..., 0] - shift[0] + cw / 2 + 1e-6).astype(int)
    cy = np.floor(offset[..., 1] - shift[1] + ch / 2 + 1e-6).astype(int)
    inside = (cx >= 0) & (cx < cw) & (cy >= 0) & (cy < ch)
    picked = canvas[np.clip(cy, 0, ch - 1), np.clip(cx, 0, cw - 1)]
    show = inside & (picked[..., 3] > 0)
    out[show] = picked[show]
    return out


def plan_label(label, art, kind, canvas, ink_row, other_rects):
    """Decides the final rectangles of the label's parts, and what is painted in each."""
    parts = label.parts
    turns = [orientation(p)[0] for p in parts]
    if len(parts) > 1 and not all(is_upright(t) for t in turns):
        raise Unsupported('a rotated label of several parts')
    carrier = max(parts, key=lambda p: (p.rect[2] - p.rect[0]) * (p.rect[3] - p.rect[1]))
    turn, scale = orientation(carrier)
    upright = is_upright(turn)
    atlas = art.texture(carrier.texture)
    x0, y0, x1, y1 = carrier.rect
    cw = canvas.shape[1]
    need = cw + 2 * MARGIN
    avail = (x1 - x0) if turn[0, 0] else (y1 - y0)
    ink = np.nonzero((canvas[..., 3] > ALPHA_FLOOR).any(axis=1))[0]
    if len(ink) and ink[-1] - ink[0] + 1 + 2 * MARGIN > ((y1 - y0) if turn[0, 0] else (x1 - x0)):
        raise Unsupported('the text is taller than the part that carries it')
    new_quads = {p.part: [tuple(c) for c in p.quad] for p in parts}
    ratio, centre_x = 1.0, (x0 + x1) / 2
    if len(parts) > 1:
        spans = [(min(c[0] for c in p.gquad), max(c[0] for c in p.gquad)) for p in parts]
        combined = (min(s[0] for s in spans) + max(s[1] for s in spans)) / 2
        own = (min(c[0] for c in carrier.gquad) + max(c[0] for c in carrier.gquad)) / 2
        centre_x += (combined - own) / float(scale[0, 0])
        if abs(centre_x - (x0 + x1) / 2) + cw / 2 + MARGIN > avail / 2:
            raise Unsupported('the text does not fit the part that carries it')
    elif need > avail:
        axis = 0 if turn[0, 0] else 1
        grow = int(np.ceil((need - avail) / float(abs(scale[0, axis]))))
        after = free_run(atlas, other_rects, carrier.rect, axis, +1, grow)
        before = free_run(atlas, other_rects, carrier.rect, axis, -1, grow)
        low, high = (x0, x1) if axis == 0 else (y0, y1)
        if after >= grow:
            new_quads[carrier.part] = [tuple(c[k] + grow if (k == axis and c[k] == high) else c[k] for k in (0, 1)) for c in carrier.quad]
        elif before >= grow:
            new_quads[carrier.part] = [tuple(c[k] - grow if (k == axis and c[k] == low) else c[k] for k in (0, 1)) for c in carrier.quad]
        else:
            moved = find_free_rect(atlas, other_rects, carrier.rect, need, y1 - y0) if upright else None
            if not moved:
                raise Unsupported(f'no free atlas space to widen by {grow}px (after {after}, before {before})')
            mx0, my0, mx1, my1 = moved
            new_quads[carrier.part] = [(mx0, my0), (mx0, my1), (mx1, my1), (mx1, my0)]
            grow = need - avail
        ratio = (avail + grow) / avail
        us = [c[0] for c in new_quads[carrier.part]]
        centre_x = (min(us) + max(us)) / 2
    plan = Plan(label, new_quads, ratio, [], turn)
    old_ink = ink_box(art.crop(kind, carrier.part))
    ink_centre = carrier.rect[1] + (old_ink[1] + old_ink[3]) / 2
    for p in parts:
        rect = plan.rect_of(p.part)
        old = art.texture(p.texture)[rect[1]:rect[3], rect[0]:rect[2]]
        if p is carrier:
            new = render(old, rect, canvas, ink_row, centre_x, turn, ink_centre)
        else:
            new = old.copy()
            new[..., 3] = 0
        plan.edits.append(Edit(p.texture, rect, new))
        old_rect = p.rect
        apart = rect[2] <= old_rect[0] or old_rect[2] <= rect[0] or rect[3] <= old_rect[1] or old_rect[3] <= rect[1]
        if apart:
            vacated = art.texture(p.texture)[old_rect[1]:old_rect[3], old_rect[0]:old_rect[2]].copy()
            vacated[..., 3] = 0
            plan.edits.append(Edit(p.texture, old_rect, vacated))
    return plan


@dataclass
class Fit:
    plan: Plan
    canvas: np.ndarray
    wanted: float
    factor: float
    source_height: float
    native_height: float

    @property
    def width_limited(self):
        return self.factor < self.wanted * 0.995


def scale_wanted(native_height, source_height, vertical):
    factor = native_height / (source_height * vertical)
    return 1.0 if abs(factor - 1.0) < SCALE_DEADBAND else factor


def fit_label(label, art, kind, canvas, native_height, other_rects):
    """The label at the native letter height, or at the largest uniform scale the atlas space allows."""
    carrier = max(label.parts, key=lambda p: (p.rect[2] - p.rect[0]) * (p.rect[3] - p.rect[1]))
    source = text_height(canvas)
    wanted = scale_wanted(native_height, source, text_scale(carrier)[0])
    floor = min(wanted, 1.0)
    factors, factor = [], wanted
    while factor > floor:
        factors.append(factor)
        factor *= SCALE_STEP
    factors.append(floor)
    last = None
    for factor in factors:
        scaled, ink_row = scale_canvas(canvas, factor)
        try:
            return Fit(plan_label(label, art, kind, scaled, ink_row, other_rects), scaled, wanted, factor, source, native_height)
        except Unsupported as e:
            last = e
    raise last


def apply_edit(atlas, edit):
    x0, y0, x1, y1 = edit.rect
    before = atlas.copy()
    region = atlas[y0:y1, x0:x1]
    region[...] = np.where(edit.rgba[..., 3:4] > 0, edit.rgba, region * np.array([1, 1, 1, 0], np.uint8))
    return (atlas[..., 3] != before[..., 3]) | ((atlas[..., 3] > 0) & (atlas[..., :3] != before[..., :3]).any(axis=-1))


def apply_geometry(mbs, plan):
    for p in plan.label.parts:
        quad = plan.new_quads[p.part]
        rec = mbs.part_record(p.part)
        rec[2:10] = [c for corner in quad for c in corner]
        rec[0] = sum(c[0] for c in quad) / 4
        rec[1] = sum(c[1] for c in quad) / 4
        rec[10:12] = list(quad[0])
        mbs.set_part(p.part, rec)
    if plan.ratio == 1.0:
        return
    carrier = max(plan.label.parts, key=lambda p: (p.rect[2] - p.rect[0]) * (p.rect[3] - p.rect[1]))
    for g in carrier.geoms:
        grec = mbs.geom_record(g)
        xs = grec[2:10:2]
        centre = (min(xs) + max(xs)) / 2
        for i in range(4):
            grec[2 + 2 * i] = centre + (grec[2 + 2 * i] - centre) * plan.ratio
        grec[10] = centre + (grec[10] - centre) * plan.ratio
        mbs.set_geom(g, grec)


def edge_drift(before_blob, after_blob, tex, changed):
    """Pixels outside the touched blocks are identical; returns how far the other pixels of touched blocks moved."""
    a = np.array(ftex.decode(before_blob, tex)).astype(int)
    b = np.array(ftex.decode(after_blob, tex)).astype(int)
    h, w = changed.shape
    touched = changed.reshape(h // 4, 4, w // 4, 4).any(axis=(1, 3)).repeat(4, 0).repeat(4, 1)
    assert (a[~touched] == b[~touched]).all(), 'pixels outside the touched blocks changed'
    near = touched & ~changed
    if not near.any():
        return 0.0, 0.0
    visible = near & (a[..., 3] > 0) & (b[..., 3] > 0)
    colour = float(np.abs(a[visible][:, :3] - b[visible][:, :3]).mean()) if visible.any() else 0.0
    return float(np.abs(a[near][:, 3] - b[near][:, 3]).mean()), colour


def check_painted(blob, textures, edit):
    """The patched atlas shows what the edit says, within what BC3 can keep."""
    x0, y0, x1, y1 = edit.rect
    shown = np.array(ftex.decode(blob, textures[edit.texture]))[y0:y1, x0:x1, 3].astype(int)
    worst = int(np.abs(shown - edit.rgba[..., 3].astype(int)).max())
    assert worst <= TOLERANCE, f'the painted label differs from the text by {worst}'


def label_metrics(kind, language, phrase, label, fit):
    carrier = max(label.parts, key=lambda p: (p.rect[2] - p.rect[0]) * (p.rect[3] - p.rect[1]))
    vertical, horizontal = text_scale(carrier)
    ink = np.nonzero(fit.canvas[..., 3] > ALPHA_FLOOR)
    return dict(kind=kind, language=language, phrase=phrase, source_height=fit.source_height,
                native_height=fit.native_height, scale=fit.factor, wanted_scale=fit.wanted,
                height=text_height(fit.canvas) * vertical, visible_height=float(np.ptp(ink[0]) + 1) * vertical,
                visible_width=float(np.ptp(ink[1]) + 1) * horizontal, rect=fit.plan.rect_of(carrier.part),
                width_limited=fit.width_limited)


def process_group(art, pool, notes, metrics=None):
    """The patched atlas and MBS files of one build/language; every kind that is not done says why."""
    mbs = {kind: Mbs(bytes(art.mbs[kind].data)) for kind in KINDS}
    labels, status = {}, {}
    for kind in KINDS:
        try:
            labels[kind] = find_label(mbs[kind], kind)
            reordered = Mbs(bytes(mbs[kind].data))
            reorder_rows(reordered, kind)
            mbs[kind] = reordered
        except Unsupported as e:
            labels.pop(kind, None)
            status[kind] = f'skipped: {e}'
    others = {}
    for kind, m in mbs.items():
        mine = {p.part for p in labels[kind].parts} if kind in labels else set()
        for (part, texture), rect in m.part_rects().items():
            if part not in mine:
                others.setdefault(texture, []).append(rect)
    plans, shared = {}, {}
    for kind, label in labels.items():
        try:
            others_here = [r for t in {p.texture for p in label.parts} for r in others.get(t, [])]
            language = language_of(art) or 'en'
            native = native_text_height(art, kind)
            tried = []
            for phrase in LANGUAGES[language]['phrases']:
                try:
                    letters, sources = native_letters(art, phrase, pool)
                    canvas, _ = compose(letters, phrase)
                    fit = fit_label(label, art, kind, canvas, native, others_here)
                    plans[kind] = fit.plan
                    break
                except Unsupported as e:
                    tried.append(f'{phrase}: {e}')
            else:
                raise Unsupported('; '.join(tried))
            own = ''.join(sorted(k for k, v in sources.items() if v == 'own')) or 'none'
            notes.append(f'{kind}: "{phrase}" ({language}), letters of its own atlas: {own}, the rest from the title art of '
                         f'other languages' + (f'; skipped {tried}' if tried else ''))
            if metrics is not None:
                metrics.append(label_metrics(kind, language, phrase, label, fit))
        except Unsupported as e:
            status[kind] = f'skipped: {e}'
    out_blob, done = art.blob, {}
    decoded = {}
    for kind, plan in plans.items():
        for edit in plan.edits:
            key = (edit.texture, edit.rect)
            if key in done and not (done[key] == edit.rgba).all():
                raise Unsupported(f'the Refined and Classic labels share {key} but would be painted differently')
            if key not in done:
                atlas = decoded.setdefault(edit.texture, art.texture(edit.texture).copy())
                changed = apply_edit(atlas, edit)
                previous = out_blob
                out_blob, blocks = patch_ftx.patch(out_blob, art.textures[edit.texture], atlas, changed)
                drift = edge_drift(previous, out_blob, art.textures[edit.texture], changed)
                check_painted(out_blob, art.textures, edit)
                done[key] = edit.rgba
                notes.append(f'{kind}: tex{edit.texture} {edit.rect}, {blocks} blocks, edge drift alpha {drift[0]:.2f} colour {drift[1]:.2f}')
        apply_geometry(mbs[kind], plan)
        status[kind] = 'ok'
    return out_blob, {k: bytes(v.data) for k, v in mbs.items()}, status


def previews(directory, name, blob, shown):
    directory.mkdir(parents=True, exist_ok=True)
    for kind, (texture, rect) in shown.items():
        image = ftex.decode(blob, ftex.textures(blob)[texture]).crop((rect[0] - 8, rect[1] - 8, rect[2] + 8, rect[3] + 8))
        bg = Image.new('RGBA', image.size, (40, 40, 60, 255))
        bg.alpha_composite(image)
        bg.resize((bg.width * 3, bg.height * 3), Image.NEAREST).save(directory / f'{name}-{kind}.png')


def collect(prepared):
    """{atlas crc: dict(atlas, where, files)} for every distinct title atlas of every build and language."""
    groups = {}
    for build in sorted(p for p in Path(prepared).iterdir() if p.is_dir()):
        for archive in sorted(build.glob('*.CPK')):
            pack = cpk.Cpk(archive, original=True)
            for entry in pack.entries:
                for kind, spec in KINDS.items():
                    if entry['path'] == spec['atlas']:
                        data = pack.read(entry)
                        group = groups.setdefault(zlib.crc32(data), dict(atlas=data, where=[], files={}))
                        group['where'].append(f'{build.name}/{archive.name}')
                        group['files'].setdefault(kind, set()).add(pack.read(pack.find(spec['mbs'])))
    return groups


def find_reference(arts):
    """The first group whose title sprites are the English ones; its letters are the reference."""
    for crc in sorted(arts):
        glyphs = reference_glyphs(arts[crc])
        if glyphs:
            return crc, glyphs
    raise SystemExit('no group holds the English title sprites NEW GAME, INSTALL and XTRA')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('prepared')
    ap.add_argument('out')
    ap.add_argument('--preview', help='folder for 3x crops of every painted label')
    args = ap.parse_args()
    groups = collect(args.prepared)
    arts, sources = {}, {}
    for crc, group in groups.items():
        if set(group['files']) != set(KINDS) or any(len(v) != 1 for v in group['files'].values()):
            raise SystemExit(f'{group["where"]}: ambiguous title files for atlas {crc:08x}')
        sources[crc] = {kind: next(iter(files)) for kind, files in group['files'].items()}
        arts[crc] = NativeArt(group['atlas'], sources[crc]['refined'], sources[crc]['classic'])
    ref_crc, reference = find_reference(arts)
    pool = build_pool(arts, reference)
    print(f'English letters from atlas {ref_crc:08x} ({groups[ref_crc]["where"][0]})')
    targets, failed = {}, []
    for crc in sorted(groups):
        notes = []
        metrics = []
        blob, mbs, status = process_group(arts[crc], pool, notes, metrics)
        print(f'atlas {crc:08x} ({groups[crc]["where"][0]} +{len(groups[crc]["where"]) - 1}): {status}')
        for line in notes:
            print('   ', line)
        for mt in metrics:
            print('    size {kind} {language} "{phrase}": letter {source_height:.1f}px, native {native_height:.1f}, scale {scale:.3f} '
                  '(wanted {wanted_scale:.3f}), shown {height:.1f} high, {visible_width:.1f} wide, rect {rect}, '
                  'limited {width_limited}'.format(**mt))
        failed += [(crc, kind, why) for kind, why in status.items() if why != 'ok']
        if args.preview:
            shown = {}
            for kind in KINDS:
                if status.get(kind) == 'ok':
                    label = find_label(Mbs(mbs[kind]), kind)
                    shown[kind] = (label.parts[0].texture, label.parts[0].rect)
            previews(Path(args.preview), f'{crc:08x}', blob, shown)
        if blob != groups[crc]['atlas']:
            for kind in KINDS:
                targets[(KINDS[kind]['atlas'], len(blob), crc)] = (groups[crc]['atlas'], blob)
        for kind, patched in mbs.items():
            if patched != sources[crc][kind]:
                targets[(KINDS[kind]['mbs'], len(patched), zlib.crc32(sources[crc][kind]))] = (sources[crc][kind], patched)
    if failed:
        raise SystemExit(f'title variants left with INSTALL: {failed}')
    ordered = sorted(targets.items(), key=lambda t: (t[0][0], t[0][2], t[0][1]))
    blob = make_dcpx.dcpx_bytes([(name, orig, new) for (name, _, _), (orig, new) in ordered])
    Path(args.out).write_bytes(blob)
    print(f'{len(targets)} targets -> {args.out} ({len(blob)} bytes)')


if __name__ == '__main__':
    main()
