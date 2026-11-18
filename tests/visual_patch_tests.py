#!/usr/bin/env python3
"""Checks of the Portuguese (Brazil) visual tooling: the inventory's texture reading and categories
(scripts/localization/visuals.py) and the committed DCPX patches of localization/pt-BR/visual. No game files needed.

    python -I tests/visual_patch_tests.py
"""
import re
import struct
import sys
import unittest
import zlib
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts' / 'localization'))
sys.path.insert(0, str(ROOT / 'scripts' / 'prompts'))
import bc3
import cpk
import ftex
import make_dcpx
import visuals

VISUAL = ROOT / 'localization' / 'pt-BR' / 'visual'
TITLE_NAMES = {'gui/hd_title.ftx', 'title.ftx', 'gui/rehd_title.mbs', 'title.mbs'}


def pack_of(images):
    """A one-section FTEX-like blob holding BC3 textures (tiled as the game stores them) and their descriptors."""
    blob, textures = bytearray(), []
    for name, image in images:
        h, w = image.shape[:2]
        blocks = image.reshape(h // 4, 4, w // 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4)
        encoded = bc3.encode_blocks(blocks)
        data = bytearray(h * w)
        for index in range(len(blocks)):
            by, bx = divmod(index, w // 4)
            offset = ftex.tiled_offset(bx, by, w // 4)
            data[offset:offset + 16] = encoded[index * 16:(index + 1) * 16]
        textures.append(dict(name=name, data=len(blob), size=len(data), width=w, height=h, dfmt=37, tiling=13, pitch=w))
        blob += data
    return bytes(blob), textures


def image(seed, w=64, h=32):
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w, 4), np.uint8)
    out[..., :3] = 255
    out[4:28, 8:56, 3] = 255
    out[8:20, 12:40, :3] = rng.integers(0, 255, (12, 28, 3))
    return out


class Reading(unittest.TestCase):
    def test_vectorised_tiling_matches_the_reference(self):
        for bw, bh, pitch in ((16, 8, 16), (32, 16, 64), (8, 24, 8)):
            offsets = visuals.tiled_offsets(bw, bh, pitch)
            expected = [ftex.tiled_offset(x, y, pitch) for y in range(bh) for x in range(bw)]
            self.assertEqual(offsets.tolist(), expected)

    def test_decode_matches_the_reference_reader(self):
        blob, (tex,) = pack_of([('a', image(1))])
        self.assertTrue((visuals.decode(blob, tex) == np.array(ftex.decode(blob, tex))).all())

    def test_identical_textures_have_no_change(self):
        blob, (tex,) = pack_of([('a', image(2))])
        self.assertIsNone(visuals.texture_change(blob, tex, blob, tex))

    def test_visible_change_is_measured_and_boxed(self):
        before = image(3)
        after = before.copy()
        after[10:14, 44:52, 3] = 0
        a, (ta,) = pack_of([('a', after)])
        b, (tb,) = pack_of([('a', before)])
        change = visuals.texture_change(a, ta, b, tb)
        self.assertGreater(change['visible'], 0)
        x0, y0, x1, y1 = change['box']
        self.assertTrue(x0 >= 40 and y0 >= 8 and x1 <= 56 and y1 <= 16, change['box'])

    def test_invisible_change_is_not_textual(self):
        before = image(4)
        after = before.copy()
        after[0:4, 0:4, :3] = 0              # colour under zero alpha
        a, (ta,) = pack_of([('a', after)])
        b, (tb,) = pack_of([('a', before)])
        change = visuals.texture_change(a, ta, b, tb)
        self.assertEqual(change['visible'], 0)
        self.assertGreater(change['blocks'], 0)

    def test_native_decompressor_matches_the_reference(self):
        fast = visuals.native_crilayla()
        if fast is None:
            self.skipTest('no C compiler')
        for payload in (bytes(range(256)) * 3 + b'odin' * 90, b'\0' * 0x100 + bytes(range(7)) * 50):
            probe = cpk._crilayla_literals(payload)
            self.assertEqual(fast(probe), cpk.crilayla_decompress(probe))


def record(**kw):
    base = dict(same_as_english=False, crc32='00000001', pixels_crc32='p00000001', archive='Odin2_RE_DE.CPK', english={'crc32': '2'},
                visible_pixels=10, layout_differs=False, layout=None, differing=[dict(visible=10)])
    base.update(kw)
    return base


class Categories(unittest.TestCase):
    def test_sibling_layouts(self):
        names = {n.lower(): dict(path=n) for n in ('GUI/REHD_Interface.mbs', 'GUI/REHD_StageName.mbs', 'Interface.mbs', '01.mbs')}
        self.assertEqual(visuals.sibling_mbs('GUI/HD_Interface.ftx', names)['path'], 'GUI/REHD_Interface.mbs')
        self.assertEqual(visuals.sibling_mbs('GUI/HD_StageName04_2.ftx', names)['path'], 'GUI/REHD_StageName.mbs')
        self.assertEqual(visuals.sibling_mbs('Interface.ftx', names)['path'], 'Interface.mbs')
        self.assertEqual(visuals.sibling_mbs('01.ftx', names)['path'], '01.mbs')
        self.assertIsNone(visuals.sibling_mbs('Other/HD_M01.ftx', names))

    def test_rules_in_order(self):
        c = visuals.classify
        self.assertEqual(c('GUI/HD_title.ftx', record(same_as_english=True), {}, {})[0], 'B')
        self.assertEqual(c('title.ftx', record(), {'00000001': 'x'}, {})[0], 'F')
        self.assertEqual(c('40_1.ftx', record(), {'p00000001': 'x'}, {})[0], 'F')
        self.assertEqual(c('GUI/HD_title.ftx', record(archive='Odin2_EU_PATCH1_RE_DE.CPK'), {}, {'gui/hd_title.ftx': 'y'})[0], 'F')
        self.assertEqual(c('GUI/HD_Bookshelf.ftx', record(visible_pixels=0), {}, {})[0], 'G')
        self.assertEqual(c('GUI/HD_Interface.ftx', record(visible_pixels=0, layout_differs=True, layout='m'), {}, {})[0], 'A')
        self.assertEqual(c('GUI/HD_title.ftx', record(visible_pixels=0, layout_differs=True, layout='m'), {}, {})[0], 'D')
        self.assertEqual(c('Other/HD_M07.ftx', record(), {}, {})[0], 'C')
        self.assertEqual(c('M07.ftx', record(), {}, {})[0], 'C')
        self.assertEqual(c('GUI/HD_fin2.ftx', record(), {}, {})[0], 'E')
        self.assertEqual(c('GUI/HD_StageName04.ftx', record(), {}, {})[0], 'A')
        self.assertEqual(set(visuals.CATEGORIES), set('ABCDEFG'))


class CommittedPatches(unittest.TestCase):
    def patches(self):
        return sorted(VISUAL.glob('*.dcpx'))

    def test_every_patch_is_a_dcpx2_delta(self):
        self.assertTrue(self.patches(), 'no visual patch')
        for path in self.patches():
            blob = path.read_bytes()
            self.assertEqual(blob[:8], b'DCPX2\0\0\0', path.name)
            targets = make_dcpx.parse(blob)
            self.assertTrue(targets, path.name)
            for name, size, before, after, runs in targets:
                self.assertNotEqual(before, after, name)
                self.assertTrue(runs, name)
                self.assertTrue(all(old is not None and old != new for _, old, new in runs), name)
                self.assertLess(len(runs) * 16, size // 4, f'{path.name} {name}: not a delta')
                offsets = [off for off, _, _ in runs]
                self.assertEqual(offsets, sorted(set(offsets)), name)
                self.assertTrue(all(off % 16 == 0 and off + 16 <= size for off in offsets), name)

    def test_title_patch_targets_the_title_files_only(self):
        targets = make_dcpx.parse((VISUAL / 'title-exit-ptbr.dcpx').read_bytes())
        self.assertEqual({t[0].lower() for t in targets}, TITLE_NAMES)
        atlases = {(t[1], t[2], t[3]) for t in targets if t[0].lower().endswith('.ftx')}
        self.assertEqual(len(atlases) * 2, len([t for t in targets if t[0].lower().endswith('.ftx')]),
                         'Refined and Classic atlases are the same file and get the same delta')

    def test_patch_sources_are_the_english_layer(self):
        layer = {(n.lower(), s, a) for n, s, b, a, _ in make_dcpx.parse((ROOT / 'launcher' / 'title' / 'title-exit.dcpx').read_bytes())}
        for name, size, before, after, runs in make_dcpx.parse((VISUAL / 'title-exit-ptbr.dcpx').read_bytes()):
            self.assertIn((name.lower(), size, before), layer, f'{name} {before:08x} is not a file the English layer writes')

    def test_inventory_summary_is_consistent(self):
        text = (VISUAL / 'INVENTORY.md').read_text(encoding='utf-8')
        rows = re.findall(r'^\| ([^|*]+?) \| (\d+) \| (\d+) \| (\d+) \|$', text, re.M)
        total = re.search(r'^\| \*\*Total\*\* \| \*\*(\d+)\*\* \| \*\*(\d+)\*\* \| \*\*(\d+)\*\* \|$', text, re.M)
        self.assertTrue(rows and total)
        for column in range(3):
            self.assertEqual(sum(int(r[1 + column]) for r in rows), int(total.group(1 + column)))

if __name__ == '__main__':
    unittest.main()
