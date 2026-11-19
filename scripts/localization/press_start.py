#!/usr/bin/env python3
"""The Classic PRESS ANY BUTTON (op_press_start.ftx and .mbs) in Portuguese (Brazil): PRESSIONE QUALQUER BOTÃO.

The atlas holds one row of the title serif face per language and each language's layout draws its own row. The
Portuguese row is set from the letters of the Spanish and French rows (the plain N from the French one), the tilde
drawn in the face's strokes, between the Spanish row's own leading and trailing ornaments, at the size of every other
row. It is painted into the atlas's free rows below the official ones; the English layout takes the Italian layout's
layers with the row key pointed at the Portuguese row, its quad widened in proportion so the letters keep their scale.

The single-row atlases (Classic op_press_start of USA and Japan, Refined OnMemory/HD_op_press_start of every build)
hold only the English row: the same Portuguese row is scaled into the whole atlas, and each key's quad grows by the
Portuguese/English size ratio of the European rows, standing on the English quad's bottom edge.

    press_start.py [--prepared game/prepared] [--out file] [--preview file]
"""
import argparse
import copy
import struct
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
SOURCES = (
    ('CUSA05262-1.01', 'Odin2_EU_PATCH1_OR_{}.CPK'),
    ('CUSA05262-1.01', 'Odin2_OR_{}.CPK'),
    ('CUSA05262-1.00', 'Odin2_OR_{}.CPK'),
)
Q_KERN = -14
LANGUAGES = ('UK', 'ES', 'FR', 'DE', 'IT')


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
    face.cells['N'] = plain.cells['N']
    face.ring, face.stroke = 2, 2
    face.cells['Ã'] = tilde_over(face, 'A')
    return face


TILDE_HEIGHT = 0.65


def tilde_over(face, letter):
    """The letter with a tilde as plain as the face's own acute: its fill colour only (the face has no outline) and a
    wave no taller than the acute."""
    mark_face = copy.copy(face)
    mark_face.ring = 0
    mark_face.height = face.height * TILDE_HEIGHT
    return K._drawn_mark(mark_face, face.cells[letter], 'tilde')


def lettering_rows(image):
    """(top, bottom) rows of a row's capitals: the rows most of its letters cross (marks and flourishes left out)."""
    ink = (image[..., 3] > 128).sum(axis=1)
    rows = np.nonzero(ink > 0.35 * ink.max())[0]
    return int(rows.min()), int(rows.max())


def row_image(face, spanish):
    """The Portuguese row between the Spanish row's leading and trailing ornaments, on the Spanish baseline."""
    left, right = text_columns(spanish, K.bright_fill(190))
    lead, trail = spanish[:, :left], spanish[:, right:]
    text = K.set_block(face, [TEXT], gap=face.gap, kern={'Q': Q_KERN, 'O': 2})
    _, es_bottom = lettering_rows(spanish[:, left:right])
    _, pt_bottom = lettering_rows(text)
    dy = es_bottom - pt_bottom
    top = max(0, -dy)
    height = max(spanish.shape[0] + top, text.shape[0] + top + dy)
    canvas = np.zeros((height, lead.shape[1] + text.shape[1] + trail.shape[1], 4), np.uint8)
    canvas[top:top + spanish.shape[0], :lead.shape[1]] = lead
    K._over(canvas, text, lead.shape[1], top + dy)
    canvas[top:top + spanish.shape[0], lead.shape[1] + text.shape[1]:] = trail
    rows = np.nonzero(canvas[..., 3].any(axis=1))[0]
    return canvas[rows.min():rows.max() + 1]


def free_rect(pixels, shape, below):
    """(texture, x0, y0, x1, y1) of the first empty rows of texture 0 under `below` that hold an image of `shape`."""
    h, w = shape[:2]
    empty = ~(pixels[..., 3] > 0).any(axis=1)
    for y in range(below + 4, pixels.shape[0] - h + 1, 4):
        if empty[y - 2:y + h + 2].all() and w <= pixels.shape[1]:
            return 0, 0, y, w, y + h
    raise ValueError('the atlas has no free rows for the Portuguese row')


def paint(blob, rect, image):
    tex = ftex.textures(blob)[rect[0]]
    full = visuals.decode(blob, tex).copy()
    changed = np.zeros(full.shape[:2], bool)
    x0, y0, x1, y1 = rect[1:]
    full[y0:y1, x0:x1] = image
    changed[y0:y1, x0:x1] = True
    return patch_ftx.patch(blob, tex, full, changed)[0]


def slot_of(layout):
    key = layout.layer_keys(0)[-1]
    return (key['texture'],) + layout.part_rect(key['part'])


SINGLE_ROWS = (
    ('CUSA05262-1.01', 'Odin2_RE_UK.CPK', 'OnMemory/HD_op_press_start.ftx', 'OnMemory/REHD_op_press_start.mbs'),
    ('CUSA05262-1.00', 'Odin2_RE_UK.CPK', 'OnMemory/HD_op_press_start.ftx', 'OnMemory/REHD_op_press_start.mbs'),
    ('CUSA05083-1.00', 'Odin2_OR_US.CPK', ATLAS, LAYOUT),
    ('CUSA05083-1.00', 'Odin2_RE_US.CPK', 'OnMemory/HD_op_press_start.ftx', 'OnMemory/REHD_op_press_start.mbs'),
    ('CUSA01290-1.02', 'Odin2_ORHD.CPK', ATLAS, LAYOUT),
    ('CUSA01290-1.02', 'Odin2_RE.CPK', 'OnMemory/HD_op_press_start.ftx', 'OnMemory/REHD_op_press_start.mbs'),
    ('CUSA01290-1.00', 'Odin2_ORHD.CPK', ATLAS, LAYOUT),
    ('CUSA01290-1.00', 'Odin2_RE.CPK', 'OnMemory/HD_op_press_start.ftx', 'OnMemory/REHD_op_press_start.mbs'),
)


