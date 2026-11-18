#!/usr/bin/env python3
"""Portuguese (Brazil) visual coverage: the OSLV deltas and the layout editor, official localization parity
(localization/pt-BR/visual/coverage.tsv against official-resources.tsv), the screenshot labels, and, when the European
game is prepared, the shipped interface delta applied to the real layouts and read back.

    python -I tests/visual_parity_tests.py
"""
import csv
import hashlib
import json
import struct
import sys
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts' / 'localization'))
sys.path.insert(0, str(ROOT / 'scripts' / 'prompts'))
import mbs as M  # noqa: E402
import oslv  # noqa: E402
import visual_parity as P  # noqa: E402

VISUAL = ROOT / 'localization' / 'pt-BR' / 'visual'
PREPARED = ROOT / 'game' / 'prepared' / 'CUSA05262-1.01'
INTERFACE = (('Odin2_EU_PATCH1_RE_UK.CPK', 'GUI/REHD_Interface.mbs'), ('Odin2_EU_PATCH1_OR_UK.CPK', 'Interface.mbs'))
# Screenshot words: English as the game draws them, Portuguese the delta must draw instead.
SCREENSHOT = {'TALK': 'FALAR', 'EXIT': 'SAIR', 'BAG': 'BOLSA', 'Level': 'Nível', 'Gold': 'Ouro', 'PlayTime': 'Tempo de jogo',
              'DATA': 'DADOS', 'TotalTime': 'Tempo total', 'Date': 'Data', 'Difficulty': 'Dificuldade', 'NO DATA': 'SEM DADOS',
              'YES': 'SIM', 'NO': 'NÃO'}
TITLE_LAYOUTS = ('Odin2_RE_UK.CPK:GUI/REHD_title.mbs', 'Odin2_EU_PATCH1_RE_UK.CPK:GUI/REHD_title.mbs',
                 'Odin2_OR_UK.CPK:title.mbs', 'Odin2_EU_PATCH1_OR_UK.CPK:title.mbs')


def letters(text):
    """The base letters a layout reads back as: accents are separate keys, and the stem of í stands at x-height."""
    import unicodedata
    text = text.replace('í', '').replace(' ', '').replace(',', '')
    return ''.join(c for c in unicodedata.normalize('NFD', text) if not unicodedata.combining(c))


def tsv(path):
    with open(path, encoding='utf-8', newline='') as f:
        return list(csv.DictReader(f, delimiter='\t'))


class Delta(unittest.TestCase):
    def test_round_trip_and_fingerprint(self):
        source = bytes(range(256)) * 64
        result = source[:1000] + b'new records!' * 3 + source[1000:2000] + b'X' * 5 + source[2005:]
        blob = oslv.encode([('GUI/a.mbs', source, result, [(1000, b'new records!' * 3)])])
        target = oslv.parse(blob)[0]
        self.assertEqual(oslv.apply(target, source), result)
        self.assertIsNone(oslv.apply(target, source[:-1] + b'\1'))
        self.assertTrue(all(op[0] == 'copy' or len(op[1]) < 64 for op in target['ops']))

    def test_layout_grows_by_appending(self):
        layout = M.Mbs(synthetic_layout())
        first = layout.append(M.KEYS, [bytes(16)] * 2)
        self.assertEqual(first, 3)
        grown = M.Mbs(layout.bytes())
        self.assertEqual(grown.count(M.KEYS), 5)
        self.assertEqual(struct.unpack_from('<I', grown.data, 4)[0], len(grown.data) - M.SIZE_BIAS)
        self.assertEqual(grown.offset(M.ANIMS), layout.offset(M.ANIMS))
        self.assertEqual(bytes(grown.record(M.ANIMS, 0)), bytes(M.Mbs(synthetic_layout()).record(M.ANIMS, 0)))
        ops = M.delta_ops(synthetic_layout(), layout.bytes(), layout.inserts)
        self.assertEqual(M.apply_ops(synthetic_layout(), ops), layout.bytes())


def synthetic_layout():
    """A minimal FMBS: 3 keys, then one animation record after them, footer."""
    head = bytearray(0x120)
    head[:4] = b'FMBS'
    keys = bytes(range(48))
    anim = b'A' * 48
    body = keys + anim
    data = head + body + b'FEOC' + bytes(12)
    struct.pack_into('<I', data, 4, len(data) - M.SIZE_BIAS)
    struct.pack_into('<I', data, 0x50, 3)
    struct.pack_into('<H', data, 0x64, 1)
    struct.pack_into('<Q', data, 0xb0 + 8 * M.KEYS, 0x120)
    struct.pack_into('<Q', data, 0xb0 + 8 * M.ANIMS, 0x120 + 48)
    return bytes(data)


