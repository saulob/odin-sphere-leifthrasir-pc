#!/usr/bin/env python3
"""Portuguese (Brazil) word sprites of atlases every European language shares: localization/pt-BR/visual/<family>-ptbr.oslv.

Such an atlas holds the sprite of every language (Bookshelf NEW / NUEVO / NEU, kitchen TAKE OUT / PARA LLEVAR) and each
language's layout draws its own. In the English archive's copy the other languages' rectangles are never drawn, so the
Portuguese word is painted into one of them (the slot, normally the Spanish one) and the English keys take that
language's part and quad for it. The word is set from letters cut out of the official sprites of the same face (a letter
is a run of fill columns with its share of the outline around it); accents come from the face's own marks.

    sprite_slots.py <family> [--prepared game/prepared] [--preview dir]
"""
import argparse
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'prompts'))
import ftex  # noqa: E402
import make_visuals as V  # noqa: E402
import mbs as M  # noqa: E402
import oslv  # noqa: E402
import patch_ftx  # noqa: E402
import visuals  # noqa: E402

ROOT = HERE.parent.parent
VISUAL = ROOT / 'localization' / 'pt-BR' / 'visual'


@dataclass(frozen=True)
class Word:
    english: tuple          # (texture, x0, y0, x1, y1) of the English sprite
    slot: tuple             # the rectangle the Portuguese word is painted into
    slot_language: str      # whose layout draws `slot` (its part and quad are taken)
    text: str
    sources: tuple          # ((rect, text), ...) official sprites of the face the letters come from


FAMILIES = {
    'bookshelf_new': dict(
        atlas='GUI/HD_Archive_00.ftx', layout='GUI/REHD_Archive_00.mbs', fill=200,
        words=(Word((2, 1444, 980, 1504, 1006), (2, 1362, 968, 1440, 994), 'ES', 'NOVO',
                    (((2, 1362, 968, 1440, 994), 'NUEVO'), ((2, 1444, 980, 1504, 1006), 'NEW'))),)),
    'kitchen': dict(
        atlas='GUI/HD_Kitchin.ftx', layout='GUI/REHD_Kitchin.mbs', fill=200, layout_by='family_respell.py kitchen',
        words=(Word((0, 1054, 990, 1156, 1004), (0, 1530, 958, 1674, 972), 'ES', 'PARA LEVAR',
                    (((0, 1530, 958, 1674, 972), 'PARA LLEVAR'),)),
               Word((0, 942, 990, 1054, 1020), (0, 942, 990, 1054, 1020), 'UK', 'BÔNUS!',
                    (((0, 942, 990, 1054, 1020), 'BONUS!'), ((0, 1470, 744, 1666, 774), '¡BONIFICACIÓN!'))),
               Word((0, 1632, 788, 1754, 818), (0, 1632, 788, 1754, 818), 'UK', 'BÔNUS!!',
                    (((0, 1632, 788, 1754, 818), 'BONUS!!'), ((0, 1470, 744, 1666, 774), '¡BONIFICACIÓN!'))),
               Word((0, 1336, 496, 1468, 526), (0, 1336, 496, 1468, 526), 'UK', 'BÔNUS!!!',
                    (((0, 1336, 496, 1468, 526), 'BONUS!!!'), ((0, 1470, 744, 1666, 774), '¡BONIFICACIÓN!'))))),
}


def fill_mask(rgba, level):
    """The letters' fill: bright, opaque pixels."""
    return (rgba[..., 3] > 200) & (rgba[..., :3].min(axis=-1) > level)


def runs_of(cols):
    runs, x = [], 0
    while x < len(cols):
        if cols[x]:
            start = x
            while x < len(cols) and cols[x]:
                x += 1
            runs.append((start, x))
        else:
            x += 1
    return runs


@dataclass
class Cut:
    cells: dict             # char -> rgba: the letter's fill columns and half the gaps on either side
    left: np.ndarray        # what precedes the first letter (frame, outline)
    right: np.ndarray
    space: np.ndarray       # the columns a word space adds, or None
    top: int                # first fill row of the letters


def cut(rgba, text, level):
    """The sprite of `text` cut into letter cells, or None when its fill runs are not one per letter."""
    fill = fill_mask(rgba, level)
    runs = runs_of(fill.any(axis=0))
    letters = [c for c in text if not c.isspace()]
    if len(runs) != len(letters):
        return None
    spaced = set()
    n = 0
    for i, c in enumerate(text):
        if c.isspace():
            spaced.add(n)
        else:
            n += 1
    gaps = [runs[i + 1][0] - runs[i][1] for i in range(len(runs) - 1) if (i + 1) not in spaced]
    gap = min(gaps) if gaps else 4
    half = gap // 2
    bounds = [(s - half, e + gap - half) for s, e in runs]
    cells = {}
    for (a, b), c in zip(bounds, letters):
        cells.setdefault(c, rgba[:, a:b])
    space = None
    for i in sorted(spaced):
        space = rgba[:, bounds[i - 1][1]:bounds[i][0]]
    tops = [int(np.nonzero(fill[:, a:b].any(axis=1))[0].min()) for a, b in runs]
    top = max(set(tops), key=tops.count)
    return Cut(cells, rgba[:, :bounds[0][0]], rgba[:, bounds[-1][1]:], space, top)