def scaled_into(image, shape):
    """`image` reduced to fit a texture of `shape`, at its top left, the rest of the texture cleared."""
    h, w = shape[:2]
    scale = min(1.0, w / image.shape[1], h / image.shape[0])
    size = (max(1, round(image.shape[1] * scale)), max(1, round(image.shape[0] * scale)))
    small = np.asarray(Image.fromarray(image).resize(size, Image.LANCZOS))
    canvas = np.zeros((h, w, 4), np.uint8)
    canvas[:size[1], :size[0]] = small
    return canvas, (0, 0, 0, size[0], size[1])


def grown_row(layout, english, rect, ratio):
    """Every key drawing the `english` rectangle draws `rect`, on its quad grown by `ratio` about the middle of its
    bottom edge."""
    fx, fy = ratio
    done = 0
    for k in range(layout.count(M.KEYS)):
        key = layout.key(k)
        if key['part'] >= layout.count(M.PARTS) or (key['texture'],) + layout.part_rect(key['part']) != english:
            continue
        values = list(struct.unpack('<12f', layout.record(M.PARTS, key['part'])))
        for i in range(1, 6):
            values[2 * i] = rect[1] if values[2 * i] <= english[1] else rect[3]
            values[2 * i + 1] = rect[2] if values[2 * i + 1] <= english[2] else rect[4]
        values[0], values[1] = (rect[1] + rect[3]) / 2, (rect[2] + rect[4]) / 2
        part = layout.append(M.PARTS, [struct.pack('<12f', *values)])
        quad = layout.geom(key['geom'])
        cx = sum(q[0] for q in quad) / 4
        bottom = max(q[1] for q in quad)
        grown = [(cx + (x - cx) * fx, bottom + (y - bottom) * fy) for x, y in quad]
        xs, ys = [q[0] for q in grown], [q[1] for q in grown]
        geom = layout.append(M.GEOMS, [struct.pack('<12f', (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2,
                                                   *[v for q in grown for v in q], *grown[0])])
        raw = bytearray(layout.record(M.KEYS, k))
        struct.pack_into('<H', raw, 10, part)
        struct.pack_into('<H', raw, 14, geom)
        layout.put(M.KEYS, k, bytes(raw))
        done += 1
    if not done:
        raise ValueError(f'no key draws the English row {english}')
    return done


def single_rows(prepared, image, ratio, targets, report):
    for build, archive, atlas_name, layout_name in SINGLE_ROWS:
        where = f'{build} {archive} {layout_name}'
        atlas_blob, layout_blob = (read(prepared, build, archive, e) for e in (atlas_name, layout_name))
        if atlas_blob is None or layout_blob is None:
            report.append((where, 'DATA UNAVAILABLE'))
            continue
        if (layout_name, zlib.crc32(layout_blob)) in targets:
            report.append((where, 'ok: same files as an earlier source'))
            continue
        layout = M.Mbs(layout_blob)
        english = slot_of(layout)
        tex = ftex.textures(atlas_blob)[english[0]]
        canvas, rect = scaled_into(image, (tex['height'], tex['width']))
        rect = (english[0],) + rect[1:]
        grown_row(layout, english, rect, ratio)
        full = np.zeros((tex['height'], tex['width'], 4), np.uint8)
        full[:] = canvas
        painted = patch_ftx.patch(atlas_blob, tex, full, np.ones(full.shape[:2], bool))[0]
        targets.setdefault((atlas_name, zlib.crc32(atlas_blob)), (atlas_blob, painted, []))
        targets[(layout_name, zlib.crc32(layout_blob))] = (layout_blob, layout.bytes(), layout.inserts)
        report.append((where, f'ok: English row {english} -> {rect}, quads x{ratio[0]:.3f} y{ratio[1]:.3f}'))


def generate(prepared):
    targets, report = {}, []
    european = None
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
        rects = {L: slot_of(M.Mbs(read(prepared, build, template.format(L), LAYOUT))) for L in LANGUAGES}
        _, sx0, sy0, sx1, sy1 = rects['ES']
        image = row_image(face_of(pixels, {L: rects[L] for L in ('ES', 'FR')}), pixels[sy0:sy1, sx0:sx1])
        rect = free_rect(pixels, image.shape, max(r[4] for r in rects.values()))
        layout = M.Mbs(layout_blob)
        graft = G.Graft(layout)
        for layer in range(layout.count(M.LAYERS)):
            graft.replace_layer(layer, donor_layout, layer)
            graft.retarget_sprite(layer, slot, rect)
        targets[(ATLAS, zlib.crc32(atlas_blob))] = (atlas_blob, paint(atlas_blob, rect, image), [])
        targets[(LAYOUT, zlib.crc32(layout_blob))] = (layout_blob, layout.bytes(), layout.inserts)
        report.append((where, f'ok: row {rect}, {image.shape[1]}x{image.shape[0]} (Italian slot {slot})'))
        if european is None:
            english = rects['UK']
            european = image, (image.shape[1] / (english[3] - english[1]), image.shape[0] / (english[4] - english[2]))
    if european is None:
        report.append(('single-row atlases', 'BLOCKED no European row to set them from'))
        return targets, report
    single_rows(prepared, *european, targets, report)
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