class Parity(unittest.TestCase):
    def test_every_official_resource_is_explained(self):
        problems, table, _ = P.check(tsv(P.INVENTORY), tsv(P.COVERAGE), P.patch_targets(), P.corpus())
        self.assertEqual(problems, [])
        self.assertGreater(sum(table.values()), 700)

    def test_screenshot_layouts_are_translated_by_a_real_target(self):
        rows = {f"{r['archive']}:{r['path']}": r for r in tsv(P.INVENTORY)}
        targets = P.patch_targets()
        for archive, path in INTERFACE:
            row = rows[f'{archive}:{path}']
            self.assertIn((path.lower(), int(row['uk_size']), int(row['uk_crc32'], 16)), targets, path)
            self.assertEqual(P.status_of(row, tsv(P.COVERAGE))['status'], 'PT_BR_TRANSLATED')

    def test_demon_lords_castle_card_is_translated(self):
        rows = {f"{r['archive']}:{r['path']}": r for r in tsv(P.INVENTORY)}
        row = rows['Odin2_RE_UK.CPK:GUI/HD_StageName12.ftx']
        self.assertEqual(P.status_of(row, tsv(P.COVERAGE))['status'], 'PT_BR_TRANSLATED')
        self.assertIn((row['path'].lower(), int(row['uk_size']), int(row['uk_crc32'], 16)), P.patch_targets())

    def test_title_rows_are_translated_over_the_exit_layer(self):
        rows = {f"{r['archive']}:{r['path']}": r for r in tsv(P.INVENTORY)}
        targets = P.patch_targets()
        for name in TITLE_LAYOUTS:
            row = rows[name]
            self.assertEqual(P.status_of(row, tsv(P.COVERAGE))['status'], 'PT_BR_TRANSLATED', name)
            hit = targets.get((row['path'].lower(), int(row['uk_size']), int(row['uk_crc32'], 16)))
            self.assertIsNotNone(hit, name)
            self.assertIn('title-exit-ptbr.dcpx', hit)

    def test_labels_match_the_corpus(self):
        pt, en = {}, {}
        for f in ('msgsheet', 'story', 'archive'):
            for e in json.loads((ROOT / 'localization' / 'pt-BR' / 'sheets' / f'{f}.json').read_text('utf-8'))['entries']:
                pt[e['source_sha256']] = e['pt_br']
        labels = {r['source']: r['pt_br'] for r in tsv(VISUAL / 'labels.tsv')}
        for english in ('Difficulty', 'Gold', 'Bag', 'Easy', 'Hard', 'Heroic', 'Score', 'Cancel', 'Sell', 'Special',
                        'Alchemy'):
            corpus = pt.get(hashlib.sha256(english.encode()).hexdigest())
            self.assertIsNotNone(corpus, english)
            self.assertEqual(labels[english], corpus, english)
        glossary = {r['source']: r['pt_br'] for r in tsv(ROOT / 'localization' / 'pt-BR' / 'glossary.tsv')}
        self.assertEqual(labels['Level'], glossary['Level'])
        self.assertEqual(labels['Item Box'], glossary['Item Box'])


class RealInterface(unittest.TestCase):
    def setUp(self):
        if not (PREPARED / INTERFACE[0][0]).is_file():
            self.skipTest('DATA UNAVAILABLE: CUSA05262 1.01 is not prepared')
        try:
            import numpy  # noqa: F401
        except ImportError:
            self.skipTest('numpy not available')
        import cpk
        import make_visuals as V
        import visuals
        visuals.use_native_reader()
        self.V, self.cpk = V, cpk
        self.targets = [t for f in sorted(VISUAL.glob('*.oslv')) for t in oslv.parse(f.read_bytes())]

    def read(self, archive, path):
        c = self.cpk.Cpk(PREPARED / archive, original=True)
        try:
            return c.read(next(e for e in c.entries if e['path'].lower() == path.lower()))
        finally:
            c.f.close()

    def words(self, data):
        import layout_text as T
        layout = M.Mbs(data)
        reader = self.V.Reader(self.V.GLYPHS, self.V.VARIANTS)
        return {letters(T.line_text(layout, line, reader)) for line in T.lines(layout, reader)}

    def test_refined_interface_reads_portuguese(self):
        source = self.read(*INTERFACE[0])
        target = next(t for t in self.targets if t['name'] == INTERFACE[0][1] and t['crc'] == zlib.crc32(source))
        result = oslv.apply(target, source)
        self.assertIsNotNone(result)
        before, after = self.words(source), self.words(result)
        for english, portuguese in SCREENSHOT.items():
            if letters(english) in before:
                self.assertNotIn(letters(english), after, english)
                self.assertIn(letters(portuguese), after, portuguese)
        for english in ('TALK', 'EXIT', 'Level', 'Gold', 'PlayTime', 'TotalTime', 'Date', 'Difficulty', 'NODATA', 'YES', 'NO'):
            self.assertIn(english, before, f'{english}: the English layout no longer spells it')

    def test_classic_interface_has_a_target(self):
        source = self.read(*INTERFACE[1])
        target = next(t for t in self.targets if t['name'] == INTERFACE[1][1] and t['crc'] == zlib.crc32(source))
        self.assertIsNotNone(oslv.apply(target, source))

    def test_other_languages_are_never_targets(self):
        keys = {(t['name'].lower(), t['size'], t['crc']) for t in self.targets}
        for language in ('ES', 'DE', 'FR', 'IT'):
            for archive, path in INTERFACE:
                data = self.read(archive.replace('_UK', '_' + language), path)
                self.assertNotIn((path.lower(), len(data), zlib.crc32(data)), keys, language)


if __name__ == '__main__':
    unittest.main(verbosity=1)
