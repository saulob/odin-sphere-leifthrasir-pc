#!/usr/bin/env python3
"""Portuguese (Brazil) visual families: each shipped delta applied to the real local source it names and the result
checked by structure (Classic cards, chapter and act titles, Halja on Japan, Mode Select, Bookshelf NEW, kitchen),
plus the format helpers they rely on. Families whose prepared game is missing are skipped, not passed.

    python -I tests/visual_family_tests.py
"""
import csv
import struct
import sys
import unicodedata
import unittest
import zlib
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts' / 'localization'))
sys.path.insert(0, str(ROOT / 'scripts' / 'prompts'))
import mbs as M  # noqa: E402
import oslv  # noqa: E402

try:
    import numpy as np
    import cpk
    import ftex
    import layout_text as T
    import visuals
except ImportError as missing:
    raise SystemExit(f'numpy, scipy and Pillow are required ({missing}); run tools/setup.ps1 -InstallPython')

VISUAL = ROOT / 'localization' / 'pt-BR' / 'visual'
PREPARED = ROOT / 'game' / 'prepared'
EUROPE = PREPARED / 'CUSA05262-1.01'


def titles():
    with open(VISUAL / 'stage-cards.tsv', encoding='utf-8', newline='') as f:
        return list(csv.DictReader(f, delimiter='\t'))


def targets(name):
    return oslv.parse((VISUAL / name).read_bytes())


def read(archive, entry):
    pack = cpk.Cpk(archive, original=True)
    try:
        return pack.read(pack.find(entry))
    finally:
        pack.f.close()


def apply(name, entry, source):
    for t in targets(name):
        if t['name'].lower() == entry.lower() and t['size'] == len(source) and t['crc'] == zlib.crc32(source):
            out = oslv.apply(t, source)
            assert out is not None
            return out
    return None


def drawn_sprites(layout):
    """(texture, x0, y0, x1, y1) of every sprite a layer of the layout draws."""
    return {(k['texture'],) + layout.part_rect(k['part']) for layer in range(layout.count(M.LAYERS))
            for k in layout.layer_keys(layer) if k['part'] < layout.count(M.PARTS)}


def box(layout, key):
    return T.quad_box(layout.geom(key['geom']))


class Format(unittest.TestCase):
    def test_diff_ops_rebuild_any_result(self):
        source = bytes(range(256)) * 8
        for result in (source[:300] + b'new bytes' + source[900:], b'x' * 40 + source, source[::-1][:500], b''):
            ops = oslv.diff_ops(source, result)
            self.assertEqual(M.apply_ops(source, ops), result)

    def test_rebuilt_target_round_trips(self):
        source, result = b'FMBS' + bytes(600), b'FMBS' + bytes(100) + b'grown' * 30 + bytes(500)
        blob = oslv.encode([('x.mbs', source, result, None)])
        (t,) = oslv.parse(blob)
        self.assertEqual(oslv.apply(t, source), result)
        self.assertIsNone(oslv.apply(t, source[:-1] + b'\1'))

    def test_no_two_deltas_claim_one_source(self):
        """The launcher lays the first matching delta on an entry; a second one for the same source would be lost."""
        seen = {}
        for path in sorted(VISUAL.glob('*.oslv')):
            for t in oslv.parse(path.read_bytes()):
                key = (t['name'].lower(), t['size'], t['crc'])
                self.assertNotIn(key, seen, f'{path.name} and {seen.get(key)} both rebuild {key}')
                seen[key] = path.name


@unittest.skipUnless((EUROPE / 'Odin2_OR_UK.CPK').is_file(), 'Europe 1.01 not available')
class ClassicCards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        visuals.use_native_reader()
        pack = cpk.Cpk(EUROPE / 'Odin2_OR_UK.CPK', original=True)
        cls.entries = {e['path']: pack.read(e) for e in pack.entries if e['path'].lower().endswith(('.ftx', '.mbs'))}
        pack.f.close()

    def card(self, card):
        ftx_bytes, mbs_bytes = self.entries[f'{card}.ftx'], self.entries[f'{card}.mbs']
        new_ftx = apply('classic-cards-ptbr.oslv', f'{card}.ftx', ftx_bytes)
        new_mbs = apply('classic-cards-ptbr.oslv', f'{card}.mbs', mbs_bytes)
        return ftx_bytes, new_ftx, new_mbs

    def test_every_card_is_rebuilt_in_three_matching_layers(self):
        for row in titles():
            with self.subTest(card=row['card']):
                ftx_bytes, new_ftx, new_mbs = self.card(row['card'])
                self.assertIsNotNone(new_ftx)
                self.assertIsNotNone(new_mbs)
                self.assertEqual(len(new_ftx), len(ftx_bytes))
                layout = M.Mbs(new_mbs)
                letters = len([c for c in row['pt_br'] if not c.isspace()])
                layers = [layout.layer_keys(i) for i in range(3)]
                self.assertEqual([len(keys) for keys in layers], [letters] * 3)
                tex = ftex.textures(new_ftx)[0]
                for scatter, settled, gone in zip(*layers):
                    self.assertEqual(scatter['part'], settled['part'])
                    self.assertEqual(settled['part'], gone['part'])
                    x0, y0, x1, y1 = layout.part_rect(settled['part'])
                    self.assertTrue(0 <= x0 < x1 <= tex['width'] and 0 <= y0 < y1 <= tex['height'])
                    a, b, c = box(layout, scatter), box(layout, settled), box(layout, gone)
                    self.assertAlmostEqual(a[0] + a[2], 2 * (b[0] + b[2]), delta=0.01)
                    self.assertAlmostEqual(c[2] - c[0], 3 * (b[2] - b[0]), delta=0.01)
                    self.assertGreater(gone['colour'], 0)
                    self.assertLess(gone['colour'], layout.count(M.COLOURS))

    def test_atlas_holds_one_sprite_per_letter_shape(self):
        ftx_bytes, new_ftx, new_mbs = self.card('12')
        layout = M.Mbs(new_mbs)
        self.assertEqual(layout.count(M.PARTS), len(set('Castelo do Lorde Demônio') - {' '}))
        atlas = visuals.decode(new_ftx, ftex.textures(new_ftx)[0])
        for p in range(layout.count(M.PARTS)):
            x0, y0, x1, y1 = layout.part_rect(p)
            self.assertGreater(int(atlas[y0:y1, x0:x1, 3].max()), 128)

    def test_other_builds_have_targets(self):
        for build, archive in (('CUSA05083-1.00', 'Odin2_OR_US.CPK'), ('CUSA01290-1.00', 'Odin2_ORHD.CPK'),
                               ('CUSA01290-1.02', 'Odin2_ORHD.CPK')):
            path = PREPARED / build / archive
            if not path.is_file():
                continue
            with self.subTest(build=build):
                self.assertIsNotNone(apply('classic-cards-ptbr.oslv', '01.mbs', read(path, '01.mbs')))

    def test_spanish_cards_are_never_targets(self):
        es = cpk.Cpk(EUROPE / 'Odin2_OR_ES.CPK', original=True)
        try:
            data = es.read(es.find('01.ftx'))
        finally:
            es.f.close()
        self.assertIsNone(apply('classic-cards-ptbr.oslv', '01.ftx', data))


