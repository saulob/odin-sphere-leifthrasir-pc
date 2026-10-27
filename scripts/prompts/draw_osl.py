#!/usr/bin/env python3
"""Paints the Xbox prompts over the PlayStation ones in Odin Sphere Leifthrasir's button art, in place: every sprite
keeps its rectangle, so the sprite definitions (.mbs) stay as they are.

The button atlas (the 512 x 256 texture of Interface.ftx) holds the same parts as Dragon's Crown Pro's
Interface_arranged000 at half the size, laid out differently. Each part Dragon's Crown Pro PC changed is taken from its
Xbox atlas (its committed xbox-prompts.dcpx applied to its stage/Interface_US.ftx) and reduced 2:1 into Odin's
rectangle, registered on the original artwork. The second button set in OnMemory.ftx (L1/L2/R1/R2 keys, symbol discs,
START/SELECT tags) has no Dragon's Crown counterpart and is painted the way Dragon's Crown Pro PC's draw_dcp.py paints:
the same Xbox colours and Poppins Bold (SIL OFL, fonts.google.com/specimen/Poppins).

    draw_osl.py interface atlas.png out.png --dc-pack Interface_US.ftx --dc-patch xbox-prompts.dcpx
    draw_osl.py onmemory000|onmemory010|onmemory020 atlas.png out.png --font Poppins-Bold.ttf

Each run writes the edited atlas, a mask of the changed pixels (out.changed.npy) and prints what it did.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont
from scipy import ndimage

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ftex

XBOX = {'A': (98, 192, 78), 'B': (228, 68, 60), 'X': (70, 142, 236), 'Y': (242, 200, 56)}
DC_TARGET = 'stage/Interface_US.ftx'
DC_TEXTURE = 'Interface_arranged000.gnf'

# Odin part (x0, y0, x1, y1) in the 512 x 256 button atlas <- the same part in Dragon's Crown's 1024 x 512 atlas.
INTERFACE_PARTS = [
    ((337, 2, 381, 46), (480, 132, 568, 220), 'square disc -> X'),
    ((385, 2, 429, 46), (612, 344, 700, 432), 'pink square disc -> X'),
    ((281, 56, 325, 100), (452, 4, 540, 92), 'circle disc -> B'),
    ((329, 56, 373, 100), (576, 132, 664, 220), 'red circle disc -> B'),
    ((325, 120, 369, 164), (516, 344, 604, 432), 'cross disc -> A'),
    ((373, 120, 417, 164), (644, 4, 732, 92), 'blue cross disc -> A'),
    ((227, 172, 271, 216), (108, 304, 196, 392), 'triangle disc -> Y'),
    ((275, 172, 319, 216), (548, 4, 636, 92), 'green triangle disc -> Y'),
    ((325, 168, 369, 212), (672, 132, 760, 220), 'PS disc -> guide'),
    ((99, 121, 169, 164), (4, 210, 144, 296), 'PS oval -> guide'),
    ((65, 111, 91, 137), (886, 6, 938, 58), 'small circle -> B'),
    ((373, 212, 397, 236), (812, 60, 860, 108), 'small circle -> B'),
    ((330, 221, 352, 244), (950, 6, 994, 53), 'small cross -> A'),
    ((476, 2, 502, 31), (858, 133, 910, 187), 'small cross -> A'),
    ((413, 58, 445, 84), (740, 63, 804, 116), 'small triangle -> Y'),
    ((421, 164, 449, 190), (636, 441, 692, 492), 'small triangle -> Y'),
    ((413, 88, 433, 108), (920, 132, 960, 172), 'small square -> X'),
    ((297, 144, 321, 168), (780, 412, 828, 460), 'small square -> X'),
    ((226, 145, 292, 167), (740, 6, 874, 50), 'START -> MENU'),
    ((2, 69, 91, 91), (203, 402, 382, 446), 'OPTIONS -> MENU'),
    ((252, 3, 333, 25), (204, 457, 367, 502), 'SELECT -> VIEW'),
    ((174, 221, 245, 243), (486, 254, 628, 298), 'SHARE -> VIEW'),
    ((224, 119, 322, 141), (204, 346, 400, 390), 'TOUCHPAD -> VIEW'),
    ((100, 168, 169, 210), (206, 252, 344, 336), 'START oval -> MENU'),
    ((103, 3, 189, 46), (5, 114, 176, 200), 'SELECT oval -> VIEW'),
    ((315, 30, 330, 52), (856, 196, 886, 240), 'digit 2 -> T'),
    ((65, 142, 78, 164), (638, 252, 664, 296), 'digit 1 -> B'),
    ((174, 172, 224, 216), (410, 345, 508, 432), 'L shoulder -> LB'),
    ((227, 56, 276, 100), (5, 409, 102, 496), 'R shoulder -> RB'),
    ((5, 3, 98, 51), (8, 6, 194, 102), 'touchpad -> VIEW'),
]

# OnMemory.ftx: (texture, box) -> what replaces it.
ONMEMORY_KEYS = {(2, 684, 58, 714): 'LB', (36, 620, 92, 650): 'LT', (438, 616, 494, 646): 'RB',
                 (374, 668, 430, 698): 'RT'}
ONMEMORY_DISCS = {(282, 674, 322, 714): 'Y', (654, 646, 694, 686): 'A', (726, 656, 766, 696): 'B',
                  (818, 646, 858, 686): 'X'}
ONMEMORY_TAGS = {'onmemory000': {(1904, 996, 1988, 1024): 'VIEW'}, 'onmemory010': {(1078, 368, 1154, 398): 'MENU'}}


# ------------------------------------------------------------------ Dragon's Crown parts, reduced 2:1

def dc_atlases(pack_path, patch_path):
    """The original and the Xbox Interface_arranged000 of Dragon's Crown Pro, from its pack and its DCPX patch."""
    pack = Path(pack_path).read_bytes()
    patch = Path(patch_path).read_bytes()
    assert patch[:8] == b'DCPX1\0\0\0', 'not a DCPX patch'
    count, pos = struct.unpack_from('<I', patch, 8)[0], 12
    xbox = None
    for _ in range(count):
        n = struct.unpack_from('<H', patch, pos)[0]; pos += 2
        name = patch[pos:pos + n].decode(); pos += n
        size, before, after, runs = struct.unpack_from('<IIII', patch, pos); pos += 16
        if name == DC_TARGET:
            assert len(pack) == size and zlib.crc32(pack) == before, f'{pack_path} is not the {DC_TARGET} the patch was made for'
            out = bytearray(pack)
            for r in range(runs):
                off = struct.unpack_from('<I', patch, pos + r * 20)[0]
                out[off:off + 16] = patch[pos + r * 20 + 4:pos + r * 20 + 20]
            assert zlib.crc32(out) == after
            xbox = bytes(out)
        pos += runs * 20
    assert xbox, f'{DC_TARGET} is not in the patch'
    decode = lambda blob: np.array(ftex.decode(blob, next(t for t in ftex.textures(blob) if t['name'] == DC_TEXTURE)))
    return decode(pack), decode(xbox)


