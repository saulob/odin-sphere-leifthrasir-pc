#!/usr/bin/env python3
"""Portuguese (Brazil) painted sprites of the interface atlas (GUI/HD_Interface.ftx and Classic Interface.ftx).

The European atlas holds every language's sprites; the English layout draws the English ones. Where an official
language already spells what Portuguese spells (DESCANSAR, NV, A, COMBO!) the English layout draws that language's keys.
Where none does (AÇÃO, CHEFE, FÚRIA, HABILIDADE PRONTA, APTIDÃO PRONTA) the Portuguese word is set from the letters of the
same face (ink_font) and painted into a donor language's slot, and the English keys take the donor's part and quad.

    interface_art.py [--prepared game/prepared] [--preview dir]    writes interface-art-ptbr.oslv (the atlases)

The layout side is applied by make_visuals.build (`edit_layout`), because the layout is also respelled there.
"""
import argparse
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
import ink_font as K  # noqa: E402
import layer_graft as G  # noqa: E402
import make_visuals as V  # noqa: E402
import mbs as M  # noqa: E402
import oslv  # noqa: E402
import patch_ftx  # noqa: E402
import visuals  # noqa: E402

ROOT = HERE.parent.parent
VISUAL = ROOT / 'localization' / 'pt-BR' / 'visual'
ATLASES = ('GUI/HD_Interface.ftx', 'Interface.ftx')


@dataclass(frozen=True)
class Paint:
    name: str
    english: tuple      # sprite the English layout draws
    donor: str          # language whose layout draws `slot`
    slot: tuple         # the donor's sprite, repainted
    face: str
    lines: tuple
    turn: int = 0       # quarter turns (counter-clockwise) of the painted block


@dataclass(frozen=True)
class Graft:
    name: str
    english: tuple
    donor: str          # language whose layers replace the English ones that draw `english`
    note: str


FACES = {
    'orange': (K.yellow_fill, 3, 0, (((0, 378, 860, 618, 918), ('MEJORA DE HABILIDAD', 'LISTA')),
                                      ((1, 1376, 802, 1480, 860), ('TALENTO', 'LISTO')),
                                      ((1, 1314, 188, 1426, 252), ('CAPACITÉ', 'PRÊTE')),
                                      ((1, 1020, 956, 1116, 1020), ('ABILITÀ', 'PRONTA')))),
    'block': (lambda rgba: K.bright_fill(200)(rgba), 1, 2, (((1, 1934, 816, 2038, 844), ('ACCIÓN',)),
                                                              ((2, 32, 458, 110, 482), ('JEFE',)),
                                                              ((1, 1836, 290, 2004, 314), ('BERSERKER',)),
                                                              ((2, 0, 410, 86, 434), ('FURIA',)),
                                                              ((2, 626, 144, 668, 166), ('HP',)),
                                                              ((2, 0, 170, 124, 194), ('EQUIPAG',)),
                                                              ((2, 180, 314, 240, 344), ('DÉF',)),
                                                              ((1, 1745, 1000, 1849, 1024), ('ACTION',)),
                                                              ((2, 248, 481, 326, 505), ('BOSS',)),
                                                              ((1, 1905, 516, 2037, 540), ('BERSERK',)))),
}
PAINTS = (
    Paint('skill up ready', (0, 1234, 666, 1296, 818), 'IT', (0, 1846, 464, 1910, 652), 'orange', ('HABILIDADE', 'PRONTA'), 1),
    Paint('ability ready', (1, 1122, 194, 1254, 256), 'ES', (1, 1376, 802, 1480, 860), 'orange', ('APTIDÃO', 'PRONTA')),
    Paint('action', (1, 1745, 1000, 1849, 1024), 'ES', (1, 1934, 816, 2038, 844), 'block', ('AÇÃO',)),
    Paint('boss', (2, 248, 481, 326, 505), 'ES', (2, 32, 458, 110, 482), 'block', ('CHEFE',)),
    Paint('berserk', (1, 1905, 516, 2037, 540), 'ES', (1, 1836, 290, 2004, 314), 'block', ('FÚRIA',)),
)
COMBO = Paint('chain', (0, 2000, 854, 2048, 1020), 'FR', (1, 0, 684, 164, 730), 'combo', ('COMBO!',))
GRAFTS = (
    Graft('rest', (0, 1404, 0, 1646, 172), 'ES', 'DESCANSAR over ENTRAR: ES draws the same words'),
    Graft('level', (0, 574, 918, 618, 1022), 'ES', 'NV (Nível): ES draws the gauge tab with its NV sprite'),
    Graft('accept', (2, 956, 462, 984, 494), 'ES', 'A (Aceitar): every official language draws A'),
)


