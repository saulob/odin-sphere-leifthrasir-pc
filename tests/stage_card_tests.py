#!/usr/bin/env python3
"""Portuguese (Brazil) stage and location cards: the title table against the corpus and glossary, the layout fitter on a
synthetic glyph pool, and, on the prepared games present, the shipped delta applied to the real card packs.

    python -I tests/stage_card_tests.py
"""
import csv
import hashlib
import json
import sys
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts' / 'localization'))
sys.path.insert(0, str(ROOT / 'scripts' / 'prompts'))
import oslv  # noqa: E402

try:
    import numpy as np
    import stage_cards as SC
    import visual_font as VF
except ImportError as missing:
    raise SystemExit(f'numpy, scipy and Pillow are required ({missing}); run tools/setup.ps1 -InstallPython')

VISUAL = ROOT / 'localization' / 'pt-BR' / 'visual'
TABLE = VISUAL / 'stage-cards.tsv'
PREPARED = ROOT / 'game' / 'prepared'
SAME_AS_ENGLISH = {'03', '54', '59'}


def tsv(path):
    with open(path, encoding='utf-8', newline='') as f:
        return list(csv.DictReader(f, delimiter='\t'))


def corpus_pairs():
    pt = {}
    for kind in ('msgsheet', 'story'):
        for e in json.loads((ROOT / 'localization' / 'pt-BR' / 'sheets' / f'{kind}.json').read_text('utf-8'))['entries']:
            pt[e['source_sha256']] = e['pt_br']
    return pt


