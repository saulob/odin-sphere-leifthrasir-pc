#!/usr/bin/env python3
"""Checks of make_title_exit.py on synthetic MBS files and atlases: label discovery, native glyph extraction and
composition, one-part, several-part and rotated labels, widening and BC3 edge preservation. No game files needed;
with TITLE_EXIT_GAME=game/prepared the real local variants are run as well (a few minutes).

    python -I scripts/title_exit/test_title_exit.py
"""
import os
import re
import struct
import sys
import unittest
from unittest import mock
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'prompts'))
import bc3
import ftex
import make_title_exit as m
import patch_ftx

LABEL_RECT = (96, 40, 268, 78)  # 172 x 38


def quad(rect):
    x0, y0, x1, y1 = rect
    return [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]


def synthetic_mbs(label_parts=(4,), shared=False, rects=None, geom_scale=1.0, kind='refined'):
    """An MBS with the title layers of `kind`: five groups of three layers sharing a ribbon part, one label each, the
    third group being INSTALL. Parts: 0 unused, 1 ribbon, 2.. labels."""
    first = min(m.KINDS[kind]['layers'])
    layers = first + 16
    parts = [(0, 0, 8, 8), (300, 0, 340, 20)] + [(10 + 200 * i, 100, 190 + 200 * i, 138) for i in range(6)]
    parts[4] = LABEL_RECT
    for part, rect in (rects or {}).items():
        parts[part] = rect
    out = bytearray(0x120)
    out[:4] = b'FMBS'
    part_at = len(out)
    for x0, y0, x1, y1 in parts:
        out += struct.pack('<12f', (x0 + x1) / 2, (y0 + y1) / 2, *[c for corner in quad((x0, y0, x1, y1)) for c in corner], x0, y0)
    geom_at = len(out)
    geoms = [(0.0, 0.0, 8.0, 8.0)] * 4
    for part in label_parts:
        x0, y0, x1, y1 = parts[part]
        geoms.append(((x0 - (x0 + x1) / 2) * geom_scale, -22.0, (x1 - (x0 + x1) / 2) * geom_scale, 16.0))
    for g in geoms:
        out += struct.pack('<12f', 0.0, -3.0, g[0], g[1], g[0], g[3], g[2], g[3], g[2], g[1], g[0], g[1])
    layer_rows, keys = [], []
    for layer in range(layers):
        group = (layer - first) // 3 if first <= layer < first + 15 else None
        row = []
        if group is not None:
            row.append((1, 0))
            labels = list(label_parts) if group == 2 else [2 + group if group < 2 else 3 + group]
            for n, p in enumerate(labels):
                row.append((p, 4 + label_parts.index(p) if group == 2 else 1))
        if layer == 0 and shared:
            row.append((label_parts[0], 4))
        layer_rows.append((len(keys), row))
        keys.extend(row)
    layer_at = len(out)
    for first_key, row in layer_rows:
        out += struct.pack('<4f', -381.0, -38.0, 396.0, 32.0) + struct.pack('<6H', first_key, 0, 0, len(row), 6656, 0)
    s7_at = len(out)
    out += bytes(36)
    key_at = len(out)
    for part, geom in keys:
        out += struct.pack('<I6H', 0, 0, 0, 0, part, 0, geom)
    label_at = len(out)
    out += bytes(48)
    for i, offset in enumerate((0, part_at, geom_at, 0, key_at, 0, layer_at, s7_at, 0, label_at, label_at)):
        struct.pack_into('<Q', out, 0xb0 + 8 * i, offset)
    return bytes(out)


def glyph(width, ink=(3, 35)):
    piece = np.zeros((38, width, 4), np.uint8)
    piece[..., :3] = 255
    piece[ink[0]:ink[1], 2:width - 2, 3] = 255
    return piece


LETTERS = {'E': glyph(23), 'X': glyph(26), 'I': glyph(9), 'T': glyph(26), 'G': glyph(28), 'A': glyph(30), 'M': glyph(32)}


class FakeArt:
    def __init__(self, atlases, mbs, textures):
        self.atlases, self.mbs_, self.textures_ = atlases, mbs, textures

    def texture(self, index):
        return self.atlases[index]

    def crop(self, kind, part):
        x0, y0, x1, y1 = self.mbs_.part_rect(part)
        return self.atlases[self.textures_[part]][y0:y1, x0:x1].copy()