def with_mark(cells, char, top):
    """A letter the cells lack: its base letter with the accent of the same letter accented otherwise (an acute,
    mirrored into a circumflex). The accent is what differs from the frame above the letters' top row."""
    import unicodedata
    base, *marks = unicodedata.normalize('NFD', char)
    plain = cells.get(base)
    if plain is None or marks != ['̂']:
        return None
    for other, cell in cells.items():
        if unicodedata.normalize('NFD', other) != base + '́' or cell.shape[0] != plain.shape[0]:
            continue
        above = cell[:top].astype(int)
        accent = np.abs(above - above[:, :1]).max(axis=-1) > 24
        if not accent.any():
            continue
        from scipy import ndimage
        fill = accent & fill_mask(cell[:top], 200)
        if not fill.any():
            continue
        outline = cell[:top][accent & ~fill]
        outline_colour = np.median(outline, axis=0).astype(np.uint8) if len(outline) else np.array([0, 0, 0, 255], np.uint8)
        white = np.median(cell[:top][fill], axis=0).astype(np.uint8)
        centre = np.nonzero(fill_mask(plain[top:], 200).any(axis=0))[0].mean()
        ys, xs = np.nonzero(fill)
        stroke = max(1, int(np.median(fill.sum(axis=1)[fill.any(axis=1)])) // 2)
        half = (xs.max() - xs.min() + 1) / 2
        mark = np.zeros((top, plain.shape[1]), bool)
        rows = range(ys.min(), ys.max() + 1)
        for r, y in enumerate(rows):
            reach = half * (r + 0.5) / len(rows)
            for x in range(mark.shape[1]):
                if abs(abs(x + 0.5 - centre) - reach) <= stroke / 2 + 0.25:
                    mark[y, x] = True
        out = plain.copy()
        ring = ndimage.binary_dilation(mark, structure=np.ones((3, 3)), iterations=2) & ~mark
        out[:top][ring] = outline_colour
        out[:top][mark] = white
        return out
    return None


def compose(cuts, text):
    """The word from the first cut's frame ends and every cut's letters."""
    first = cuts[0]
    cells = {}
    for c in cuts:
        if c.top == first.top and c.left.shape[0] == first.left.shape[0] or any(ord(ch) > 127 for ch in c.cells):
            for ch, cell in c.cells.items():
                cells.setdefault(ch, cell)
    pieces = [first.left]
    for ch in text:
        if ch.isspace():
            space = next((c.space for c in cuts if c.space is not None and c.space.shape[0] == first.left.shape[0]), None)
            if space is None:
                raise ValueError(f'{text}: no word space in the sources')
            pieces.append(space)
            continue
        cell = cells.get(ch)
        if cell is None:
            cell = with_mark(cells, ch, first.top)
        if cell is None:
            raise ValueError(f'{text}: no {ch!r} in the face')
        pieces.append(cell)
    pieces.append(first.right)
    return np.concatenate(pieces, axis=1)


def sprite(atlas, rect):
    t, x0, y0, x1, y1 = rect
    return atlas[t][y0:y1, x0:x1]


def design(family, atlas):
    """{word: rgba painted over its slot} from the European atlas."""
    spec = FAMILIES[family]
    out = {}
    for word in spec['words']:
        cuts = []
        for rect, text in word.sources:
            found = cut(sprite(atlas, rect), text, spec['fill'])
            if found is None:
                raise ValueError(f'{text}: its sprite does not cut into one cell per letter')
            cuts.append(found)
        image = compose(cuts, word.text)
        slot = sprite(atlas, word.slot)
        if image.shape[1] > slot.shape[1] or image.shape[0] != slot.shape[0]:
            raise ValueError(f'{word.text}: {image.shape[1]}x{image.shape[0]} pixels, the slot holds {slot.shape[1]}x{slot.shape[0]}')
        canvas = np.zeros_like(slot)
        left = (slot.shape[1] - image.shape[1]) // 2
        canvas[:, left:left + image.shape[1]] = image
        out[word] = canvas
    return out


def paint(blob, word_images):
    """The atlas pack with every slot repainted; only the slot's 4x4 blocks are re-encoded."""
    textures = ftex.textures(blob)
    out = blob
    for word, image in word_images.items():
        t, x0, y0, x1, y1 = word.slot
        tex = textures[t]
        full = visuals.decode(out, tex)
        changed = np.zeros(full.shape[:2], bool)
        full = full.copy()
        full[y0:y1, x0:x1] = image
        changed[y0:y1, x0:x1] = True
        out, _ = patch_ftx.patch(out, tex, full, changed)
    return out


def swap_keys(english, other, word):
    """The English layout with every key drawing the English sprite taking the part and quad the slot language's layout
    gives its sprite in the same layer."""
    layout = M.Mbs(english)
    theirs = M.Mbs(other)
    done = 0
    for layer in range(layout.count(M.LAYERS)):
        mine = [k for k in layout.layer_keys(layer) if k['part'] < layout.count(M.PARTS)
                and (k['texture'],) + layout.part_rect(k['part']) == word.english]
        if not mine:
            continue
        if layer >= theirs.count(M.LAYERS):
            raise ValueError(f'layer {layer}: not in the {word.slot_language} layout')
        their = [k for k in theirs.layer_keys(layer) if k['part'] < theirs.count(M.PARTS)
                 and (k['texture'],) + theirs.part_rect(k['part']) == word.slot]
        if len(their) != len(mine):
            raise ValueError(f'layer {layer}: the {word.slot_language} layout draws the slot {len(their)} times, not {len(mine)}')
        for k, t in zip(mine, their):
            layout.put(M.PARTS, k['part'], bytes(theirs.record(M.PARTS, t['part'])))
            layout.put(M.GEOMS, k['geom'], bytes(theirs.record(M.GEOMS, t['geom'])))
            done += 1
    if not done:
        raise ValueError('the English layout does not draw the English sprite')
    return layout.bytes()


def generate(family, prepared):
    spec = FAMILIES[family]
    archives = V.Archives(prepared)
    eu_atlas_blob = archives.read(V.EUROPE, [a.format('UK') for a in V.EUROPE_ARCHIVES], spec['atlas'])
    eu_atlas = V.atlas(eu_atlas_blob)
    images = design(family, eu_atlas)
    targets, report = {}, []
    pairs = [(spec['atlas'], spec['layout'])] + ([spec['classic']] if spec.get('classic') else [])
    for (atlas_name, layout_name), build_name, archive in ((p, b, a) for p in pairs for b, names in V.ENGLISH.items()
                                                          for a in names):
            pack = archives.get(build_name, archive)
            if pack is None:
                report.append((build_name, archive, 'DATA UNAVAILABLE'))
                continue
            entries = {e['path'].lower(): e for e in pack.entries}
            if atlas_name.lower() not in entries or layout_name.lower() not in entries:
                continue
            atlas_blob = pack.read(entries[atlas_name.lower()])
            layout_blob = pack.read(entries[layout_name.lower()])
            where = f'{build_name} {archive}'
            pixels = V.atlas(atlas_blob)
            if any(not np.array_equal(sprite(pixels, w.english), sprite(eu_atlas, w.english)) or
                   not np.array_equal(sprite(pixels, w.slot), sprite(eu_atlas, w.slot)) for w in images):
                report.append((where, atlas_name, 'BLOCKED: the atlas does not hold the European sprites in place'))
                continue
            try:
                new_layout = layout_blob
                for word in images:
                    if word.slot == word.english:
                        continue
                    other = archives.read(V.EUROPE, [a.format(word.slot_language) for a in V.EUROPE_ARCHIVES], layout_name)
                    new_layout = swap_keys(new_layout, other, word)
            except ValueError as e:
                report.append((where, layout_name, f'BLOCKED: {e}'))
                continue
            new_atlas = paint(atlas_blob, images)
            targets[(atlas_name, zlib.crc32(atlas_blob))] = (atlas_blob, new_atlas)
            if new_layout != layout_blob and not spec.get('layout_by'):
                targets[(layout_name, zlib.crc32(layout_blob))] = (layout_blob, new_layout)
            report.append((where, atlas_name, 'ok: ' + ', '.join(w.text for w in images)))
    return targets, report, images


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('family', choices=sorted(FAMILIES))
    ap.add_argument('--prepared', default=str(ROOT / 'game' / 'prepared'))
    ap.add_argument('--out')
    ap.add_argument('--preview')
    args = ap.parse_args()
    visuals.use_native_reader()
    targets, report, images = generate(args.family, args.prepared)
    out = args.out or VISUAL / f'{args.family.replace("_", "-")}-ptbr.oslv'
    blob = oslv.encode([(name, old, new, []) for (name, _), (old, new) in targets.items()])
    Path(out).write_bytes(blob)
    print(f'{len(targets)} entries -> {out} ({len(blob)} bytes)')
    for r in report:
        print(*r)
    if args.preview:
        Path(args.preview).mkdir(parents=True, exist_ok=True)
        for word, image in images.items():
            big = Image.fromarray(image).resize((image.shape[1] * 4, image.shape[0] * 4), Image.NEAREST)
            bg = Image.new('RGBA', big.size, (40, 40, 60, 255))
            bg.alpha_composite(big)
            bg.convert('RGB').save(Path(args.preview) / f'{args.family}-{word.text}.png')
    return 1 if any('BLOCKED' in r[-1] for r in report) else 0


if __name__ == '__main__':
    sys.exit(main())