@unittest.skipUnless((EUROPE / 'Odin2_EU_PATCH1_RE_UK.CPK').is_file(), 'Europe 1.01 not available')
class ChapterTitles(unittest.TestCase):
    def test_every_title_is_spelled_with_its_marks_in_three_layers(self):
        import stage_acts as S
        source = read(EUROPE / 'Odin2_EU_PATCH1_RE_UK.CPK', S.LAYOUT)
        layout = M.Mbs(apply('stage-acts-ptbr.oslv', S.LAYOUT, source))
        english = M.Mbs(source)
        for name, by_language in S.OFFICIAL.items():
            for index, (channel, text) in enumerate(zip(S.word_channels(layout, name), by_language['UK'])):
                portuguese = S.PORTUGUESE.get(text, text)
                scatter, settled, gone = S.channel_layers(layout, channel)
                counts = [len(layout.layer_keys(L)) for L in (scatter, settled, gone)]
                with self.subTest(anim=name, text=portuguese):
                    expected = len(S.decompose(portuguese)) + sum(len(m) for _, m in S.decompose(portuguese))
                    self.assertEqual(counts, [expected] * 3)
                    if portuguese == text:
                        self.assertEqual(S.channel_layers(english, channel), (scatter, settled, gone))

    def test_spanish_layout_is_never_a_target(self):
        import stage_acts as S
        source = read(EUROPE / 'Odin2_EU_PATCH1_RE_ES.CPK', S.LAYOUT)
        self.assertIsNone(apply('stage-acts-ptbr.oslv', S.LAYOUT, source))


class HaljaJapan(unittest.TestCase):
    def test_japan_card_59_is_lettered(self):
        found = 0
        for build in ('CUSA01290-1.00', 'CUSA01290-1.02'):
            path = PREPARED / build / 'Odin2_RE.CPK'
            if not path.is_file():
                continue
            visuals.use_native_reader()
            source = read(path, 'GUI/HD_StageName59.ftx')
            result = apply('stage-cards-ptbr.oslv', 'GUI/HD_StageName59.ftx', source)
            with self.subTest(build=build):
                self.assertIsNotNone(result)
                self.assertEqual(len(result), len(source))
                textures = ftex.textures(source)
                for i, tex in enumerate(textures):
                    same = result[tex['data']:tex['data'] + tex['size']] == source[tex['data']:tex['data'] + tex['size']]
                    self.assertEqual(same, i != 0, f'texture {i}')
            found += 1
        if not found:
            self.skipTest('no Japanese build prepared')

    def test_lowercase_j_is_built_from_the_face(self):
        import stage_cards as SC
        import visual_font as VF
        pool = VF.VisualGlyphPool('toy')
        for char, height in (('i', 80), ('g', 80)):
            rgba = np.zeros((height + 12, 30, 4), np.uint8)
            rgba[6:6 + 60, 10:20] = (60, 120, 220, 255)
            if char == 'i':
                rgba[6:14, 10:20, 3] = 255
            else:
                rgba[6 + 60:6 + height, 4:20] = (60, 120, 220, 255)
            ink = (4 if char == 'g' else 10, 6, 20, 6 + (height if char == 'g' else 60))
            pool.add(VF.VisualGlyph(char, rgba, ink, 60.0, 0.33 if char == 'g' else 0.0, f'UK toy {char}'))
        j = SC.compose_j(pool)
        self.assertIsNotNone(j)
        self.assertEqual(j.char, 'j')
        self.assertGreater(j.drop, 0.2)
        self.assertIn('tail of', j.source)


def letters(text):
    return Counter(c for c in unicodedata.normalize('NFD', text) if c.isalpha() and not unicodedata.combining(c))