def atlas_with(rects, width=1024, height=256, extra=()):
    atlas = np.zeros((height, width, 4), np.uint8)
    atlas[..., :3] = 255
    for x0, y0, x1, y1 in rects:
        atlas[y0 + 4:y1 - 4, x0 + 6:x1 - 6, 3] = 255
    for x0, y0, x1, y1 in extra:
        atlas[y0:y1, x0:x1, 3] = 255
    return atlas


class LabelDiscovery(unittest.TestCase):
    def test_one_part_found(self):
        label = m.find_label(m.Mbs(synthetic_mbs()), 'refined')
        self.assertEqual([(p.part, p.rect) for p in label.parts], [(4, LABEL_RECT)])
        self.assertEqual(label.parts[0].geoms, [4])

    def test_classic_layers(self):
        label = m.find_label(m.Mbs(synthetic_mbs(kind='classic')), 'classic')
        self.assertEqual([p.part for p in label.parts], [4])

    def test_several_parts_found_in_drawing_order(self):
        data = synthetic_mbs(label_parts=(5, 4), rects={4: (500, 40, 696, 84), 5: LABEL_RECT})
        label = m.find_label(m.Mbs(data), 'refined')
        self.assertEqual([p.part for p in label.parts], [5, 4])
        self.assertEqual(len(label.owners), 6)

    def test_shared_part_rejected(self):
        with self.assertRaises(m.Unsupported):
            m.find_label(m.Mbs(synthetic_mbs(shared=True)), 'refined')

    def test_parts_sharing_a_rectangle_rejected(self):
        with self.assertRaises(m.Unsupported):
            m.find_label(m.Mbs(synthetic_mbs(label_parts=(4, 5), rects={5: LABEL_RECT})), 'refined')

    def test_short_file_rejected(self):
        with self.assertRaises(Exception):
            m.find_label(m.Mbs(b'FMBS' + bytes(0x200)), 'refined')

    def test_rotation_and_anisotropy(self):
        upright = m.find_label(m.Mbs(synthetic_mbs()), 'refined').parts[0]
        self.assertTrue(m.is_upright(m.orientation(upright)[0]))
        stretched = m.find_label(m.Mbs(synthetic_mbs(geom_scale=0.5)), 'refined').parts[0]
        self.assertTrue(m.is_upright(m.orientation(stretched)[0]))
        turned = m.Part(4, 0, [4], quad((96, 40, 134, 246)), [(-86.0, -22.0), (86.0, -22.0), (86.0, 16.0), (-86.0, 16.0)])
        self.assertFalse(m.is_upright(m.orientation(turned)[0]))