def sprite(atlas, rect):
    t, x0, y0, x1, y1 = rect
    return atlas[t][y0:y1, x0:x1]


def face_of(atlas, name):
    fill, ring, stroke, sources = FACES[name]
    face = K.Face()
    face.ring, face.stroke = ring, stroke
    for rect, lines in sources:
        K.extract(sprite(atlas, rect), list(lines), fill, face)
    return face


def fitted(block, slot_shape, turn):
    """`block` turned and, when larger than the slot, reduced to fit; centred on a transparent slot-sized canvas."""
    if turn:
        block = np.rot90(block, turn)
    h, w = slot_shape[:2]
    if block.shape[0] > h or block.shape[1] > w:
        scale = min(h / block.shape[0], w / block.shape[1])
        size = (max(1, int(block.shape[1] * scale)), max(1, int(block.shape[0] * scale)))
        block = np.asarray(Image.fromarray(block).resize(size, Image.LANCZOS))
    canvas = np.zeros((h, w, 4), np.uint8)
    y, x = (h - block.shape[0]) // 2, (w - block.shape[1]) // 2
    canvas[y:y + block.shape[0], x:x + block.shape[1]] = block
    return canvas


def combo_word(atlas, slot):
    """The French COMBO ! sprite with the space before the exclamation mark closed (Portuguese sets COMBO!)."""
    image = sprite(atlas, slot)
    mask = K.bright_fill(200)(image)
    columns = np.nonzero(mask.any(axis=0))[0]
    runs = np.split(columns, np.nonzero(np.diff(columns) > 1)[0] + 1)
    mark, word = runs[-1], runs[-2]
    gap = mark[0] - word[-1] - 1
    close = max(0, gap - 4)
    out = np.zeros_like(image)
    cut = mark[0] - 3
    out[:, :cut] = image[:, :cut]
    right = image[:, cut:]
    x = cut - close
    out[:, x:x + right.shape[1]] = np.where(right[..., 3:4] > 0, right, out[:, x:x + right.shape[1]])
    return out


def design(atlas):
    """{slot: rgba painted over it}."""
    faces = {}
    out = {}
    for item in PAINTS:
        face = faces.setdefault(item.face, face_of(atlas, item.face))
        block = K.set_block(face, list(item.lines))
        out[item.slot] = fitted(block, sprite(atlas, item.slot).shape, item.turn)
    out[COMBO.slot] = combo_word(atlas, COMBO.slot)
    return out


def paint(blob, images):
    textures = ftex.textures(blob)
    out = blob
    for slot, image in images.items():
        t, x0, y0, x1, y1 = slot
        tex = textures[t]
        full = visuals.decode(out, tex).copy()
        changed = np.zeros(full.shape[:2], bool)
        full[y0:y1, x0:x1] = image
        changed[y0:y1, x0:x1] = True
        out, _ = patch_ftx.patch(out, tex, full, changed)
    return out


def rects():
    out = []
    for item in [*PAINTS, COMBO]:
        out += [item.slot, item.english]
    return out + [g.english for g in GRAFTS]


def locate(pixels, sprite, agreement=0.94):
    """The rectangle of `pixels` whose alpha best matches the sprite's (at least `agreement` of the pixels equal)."""
    from scipy.signal import fftconvolve
    alpha = sprite[..., 3].astype(np.float32) / 255
    if not (alpha > 0.5).any():
        return None
    h, w = alpha.shape
    best = None
    for t, texture in enumerate(pixels):
        if texture.shape[0] < h or texture.shape[1] < w:
            continue
        image = texture[..., 3].astype(np.float32) / 255
        ssd = (fftconvolve(image * image, np.ones_like(alpha), mode='valid')
               - 2 * fftconvolve(image, alpha[::-1, ::-1], mode='valid') + float((alpha * alpha).sum()))
        y, x = np.unravel_index(int(np.argmin(ssd)), ssd.shape)
        same = float(((image[y:y + h, x:x + w] > 0.5) == (alpha > 0.5)).mean())
        if best is None or same > best[0]:
            best = (same, (t, int(x), int(y), int(x) + w, int(y) + h))
    return best[1] if best and best[0] >= agreement else None