def reduce2(a):
    """2:1 box filter in premultiplied alpha."""
    p = a.astype(np.float64)
    p[..., :3] *= p[..., 3:4] / 255
    h, w = p.shape[0] // 2, p.shape[1] // 2
    q = p[:2 * h, :2 * w].reshape(h, 2, w, 2, 4).mean((1, 3))
    al = q[..., 3:4]
    q[..., :3] = np.where(al > 0, q[..., :3] * 255 / np.maximum(al, 1e-9), 0)
    return np.clip(np.rint(q), 0, 255).astype(np.uint8)


def premultiplied(a):
    f = a.astype(np.float64)
    return np.concatenate([f[..., :3] * f[..., 3:4] / 255, f[..., 3:4]], axis=-1)


def paint_interface(a, dc_orig, dc_xbox):
    """Replaces every part of INTERFACE_PARTS; returns the changed-pixel mask and one report line per part."""
    labels, _ = ndimage.label(a[..., 3] > 8)
    dc_labels, _ = ndimage.label(dc_xbox[..., 3] > 8)
    changed = np.zeros(a.shape[:2], bool)
    report = []
    src = a.copy()
    for (ox0, oy0, ox1, oy1), (dx0, dy0, dx1, dy1), what in INTERFACE_PARTS:
        own = set(np.unique(labels[oy0:oy1, ox0:ox1])) - {0}
        dc_own = set(np.unique(dc_labels[dy0:dy1, dx0:dx1])) - {0}
        m = 2
        while m:
            x0, y0, x1, y1 = max(ox0 - m, 0), max(oy0 - m, 0), min(ox1 + m, a.shape[1]), min(oy1 + m, a.shape[0])
            if set(np.unique(labels[y0:y1, x0:x1])) - {0} <= own:
                break
            m -= 1
        if not m:
            x0, y0, x1, y1 = ox0, oy0, ox1, oy1
        w, h = x1 - x0, y1 - y0
        target = premultiplied(src[y0:y1, x0:x1])
        best = None
        for sy in range(-3, 4):
            for sx in range(-3, 4):
                X, Y = dx0 + 2 * (x0 - ox0) + sx, dy0 + 2 * (y0 - oy0) + sy
                if X < 0 or Y < 0 or X + 2 * w > dc_orig.shape[1] or Y + 2 * h > dc_orig.shape[0]:
                    continue
                e = np.sqrt(((premultiplied(reduce2(dc_orig[Y:Y + 2 * h, X:X + 2 * w])) - target) ** 2).mean())
                if best is None or e < best[0]:
                    best = (e, X, Y)
        e, X, Y = best
        window = dc_xbox[Y:Y + 2 * h, X:X + 2 * w].copy()
        others = dc_labels[Y:Y + 2 * h, X:X + 2 * w]
        window[(others > 0) & ~np.isin(others, list(dc_own))] = 0
        part = reduce2(window)
        mine = np.isin(dc_labels, list(dc_own))
        ring = int((dc_xbox[..., 3] * mine).sum() - (window[..., 3] * mine[Y:Y + 2 * h, X:X + 2 * w]).sum())
        a[y0:y1, x0:x1] = part
        changed[y0:y1, x0:x1] |= np.any(part != src[y0:y1, x0:x1], axis=-1)
        report.append(f'{what:24s} odin ({x0},{y0})-({x1},{y1}) <- dc ({X},{Y}) original rmse {e:5.2f}'
                      + (f'  [dc xbox alpha outside: {ring}]' if ring else ''))
    return changed, report