class NativeGlyphs(unittest.TestCase):
    def sprite(self):
        sprite = np.zeros((38, 120, 4), np.uint8)
        sprite[..., :3] = 255
        sprite[3:36, 4:30, 3] = 255                 # a letter
        sprite[3:36, 34:60, 3] = 255                # a letter that touches the next one
        sprite[3:36, 60:64, 3] = 80
        sprite[3:36, 64:90, 3] = 255
        return sprite

    def test_components_are_isolated_and_full_height(self):
        pieces = m.components(self.sprite())
        self.assertEqual(len(pieces), 2)
        for x0, x1, y0, y1, piece in pieces:
            self.assertEqual(piece.shape[0], 38)
        self.assertEqual(pieces[0][1] - pieces[0][0], 26)

    def test_cut_takes_the_left_letter_at_the_thinnest_ink(self):
        pair = np.zeros((38, 60, 4), np.uint8)
        pair[..., :3] = 255
        pair[3:36, 0:26, 3] = 255
        pair[3:36, 34:60, 3] = 255
        pair[10:12, 26:34, 3] = 255                 # a thin bridge
        left = m.cut_left(pair)
        self.assertTrue(24 <= left.shape[1] <= 30)
        self.assertEqual(int(left[..., 3].max()), 255)

    def test_composition_is_deterministic_and_native(self):
        first, row = m.compose(LETTERS, 'EXIT GAME')
        second, again = m.compose({k: v.copy() for k, v in LETTERS.items()}, 'EXIT GAME')
        self.assertTrue((first == second).all() and row == again)
        self.assertEqual({tuple(c) for c in first[first[..., 3] > 0][:, :3]}, {(255, 255, 255)})
        self.assertEqual(first.shape[0], 38)
        self.assertEqual(row, 19.0)

    def test_composition_width_follows_the_phrase(self):
        letters = {c: glyph(20) for c in 'SALIRDGCO'}
        short, _ = m.compose(letters, 'SALIR')
        long, _ = m.compose(letters, 'ESCI DAL GIOCO'.replace('E', 'S'))
        self.assertEqual(short.shape[1], 5 * 20 + 4 * m.LETTER_GAP)
        self.assertGreater(long.shape[1], short.shape[1] + m.WORD_GAP)

    def test_words_are_separated_by_the_word_gap(self):
        letters = {c: glyph(10) for c in 'AB'}
        canvas, _ = m.compose(letters, 'A B')
        self.assertEqual(canvas.shape[1], 20 + m.WORD_GAP)

    def test_every_language_has_phrases_ending_in_english(self):
        self.assertEqual(set(m.LANGUAGES), {'en', 'de', 'es', 'fr', 'it'})
        for name, spec in m.LANGUAGES.items():
            self.assertEqual(spec['phrases'][-1], 'EXIT GAME', name)
            self.assertEqual(len(set(spec['counts'])), 3, name)
        self.assertEqual(len({spec['counts'] for spec in m.LANGUAGES.values()}), len(m.LANGUAGES))

    def test_phrases_use_only_letters_the_title_face_has(self):
        for name, spec in m.LANGUAGES.items():
            self.assertTrue(set(spec['phrases'][-1].replace(' ', '')) <= set('EXITGAM'))
        for banned in ('SPIEL BEENDEN', 'QUITTER LE JEU', 'SALIR DEL JUEGO'):
            for spec in m.LANGUAGES.values():
                self.assertNotIn(banned, spec['phrases'])

    def test_missing_glyph_is_refused_before_composition(self):
        art = type('Art', (), {})()
        with mock.patch.object(m, 'labelled_glyphs', return_value={}):
            with self.assertRaises(m.Unsupported):
                m.native_letters(art, 'QUITTER', {'T': glyph(10)})

    def test_own_glyphs_win_over_the_pool(self):
        own, pooled = glyph(11), glyph(12)
        with mock.patch.object(m, 'labelled_glyphs', return_value={'A': own}):
            letters, sources = m.native_letters(None, 'AT', {'A': pooled, 'T': pooled})
        self.assertIs(letters['A'], own)
        self.assertEqual(sources, {'A': 'own', 'T': 'pool'})

    def test_free_rect_is_aligned_transparent_and_avoids_other_art(self):
        atlas = np.zeros((64, 128, 4), np.uint8)
        atlas[0:64, 0:60, 3] = 255
        found = m.find_free_rect(atlas, [(60, 0, 100, 64)], (0, 0, 20, 10), 16, 8)
        self.assertIsNotNone(found)
        x0, y0, x1, y1 = found
        self.assertEqual((x0 % 4, y0 % 4), (0, 0))
        self.assertGreaterEqual(x0, 100 + 2)
        self.assertEqual((x1 - x0, y1 - y0), (16, 8))

    def test_free_rect_none_when_atlas_is_full(self):
        atlas = np.full((32, 32, 4), 255, np.uint8)
        self.assertIsNone(m.find_free_rect(atlas, [], (0, 0, 4, 4), 8, 8))

    def test_no_external_font_in_the_generator(self):
        source = Path(m.__file__).read_text()
        for word in ('ImageFont', 'ImageDraw', '.ttf', 'Marcellus', '--font', 'truetype'):
            self.assertNotIn(word, source)
        self.assertFalse(list(Path(m.__file__).parent.glob('*.ttf')))


