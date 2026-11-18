#!/usr/bin/env python3
"""The Classic PRESS ANY BUTTON (op_press_start.ftx and .mbs) in Portuguese (Brazil): PRESSIONE QUALQUER BOTÃO.

The atlas holds one row of the title serif face per language and each language's layout draws its own row. The
Portuguese row is set from the letters of the Spanish and French rows (the first and last letters of a row carry its
flourishes, so the swash P and the flourish of the last letter come from the Spanish row, the plain N from the French
one), the acute is the face's own and the tilde is drawn in its strokes. It is painted into the Italian slot, which
is as wide as the Portuguese line, and the English layout takes the Italian layout's layers.

    press_start.py [--prepared game/prepared] [--out file] [--preview file]
"""
import argparse
import sys
import zlib
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'prompts'))
import cpk  # noqa: E402
import ftex  # noqa: E402
import ink_font as K  # noqa: E402
import layer_graft as G  # noqa: E402
import mbs as M  # noqa: E402
import oslv  # noqa: E402
import patch_ftx  # noqa: E402
import visuals  # noqa: E402

ROOT = HERE.parent.parent
DEFAULT_OUT = ROOT / 'localization' / 'pt-BR' / 'visual' / 'press-start-ptbr.oslv'
ATLAS, LAYOUT = 'op_press_start.ftx', 'op_press_start.mbs'
TEXT = 'PRESSIONE QUALQUER BOTÃO'
FLOURISH = '❦'
SOURCES = (
    ('CUSA05262-1.01', 'Odin2_EU_PATCH1_OR_{}.CPK'),
    ('CUSA05262-1.01', 'Odin2_OR_{}.CPK'),
    ('CUSA05262-1.00', 'Odin2_OR_{}.CPK'),
)
Q_KERN = -14


def read(prepared, build, archive, entry):
    path = Path(prepared) / build / archive
    if not path.is_file():
        return None
    pack = cpk.Cpk(path, original=True)
    try:
        hit = next((e for e in pack.entries if e['path'].lower() == entry.lower()), None)
        return pack.read(hit) if hit else None
    finally:
        pack.f.close()


def text_columns(image, fill):
    labels, _ = ndimage.label(fill(image), structure=np.ones((3, 3)))
    spans = sorted((s[1].start, s[1].stop) for s in ndimage.find_objects(labels))
    profile = (image[..., 3] > 40).sum(axis=0)
    left = min(range(spans[0][0] + 28, spans[0][0] + 50), key=lambda c: (profile[c], c))
    right = min(range(spans[-1][1] - 52, spans[-1][1] - 28), key=lambda c: (profile[c], -c))
    return left, right


def face_of(atlas, rects):
    fill = K.bright_fill(190)
    faces = {}
    for name, texts in (('ES', ('PULSA CUALQUIER BOTÓN',)), ('FR', ('APPUYEZ SUR UN BOUTON', 'APPUYEZ SUR UN TOUCHE')),
                        ('UK', ('PRESS ANY',))):
        if name not in rects:
            continue
        texture, x0, y0, x1, y1 = rects[name]
        image = (atlas[texture] if isinstance(atlas, list) else atlas)[y0:y1, x0:x1]
        left, right = text_columns(image, fill)
        if name == 'UK':
            right = image.shape[1]
        for text in texts:
            face = K.Face()
            try:
                K.extract(image[:, left:right], [text], fill, face)
            except ValueError:
                continue
            faces[name] = face
            break
        else:
            raise ValueError(f'the {name} row does not spell its text')
    face, plain = faces['ES'], faces.get('FR') or faces['UK']
    swash_last = face.cells['N']
    face.cells['N'] = plain.cells['N']
    width = swash_last.rgba.shape[1] - (swash_last.fill[2] - 6)
    flourish = swash_last.rgba[:, swash_last.fill[2] - 6:].copy()
    face.cells[FLOURISH] = K.Cell(flourish, (0, swash_last.fill[1], width, swash_last.fill[3]), swash_last.base_row)
    face.ring, face.stroke = 2, 2
    return face


def row_image(face):
    return K.set_block(face, [TEXT + FLOURISH], gap=face.gap, kern={'Q': Q_KERN, 'O': 2})


def fitted(image, shape):
    h, w = shape[:2]
    if image.shape[1] > w or image.shape[0] > h:
        scale = min(w / image.shape[1], h / image.shape[0])
        image = np.asarray(Image.fromarray(image).resize((max(1, int(image.shape[1] * scale)), max(1, int(image.shape[0] * scale))),
                                                         Image.LANCZOS))
    canvas = np.zeros((h, w, 4), np.uint8)
    y, x = (h - image.shape[0]) // 2, 0
    canvas[y:y + image.shape[0], x:x + image.shape[1]] = image
    return canvas


def paint(blob, slot, image):
    tex = ftex.textures(blob)[slot[0]]
    full = visuals.decode(blob, tex).copy()
    changed = np.zeros(full.shape[:2], bool)
    x0, y0, x1, y1 = slot[1:]
    full[y0:y1, x0:x1] = image
    changed[y0:y1, x0:x1] = True
    return patch_ftx.patch(blob, tex, full, changed)[0]


def slot_of(layout):
    key = layout.layer_keys(0)[-1]
    return (key['texture'],) + layout.part_rect(key['part'])


def generate(prepared):
    targets, report = {}, []
    for build, template in SOURCES:
        archive = template.format('UK')
        where = f'{build} {archive}'
        atlas_blob, layout_blob = (read(prepared, build, archive, e) for e in (ATLAS, LAYOUT))
        donor = read(prepared, build, template.format('IT'), LAYOUT)
        if atlas_blob is None or layout_blob is None or donor is None:
            report.append((where, 'DATA UNAVAILABLE'))
            continue
        if (LAYOUT, zlib.crc32(layout_blob)) in targets:
            report.append((where, 'ok: same files as an earlier source'))
            continue
        donor_layout = M.Mbs(donor)
        slot = slot_of(donor_layout)
        pixels = visuals.decode(atlas_blob, ftex.textures(atlas_blob)[0])
        rects = {L: slot_of(M.Mbs(read(prepared, build, template.format(L), LAYOUT))) for L in ('ES', 'FR')}
        image = fitted(row_image(face_of(pixels, rects)), (slot[4] - slot[2], slot[3] - slot[1], 4))
        layout = M.Mbs(layout_blob)
        graft = G.Graft(layout)
        for layer in range(layout.count(M.LAYERS)):
            graft.replace_layer(layer, donor_layout, layer)
        targets[(ATLAS, zlib.crc32(atlas_blob))] = (atlas_blob, paint(atlas_blob, slot, image), [])
        targets[(LAYOUT, zlib.crc32(layout_blob))] = (layout_blob, layout.bytes(), layout.inserts)
        report.append((where, f'ok: slot {slot}, line {image.shape[1]}x{image.shape[0]}'))
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
    print(f'{len(targets)} targets -> {args.out} ({len(blob)} bytes)')
    for row in report:
        print(*row)
    return 1 if any('BLOCKED' in r[-1] for r in report) else 0


if __name__ == '__main__':
    sys.exit(main())