# ------------------------------------------------------------------ painted like Dragon's Crown Pro PC's draw_dcp.py

def rebuild_disc(a, box, frac=0.78):
    """Erases the inside of a glossy disc: every pixel closer than frac*R to the centre takes the colour found at frac*R
    along the same angle, so the rim and the top highlight survive. From draw_dcp.py."""
    x0, y0, x1, y1 = box
    sub = a[y0:y1, x0:x1]
    h, w = sub.shape[:2]
    cy, cx = (h - 1) / 2, (w - 1) / 2
    yy, xx = np.mgrid[0:h, 0:w]
    r = np.sqrt(((yy - cy) / (h / 2)) ** 2 + ((xx - cx) / (w / 2)) ** 2)
    ang = np.arctan2((yy - cy) / (h / 2), (xx - cx) / (w / 2))
    nb = 90
    bins = ((ang + np.pi) / (2 * np.pi) * nb).astype(int) % nb
    ann = (r >= frac) & (r < frac + 0.1)
    prof = np.zeros((nb, 4))
    for b in range(nb):
        sel = ann & (np.abs(((bins - b + nb // 2) % nb) - nb // 2) <= 1)
        prof[b] = np.median(sub[sel], axis=0)
    k = np.array([1, 2, 3, 2, 1], float); k /= k.sum()
    for c in range(4):
        prof[:, c] = np.convolve(np.r_[prof[-2:, c], prof[:, c], prof[:2, c]], k, 'valid')
    inner = r < frac
    sub[inner] = prof[bins[inner]].round().astype(np.uint8)


def text_layer(size, text, font_path, px, fill, scale=8, center=None):
    big = Image.new('RGBA', (size[0] * scale, size[1] * scale), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    f = ImageFont.truetype(font_path, px * scale)
    box = d.textbbox((0, 0), text, font=f, anchor='ls')
    cx, cy = center if center else (size[0] / 2, size[1] / 2)
    x = cx * scale - (box[2] - box[0]) / 2 - box[0]
    y = cy * scale - (box[3] - box[1]) / 2 - box[1]
    d.text((x, y), text, font=f, fill=fill, anchor='ls')
    return big.resize(size, Image.LANCZOS)


def cap_px(font_path, cap_height):
    """The font size whose capitals are cap_height pixels tall."""
    f = ImageFont.truetype(font_path, 1000)
    top, bottom = f.getbbox('H', anchor='ls')[1], f.getbbox('H', anchor='ls')[3]
    return cap_height * 1000 / (bottom - top)


def paste(a, box, layer):
    x0, y0, x1, y1 = box
    base = Image.fromarray(a[y0:y1, x0:x1])
    base.alpha_composite(layer)
    a[y0:y1, x0:x1] = np.array(base)


def erase_text(a, box, light):
    """Fills the rectangle around the light text of a dark key or tag, row by row, with the body colour beside it (the
    keys shade from top to bottom); returns the text's (x0, y0, x1, y1) inside the box and the body mask."""
    x0, y0, x1, y1 = box
    sub = a[y0:y1, x0:x1]
    lum = sub[..., :3].astype(int).max(-1)
    body = ndimage.binary_erosion(sub[..., 3] > 200, iterations=2)
    ys, xs = np.nonzero(body & (lum > light))
    extent = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
    cols = np.zeros(body.shape[1], bool)
    cols[max(extent[0] - 2, 0):extent[2] + 2] = True
    dark = body & (lum <= light)
    opaque = sub[..., 3] > 200
    rows = range(max(extent[1] - 2, 0), min(extent[3] + 2, body.shape[0]))
    clear = opaque & (lum <= light)
    clear[rows.start:rows.stop] = False
    for y in rows:
        beside = dark[y] & ~cols
        fill = np.median(sub[y, beside] if beside.sum() >= 2 else sub[clear], axis=0)
        sub[y, opaque[y] & cols] = fill.round().astype(np.uint8)
    return extent, body


def paint_label(a, box, word, font, light, fill):
    (tx0, ty0, tx1, ty1), body = erase_text(a, box, light)
    size = (box[2] - box[0], box[3] - box[1])
    cols = np.nonzero(body.any(0))[0]
    room = (cols.max() - cols.min() + 1) * 0.86
    px = cap_px(font, ty1 - ty0)
    width = ImageFont.truetype(font, 1000).getlength(word) * px / 1000
    px *= min(1.0, room / width)
    paste(a, box, text_layer(size, word, font, px, fill, center=((cols.min() + cols.max() + 1) / 2, (ty0 + ty1) / 2)))


def symbol_radius(a, box):
    """How far from the centre, as a fraction of the radius, a disc's coloured symbol reaches."""
    x0, y0, x1, y1 = box
    sub = a[y0:y1, x0:x1].astype(int)
    h, w = sub.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    r = np.sqrt(((yy - (h - 1) / 2) / (h / 2)) ** 2 + ((xx - (w - 1) / 2) / (w / 2)) ** 2)
    rgb = sub[..., :3]
    symbol = (rgb.max(-1) - rgb.min(-1) > 60) & (sub[..., 3] > 128) & (r < 0.9)
    return r[symbol].max()


def paint_disc(a, box, letter, font):
    rebuild_disc(a, box, min(symbol_radius(a, box) + 0.08, 0.86))
    size = (box[2] - box[0], box[3] - box[1])
    px = cap_px(font, size[1] * 0.5)
    shadow = text_layer(size, letter, font, px, (0, 0, 0, 200)).filter(ImageFilter.GaussianBlur(0.8))
    paste(a, box, shadow)
    paste(a, box, text_layer(size, letter, font, px, XBOX[letter] + (255,)))


def paint_onmemory(a, which, font):
    src = a.copy()
    if which == 'onmemory020':
        for box, word in ONMEMORY_KEYS.items():
            paint_label(a, box, word, font, 85, (240, 240, 240, 255))
        for box, letter in ONMEMORY_DISCS.items():
            paint_disc(a, box, letter, font)
    for box, word in ONMEMORY_TAGS.get(which, {}).items():
        paint_label(a, box, word, font, 90, (255, 255, 255, 255))
    return np.any(a != src, axis=-1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('which', choices=['interface', 'onmemory000', 'onmemory010', 'onmemory020'])
    ap.add_argument('atlas'); ap.add_argument('out')
    ap.add_argument('--dc-pack', help="Dragon's Crown Pro 1.03 stage/Interface_US.ftx, unpatched")
    ap.add_argument('--dc-patch', help="Dragon's Crown Pro PC launcher/prompts/xbox-prompts.dcpx")
    ap.add_argument('--font', help='Poppins-Bold.ttf')
    args = ap.parse_args()
    a = np.array(Image.open(args.atlas).convert('RGBA'))
    if args.which == 'interface':
        if not (args.dc_pack and args.dc_patch):
            ap.error('interface needs --dc-pack and --dc-patch')
        assert a.shape[:2] == (256, 512), 'not the 512 x 256 button atlas'
        changed, report = paint_interface(a, *dc_atlases(args.dc_pack, args.dc_patch))
        print('\n'.join(report))
    else:
        if not args.font or not Path(args.font).is_file():
            ap.error('the OnMemory atlases need --font Poppins-Bold.ttf')
        changed = paint_onmemory(a, args.which, args.font)
    Image.fromarray(a).save(args.out)
    np.save(Path(args.out).with_suffix('.changed.npy'), changed)
    print(f'{args.which}: {changed.sum()} pixels changed -> {args.out}')


if __name__ == '__main__':
    main()