class SizeNormalization(unittest.TestCase):
    def art(self, data, atlases):
        mbs = m.Mbs(data)
        return mbs, FakeArt(atlases, mbs, {p: 0 for p in range(mbs.nparts)})

    def test_scaling_is_uniform_and_keeps_native_look(self):
        canvas, _ = m.compose(LETTERS, 'EXIT GAME')
        for factor in (0.8, 1.3, 1.5):
            scaled, _ = m.scale_canvas(canvas, factor)
            self.assertAlmostEqual(scaled.shape[1] / canvas.shape[1], scaled.shape[0] / canvas.shape[0], delta=0.03)
            self.assertAlmostEqual(scaled.shape[0] / canvas.shape[0], factor, delta=0.03)
            self.assertEqual({tuple(c) for c in scaled[scaled[..., 3] > 0][:, :3]}, {(255, 255, 255)})
            self.assertGreater(int(scaled[..., 3].max()), 250)

    def test_scaling_is_deterministic_and_identity_at_one(self):
        canvas, _ = m.compose(LETTERS, 'EXIT GAME')
        self.assertIs(m.scale_canvas(canvas, 1.0)[0], canvas)
        self.assertTrue((m.scale_canvas(canvas, 1.3)[0] == m.scale_canvas(canvas.copy(), 1.3)[0]).all())

    def test_edge_alpha_is_not_hardened(self):
        glyph_ = np.zeros((38, 20, 4), np.uint8)
        glyph_[..., :3] = 255
        glyph_[5:33, 4:16, 3] = 255
        glyph_[5:33, 3, 3] = 90
        scaled, _ = m.scale_canvas(glyph_, 1.3)
        self.assertTrue(((scaled[..., 3] > 0) & (scaled[..., 3] < 255)).any())

    def test_wanted_scale_has_a_dead_band(self):
        self.assertEqual(m.scale_wanted(31.5, 31.0, 1.0), 1.0)
        self.assertAlmostEqual(m.scale_wanted(14.7, 31.0, 0.35), 14.7 / (31.0 * 0.35))
        self.assertAlmostEqual(m.scale_wanted(33.0, 33.0, 0.5), 2.0)

    def test_text_scale_follows_the_text_axes_of_rotated_sprites(self):
        part = m.Part(1, 0, [1], [(0, 0), (0, 40), (200, 40), (200, 0)], [(0, 0), (0, 40), (200, 40), (200, 0)])
        self.assertEqual(m.text_scale(part), (1.0, 1.0))
        rotated = m.Part(1, 0, [1], [(0, 0), (0, 200), (40, 200), (40, 0)], [(0, 0), (200, 0), (200, 40), (0, 40)])
        self.assertFalse(m.is_upright(m.orientation(rotated)[0]))
        self.assertEqual(m.text_scale(rotated), (1.0, 1.0))
        half = m.Part(1, 0, [1], [(0, 0), (0, 40), (200, 40), (200, 0)], [(0, 0), (0, 20), (100, 20), (100, 0)])
        self.assertEqual(m.text_scale(half), (0.5, 0.5))

    def test_text_height_is_the_median_piece_height(self):
        image = np.zeros((38, 100, 4), np.uint8)
        image[..., :3] = 255
        for x, top in ((2, 4), (30, 4), (60, 10)):
            image[top:top + 30, x:x + 20, 3] = 255
        self.assertEqual(m.text_height(image), 30.0)

    def test_small_label_grows_to_the_native_height_uniformly(self):
        data = synthetic_mbs(rects={4: (96, 40, 396, 94)})
        mbs, art = self.art(data, {0: atlas_with([(96, 40, 396, 94)])})
        label = m.find_label(mbs, 'refined')
        canvas, _ = m.compose(LETTERS, 'EXIT GAME')
        vertical = m.text_scale(label.parts[0])[0]
        native = 1.3 * m.text_height(canvas) * vertical
        fit = m.fit_label(label, art, 'refined', canvas, native, [])
        self.assertAlmostEqual(fit.factor, 1.3, delta=0.01)
        self.assertFalse(fit.width_limited)
        self.assertAlmostEqual(m.text_height(fit.canvas) * vertical, native, delta=1.0)

    def test_label_that_cannot_reach_the_native_height_is_limited_not_distorted(self):
        data = synthetic_mbs()
        mbs, art = self.art(data, {0: atlas_with([LABEL_RECT])})
        label = m.find_label(mbs, 'refined')
        canvas, _ = m.compose(LETTERS, 'EXIT GAME')
        fit = m.fit_label(label, art, 'refined', canvas, 60.0, [])
        self.assertTrue(fit.width_limited)
        self.assertGreater(fit.factor, 1.0)
        self.assertLess(fit.factor, fit.wanted)
        self.assertAlmostEqual(fit.canvas.shape[1] / canvas.shape[1], fit.canvas.shape[0] / canvas.shape[0], delta=0.03)


