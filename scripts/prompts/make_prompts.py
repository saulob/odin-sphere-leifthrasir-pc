#!/usr/bin/env python3
"""Builds launcher/prompts/xbox-prompts.dcpx from the prepared game folders: every distinct copy of the button art
(Interface.ftx, GUI/HD_Interface.ftx, OnMemory.ftx, OnMemory/HD_OnMemory.ftx) in every archive of every build found
is painted (draw_osl.py), re-encoded where it changed (patch_ftx.py) and becomes one DCPX target, named as the archive
names it and keyed by its CRC32, so one patch serves every supported build and language.

    make_prompts.py game/prepared out.dcpx --dc-pack Interface_US.ftx --dc-patch xbox-prompts.dcpx --font Poppins-Bold.ttf
"""
import argparse
import sys
import zlib
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cpk
import draw_osl
import ftex
import make_dcpx
import patch_ftx

INTERFACE = ('Interface.ftx', 'GUI/HD_Interface.ftx')
ONMEMORY = ('OnMemory.ftx', 'OnMemory/HD_OnMemory.ftx')
ONMEMORY_TEXTURES = {'000': ('onmemory000', 2048, 1024), '010': ('onmemory010', 2048, 1024),
                     '020': ('onmemory020', 1024, 1024)}


def button_atlas(blob):
    """The 512 x 256 texture with transparency: the button parts (the other 512 x 256 one is opaque parchment)."""
    found = [t for t in ftex.textures(blob) if (t['width'], t['height']) == (512, 256)
             and np.array(ftex.decode(blob, t))[..., 3].mean() < 200]
    assert len(found) == 1, f'{len(found)} button atlases'
    return found[0]


def paint_interface(blob, dc):
    tex = button_atlas(blob)
    a = np.array(ftex.decode(blob, tex))
    changed, report = draw_osl.paint_interface(a, *dc)
    worst = max(float(line.split('rmse')[1].split()[0]) for line in report)
    assert worst < 15, f'the button atlas does not match the expected layout (worst part rmse {worst:.1f})'
    out, blocks = patch_ftx.patch(blob, tex, a, changed)
    return out, f"{tex['name']}: {blocks} blocks, worst part rmse {worst:.1f}"


def paint_onmemory(blob, font, reference):
    notes = []
    for tex in ftex.textures(blob):
        suffix = tex['name'].replace('.gnf', '')[-3:]
        if suffix not in ONMEMORY_TEXTURES:
            continue
        which, w, h = ONMEMORY_TEXTURES[suffix]
        assert (tex['width'], tex['height']) == (w, h), f"{tex['name']}: unexpected size"
        a = np.array(ftex.decode(blob, tex))
        before = a.copy()
        changed = draw_osl.paint_onmemory(a, which, font)
        if which in reference:
            ref = reference[which]
            diff = np.sqrt(((before[changed].astype(float) - ref[changed].astype(float)) ** 2).mean())
            assert diff < 12, f"{tex['name']}: the glyphs are not where expected (rmse {diff:.1f})"
        else:
            reference[which] = before
        blob, blocks = patch_ftx.patch(blob, tex, a, changed)
        notes.append(f"{tex['name']}: {blocks} blocks")
    assert len(notes) == len(ONMEMORY_TEXTURES), 'OnMemory textures missing'
    return blob, ', '.join(notes)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('prepared', help='game/prepared, holding one folder per build')
    ap.add_argument('out')
    ap.add_argument('--dc-pack', required=True, help="Dragon's Crown Pro 1.03 stage/Interface_US.ftx, unpatched")
    ap.add_argument('--dc-patch', required=True, help="Dragon's Crown Pro PC launcher/prompts/xbox-prompts.dcpx")
    ap.add_argument('--font', required=True, help='Poppins-Bold.ttf')
    args = ap.parse_args()
    dc = draw_osl.dc_atlases(args.dc_pack, args.dc_patch)
    painted = {}      # crc of the original file -> patched bytes
    targets = {}      # (archive path, crc) -> (original, patched)
    reference = {}
    for build in sorted(p for p in Path(args.prepared).iterdir() if p.is_dir()):
        for archive in sorted(build.glob('*.CPK')):
            pack = cpk.Cpk(archive)
            for entry in pack.entries:
                name = entry['path']
                if name not in INTERFACE + ONMEMORY:
                    continue
                data = pack.read(entry)
                crc = zlib.crc32(data)
                if crc not in painted:
                    if name in INTERFACE:
                        painted[crc], note = paint_interface(data, dc)
                    else:
                        painted[crc], note = paint_onmemory(data, args.font, reference)
                    print(f'{build.name}/{archive.name}: {name} {crc:08x} -> {note}')
                targets[(name, crc)] = (data, painted[crc])
    blob = make_dcpx.dcpx_bytes([(name, orig, new) for (name, _), (orig, new) in sorted(targets.items())])
    Path(args.out).write_bytes(blob)
    print(f'{len(targets)} targets -> {args.out} ({len(blob)} bytes)')


if __name__ == '__main__':
    main()