@unittest.skipUnless((EUROPE / 'Odin2_RE_UK.CPK').is_file(), 'Europe 1.01 not available')
class SharedAtlasWords(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        visuals.use_native_reader()

    def changed_outside(self, atlas_entry, delta, rects):
        source = read(EUROPE / 'Odin2_RE_UK.CPK', atlas_entry)
        result = apply(delta, atlas_entry, source)
        self.assertIsNotNone(result)
        for t, tex in enumerate(ftex.textures(source)):
            a, b = visuals.decode(source, tex).astype(int), visuals.decode(result, tex).astype(int)
            outside = np.ones(a.shape[:2], bool)
            blocks = np.zeros(a.shape[:2], bool)
            for rt, x0, y0, x1, y1 in rects:
                if rt == t:
                    outside[y0:y1, x0:x1] = False
                    blocks[y0 // 4 * 4:(y1 + 3) // 4 * 4, x0 // 4 * 4:(x1 + 3) // 4 * 4] = True
            seen = np.maximum(a[..., 3], b[..., 3])[..., None] / 255
            drift = np.maximum(np.abs(a[..., 3] - b[..., 3]), (np.abs(a[..., :3] - b[..., :3]) * seen).max(axis=-1))
            self.assertFalse((drift[outside & ~blocks] > 0).any(), f'texture {t} changed outside the touched blocks')
            self.assertLessEqual(int(drift[outside & blocks].max(initial=0)), 48, f'texture {t}: neighbours drift')
        return source, result

    def test_bookshelf_new_badge(self):
        import sprite_slots as SS
        word = SS.FAMILIES['bookshelf_new']['words'][0]
        self.changed_outside('GUI/HD_Archive_00.ftx', 'bookshelf-new-ptbr.oslv', [word.slot])
        layout = M.Mbs(apply('bookshelf-new-ptbr.oslv', 'GUI/REHD_Archive_00.mbs',
                             read(EUROPE / 'Odin2_RE_UK.CPK', 'GUI/REHD_Archive_00.mbs')))
        drawn = drawn_sprites(layout)
        self.assertIn(word.slot, drawn)
        self.assertNotIn(word.english, drawn)

    def test_kitchen_words(self):
        import sprite_slots as SS
        words = SS.FAMILIES['kitchen']['words']
        self.changed_outside('GUI/HD_Kitchin.ftx', 'kitchen-ptbr.oslv', [w.slot for w in words])
        source = read(EUROPE / 'Odin2_RE_UK.CPK', 'GUI/REHD_Kitchin.mbs')
        layout = M.Mbs(apply('kitchen-layout-ptbr.oslv', 'GUI/REHD_Kitchin.mbs', source))
        drawn = drawn_sprites(layout)
        take_out = words[0]
        self.assertIn(take_out.slot, drawn)
        self.assertNotIn(take_out.english, drawn)
        import family_respell as F
        for rect in F.FAMILIES['kitchen']['sprites']:
            self.assertNotIn(rect, drawn)

    def test_mode_select_words_read_back(self):
        import family_respell as F
        import glyph_table
        import make_visuals as V
        spec = F.FAMILIES['mode_select']
        europe = {L: M.Mbs(read(EUROPE / f'Odin2_RE_{L}.CPK', 'GUI/REHD_Mode_Select.mbs')) for L in V.LANGUAGES}
        table, _ = glyph_table.build(europe, spec['texts'], spec['largest'])
        reader = V.Reader({k: v for k, v in table.items() if not v.startswith('#')})
        source = read(EUROPE / 'Odin2_RE_UK.CPK', 'GUI/REHD_Mode_Select.mbs')
        layout = M.Mbs(apply('mode-select-ptbr.oslv', 'GUI/REHD_Mode_Select.mbs', source))
        read_back = Counter()
        for line in T.lines(layout, reader):
            read_back += letters(F.bare(T.line_text(layout, line, reader)))
        for word in spec['labels'].values():
            with self.subTest(word=word):
                self.assertFalse(letters(F.bare(word)) - read_back)


@unittest.skipUnless((EUROPE / 'Odin2_EU_PATCH1_RE_UK.CPK').is_file(), 'Europe 1.01 not available')
class InterfaceArt(unittest.TestCase):
    PATCH = EUROPE / 'Odin2_EU_PATCH1_RE_UK.CPK'
    BASE = EUROPE / 'Odin2_RE_UK.CPK'

    def drawn(self, archive):
        import interface_art as IA
        layout_blob = read(archive, 'GUI/REHD_Interface.mbs')
        atlas_blob = read(archive, 'GUI/HD_Interface.ftx')
        layout = M.Mbs(apply('interface-ptbr.oslv', 'GUI/REHD_Interface.mbs', layout_blob))
        atlas = apply('interface-art-ptbr.oslv', 'GUI/HD_Interface.ftx', atlas_blob)
        self.assertIsNotNone(atlas)
        import make_visuals as V
        return IA, layout, V.atlas(atlas_blob), V.atlas(atlas)

    def test_every_painted_sprite_replaces_the_english_one(self):
        for archive in (self.PATCH, self.BASE):
            IA, layout, before, after = self.drawn(archive)
            european = before
            where = IA.resolve(IA.V.atlas(read(self.PATCH, 'GUI/HD_Interface.ftx')), european)
            self.assertIsNotNone(where)
            drawn = drawn_sprites(layout)
            source_drawn = drawn_sprites(M.Mbs(read(archive, 'GUI/REHD_Interface.mbs')))
            for item in [*IA.PAINTS, IA.COMBO, *IA.GRAFTS]:
                with self.subTest(archive=archive.name, item=item.name):
                    self.assertNotIn(where[item.english], drawn)
            for item in [*IA.PAINTS, IA.COMBO]:
                with self.subTest(archive=archive.name, painted=item.name):
                    slot = where[item.slot]
                    t, x0, y0, x1, y1 = slot
                    self.assertFalse(np.array_equal(before[t][y0:y1, x0:x1], after[t][y0:y1, x0:x1]))
                    if where[item.english] in source_drawn:
                        self.assertIn(slot, drawn)

    def test_pow_label_follows_the_corpus(self):
        import json
        pt = json.loads((ROOT / 'localization' / 'pt-BR' / 'sheets' / 'msgsheet.json').read_text(encoding='utf-8'))
        text = json.dumps(pt, ensure_ascii=False)
        self.assertGreaterEqual(text.count('de POW'), 4)
        self.assertNotIn('Custo de Poder', text)
        IA, layout, _, _ = self.drawn(self.PATCH)
        self.assertIn((0, 1234, 818, 1296, 840), drawn_sprites(layout))

    def test_only_the_slots_change(self):
        IA, layout, before, after = self.drawn(self.PATCH)
        changed = np.zeros(before[0].shape[:2], bool), np.zeros(before[1].shape[:2], bool), np.zeros(before[2].shape[:2], bool)
        for t in range(len(before)):
            diff = np.any(before[t] != after[t], axis=-1)
            for slot in [*(p.slot for p in IA.PAINTS), IA.COMBO.slot]:
                if slot[0] == t:
                    diff[slot[2] // 4 * 4:-(-slot[4] // 4) * 4, slot[1] // 4 * 4:-(-slot[3] // 4) * 4] = False
            self.assertFalse(diff.any(), f'texture {t} changed outside the painted slots')


@unittest.skipUnless((EUROPE / 'Odin2_RE_UK.CPK').is_file(), 'Europe 1.01 not available')
class ManualPages(unittest.TestCase):
    def test_last_page_is_language_independent(self):
        import ftex
        import visuals
        pages = {}
        for language in ('UK', 'ES', 'FR', 'DE', 'IT'):
            blob = read(EUROPE / f'Odin2_RE_{language}.CPK', 'Other/HD_M43.ftx')
            pages[language] = visuals.decode(blob, ftex.textures(blob)[0])
        for language, pixels in pages.items():
            self.assertTrue(np.array_equal(pixels, pages['UK']), language)


@unittest.skipUnless((EUROPE / 'Odin2_OR_UK.CPK').is_file(), 'Europe 1.01 not available')
class ButterflyTitles(unittest.TestCase):
    def test_titles_are_two_pass_layers_of_the_portuguese_words(self):
        import butterfly_titles as B
        source = read(EUROPE / 'Odin2_OR_UK.CPK', 'Butterfly.mbs')
        layout = M.Mbs(apply('butterfly-ptbr.oslv', 'Butterfly.mbs', source))
        english = M.Mbs(source)
        for anim, text in B.PORTUGUESE.items():
            letters = len(B.S.decompose(text)) + text.count('G')
            marks = sum(len(m) for _, m in B.S.decompose(text))
            for layer in B.layers_of(layout, B.channel_of(layout, anim)):
                with self.subTest(anim=anim, layer=layer):
                    self.assertEqual(len(layout.layer_keys(layer)), 2 * (letters + marks + 4))
            self.assertEqual(B.layers_of(layout, B.channel_of(layout, anim)), B.layers_of(english, B.channel_of(english, anim)))

    def test_other_sources_are_refused_and_output_is_deterministic(self):
        import butterfly_titles as B
        source = read(EUROPE / 'Odin2_OR_UK.CPK', 'Butterfly.mbs')
        target = next(t for t in targets('butterfly-ptbr.oslv') if t['name'].lower() == 'butterfly.mbs')
        self.assertIsNone(oslv.apply(target, source[:-1] + bytes([source[-1] ^ 1])))
        again, _ = B.generate(PREPARED)
        self.assertEqual(oslv.encode([(n, o, w, i) for (n, _), (o, w, i) in again.items()]),
                         (VISUAL / 'butterfly-ptbr.oslv').read_bytes())


@unittest.skipUnless((EUROPE / 'Odin2_OR_UK.CPK').is_file(), 'Europe 1.01 not available')
class PressStart(unittest.TestCase):
    ARCHIVES = (EUROPE / 'Odin2_OR_UK.CPK', EUROPE / 'Odin2_EU_PATCH1_OR_UK.CPK')

    def test_layout_draws_the_painted_slot_and_only_it_changes(self):
        import ftex
        import press_start as P
        import visuals
        for archive in self.ARCHIVES:
            with self.subTest(archive=archive.name):
                atlas = read(archive, 'op_press_start.ftx')
                layout = M.Mbs(apply('press-start-ptbr.oslv', 'op_press_start.mbs', read(archive, 'op_press_start.mbs')))
                painted = apply('press-start-ptbr.oslv', 'op_press_start.ftx', atlas)
                self.assertIsNotNone(painted)
                slot = P.slot_of(layout)
                before = visuals.decode(atlas, ftex.textures(atlas)[0])
                after = visuals.decode(painted, ftex.textures(painted)[0])
                diff = np.any(before != after, axis=-1)
                self.assertTrue(diff[slot[2]:slot[4], slot[1]:slot[3]].any())
                diff[slot[2] // 4 * 4:-(-slot[4] // 4) * 4, slot[1] // 4 * 4:-(-slot[3] // 4) * 4] = False
                self.assertFalse(diff.any())

    def test_row_has_the_official_size_and_the_italian_row_is_kept(self):
        import ftex
        import press_start as P
        import visuals
        archive = self.ARCHIVES[1]
        atlas = read(archive, 'op_press_start.ftx')
        painted = apply('press-start-ptbr.oslv', 'op_press_start.ftx', atlas)
        layout = M.Mbs(apply('press-start-ptbr.oslv', 'op_press_start.mbs', read(archive, 'op_press_start.mbs')))
        spanish = P.slot_of(M.Mbs(read(archive.with_name(archive.name.replace('_UK', '_ES')), 'op_press_start.mbs')))
        italian = P.slot_of(M.Mbs(read(archive.with_name(archive.name.replace('_UK', '_IT')), 'op_press_start.mbs')))
        row = P.slot_of(layout)
        self.assertNotEqual(row, italian)
        self.assertGreaterEqual(row[4] - row[2], 0.9 * (spanish[4] - spanish[2]))
        self.assertGreater(row[3] - row[1], spanish[3] - spanish[1])
        before = visuals.decode(atlas, ftex.textures(atlas)[0])
        after = visuals.decode(painted, ftex.textures(painted)[0])
        t, x0, y0, x1, y1 = italian
        self.assertFalse(np.any(before[y0:y1, x0:x1] != after[y0:y1, x0:x1]))

    def test_single_row_atlases_of_every_build_show_the_portuguese_row(self):
        import ftex
        import press_start as P
        import visuals
        for build, archive, atlas_name, layout_name in P.SINGLE_ROWS:
            path = PREPARED / build / archive
            if not path.is_file():
                continue
            with self.subTest(build=build, layout=layout_name):
                atlas = read(path, atlas_name)
                source = read(path, layout_name)
                layout = M.Mbs(apply('press-start-ptbr.oslv', layout_name, source))
                english = P.slot_of(M.Mbs(source))
                row = P.slot_of(layout)
                self.assertNotEqual(row, english)
                painted = apply('press-start-ptbr.oslv', atlas_name, atlas)
                after = visuals.decode(painted, ftex.textures(painted)[row[0]])
                self.assertTrue((after[row[2]:row[4], row[1]:row[3], 3] > 128).any())
                old_w = T.quad_box(M.Mbs(source).geom(M.Mbs(source).layer_keys(0)[0]['geom']))
                new_w = T.quad_box(layout.geom(layout.layer_keys(0)[0]['geom']))
                self.assertGreater(new_w[2] - new_w[0], 1.3 * (old_w[2] - old_w[0]))
                self.assertAlmostEqual(new_w[3], old_w[3], places=3)

    def test_other_sources_are_refused_and_output_is_deterministic(self):
        import press_start as P
        source = read(self.ARCHIVES[0], 'op_press_start.ftx')
        target = next(t for t in targets('press-start-ptbr.oslv') if t['name'] == 'op_press_start.ftx')
        self.assertIsNone(oslv.apply(target, source[:-1] + bytes([source[-1] ^ 1])))
        again, _ = P.generate(PREPARED)
        self.assertEqual(oslv.encode([(n, o, w, i) for (n, _), (o, w, i) in again.items()]),
                         (VISUAL / 'press-start-ptbr.oslv').read_bytes())


def plain(text):
    return ''.join(c for c in unicodedata.normalize('NFD', text) if not unicodedata.combining(c) and not c.isspace())


def circumflex_problems(layout, reader, layer):
    """What is wrong with the ô of the layer's Bônus line: it must be one upturned v centred above the o, without the
    acute or grave strokes that once drew a caron there."""
    keys = [k['index'] for k in layout.layer_keys(layer)]
    letters = [(k, reader.char(layout, k), T.quad_box(layout.geom(layout.key(k)['geom']))) for k in keys]
    b = next(i for i, (_, ch, _) in enumerate(letters) if ch == 'B')
    o = next(box for _, ch, box in letters[b:] if ch == 'o')
    above = [(k, ch, box) for k, ch, box in letters
             if box[0] < o[2] and box[2] > o[0] and box[3] <= o[1] + 0.5 * (o[3] - o[1]) and box[1] < o[1]]
    problems = []
    if [ch for _, ch, _ in above] != ['v']:
        return [f'marks over the o: {[ch for _, ch, _ in above]}']
    k, _, box = above[0]
    key = layout.key(k)
    if T.turn(layout.part_uv(key['part']), layout.geom(key['geom'])) != ((-1, 0), (0, -1)):
        problems.append('the v is not upturned')
    if abs((box[0] + box[2]) / 2 - (o[0] + o[2]) / 2) > 0.12 * (o[2] - o[0]):
        problems.append('not centred over the o')
    if box[3] > o[1] + 0.05 * (o[3] - o[1]):
        problems.append('touches the o')
    return problems


@unittest.skipUnless((EUROPE / 'Odin2_EU_PATCH1_OR_UK.CPK').is_file(), 'Europe 1.01 not available')
class ClassicInterface(unittest.TestCase):
    PORTUGUESE = {'SUCCESS': ['SUCESSO'], 'STATUS': ['Equipamento', 'Pontos transferidos'],
                  'CLEAR_B_1': ['Tempo de conclusão'], 'CLEAR_B_3': ['Tempo de conclusão'], 'CLEAR_C_1': ['Bônus de dano'],
                  'CLEAR_D_1': ['Pontos transferidos'], 'CLEAR_F_1': ['Dano'], 'DETA_BASE': ['Última partida'],
                  'DETA_SERVER': ['Última partida'], 'L_C1': ['CARREGANDO'], 'EXIT_UNDER': ['SAIR'], 'EXIT_UP': ['SAIR'],
                  'EXIT_LEFT': ['SAIR'], 'EXIT_RIGHT': ['SAIR']}
    ENGLISH = {'SUCCESS', 'Equipment', 'ClearTime', 'ClearTmie', 'DamageBonus', 'CarriedOver', 'CarryOverScore', 'Dmg',
               'LastPlay', 'NOWLOADING', 'EXIT', 'NODATA', 'SAVE', 'SHOP'}

    @classmethod
    def setUpClass(cls):
        import make_visuals as V
        archive = EUROPE / 'Odin2_EU_PATCH1_OR_UK.CPK'
        cls.layout = M.Mbs(apply('interface-ptbr.oslv', 'Interface.mbs', read(archive, 'Interface.mbs')))
        cls.reader = V.Reader(V.GLYPHS, V.VARIANTS)

    def anim_lines(self, name):
        first, count = next((f, c) for n, f, c in self.layout.anims() if n == name)
        layers = {self.layout.segment_layer(s) for ch in range(first, first + count) for s in self.layout.channel_segments(ch)}
        return [plain(T.line_text(self.layout, line, self.reader))
                for layer in sorted(layers) if layer < self.layout.count(M.LAYERS)
                for line in T.lines(self.layout, self.reader, [layer])]

    def test_every_local_classic_layout_takes_the_patch(self):
        for build, archive in (('CUSA05083-1.00', 'Odin2_OR_US.CPK'), ('CUSA05262-1.00', 'Odin2_OR_UK.CPK'),
                               ('CUSA05262-1.01', 'Odin2_OR_UK.CPK'), ('CUSA05262-1.01', 'Odin2_EU_PATCH1_OR_UK.CPK')):
            path = PREPARED / build / archive
            if path.is_file():
                with self.subTest(build=build, archive=archive):
                    self.assertIsNotNone(apply('interface-ptbr.oslv', 'Interface.mbs', read(path, 'Interface.mbs')))

    def test_labels_read_in_portuguese_and_no_english_is_left(self):
        for name, words in self.PORTUGUESE.items():
            with self.subTest(anim=name):
                lines = self.anim_lines(name)
                for word in words:
                    self.assertIn(plain(word), lines)
                self.assertFalse(self.ENGLISH & set(lines), lines)

    def test_bonus_de_dano_has_a_circumflex(self):
        first, count = next((f, c) for n, f, c in self.layout.anims() if n == 'CLEAR_C_1')
        layer = self.layout.segment_layer(next(iter(self.layout.channel_segments(first + 1))))
        lines = [plain(T.line_text(self.layout, line, self.reader)) for line in T.lines(self.layout, self.reader, [layer])]
        self.assertIn('Bonusdedano', lines)
        self.assertEqual(circumflex_problems(self.layout, self.reader, layer), [])

    def test_refined_bonus_rank_has_a_circumflex(self):
        import make_visuals as V
        layout = M.Mbs(apply('interface-ptbr.oslv', 'GUI/REHD_Interface.mbs',
                             read(EUROPE / 'Odin2_EU_PATCH1_RE_UK.CPK', 'GUI/REHD_Interface.mbs')))
        reader = V.Reader(V.GLYPHS, V.VARIANTS)
        layers = [l.layer for layer in range(layout.count(M.LAYERS)) for l in T.lines(layout, reader, [layer])
                  if plain(T.line_text(layout, l, reader)).startswith('BonusRank')]
        self.assertTrue(layers)
        for layer in layers:
            self.assertEqual(circumflex_problems(layout, reader, layer), [], layer)

    def test_no_data_slot_draws_one_line_in_both_states(self):
        for name in ('DETA_NO', 'DETA_NO_00'):
            with self.subTest(anim=name):
                self.assertEqual(self.anim_lines(name), ['SEMDADOS'])

    def test_act_titles_are_set_in_the_settled_layers(self):
        import stage_acts as S
        expected = {'L_VALKYRIE': 'VALQUIRIA', 'L_0_0': 'Ato', 'L_A_A': 'Preludio', 'L_A_B': 'Epilogo',
                    'L_REEL': 'RODADODESTINO', 'L_POOKA_00': 'OPRINCIPEPOOKA'}
        for name, text in expected.items():
            with self.subTest(anim=name):
                channel = S.word_channels(self.layout, name)[0]
                _, settled, _ = S.channel_layers(self.layout, channel)
                keys = sorted(self.layout.layer_keys(settled), key=lambda k: T.quad_box(self.layout.geom(k['geom']))[0])
                letters = [self.reader.char(self.layout, k['index']) for k in keys]
                self.assertEqual(''.join(c for c in letters if c and len(c) == 1).casefold(), text.casefold())
        channel = S.word_channels(self.layout, 'L_A_3')[1]
        keys = self.layout.layer_keys(S.channel_layers(self.layout, channel)[1])
        letters = [self.reader.char(self.layout, k['index']) for k in keys]
        self.assertTrue({'C', 'a', 'p'} <= set(letters) and 'h' not in letters, letters)

    def test_loading_arc_spells_carregando_on_one_circle(self):
        import arc_text as A
        found = []
        for layer in range(self.layout.count(M.LAYERS)):
            keys = A.letters(self.layout, self.reader, layer)
            if keys and len(keys) >= 4 and ''.join(c for _, c in keys) in ('CARREGANDO', 'NOWLOADING'):
                found.append(keys)
        self.assertEqual([''.join(c for _, c in keys) for keys in found], ['CARREGANDO'])
        centres = [A._centre(self.layout.geom(self.layout.key(k)['geom'])) for k, _ in found[0]]
        cx, cy, r = A._circle(centres)
        self.assertLess(max(abs(np.hypot(x - cx, y - cy) - r) for x, y in centres), 0.05 * r)


JAPAN = PREPARED / 'CUSA01290-1.02'


@unittest.skipUnless((JAPAN / 'Odin2_ORHD.CPK').is_file() and (EUROPE / 'Odin2_EU_PATCH1_OR_UK.CPK').is_file(),
                     'Japan 1.02 or Europe 1.01 not available')
class JapanInterface(unittest.TestCase):
    """Japan has no Spanish or English slot: its English-spelled interface is respelled from its own layouts."""

    def lines_of(self, archive, layout_name, atlas_name, anim):
        import make_visuals as V
        reference = V.atlas(read(EUROPE / 'Odin2_EU_PATCH1_OR_UK.CPK', 'Interface.ftx'))
        source = read(archive, layout_name)
        layout = M.Mbs(apply('interface-ptbr.oslv', layout_name, source))
        found = V.matches(V.GLYPHS, reference, M.Mbs(source), V.atlas(read(archive, atlas_name)))
        reader = V.Reader({found[k][0]: ch for k, ch in V.GLYPHS.items() if k in found}, V.VARIANTS)
        first, count = next((f, c) for n, f, c in layout.anims() if n == anim)
        layers = {layout.segment_layer(s) for ch in range(first, first + count) for s in layout.channel_segments(ch)}
        return [plain(T.line_text(layout, line, reader)) for layer in sorted(layers) if layer < layout.count(M.LAYERS)
                for line in T.lines(layout, reader, [layer])]

    def test_classic_exit_and_success(self):
        for anim in ('EXIT_UNDER', 'EXIT_UP', 'EXIT_LEFT', 'EXIT_RIGHT'):
            lines = self.lines_of(JAPAN / 'Odin2_ORHD.CPK', 'Interface.mbs', 'Interface.ftx', anim)
            self.assertIn('SAIR', lines)
            self.assertNotIn('EXIT', lines)
        self.assertEqual(self.lines_of(JAPAN / 'Odin2_ORHD.CPK', 'Interface.mbs', 'Interface.ftx', 'SUCCESS'), ['SUCESSO'])

    def test_refined_exit_marker(self):
        lines = self.lines_of(JAPAN / 'Odin2_RE.CPK', 'GUI/REHD_Interface.mbs', 'GUI/HD_Interface.ftx', 'EXIT_UNDER')
        self.assertIn('SAIR', lines)
        self.assertFalse({'EXIT', 'IN'} & set(lines), lines)

    def test_both_japanese_versions_take_the_patch(self):
        for build in ('CUSA01290-1.00', 'CUSA01290-1.02'):
            for archive, entry in (('Odin2_ORHD.CPK', 'Interface.mbs'), ('Odin2_RE.CPK', 'GUI/REHD_Interface.mbs')):
                path = PREPARED / build / archive
                if path.is_file():
                    with self.subTest(build=build, entry=entry):
                        self.assertIsNotNone(apply('interface-ptbr.oslv', entry, read(path, entry)))


@unittest.skipUnless((EUROPE / 'Odin2_RE_UK.CPK').is_file(), 'Europe 1.01 not available')
class WorldMapBanner(unittest.TestCase):
    def test_only_the_caption_blocks_change(self):
        import ftex
        import map_title as W
        import visuals
        for archive, entry in ((EUROPE / 'Odin2_RE_UK.CPK', 'GUI/HD_worldmap.ftx'), (EUROPE / 'Odin2_OR_UK.CPK', 'worldmap.ftx')):
            with self.subTest(entry=entry):
                source = read(archive, entry)
                painted = apply('worldmap-ptbr.oslv', entry, source)
                self.assertIsNotNone(painted)
                for i, (a, b) in enumerate(zip(ftex.textures(source), ftex.textures(painted))):
                    if i != W.TEXTURE:
                        self.assertEqual(source[a['data']:a['data'] + a['size']], painted[b['data']:b['data'] + b['size']])
                        continue
                    diff = np.any(visuals.decode(source, a) != visuals.decode(painted, b), axis=-1)
                    x0, y0, x1, y1 = W.CAPTION
                    self.assertTrue(diff[y0:y1, x0:x1].any())
                    diff[y0 // 4 * 4:-(-y1 // 4) * 4, x0 // 4 * 4:-(-x1 // 4) * 4] = False
                    self.assertFalse(diff.any())

    def test_every_local_build_is_patched_and_output_is_deterministic(self):
        import map_title as W
        found, report, _ = W.generate(PREPARED)
        self.assertFalse([r for r in report if 'BLOCKED' in r[-1]], report)
        self.assertEqual(oslv.encode([(n, o, w, []) for (n, _, _), (o, w) in found.items()]),
                         (VISUAL / 'worldmap-ptbr.oslv').read_bytes())


class Wording(unittest.TestCase):
    EUROPEAN = ('Pulsa', 'Pulse', 'Prima', 'Premi', 'Press')

    def test_portuguese_text_and_labels_use_brazilian_prompts(self):
        import json
        import re
        pattern = re.compile(r'\b(' + '|'.join(self.EUROPEAN) + r')\b')
        found = []
        for sheet in (ROOT / 'localization' / 'pt-BR' / 'sheets').glob('*.json'):
            found += [e['pt_br'] for e in json.loads(sheet.read_text(encoding='utf-8'))['entries'] if pattern.search(e['pt_br'])]
        with open(VISUAL / 'labels.tsv', encoding='utf-8', newline='') as f:
            found += [row['pt_br'] for row in csv.DictReader(f, delimiter='\t') if pattern.search(row['pt_br'])]
        self.assertEqual(found, [])

    def test_vanir_objective_reads_expulse(self):
        import json
        texts = [e['pt_br'] for e in json.loads((ROOT / 'localization' / 'pt-BR' / 'sheets' / 'story.json').read_text(
            encoding='utf-8'))['entries']]
        self.assertIn('Expulse os Vanir e siga para o Caldeirão.', texts)
        self.assertFalse([t for t in texts if 'Repila' in t])

    def test_no_portuguese_text_names_the_english_exit_marker(self):
        import json
        found = [e['pt_br'] for sheet in (ROOT / 'localization' / 'pt-BR' / 'sheets').glob('*.json')
                 for e in json.loads(sheet.read_text(encoding='utf-8'))['entries'] if 'EXIT' in e['pt_br']]
        self.assertEqual(found, [])


@unittest.skipUnless((EUROPE / 'Odin2_EU_PATCH1_RE_UK.CPK').is_file(), 'Europe 1.01 not available')
class TitleSecondary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, str(ROOT / 'scripts' / 'title_exit'))
        import make_dcpx
        import make_title_exit as X
        cls.X = X
        cls.base = X.dcpx_targets((ROOT / 'launcher' / 'title' / 'title-exit.dcpx').read_bytes())
        cls.pt = X.dcpx_targets((VISUAL / 'title-exit-ptbr.dcpx').read_bytes())

    def final(self, archive, name):
        source = read(archive, name)
        layered = self.X.apply_dcpx(self.base, name, source)
        target = self.pt.get((name.lower(), len(layered), zlib.crc32(layered)))
        self.assertIsNotNone(target, name)
        out = bytearray(layered)
        for offset, _, after in target[1]:
            out[offset:offset + 16] = after
        return layered, bytes(out)

    def test_refined_layers_draw_the_portuguese_slots(self):
        import title_secondary as S
        archive = EUROPE / 'Odin2_EU_PATCH1_RE_UK.CPK'
        _, layout_blob = self.final(archive, 'GUI/REHD_title.mbs')
        layout = M.Mbs(layout_blob)
        spanish = M.Mbs(read(EUROPE / 'Odin2_EU_PATCH1_RE_ES.CPK', 'GUI/REHD_title.mbs'))
        slots = S.slots_of('refined', M.Mbs(self.X.apply_dcpx(self.base, 'GUI/REHD_title.mbs', read(archive, 'GUI/REHD_title.mbs'))),
                           spanish, 'ES')
        for layer in (7, 23, 24):
            drawn = S.drawn_rects(layout, layer)
            self.assertNotIn(S.COPYRIGHT, drawn)
            self.assertNotIn(S.PRESS_LEFT, drawn)
            self.assertIn(slots['copyright'], drawn)
        for layer in (23, 24):
            self.assertTrue(any(self.is_press_row(r, slots['press']) for r in S.drawn_rects(layout, layer)))

    @staticmethod
    def is_press_row(rect, slot):
        return rect != slot and rect[3] - rect[1] > slot[3] - slot[1] and rect[4] - rect[2] >= 0.9 * (slot[4] - slot[2])

    def test_atlas_changes_only_in_the_painted_slots_over_the_menu_rows(self):
        import ftex
        import title_secondary as S
        import visuals
        archive = EUROPE / 'Odin2_EU_PATCH1_RE_UK.CPK'
        layered, final = self.final(archive, 'GUI/HD_title.ftx')
        before = [visuals.decode(layered, t) for t in ftex.textures(layered)]
        after = [visuals.decode(final, t) for t in ftex.textures(final)]
        spanish = M.Mbs(read(EUROPE / 'Odin2_EU_PATCH1_RE_ES.CPK', 'GUI/REHD_title.mbs'))
        english = M.Mbs(self.X.apply_dcpx(self.base, 'GUI/REHD_title.mbs', read(archive, 'GUI/REHD_title.mbs')))
        slots = S.slots_of('refined', english, spanish, 'ES')
        t, x0, y0, x1, y1 = slots['copyright']
        self.assertTrue(np.any(before[t][y0:y1, x0:x1] != after[t][y0:y1, x0:x1]))
        layout = M.Mbs(self.final(archive, 'GUI/REHD_title.mbs')[1])
        row = next(r for r in S.drawn_rects(layout, 23) if self.is_press_row(r, slots['press']))
        t, x0, y0, x1, y1 = row
        self.assertFalse(np.any(before[t][y0:y1, x0:x1, 3]), 'the press row is painted into free atlas rows')
        self.assertTrue(np.any(after[t][y0:y1, x0:x1, 3] > 128))
        self.assertFalse(np.any(before[1] != after[1]))

    def test_classic_layout_draws_the_portuguese_trademark_and_press_slots(self):
        import title_secondary as S
        archive = EUROPE / 'Odin2_EU_PATCH1_OR_UK.CPK'
        _, blob = self.final(archive, 'title.mbs')
        layout = M.Mbs(blob)
        spanish = M.Mbs(read(EUROPE / 'Odin2_EU_PATCH1_OR_ES.CPK', 'title.mbs'))
        english = M.Mbs(self.X.apply_dcpx(self.base, 'title.mbs', read(archive, 'title.mbs')))
        slots = S.slots_of('classic', english, spanish, 'ES')
        drawn = S.drawn_rects(layout, 7)
        self.assertNotIn(S.TRADEMARK, drawn)
        self.assertIn(slots['copyright'], drawn)
        self.assertTrue(any(self.is_press_row(r, slots['press']) for r in drawn))

    def test_wrong_source_is_refused(self):
        archive = EUROPE / 'Odin2_EU_PATCH1_RE_UK.CPK'
        source = bytearray(read(archive, 'GUI/REHD_title.mbs'))
        source[100] ^= 1
        self.assertIsNone(self.X.apply_dcpx(self.base, 'GUI/REHD_title.mbs', bytes(source)))


if __name__ == '__main__':
    unittest.main(verbosity=1)