class Planning(unittest.TestCase):
    def setUp(self):
        self.canvas, self.row = m.compose(LETTERS, 'EXIT GAME')

    def art(self, data, atlases, kind='refined'):
        mbs = m.Mbs(data)
        textures = {p: 0 for p in range(mbs.nparts)}
        return mbs, FakeArt(atlases, mbs, textures)

    def test_one_part_widens_into_free_space_by_what_the_text_needs(self):
        data = synthetic_mbs()
        mbs, art = self.art(data, {0: atlas_with([LABEL_RECT])})
        plan = m.plan_label(m.find_label(mbs, 'refined'), art, 'refined', self.canvas, self.row, [])
        need = self.canvas.shape[1] + 2 * m.MARGIN
        grown = plan.rect_of(4)[2] - LABEL_RECT[2]
        self.assertEqual(grown, need - 172)
        self.assertEqual(plan.rect_of(4)[:2], LABEL_RECT[:2])
        self.assertGreater(plan.ratio, 1.0)
        edit = plan.edits[0]
        self.assertEqual(edit.rect, plan.rect_of(4))
        ink = np.nonzero(edit.rgba[..., 3].any(axis=0))[0]
        self.assertGreaterEqual(ink[0], m.MARGIN - 1)
        self.assertLessEqual(ink[-1], edit.rgba.shape[1] - m.MARGIN)

    def test_wide_label_is_not_widened(self):
        data = synthetic_mbs(rects={4: (96, 40, 396, 78)})
        mbs, art = self.art(data, {0: atlas_with([(96, 40, 396, 78)])})
        plan = m.plan_label(m.find_label(mbs, 'refined'), art, 'refined', self.canvas, self.row, [])
        self.assertEqual(plan.ratio, 1.0)
        self.assertEqual(plan.rect_of(4), (96, 40, 396, 78))

    def test_no_room_rejected(self):
        data = synthetic_mbs()
        blocked = atlas_with([LABEL_RECT], width=400, height=100, extra=[(280, 30, 330, 90), (0, 30, 90, 90)])
        mbs, art = self.art(data, {0: blocked})
        with self.assertRaises(m.Unsupported):
            m.plan_label(m.find_label(mbs, 'refined'), art, 'refined', self.canvas, self.row, [])

    def test_other_sprites_block_growth(self):
        mbs, art = self.art(synthetic_mbs(), {0: atlas_with([LABEL_RECT], width=400, height=100)})
        with self.assertRaises(m.Unsupported):
            m.plan_label(m.find_label(mbs, 'refined'), art, 'refined', self.canvas, self.row,
                         [(270, 30, 300, 90), (0, 30, 90, 90)])

    def test_label_moves_to_free_atlas_space_when_it_cannot_grow_in_place(self):
        data = synthetic_mbs()
        blocked = atlas_with([LABEL_RECT], extra=[(280, 30, 330, 90), (0, 30, 90, 90)])
        mbs, art = self.art(data, {0: blocked})
        plan = m.plan_label(m.find_label(mbs, 'refined'), art, 'refined', self.canvas, self.row,
                            [(280, 30, 330, 90), (0, 30, 90, 90)])
        moved = plan.rect_of(4)
        self.assertEqual((moved[0] % 4, moved[1] % 4), (0, 0))
        self.assertNotEqual(moved[:2], LABEL_RECT[:2])

    def test_two_parts_text_in_the_big_one_and_the_other_cleared(self):
        rects = {5: (700, 40, 760, 84), 4: (96, 40, 596, 86)}
        data = synthetic_mbs(label_parts=(4, 5), rects=rects)
        atlas = atlas_with([rects[4], rects[5]])
        mbs, art = self.art(data, {0: atlas})
        plan = m.plan_label(m.find_label(mbs, 'refined'), art, 'refined', self.canvas, self.row, [])
        self.assertEqual(plan.ratio, 1.0)
        by_rect = {e.rect: e for e in plan.edits}
        self.assertEqual(set(by_rect), {rects[4], rects[5]})
        self.assertGreater(int(by_rect[rects[4]].rgba[..., 3].max()), 200)
        self.assertEqual(int(by_rect[rects[5]].rgba[..., 3].max()), 0)

    def test_two_parts_text_must_fit_the_carrier(self):
        rects = {5: (700, 40, 900, 84), 4: (96, 40, 246, 86)}
        mbs, art = self.art(synthetic_mbs(label_parts=(4, 5), rects=rects), {0: atlas_with([rects[4], rects[5]])})
        with self.assertRaises(m.Unsupported):
            m.plan_label(m.find_label(mbs, 'refined'), art, 'refined', self.canvas, self.row, [])

    def test_rotated_label_is_painted_through_its_turn(self):
        rect = (96, 40, 134, 346)
        turn = np.array([[0, 1], [-1, 0]])
        label = m.Label('refined', [m.Part(4, 0, [4], quad(rect), [(-86.0, -22.0), (86.0, -22.0), (86.0, 16.0), (-86.0, 16.0)])])
        mbs = m.Mbs(synthetic_mbs(rects={4: rect}))
        art = FakeArt({0: atlas_with([rect], height=400)}, mbs, {p: 0 for p in range(mbs.nparts)})
        plan = m.plan_label(label, art, 'refined', self.canvas, self.row, [])
        self.assertFalse(m.is_upright(plan.turn))
        self.assertEqual(plan.rect_of(4), rect)
        rgba = plan.edits[0].rgba
        self.assertEqual(rgba.shape[:2], (306, 38))
        self.assertGreater(int(rgba[..., 3].max()), 200)
        rows = np.nonzero(rgba[..., 3].any(axis=1))[0]
        self.assertEqual(rows.max() - rows.min() + 1, self.canvas.shape[1] - 4)

    def test_geometry_follows_the_part(self):
        data = synthetic_mbs()
        mbs, art = self.art(data, {0: atlas_with([LABEL_RECT])})
        plan = m.plan_label(m.find_label(mbs, 'refined'), art, 'refined', self.canvas, self.row, [])
        m.apply_geometry(mbs, plan)
        again = m.find_label(mbs, 'refined').parts[0]
        self.assertEqual(again.rect, plan.rect_of(4))
        grec = mbs.geom_record(again.geoms[0])
        self.assertAlmostEqual((grec[6] - grec[2]) / 172.0, plan.ratio, places=3)
        self.assertAlmostEqual(grec[2] + grec[6], 0.0, places=3)

    def test_edit_changes_only_the_label_rectangle(self):
        data = synthetic_mbs()
        atlas = atlas_with([LABEL_RECT])
        mbs, art = self.art(data, {0: atlas.copy()})
        plan = m.plan_label(m.find_label(mbs, 'refined'), art, 'refined', self.canvas, self.row, [])
        target = atlas.copy()
        changed = m.apply_edit(target, plan.edits[0])
        x0, y0, x1, y1 = plan.edits[0].rect
        outside = np.ones(atlas.shape[:2], bool)
        outside[y0:y1, x0:x1] = False
        self.assertTrue((target[outside] == atlas[outside]).all())
        self.assertFalse(changed[outside].any())
        self.assertTrue((target[y0:y1, x0:x1][target[y0:y1, x0:x1, 3] > 0][:, :3] == 255).all())


