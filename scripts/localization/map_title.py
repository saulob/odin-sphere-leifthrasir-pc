#!/usr/bin/env python3
"""The world map banner (GUI/HD_worldmap.ftx, Classic worldmap.ftx) in Portuguese (Brazil): Map of -> Mapa de.

The banner's caption is dark ink painted on its parchment, in texture 9 of the atlas every language shares. The
Portuguese is set from the caption's own letters: M, a and p as painted, d the face's p turned half a turn (the uncial
d), e its o opened on the lower right with a tongue in the same ink. The English letters are lifted from the parchment
(the ink pixels filled from their clean neighbours), the new line is centred where the English one stood, and only
the 4x4 blocks that change are re-encoded. ERION, the proper name, is not touched.

    map_title.py [--prepared game/prepared] [--out file] [--preview file]
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
import oslv  # noqa: E402
import patch_ftx  # noqa: E402
import visuals  # noqa: E402

ROOT = HERE.parent.parent
DEFAULT_OUT = ROOT / 'localization' / 'pt-BR' / 'visual' / 'worldmap-ptbr.oslv'
ENTRIES = ('GUI/HD_worldmap.ftx', 'worldmap.ftx')
SOURCES = (
    ('CUSA05262-1.01', ('Odin2_RE_UK.CPK', 'Odin2_OR_UK.CPK')),
    ('CUSA05262-1.00', ('Odin2_RE_UK.CPK', 'Odin2_OR_UK.CPK')),
    ('CUSA05083-1.00', ('Odin2_RE_US.CPK', 'Odin2_OR_US.CPK')),
    ('CUSA01290-1.02', ('Odin2_RE.CPK', 'Odin2_ORHD.CPK')),
    ('CUSA01290-1.00', ('Odin2_RE.CPK', 'Odin2_ORHD.CPK')),
)
TEXTURE = 9
CAPTION = (140, 59, 248, 83)    # x0, y0, x1, y1: the caption's rows, between the scroll's top art and ERION
INK = 150                       # luminance below which a pixel carries ink
LETTERS = 'Mapof'
TEXT = 'Mapa de'


class MapError(Exception):
    pass


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


def luminance(rgb):
    return rgb[..., :3].astype(np.float32).mean(-1)


def parchment(region, mask):
    """The region with the masked pixels filled from their unmasked neighbours, repeatedly averaged inwards."""
    out = region.astype(np.float32).copy()
    known = ~mask
    kernel = np.ones((3, 3), np.float32)
    while not known.all():
        weight = ndimage.convolve(known.astype(np.float32), kernel, mode='nearest')
        grow = (~known) & (weight > 0)
        if not grow.any():
            raise MapError('the caption has no clean parchment around it')
        for c in range(3):
            total = ndimage.convolve(out[..., c] * known, kernel, mode='nearest')
            out[..., c][grow] = total[grow] / weight[grow]
        known = known | grow
    return out


def caption_letters(region):
    """{letter: alpha} of the English caption (M, a, p, o, f, left to right) and the ink colour."""
    lum = luminance(region)
    labels, count = ndimage.label(lum < INK, structure=np.ones((3, 3)))
    found = [(i + 1, s) for i, s in enumerate(ndimage.find_objects(labels)) if s[0].stop - s[0].start >= 6]
    found.sort(key=lambda f: f[1][1].start)
    if len(found) != len(LETTERS):
        raise MapError(f'the caption has {len(found)} letters, not {len(LETTERS)}')
    ink = np.median(region[lum < np.percentile(lum, 3)][:, :3], axis=0)
    out = {}
    for ch, (label, (ys, xs)) in zip(LETTERS, found):
        ys = slice(max(0, ys.start - 1), ys.stop + 1)
        xs = slice(max(0, xs.start - 1), xs.stop + 1)
        own = ndimage.binary_dilation(labels == label, iterations=1)[ys, xs]
        out[ch] = (ys.start, xs.start, lum[ys, xs], own)
    return out, ink


def alpha_of(lum, paper):
    ink_l = lum.min()
    return np.clip((paper - lum) / np.maximum(1.0, paper - ink_l), 0.0, 1.0)


def made_e(o_alpha):
    """The face's o opened on the lower right with a tongue across its middle: the uncial e."""
    a = o_alpha.copy()
    h, w = a.shape
    mid = h // 2
    a[mid + 1:h - 2, w * 2 // 3:] = 0.0
    inner = np.nonzero(a[mid] > 0.5)[0]
    if len(inner):
        a[mid, inner.min():inner.max() + 1] = np.maximum(a[mid, inner.min():inner.max() + 1], 0.9)
        a[mid - 1, inner.min() + 1:inner.max()] = np.maximum(a[mid - 1, inner.min() + 1:inner.max()], 0.35)
    return a


def caption(region):
    """The region with the caption respelled; (new rgb region, changed mask)."""
    letters, ink = caption_letters(region)
    paper_mask = ndimage.binary_dilation(luminance(region) < 200, iterations=1)
    english = np.zeros(paper_mask.shape, bool)
    for ch, (y, x, lum, own) in letters.items():
        english[y:y + lum.shape[0], x:x + lum.shape[1]] = True
    paper = parchment(region, paper_mask & english)
    paper_l = luminance(paper)
    glyph = {}
    for ch, (y, x, lum, own) in letters.items():
        glyph[ch] = (y, alpha_of(lum, paper_l[y:y + lum.shape[0], x:x + lum.shape[1]]) * own)
    glyph['d'] = (glyph['p'][0] - (glyph['p'][1].shape[0] - glyph['a'][1].shape[0]), np.rot90(glyph['p'][1], 2))
    glyph['e'] = (glyph['o'][0], made_e(glyph['o'][1]))
    xs = {ch: x for ch, (y, x, lum, _) in letters.items()}
    ends = {ch: x + lum.shape[1] for ch, (y, x, lum, _) in letters.items()}
    kern = xs['a'] - ends['M'], xs['p'] - ends['a']
    gap = int(round(np.mean(kern)))
    space = xs['o'] - ends['p']
    plan, at = [], 0
    for i, ch in enumerate(TEXT):
        if ch == ' ':
            at += space - gap
            continue
        y, a = glyph[ch]
        if i:
            at += gap
        plan.append((y, at, a))
        at += a.shape[1]
    left = (xs['M'] + ends['f']) // 2 - at // 2
    alpha = np.zeros(paper_l.shape, np.float32)
    for y, x, a in plan:
        x += left
        if x < 0 or x + a.shape[1] > alpha.shape[1] or y < 0 or y + a.shape[0] > alpha.shape[0]:
            raise MapError('the Portuguese caption does not fit the banner')
        alpha[y:y + a.shape[0], x:x + a.shape[1]] = np.maximum(alpha[y:y + a.shape[0], x:x + a.shape[1]], a)
    out = region.astype(np.float32).copy()
    out[..., :3] = paper[..., :3] * (1 - alpha[..., None]) + ink[None, None, :] * alpha[..., None]
    out = np.clip(np.round(out), 0, 255).astype(np.uint8)
    return out, (out != region).any(-1)


def respell(blob):
    """The atlas with the caption in Portuguese, and the RGBA of the touched region."""
    tex = ftex.textures(blob)[TEXTURE]
    full = visuals.decode(blob, tex).copy()
    x0, y0, x1, y1 = CAPTION
    region, changed = caption(full[y0:y1, x0:x1])
    full[y0:y1, x0:x1] = region
    mask = np.zeros(full.shape[:2], bool)
    mask[y0:y1, x0:x1] = changed
    patched, _ = patch_ftx.patch(blob, tex, full, mask)
    return patched, full[y0 - 6:y1 + 30, x0 - 100:x1 + 10]


def generate(prepared):
    targets, report, previews = {}, [], []
    for build, archives in SOURCES:
        for archive, entry in zip(archives, ENTRIES):
            where = f'{build} {archive} {entry}'
            blob = read(prepared, build, archive, entry)
            if blob is None:
                report.append((where, 'DATA UNAVAILABLE'))
                continue
            key = (entry, len(blob), zlib.crc32(blob))
            if key in targets:
                report.append((where, 'ok: same atlas as an earlier source'))
                continue
            try:
                patched, preview = respell(blob)
            except MapError as e:
                report.append((where, f'BLOCKED {e}'))
                continue
            targets[key] = (blob, patched)
            previews.append(preview)
            report.append((where, f'ok: {len(blob)} bytes'))
    return targets, report, previews


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--prepared', default=str(ROOT / 'game' / 'prepared'))
    ap.add_argument('--out', default=str(DEFAULT_OUT))
    ap.add_argument('--preview')
    args = ap.parse_args()
    visuals.use_native_reader()
    targets, report, previews = generate(args.prepared)
    blob = oslv.encode([(name, old, new, []) for (name, _, _), (old, new) in targets.items()])
    Path(args.out).write_bytes(blob)
    print(f'{len(targets)} targets -> {args.out} ({len(blob)} bytes)')
    for row in report:
        print(*row)
    if args.preview and previews:
        tile = previews[0]
        Image.fromarray(tile).resize((tile.shape[1] * 4, tile.shape[0] * 4), Image.NEAREST).save(args.preview)
    return 1 if any('BLOCKED' in r[-1] for r in report) else 0


if __name__ == '__main__':
    sys.exit(main())