def sha(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def toy_pool():
    """A pool of box glyphs: enough to exercise the layout without any game art."""
    pool = VF.VisualGlyphPool('toy', cap_ratio=2.4)
    pool.gaps = {('low', 'low'): 0.1, ('cap', 'low'): 0.03, ('low', 'cap'): 0.1}
    for char in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ,':
        height = 2.4 * 40 if char.isupper() else 40
        rgba = np.zeros((int(height) + 12, 36 + 12, 4), np.uint8)
        rgba[6:6 + int(height), 6:42] = (60, 120, 220, 255)
        pool.add(VF.VisualGlyph(char, rgba, (6, 6, 42, 6 + int(height)), 40.0, 0.0, f'toy {char}'))
    return pool


class Table(unittest.TestCase):
    def test_every_official_card_has_a_title(self):
        cards = {r['card'] for r in tsv(TABLE)}
        official = {r['path'][len('GUI/HD_StageName'):-4] for r in tsv(VISUAL / 'official-resources.tsv')
                    if r['archive'] == 'Odin2_RE_UK.CPK' and r['path'].startswith('GUI/HD_StageName') and r['path'][16:-4]}
        self.assertEqual(official - cards, set())

    def test_titles_use_the_corpus_wording(self):
        pt = corpus_pairs()
        corpus = {}
        for kind in ('msgsheet', 'story'):
            for r in json.loads((ROOT / 'localization' / 'en' / 'sheets' / f'{kind}.json').read_text('utf-8'))['resources']:
                for u in r['units']:
                    corpus.setdefault(' '.join(u.replace('@s(1)', '').split()), []).append(pt.get(sha(u), ''))
        rows = tsv(TABLE)
        for row in rows:
            options = {' '.join(p.replace('@s(1)', '').split()) for p in corpus.get(' '.join(row['english'].replace('Cafe', 'Café').split()), [])}
            if row['basis'] == 'corpus':
                self.assertIn(row['pt_br'], options, row['card'])
        text = ' '.join(r['pt_br'] for r in rows)
        self.assertNotIn('Castelo Antigo', text)
        glossary = {r['source']: r['pt_br'] for r in tsv(ROOT / 'localization' / 'pt-BR' / 'glossary.tsv')}
        for row in rows:
            if 'Old Castle' in row['english']:
                self.assertIn(glossary['Old Castle'], row['pt_br'])
            if 'Sorcerer' in row['english']:
                self.assertIn(glossary['Sorcerer'], row['pt_br'])
            if 'Demon Lord' in row['english']:
                self.assertIn('Lorde Demônio', row['pt_br'])

    def test_corpus_spells_each_location_once(self):
        pt = {}
        for kind in ('msgsheet', 'story'):
            for e in json.loads((ROOT / 'localization' / 'pt-BR' / 'sheets' / f'{kind}.json').read_text('utf-8'))['entries']:
                pt[e['source_sha256']] = e['pt_br']
        for plain in ('Old Castle Terrace', 'Obliterated Ruins'):
            self.assertEqual(pt[sha(plain)], pt[sha('@s(1)' + plain)].replace('@s(1)', ''), plain)

    def test_demon_lords_castle_card(self):
        row = next(r for r in tsv(TABLE) if r['card'] == '12')
        self.assertEqual((row['english'], row['pt_br']), ("Demon Lord's Castle", 'Castelo do Lorde Demônio'))
        official = next(r for r in tsv(VISUAL / 'official-resources.tsv')
                        if r['archive'] == 'Odin2_RE_UK.CPK' and r['path'] == 'GUI/HD_StageName12.ftx')
        key = ('gui/hd_stagename12.ftx', int(official['uk_size']), int(official['uk_crc32'], 16))
        self.assertIn(key, {(t['name'].lower(), t['size'], t['crc']) for t in oslv.parse((VISUAL / 'stage-cards-ptbr.oslv').read_bytes())})

    def test_every_translated_card_has_a_target(self):
        targets = {(t['name'].lower(), t['size'], t['crc']) for t in oslv.parse((VISUAL / 'stage-cards-ptbr.oslv').read_bytes())}
        rows = {r['path']: r for r in tsv(VISUAL / 'official-resources.tsv') if r['archive'] == 'Odin2_RE_UK.CPK'}
        for row in tsv(TABLE):
            if row['card'] in SAME_AS_ENGLISH:
                self.assertEqual(row['english'], row['pt_br'])
                continue
            official = rows[f"GUI/HD_StageName{row['card']}.ftx"]
            self.assertIn((official['path'].lower(), int(official['uk_size']), int(official['uk_crc32'], 16)), targets, row['card'])

    def test_other_languages_cards_are_never_targets(self):
        targets = {(t['name'].lower(), t['size'], t['crc']) for t in oslv.parse((VISUAL / 'stage-cards-ptbr.oslv').read_bytes())}
        for row in tsv(VISUAL / 'official-resources.tsv'):
            if row['archive'] == 'Odin2_RE_UK.CPK' and row['path'].startswith('GUI/HD_StageName') and row['es_crc32']:
                self.assertNotIn((row['path'].lower(), int(row['es_size']), int(row['es_crc32'], 16)), targets)


class Layout(unittest.TestCase):
    def test_title_stays_inside_the_card_and_clear_of_itself(self):
        pool = toy_pool()
        for text in ('Castelo do Lorde', 'Becos da Capital', 'Linha de Frente da Guerra'):
            scale, placed = SC.fit(pool, text)
            self.assertGreaterEqual(scale, SC.SCALE_RANGE[0])
            for p in placed:
                self.assertGreaterEqual(p.box[0], SC.MARGIN)
                self.assertLessEqual(p.box[2], SC.REGION_SIZE[0] - SC.MARGIN)
                self.assertGreaterEqual(p.box[1], SC.MARGIN)
                self.assertLessEqual(p.box[3], SC.REGION_SIZE[1] - SC.MARGIN)
            self.assertFalse(SC.words_touch(placed))

    def test_composition_is_deterministic(self):
        pool = toy_pool()
        first, scale = SC.compose(pool, 'Cozinha Pooka')
        second, again = SC.compose(pool, 'Cozinha Pooka')
        self.assertEqual(scale, again)
        self.assertTrue(np.array_equal(first, second))

    def test_a_title_that_cannot_fit_is_refused(self):
        with self.assertRaises(SC.FitError):
            SC.compose(toy_pool(), 'A' + 'b' * 40)

    def test_missing_letter_is_named(self):
        pool = toy_pool()
        self.assertEqual(pool.missing('Aa1'), ['1'])


class Real(unittest.TestCase):
    def setUp(self):
        if not (PREPARED / 'CUSA05262-1.01' / 'Odin2_RE_UK.CPK').is_file():
            self.skipTest('DATA UNAVAILABLE: CUSA05262 1.01 is not prepared')
        try:
            import visuals
        except ImportError:
            self.skipTest('numpy, scipy or Pillow not available')
        visuals.use_native_reader()
        self.targets = oslv.parse((VISUAL / 'stage-cards-ptbr.oslv').read_bytes())

    def read(self, build, archive, entry):
        import cpk
        pack = cpk.Cpk(PREPARED / build / archive, original=True)
        try:
            return pack.read(pack.find(entry))
        finally:
            pack.f.close()

    def test_europe_card_12_is_repainted_in_its_three_regions_only(self):
        import ftex
        import visuals
        blob = self.read('CUSA05262-1.01', 'Odin2_RE_UK.CPK', 'GUI/HD_StageName12.ftx')
        target = next(t for t in self.targets if t['name'] == 'GUI/HD_StageName12.ftx' and t['crc'] == zlib.crc32(blob))
        patched = oslv.apply(target, blob)
        self.assertIsNotNone(patched)
        self.assertEqual(len(patched), len(blob))
        tex = ftex.textures(blob)[0]
        before, after = visuals.decode(blob, tex).astype(int), visuals.decode(patched, tex).astype(int)
        moved = (before != after).any(axis=-1)
        inside = np.zeros(moved.shape, bool)
        for x0, y0, x1, y1 in (SC.REGION_A, SC.REGION_B, SC.REGION_C):
            inside[max(0, y0 - 4):y1 + 4, max(0, x0 - 4):x1 + 4] = True
        self.assertFalse(moved[~inside].any())
        x0, y0, x1, y1 = SC.REGION_C
        ink = after[y0:y1, x0:x1, 3] > 64
        self.assertGreater(ink.sum(), 5000)
        ys, xs = np.nonzero(ink)
        self.assertGreaterEqual(xs.min(), SC.MARGIN - 4)
        self.assertLessEqual(xs.max(), x1 - x0 - SC.MARGIN + 4)
        old_ink = before[y0:y1, x0:x1, 3] > 64
        self.assertLess((ink & old_ink).sum() / ink.sum(), 0.8)
        other = [t for t in self.targets if t['name'] == 'GUI/HD_StageName12.ftx' and t['crc'] != zlib.crc32(blob)]
        self.assertTrue(all(oslv.apply(t, blob) is None for t in other))

    def test_card_with_the_english_name_is_left_as_it_is(self):
        blob = self.read('CUSA05262-1.01', 'Odin2_RE_UK.CPK', 'GUI/HD_StageName03.ftx')
        target = next(t for t in self.targets if t['name'] == 'GUI/HD_StageName03.ftx' and t['crc'] == zlib.crc32(blob))
        self.assertEqual(oslv.apply(target, blob), blob)

    def test_other_languages_cards_stay_official(self):
        for language in ('ES', 'FR', 'DE', 'IT'):
            blob = self.read('CUSA05262-1.01', f'Odin2_RE_{language}.CPK', 'GUI/HD_StageName12.ftx')
            self.assertFalse(any(t['crc'] == zlib.crc32(blob) and t['size'] == len(blob) for t in self.targets), language)

    def test_usa_and_japan_cards_have_targets(self):
        for build, archive in (('CUSA05083-1.00', 'Odin2_RE_US.CPK'), ('CUSA01290-1.00', 'Odin2_RE.CPK'), ('CUSA01290-1.02', 'Odin2_RE.CPK')):
            if not (PREPARED / build / archive).is_file():
                continue
            blob = self.read(build, archive, 'GUI/HD_StageName12.ftx')
            target = next((t for t in self.targets if t['name'] == 'GUI/HD_StageName12.ftx' and t['crc'] == zlib.crc32(blob)), None)
            self.assertIsNotNone(target, build)
            self.assertIsNotNone(oslv.apply(target, blob), build)


if __name__ == '__main__':
    unittest.main(verbosity=1)
