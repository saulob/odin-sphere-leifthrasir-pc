#!/usr/bin/env python3
"""Tests of the Portuguese (Brazil) credit captions of the title atlas.

With TITLE_EXIT_GAME=game/prepared the real Europe title atlas and the Spanish credit line are used.
"""
import os
import sys
import unittest
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'localization'))
sys.path.insert(0, str(HERE.parent / 'prompts'))

import cpk  # noqa: E402
import ftex  # noqa: E402
import mbs as M  # noqa: E402
import mbs_render  # noqa: E402
import title_credits as C  # noqa: E402
import visuals  # noqa: E402

GAME = os.environ.get('TITLE_EXIT_GAME')
ORIGIN, SIZE = (-494.84, -323.0), (670, 635)
NAME_BOXES = {'george': (466, 358, 550, 376), 'kentaro': (468, 401, 551, 418)}
OLD_CAPTION_BOXES = {'presented': (458, 204, 551, 226), 'producer': (486, 343, 530, 357),
                     'director': (490, 386, 528, 401)}
NEW_CAPTION_BOXES = {'presented': (456, 204, 554, 228), 'producer': (486, 343, 530, 357),
                     'director': (490, 386, 528, 401)}


def stroke_stats(cell, floor=32):
    alpha = cell[..., 3].astype(np.float64)
    ink = alpha >= floor
    rows = np.nonzero(ink.any(axis=1))[0]
    columns = np.nonzero(ink.any(axis=0))[0]
    height = int(rows.max() - rows.min() + 1)
    return dict(height=height, wide=int(columns.max() - columns.min() + 1),
                width=float(alpha[ink].sum() / 255.0 / height),
                soft=float(((alpha > 0) & (alpha < 128)).sum() / max((alpha > 0).sum(), 1)),
                rgb={tuple(int(v) for v in c) for c in cell[ink][:, :3]})


class Helpers(unittest.TestCase):
    def test_gaps_and_bands(self):
        profile = [True, True, False, False, True, False, True, True]
        self.assertEqual(C._gaps(profile), [(2, 4), (5, 6)])
        image = np.zeros((8, 3, 4), np.uint8)
        image[1:3, :, 3] = 9
        image[5:7, :, 3] = 9
        self.assertEqual(C._bands(image), [(1, 3), (5, 7)])

    def test_crop(self):
        image = np.zeros((6, 6, 4), np.uint8)
        image[2:5, 1:4, 3] = 7
        self.assertEqual(C._crop(image).shape, (3, 3, 4))

    def test_blocks_cover_whole_cells(self):
        written = np.zeros((9, 9), bool)
        written[5, 6] = True
        blocks = C._blocks(written)
        self.assertEqual(int(blocks.sum()), 16)
        self.assertTrue(blocks[4:8, 4:8].all())
        self.assertFalse(blocks[0:4, 0:4].any())

    def test_quad_match_is_exact_enough(self):
        self.assertTrue(C._same_quad(C.QUADS[0], C.QUADS[0]))
        self.assertFalse(C._same_quad([(x + 0.5, y) for x, y in C.QUADS[0]], C.QUADS[0]))

    def test_capital_a_is_one_face_weight(self):
        a = stroke_stats(C.capital_a())
        self.assertGreaterEqual(a['height'], 19)
        self.assertLessEqual(a['height'], 23)
        self.assertIn((134, 114, 79), a['rgb'])
        self.assertGreater(a['width'], 2.0)
        self.assertLess(a['width'], 6.0)
        self.assertGreater(a['wide'], 12)
        self.assertLessEqual(a['wide'], 18)
        self.assertGreater(a['soft'], 0.2)

    def test_fit_shrinks_only_when_needed(self):
        line = np.zeros((30, 200, 4), np.uint8)
        line[:, 10:190, 3] = 200
        self.assertIs(C._fit(line, 500), line)
        self.assertLess(C._fit(line, 60).shape[1], line.shape[1])