def synthetic_selection(kind='refined', break_row=False, share_segment=False):
    """A title MBS reduced to its animation table, channels, segments and transforms: the game's selection animations
    in the four-row variant (and the three-row one), plus a TITLE animation that draws no row."""
    names = m.KINDS[kind]['roles']
    prefix = 'RE_' if kind == 'refined' else ''
    layers = {role: tuple(range(first, first + 3)) for role, first in names.items()}
    ys = (78, 109, 145, 180)
    shown_before = ['NEWGAME', 'LOAD', 'INSTALL', 'MODE']
    anims = []
    for role in ('NEWGAME', 'LOAD', 'INSTALL', 'MODE'):
        three = [(layers[r][0], y) for r, y in zip(('NEWGAME', 'LOAD', 'MODE'), (93, 128, 159))]
        four = [(layers[r][0] if r == role else layers[r][2], y) for r, y in zip(shown_before, ys)]
        anims.append((f'{prefix}{role}_2', three))
        anims.append((f'{prefix}{role}_2_1', four))
    anims.append((f'{prefix}TITLE', [(1, 10)]))
    segments, channels, table = [], [], []
    for name, rows in anims:
        first = len(channels)
        for layer, y in rows:
            channels.append((len(segments), 1))
            segments.append((layer, y))
        table.append((name, first, len(rows)))
    if share_segment:
        channels.append((0, 1))
        table[-1] = (table[-1][0], table[-1][1], table[-1][2] + 1)
    out = bytearray(0x120)
    out[:4] = b'FMBS'
    s7_at = len(out)
    ys_used = sorted({y for _, y in segments})
    for y in ys_used:
        out += struct.pack('<8f', 0.0, float(y), 0, 0, 0, 0, 1.0, 1.0) + b'\xff\xff\xff\xff'
    s8_at = len(out)
    for layer, y in segments:
        out += struct.pack('<IHHHH', layer, ys_used.index(y), 1, 0x820, 0) + bytes(20)
    label_at = len(out)
    for name, first, count in table:
        out += bytes(16) + name.encode().ljust(24, b'\0') + struct.pack('<HBBI', first, count, 0, 0)
    s10_at = len(out)
    for first, count in channels:
        out += struct.pack('<HH5I', first, count, 1, 0, 0, 0, 0)
    for i, offset in enumerate((0, s7_at, s7_at, 0, s7_at, 0, s7_at, s7_at, s8_at, label_at, s10_at)):
        struct.pack_into('<Q', out, 0xb0 + 8 * i, offset)
    return bytes(out)