def resolve(european, pixels):
    """{European rect: the same sprite's rect in `pixels`} for every sprite this module paints over or replaces, or
    None when the atlas lacks one of them (a different rasterization)."""
    out = {}
    for rect in rects():
        if rect in out:
            continue
        if np.array_equal(sprite(pixels, rect) if rect[0] < len(pixels) else None, sprite(european, rect)):
            out[rect] = rect
            continue
        found = locate(pixels, sprite(european, rect))
        if found is None:
            return None
        out[rect] = found
    return out


def edit_layout(layout, europe, peers, lang_of, where_rects, report, where):
    """Draws the Portuguese sprites in a respelled English layout. `peers` is make_visuals.layer_peers of it."""
    graft = G.Graft(layout)
    at = where_rects.get
    donors = {language: europe[language] for language in set(lang_of.values())}
    done = 0
    for item in [*GRAFTS, *PAINTS, COMBO]:
        donor = donors[item.donor]
        for layer in G.layers_drawing(layout, at(item.english)):
            mine = [lb for pid, lb in peers.get(layer, ()) if lang_of[pid] == item.donor]
            if not mine:
                report.append((where, 'art', item.name, '', f'BLOCKED layer {layer}: no {item.donor} peer'))
                continue
            try:
                if isinstance(item, Graft) or item is COMBO:
                    graft.replace_layer(layer, donor, mine[0])
                elif item.face == 'block':
                    graft.retarget_sprite(layer, at(item.english), at(item.slot))
                else:
                    graft.swap_sprite(layer, donor, mine[0], at(item.english), at(item.slot))
            except ValueError as e:
                report.append((where, 'art', item.name, '', f'BLOCKED {e}'))
                continue
            done += 1
    report.append((where, 'art', 'painted sprites', '', f'{done} layers'))
    return done


def prompt_targets():
    """The controller-prompt patch's exact-source targets: the atlas may reach the game with it already laid on."""
    import make_dcpx
    blob = (ROOT / 'launcher' / 'prompts' / 'xbox-prompts.dcpx').read_bytes()
    return {(n.lower(), size, crc): runs for n, size, crc, _, runs in make_dcpx.parse(blob)}


def with_prompts(prompts, name, data):
    runs = prompts.get((name.lower(), len(data), zlib.crc32(data)))
    if runs is None:
        return data
    out = bytearray(data)
    for offset, _, after in runs:
        out[offset:offset + 16] = after
    return bytes(out)


def build(prepared, preview=None):
    archives = V.Archives(prepared)
    european = V.atlas(archives.read(V.EUROPE, [a.format('UK') for a in V.EUROPE_ARCHIVES], ATLASES[0]))
    images = design(european)
    prompts = prompt_targets()
    targets, report = {}, []
    for atlas_name in ATLASES:
        for build_name, names in V.ENGLISH.items():
            for archive in names:
                pack = archives.get(build_name, archive)
                if pack is None:
                    continue
                entry = next((e for e in pack.entries if e['path'].lower() == atlas_name.lower()), None)
                if entry is None:
                    continue
                blob = pack.read(entry)
                where = f'{build_name} {archive} {atlas_name}'
                where_rects = resolve(european, V.atlas(blob))
                if where_rects is None:
                    report.append((where, 'BLOCKED: the atlas does not hold the European sprites'))
                    continue
                moved = {where_rects[slot]: image for slot, image in images.items()}
                for variant in {zlib.crc32(v): v for v in (blob, with_prompts(prompts, atlas_name, blob))}.values():
                    targets[(atlas_name, zlib.crc32(variant))] = (variant, paint(variant, moved))
                report.append((where, 'ok'))
    if preview:
        Path(preview).mkdir(parents=True, exist_ok=True)
        for slot, image in images.items():
            big = Image.fromarray(image).resize((image.shape[1] * 4, image.shape[0] * 4), Image.NEAREST)
            bg = Image.new('RGBA', big.size, (120, 120, 160, 255))
            bg.alpha_composite(big)
            bg.convert('RGB').save(Path(preview) / f'interface-{slot[0]}-{slot[1]}-{slot[2]}.png')
    return targets, report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--prepared', default=str(ROOT / 'game' / 'prepared'))
    ap.add_argument('--out', default=str(VISUAL / 'interface-art-ptbr.oslv'))
    ap.add_argument('--preview')
    args = ap.parse_args()
    visuals.use_native_reader()
    targets, report = build(args.prepared, args.preview)
    blob = oslv.encode([(name, old, new, []) for (name, _), (old, new) in targets.items()])
    Path(args.out).write_bytes(blob)
    print(f'{len(targets)} atlases -> {args.out} ({len(blob)} bytes)')
    for row in report:
        print(*row)


if __name__ == '__main__':
    main()