@unittest.skipUnless(GAME, 'set TITLE_EXIT_GAME to the game/prepared folder')
class RealTitle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blob = cls.read('CUSA05262-1.01/Odin2_EU_PATCH1_RE_UK.CPK', 'GUI/HD_title.ftx')
        cls.uk = cls.read('CUSA05262-1.01/Odin2_EU_PATCH1_RE_UK.CPK', 'GUI/REHD_title.mbs')
        cls.es = cls.read('CUSA05262-1.01/Odin2_EU_PATCH1_RE_ES.CPK', 'GUI/REHD_title.mbs')
        cls.textures = [visuals.decode(cls.blob, t) for t in ftex.textures(cls.blob)]
        cls.patched, cls.written, cls.stats = C.paint(cls.textures, {'refined': cls.uk},
                                                      {'refined': {'ES': cls.es}})
        cls.layout = M.Mbs(cls.uk)
        cls.before = mbs_render.draw(cls.layout, {0: cls.textures[0]}, layers=[171], scale=1.0,
                                     origin=ORIGIN, size=SIZE)[0]
        cls.after = mbs_render.draw(cls.layout, {0: cls.patched}, layers=[171], scale=1.0,
                                    origin=ORIGIN, size=SIZE)[0]

    @classmethod
    def read(cls, pack, entry):
        reader = cpk.Cpk(str(Path(GAME) / pack), original=True)
        try:
            return reader.read(reader.find(entry))
        finally:
            reader.f.close()

    def test_personal_names_are_pixel_identical(self):
        for name, (x0, y0, x1, y1) in NAME_BOXES.items():
            self.assertEqual(int(np.any(self.before[y0:y1, x0:x1] != self.after[y0:y1, x0:x1], axis=2).sum()),
                             0, name)

    def test_name_rows_are_never_written(self):
        keys = C.credit_keys(self.layout)
        for i in (2, 3):
            x0, y0, x1, y1 = self.layout.part_rect(keys[i]['part'])
            for index in (1, 3):
                lo, hi = C._bands(self.textures[0][y0:y1, x0:x1])[index]
                self.assertFalse(self.written[y0 + lo:y0 + hi, x0:x1].any())

    def test_english_captions_are_replaced(self):
        for name, (x0, y0, x1, y1) in OLD_CAPTION_BOXES.items():
            old = self.before[y0:y1, x0:x1, 3] > 0
            changed = np.any(self.before[y0:y1, x0:x1] != self.after[y0:y1, x0:x1], axis=2)
            self.assertTrue(old.any(), name)
            self.assertGreater(changed[old].mean(), 0.6, name)

    def test_old_caption_rows_are_all_rewritten(self):
        keys = C.credit_keys(self.layout)
        for i in (2, 3):
            x0, y0, x1, y1 = self.layout.part_rect(keys[i]['part'])
            for index in (0, 2):
                lo, hi = C._bands(self.textures[0][y0:y1, x0:x1])[index]
                self.assertTrue(self.written[y0 + lo:y0 + hi, x0:x1].all())

    def test_portuguese_captions_are_drawn(self):
        for name, (x0, y0, x1, y1) in NEW_CAPTION_BOXES.items():
            sub = self.after[y0:y1, x0:x1, 3]
            self.assertGreater(int((sub > 0).sum()), 100, name)
            self.assertGreaterEqual(int(sub.max()), 180, name)
        ys, xs = np.nonzero(self.after[204:228, 456:554, 3])
        self.assertGreater(xs.max() - xs.min(), 80, 'presented')

    def test_captions_do_not_touch_the_names(self):
        def overlap(a, b):
            return not (a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1])
        for name, box in NEW_CAPTION_BOXES.items():
            for other, name_box in NAME_BOXES.items():
                self.assertFalse(overlap(box, name_box), f'{name} over {other}')

    def test_outside_the_caption_texels_nothing_moves(self):
        blocks = C._blocks(self.written)
        changed = np.any(self.patched != self.textures[0], axis=2)
        self.assertFalse(changed[~blocks].any())

    def test_other_textures_are_untouched(self):
        out, _ = C.apply(self.blob, {'refined': self.uk}, {'refined': {'ES': self.es}})
        old = [visuals.decode(self.blob, t) for t in ftex.textures(self.blob)]
        new = [visuals.decode(out, t) for t in ftex.textures(out)]
        for i in range(1, len(old)):
            self.assertTrue(np.array_equal(old[i], new[i]), f'texture {i}')

    def test_generation_is_deterministic(self):
        first, _ = C.apply(self.blob, {'refined': self.uk}, {'refined': {'ES': self.es}})
        second, _ = C.apply(self.blob, {'refined': self.uk}, {'refined': {'ES': self.es}})
        self.assertEqual(first, second)

    def test_capital_a_matches_the_face(self):
        es = M.Mbs(self.es)
        keys = C.credit_keys(es)
        line = C._strip(self.textures, es, keys[0], keys[1])
        a = stroke_stats(C.capital_a())
        p = stroke_stats(line[6:28, 3:17])
        self.assertLessEqual(abs(a['height'] - p['height']), 4)
        self.assertLess(abs(a['width'] - p['width']) / p['width'], 0.45)
        self.assertLess(abs(a['soft'] - p['soft']), 0.25)


if __name__ == '__main__':
    unittest.main()