class RowOrder(unittest.TestCase):
    def test_every_four_row_variant_ends_with_mode_then_exit(self):
        for kind in m.KINDS:
            data = m.Mbs(synthetic_selection(kind))
            changed = m.reorder_rows(data, kind)
            self.assertEqual(len(changed), 8)
            layers = m.row_layers(kind)
            for name, first, count, match in m.selection_animations(data):
                rows = m.visible_rows(data, kind, first, count)
                if match.group(3) == '1':
                    self.assertEqual([role for _, role in rows], ['NEWGAME', 'LOAD', 'MODE', 'INSTALL'])
                    self.assertEqual([y for y, _ in rows], [78, 109, 145, 180])
                else:
                    self.assertEqual([y for y, _ in rows], [93, 128, 159])

    def test_selected_row_follows_its_own_slot(self):
        data = m.Mbs(synthetic_selection())
        m.reorder_rows(data, 'refined')
        layers = m.row_layers('refined')
        by_name = {n: (f, c) for n, f, c, _ in m.selection_animations(data)}
        first, count = by_name['RE_INSTALL_2_1']
        drawn = [data.segment_layer(data.channel_segments(c)[-1]) for c in range(first, first + count)]
        self.assertEqual(drawn, [layers['NEWGAME'][2], layers['LOAD'][2], layers['MODE'][0], layers['INSTALL'][2]])
        first, count = by_name['RE_MODE_2_1']
        drawn = [data.segment_layer(data.channel_segments(c)[-1]) for c in range(first, first + count)]
        self.assertEqual(drawn, [layers['NEWGAME'][2], layers['LOAD'][2], layers['MODE'][2], layers['INSTALL'][0]])

    def test_equal_size_and_other_animations_untouched(self):
        original = synthetic_selection()
        data = m.Mbs(original)
        m.reorder_rows(data, 'refined')
        self.assertEqual(len(data.data), len(original))
        title = {n: (f, c) for n, f, c in data.animations()}['RE_TITLE']
        before = m.Mbs(original)
        self.assertEqual([data.segment_layer(s) for s in data.channel_segments(title[0])],
                         [before.segment_layer(s) for s in before.channel_segments(title[0])])

    def test_swapping_twice_restores_the_file(self):
        original = synthetic_selection('classic')
        data = m.Mbs(original)
        m.reorder_rows(data, 'classic')
        self.assertNotEqual(bytes(data.data), original)
        again = m.Mbs(bytes(data.data))
        swap = {}
        roles = m.row_layers('classic')
        for a, b in zip(roles['INSTALL'], roles['MODE']):
            swap[a], swap[b] = b, a
        for _, first, count, _ in m.selection_animations(again):
            for channel in range(first, first + count):
                for segment in again.channel_segments(channel):
                    again.set_segment_layer(segment, swap.get(again.segment_layer(segment), again.segment_layer(segment)))
        self.assertEqual(bytes(again.data), original)

    def test_shared_segment_with_another_animation_is_refused(self):
        data = m.Mbs(synthetic_selection(share_segment=True))
        with self.assertRaises(m.Unsupported):
            m.reorder_rows(data, 'refined')

    def test_unexpected_layout_is_refused(self):
        data = m.Mbs(synthetic_selection())
        layers = m.row_layers('refined')
        rows = {n: (f, c) for n, f, c, _ in m.selection_animations(data)}['RE_LOAD_2_1']
        data.set_segment_layer(data.channel_segments(rows[0] + 3)[0], layers['LOAD'][2])
        with self.assertRaises(m.Unsupported):
            m.reorder_rows(data, 'refined')

    def test_file_without_selection_animations_is_refused(self):
        with self.assertRaises(m.Unsupported):
            m.reorder_rows(m.Mbs(b'FMBS' + bytes(0x200)), 'refined')


class Bc3Edges(unittest.TestCase):
    def synthetic_pack(self, image):
        h, w = image.shape[:2]
        blocks = image.reshape(h // 4, 4, w // 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 4)
        encoded = bc3.encode_blocks(blocks)
        blob = bytearray(h * w)
        for index in range(len(blocks)):
            by, bx = divmod(index, w // 4)
            offset = ftex.tiled_offset(bx, by, w // 4)
            blob[offset:offset + 16] = encoded[index * 16:(index + 1) * 16]
        tex = dict(name='t', data=0, size=len(blob), width=w, height=h, dfmt=37, tiling=13, pitch=w)
        return bytes(blob), tex

    def test_neighbours_keep_their_pixels(self):
        rng = np.random.default_rng(1)
        image = np.zeros((32, 64, 4), np.uint8)
        image[..., :3] = 255
        image[2:30, 4:60, 3] = 255
        image[10:20, 10:40, :3] = rng.integers(0, 255, (10, 30, 3))
        blob, tex = self.synthetic_pack(image)
        decoded = np.array(ftex.decode(blob, tex))
        edited = decoded.copy()
        edited[13:22, 18:47, 3] = 0                 # a rectangle that is not 4-aligned on any side
        edited[14:21, 20:44, 3] = 255
        changed = (edited != decoded).any(axis=-1)
        out, blocks = patch_ftx.patch(blob, tex, edited, changed)
        after = np.array(ftex.decode(out, tex)).astype(int)
        touched = changed.reshape(8, 4, 16, 4).any(axis=(1, 3)).repeat(4, 0).repeat(4, 1)
        before = decoded.astype(int)
        self.assertTrue((after[~touched] == before[~touched]).all())
        near = touched & ~changed
        self.assertLess(np.abs(after[near][:, 3] - before[near][:, 3]).mean(), 24)
        self.assertGreater(blocks, 0)
        self.assertEqual(len(out), len(blob))


@unittest.skipUnless(os.environ.get('TITLE_EXIT_GAME'), 'set TITLE_EXIT_GAME to the game/prepared folder')
class LocalVariants(unittest.TestCase):
    """Every distinct title atlas of the local game folders gets EXIT GAME in both title variants."""

    def test_no_local_variant_keeps_install(self):
        groups = m.collect(os.environ['TITLE_EXIT_GAME'])
        arts = {}
        for crc, group in groups.items():
            self.assertEqual({k: len(v) for k, v in group['files'].items()}, {'refined': 1, 'classic': 1})
            arts[crc] = m.NativeArt(group['atlas'], next(iter(group['files']['refined'])), next(iter(group['files']['classic'])))
        _, reference = m.find_reference(arts)
        self.assertEqual(set(reference), set('EXITGAM'))
        pool = m.build_pool(arts, reference)
        for crc, art in arts.items():
            notes = []
            metrics = []
            blob, files, status = m.process_group(art, pool, notes, metrics)
            self.assertEqual(status, {'refined': 'ok', 'classic': 'ok'}, f'{crc:08x}')
            for kind, data in files.items():
                patched = m.Mbs(data)
                self.assertTrue(m.find_label(patched, kind).parts)
                shown = 0
                for name, first, count, match in m.selection_animations(patched):
                    expected = m.FOUR_ROWS[kind].get(int(match.group(3) or 0))
                    if expected:
                        rows = m.visible_rows(patched, kind, first, count)
                        self.assertEqual([role for _, role in rows], expected, f'{crc:08x} {name}')
                        self.assertEqual(len({y for y, _ in rows}), len(rows))
                        shown += 1
                self.assertGreaterEqual(shown, 8, f'{crc:08x} {kind}')
                self.check_install_layers_agree(patched, kind, f'{crc:08x}')
            self.check_sizes(crc, art, files, metrics)

    def check_install_layers_agree(self, patched, kind, name):
        mine = {p.part for p in m.find_label(patched, kind).parts}
        shapes = {tuple(sorted((k['part'], k['geom']) for k in patched.keyframes(i) if k['part'] in mine))
                  for i in m.KINDS[kind]['install']}
        self.assertEqual(len(shapes), 1, f'{name} {kind}: the EXIT art differs between selection states')

    def check_sizes(self, crc, art, files, metrics):
        by_kind = {mt['kind']: mt for mt in metrics}
        self.assertEqual(set(by_kind), {'refined', 'classic'})
        natives = {}
        for kind, mt in by_kind.items():
            natives[kind] = m.native_text_height(art, kind)
            self.assertAlmostEqual(mt['native_height'], natives[kind])
            ratio = mt['height'] / mt['native_height']
            self.assertLessEqual(abs(ratio - 1), 0.07 if mt['width_limited'] else 0.04, f'{crc} {kind} {mt}')
            self.assertGreaterEqual(mt['scale'], min(mt['wanted_scale'], 1.0))
            self.assertAlmostEqual(mt['visible_height'] / mt['height'], 1.0, delta=0.12)
        if m.language_of(art) in ('es', 'fr', 'de', 'it'):
            self.assertNotAlmostEqual(natives['refined'], natives['classic'], delta=1.0)
        for kind in by_kind:
            before = m.find_label(art.mbs[kind], kind).parts
            after = m.find_label(m.Mbs(files[kind]), kind).parts
            self.assertEqual([m.is_upright(m.orientation(p)[0]) for p in after], [m.is_upright(m.orientation(p)[0]) for p in before])
        if m.language_of(art) == 'de':
            label = m.find_label(m.Mbs(files['classic']), 'classic')
            self.assertGreater(len(label.parts), 1)


if __name__ == '__main__':
    unittest.main()
