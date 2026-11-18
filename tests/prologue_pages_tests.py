#!/usr/bin/env python3
"""The chapter prologue pages in Portuguese (Brazil): the Spanish Installation layouts and atlas of Europe read as the
official text, rebuilt by scripts/localization/installation_poems.py, read back glyph by glyph and drawn headless.

    python -I tests/prologue_pages_tests.py
"""
import sys
import tempfile
import unittest
import zlib
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts' / 'localization'))
sys.path.insert(0, str(ROOT / 'scripts' / 'prompts'))
try:
    import numpy as np
    from PIL import Image
    import installation_poems as P
    import mbs as M
    import odin_text as OT
    import oslv
    import visuals
except ImportError as missing:
    raise SystemExit(f'numpy, scipy and Pillow are required ({missing}); run tools/setup.ps1 -InstallPython')

PREPARED = ROOT / 'game' / 'prepared'
EUROPE = ('CUSA05262-1.01', 'CUSA05262-1.00')
SPANISH_GWENDOLYN = ['LA VALQUIRIA', 'El alma refulge con orgullo', 'tras caer en la batalla.', 'Los pájaros poseen',
                     'la creencia de que', 'este es el único camino', 'para lograr la inmortalidad.']
PORTUGUESE_GWENDOLYN = ['A VALQUÍRIA', 'A alma brilha com orgulho', 'ao cair em batalha', 'As aves acreditam que', '',
                        'este é o único meio', 'de ganhar uma alma imortal']
COMPOSED = ('ã', 'õ', 'ç', '-')

if not (PREPARED / EUROPE[0] / 'Odin2_OR_ES.CPK').is_file():
    print('prologue pages: DATA UNAVAILABLE (CUSA05262-1.01 is not prepared)')
    sys.exit(0)

visuals.use_native_reader()
PREP = P.Prepared(PREPARED)
RESULT = P.generate(PREP, EUROPE[0])
TABLE = P.read_table()
ATLAS = visuals.decode(RESULT.atlas[1], P.texture(RESULT.atlas[1]))
SPANISH_ATLAS = visuals.decode(RESULT.atlas[0], P.texture(RESULT.atlas[0]))


def pages():
    """[(layout name, Spanish layout, Portuguese layout, face)] of the Classic and HD pages."""
    out = []
    for name, (old, new) in RESULT.layouts.items():
        out.append((name, M.Mbs(old), M.Mbs(new), RESULT.faces[name]))
    return out


def channels(spanish, anim):
    return [line.channel for line in P.page_lines(spanish, anim)]


def decoded(layout, spanish, anim, face, copy=0, labels=None):
    return P.decode_page(layout, channels(spanish, anim), labels or P.labels_of(face, layout), face, copy)


def expected_lines(anim):
    title, poem = P.portuguese_page(TABLE, anim)
    return title, [line for _, line in poem]


class SpanishPages(unittest.TestCase):
    def test_gwendolyn_decodes_as_the_spanish_text(self):
        for name, spanish, _, face in pages():
            with self.subTest(page=name):
                self.assertEqual(decoded(spanish, spanish, 'VALKYRIE', face, labels=P.spanish_labels(face, spanish)),
                                 SPANISH_GWENDOLYN)

    def test_every_page_decodes_as_its_story_text(self):
        for name, spanish, _, face in pages():
            labels = P.spanish_labels(face, spanish)
            for anim in P.BOOKS:
                title, poem = P.official_page(PREP, EUROPE[0], 'ES', anim)
                with self.subTest(page=name, anim=anim):
                    self.assertEqual(decoded(spanish, spanish, anim, face, labels=labels),
                                     [title] + [line for _, line in poem])

    def test_spanish_glyph_parts_have_one_letter(self):
        for name, spanish, _, face in pages():
            labels = P.spanish_labels(face, spanish)
            with self.subTest(page=name):
                self.assertEqual(len(labels), 59)
                self.assertEqual(len({part for part, _ in labels}), 58)
                for anim in P.BOOKS:
                    for line in P.page_lines(spanish, anim):
                        glyphs, _ = P.glyph_runs(spanish, line.layers[1])
                        missing = [k['part'] for k, _, _ in glyphs if (k['part'], P.orientation(spanish, k)) not in labels]
                        self.assertEqual(missing, [], f'{anim}: unlabelled parts')


class PortuguesePages(unittest.TestCase):
    def test_gwendolyn(self):
        for name, spanish, portuguese, face in pages():
            with self.subTest(page=name):
                self.assertEqual(decoded(portuguese, spanish, 'VALKYRIE', face), PORTUGUESE_GWENDOLYN)

    def test_gwendolyn_four_glow_copies(self):
        for name, spanish, portuguese, face in pages():
            correct = 0
            for copy in range(P.COPIES):
                lines = decoded(portuguese, spanish, 'VALKYRIE', face, copy)
                correct += sum(a == b for a, b in zip(lines, PORTUGUESE_GWENDOLYN))
            with self.subTest(page=name):
                self.assertEqual(correct, 7 * P.COPIES)

    def test_every_book_reads_the_canonical_map(self):
        for name, spanish, portuguese, face in pages():
            for anim in P.BOOKS:
                title, lines = expected_lines(anim)
                for copy in range(P.COPIES):
                    shown = [t for t in decoded(portuguese, spanish, anim, face, copy) if t]
                    with self.subTest(page=name, anim=anim, copy=copy):
                        self.assertEqual(shown[0], title)
                        self.assertEqual(' '.join(shown[1:]), ' '.join(lines))
                        for text in shown[1:]:
                            self.assertTrue(text in lines or any(text == ' '.join(lines[i:j])
                                                                 for i in range(len(lines))
                                                                 for j in range(i + 2, len(lines) + 1)), text)

    def test_titles(self):
        titles = {anim: expected_lines(anim)[0] for anim in P.BOOKS}
        self.assertEqual(titles, {'VALKYRIE': 'A VALQUÍRIA', 'POOKA': 'O PRÍNCIPE POOKA', 'FAIRY': 'TERRA DAS FADAS',
                                  'BLACK': 'A ESPADA NEGRA', 'FATE': 'DESTINO', 'ENDING': 'O FIM DE ERION',
                                  'REEL': 'RODA DO DESTINO'})

    def test_every_record_of_the_map(self):
        self.assertEqual(sum(len(records) for records in TABLE.values()), 41)
        name, spanish, portuguese, face = pages()[0]
        drawn = {anim: [t for t in decoded(portuguese, spanish, anim, face) if t] for anim in P.BOOKS}
        covered = 0
        for anim, container in P.BOOKS.items():
            if container is None:
                continue
            words = ' '.join(drawn[anim])
            for index, record in TABLE[container].items():
                text = ' '.join(OT.visible(line).strip() for line in OT.lines(record))
                if index < P.POEM_FROM:
                    covered += text.upper() == drawn[anim][0] or (container, index) == ('d0_2_1', 0)
                else:
                    covered += text in words
        self.assertEqual(covered, 41)

    def test_screens_keep_their_records(self):
        name, spanish, _, _ = pages()[0]
        plans = RESULT.plans[name]
        for anim in P.BOOKS:
            for screen, screen_plans in plans[anim]:
                with self.subTest(anim=anim, screen=[l.channel for l in screen]):
                    self.assertLessEqual(len(screen_plans), len(screen))
                    self.assertTrue(screen_plans)
        self.assertEqual([p.text for _, ps in plans['FATE'] for p in ps][3:6],
                         ['o fim é simples', 'Mas sabemos que encontraremos', 'nosso verdadeiro destino'])

    def test_spanish_title_card_kept_apart_from_the_book_name(self):
        self.assertEqual(OT.visible(TABLE['d0_2_1'][1]).upper(), 'A ESPADA NEGRA')
        self.assertEqual(expected_lines('BLACK')[0], 'A ESPADA NEGRA')


class Glyphs(unittest.TestCase):
    def test_portuguese_letters_have_glyphs(self):
        needed = P.needed_glyphs({anim: P.portuguese_page(TABLE, anim) for anim in P.BOOKS})
        for name, _, _, face in pages():
            with self.subTest(page=name):
                self.assertEqual(needed - face.atlas, set())
                for ch in ('ç', 'ê', 'ã', 'õ', 'á', 'é', 'í', 'ó', 'ú'):
                    self.assertIn(('body', ch), face.atlas)
                self.assertIn(('title', 'Í'), face.atlas)

    def test_added_glyphs(self):
        added = {(kind, ch): (source, rect) for kind, ch, source, rect, _ in RESULT.added}
        self.assertEqual(set(added), {('body', 'M'), ('body', 'O'), ('body', 'ê'), ('title', 'F'), ('title', 'G'),
                                      ('title', 'M'), ('body', '-'), ('body', 'ã'), ('body', 'ç'), ('body', 'õ')})
        self.assertEqual(added[('body', 'ê')][0], 'FR')
        for ch in COMPOSED:
            self.assertEqual(added[('body', ch)][0], 'composed')

    def test_parts_stay_inside_the_atlas(self):
        height, width = ATLAS.shape[:2]
        for name, spanish, portuguese, _ in pages():
            for i in range(portuguese.count(M.PARTS)):
                x0, y0, x1, y1 = portuguese.part_rect(i)
                with self.subTest(page=name, part=i):
                    self.assertTrue(0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height)

    def test_added_rects_overlap_nothing_and_hold_ink(self):
        name, spanish, portuguese, _ = pages()[0]
        old = [spanish.part_rect(i) for i in range(spanish.count(M.PARTS))]
        new = [rect for _, _, _, rect, _ in RESULT.added]
        for i, a in enumerate(new):
            self.assertGreaterEqual(a[1], P.ATLAS_TOP)
            self.assertTrue(all(a[0] % 4 == 0 and a[1] % 4 == 0 for _ in [0]))
            for b in old + new[:i]:
                self.assertFalse(a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3], (a, b))
            self.assertGreater(int(ATLAS[a[1]:a[3], a[0]:a[2], 3].sum()), 0)

    def test_atlas_keeps_every_spanish_pixel(self):
        self.assertTrue(np.array_equal(ATLAS[:P.ATLAS_TOP - 4], SPANISH_ATLAS[:P.ATLAS_TOP - 4]))
        self.assertEqual(int(SPANISH_ATLAS[P.ATLAS_TOP:, :, 3].sum()), 0)

    def test_composed_glyphs(self):
        face = RESULT.face

        def img(ch):
            g = face.glyph('body', ch)
            return P.upright(ATLAS, g.part, g.pattern)

        heights = [g.h for (kind, ch), g in face.glyphs.items() if kind == 'body' and ch.isalpha() and ch.islower()]
        for ch, base in (('ã', 'a'), ('õ', 'o')):
            g, b = face.glyph('body', ch), face.glyph('body', base)
            composed, plain = img(ch)[..., 3], img(base)[..., 3]
            c_top, c_bottom, _, _ = P.ink(composed)
            p_top, p_bottom, _, _ = P.ink(plain)
            with self.subTest(ch=ch):
                self.assertEqual(composed.shape[0] - c_bottom, plain.shape[0] - p_bottom)
                self.assertLess(c_top, composed.shape[0] - plain.shape[0] + p_top - 1)
                self.assertAlmostEqual(g.bottom, b.bottom, delta=0.01)
                self.assertTrue(b.h < g.h <= 1.6 * b.h)
                self.assertTrue(0.9 * median(heights) <= g.h <= 1.8 * median(heights))
                diff = np.abs(composed[-plain.shape[0]:, :plain.shape[1]].astype(int) - plain.astype(int)).mean()
                self.assertLess(diff, 8)
        c, cedilla = img('c')[..., 3], img('ç')[..., 3]
        self.assertGreater(P.ink(cedilla)[1], P.ink(c)[1] + 2)
        self.assertGreater(face.glyph('body', 'ç').bottom, face.glyph('body', 'c').bottom)
        n, hyphen = img('n')[..., 3], img('-')[..., 3]
        n_top, n_bottom, n_left, n_right = P.ink(n)
        h_top, h_bottom, h_left, h_right = P.ink(hyphen)
        self.assertTrue(n_top < (h_top + h_bottom) / 2 < n_bottom)
        self.assertLess(h_bottom - h_top, 0.5 * (n_bottom - n_top))
        self.assertTrue(0.3 * (n_right - n_left) <= h_right - h_left <= 1.2 * (n_right - n_left))


def final_span(layout, channel):
    """(top, bottom) of a channel's settled layer at its settled transform."""
    segments = list(layout.channel_segments(channel))
    layer = layout.segment_layer(segments[4])
    x, y, sx, sy = layout.segment_transform(segments[4])
    boxes = [P.box(layout, k) for k in P.letter_keys(layout, layer)]
    return y + min(b[1] for b in boxes) * sy, y + max(b[3] for b in boxes) * sy


class Layout(unittest.TestCase):
    def test_lines_fit_and_keep_their_centre(self):
        for name, spanish, portuguese, face in pages():
            for anim, plans in RESULT.plans[name].items():
                for _, screen_plans in plans:
                    for plan in screen_plans:
                        es_centre, _ = P.line_frame(spanish, plan.line)
                        boxes = [b for _, b in plan.items]
                        width = max(b[2] for b in boxes) - min(b[0] for b in boxes)
                        kind = 'title' if plan is plans[0][1][0] else 'body'
                        with self.subTest(page=name, text=plan.text):
                            self.assertLessEqual(width, face.budget[kind] + 0.01)
                            self.assertAlmostEqual((max(b[2] for b in boxes) + min(b[0] for b in boxes)) / 2, es_centre,
                                                   delta=0.01)
                            self.assertGreaterEqual(plan.squeeze, P.MIN_SQUEEZE * face.title_squeeze.get(anim, 1.0)
                                                    if kind == 'title' else P.MIN_SQUEEZE)

    def test_lines_of_a_screen_do_not_overlap(self):
        for name, spanish, portuguese, face in pages():
            for anim in P.BOOKS:
                lines = P.page_lines(spanish, anim)
                for screen in P.screens(lines[1:]):
                    spans = sorted(final_span(portuguese, line.channel) for line in screen
                                   if len(P.letter_keys(portuguese, portuguese.segment_layer(
                                       list(portuguese.channel_segments(line.channel))[4]))) > P.COPIES)
                    with self.subTest(page=name, anim=anim, screen=[l.channel for l in screen]):
                        for a, b in zip(spans, spans[1:]):
                            self.assertLess(a[1], b[0] + 1.0)

    def test_restaged_screens_are_centred(self):
        name, spanish, portuguese, _ = pages()[0]
        lines = P.page_lines(spanish, 'VALKYRIE')
        screen = P.screens(lines[1:])[1]
        before = [P.segment_ys(spanish, line.channel) for line in screen]
        after = P.segment_ys(portuguese, screen[0].channel)
        self.assertEqual(len(after), len(before[0]))
        for s in range(len(after)):
            self.assertAlmostEqual(after[s], (before[0][s] + before[1][s]) / 2, places=3)
        blank = list(portuguese.channel_segments(screen[1].channel))
        self.assertTrue(all(portuguese.segment_layer(s) == 0 for s in blank))
        if screen[1].glow >= 0:
            self.assertTrue(all(portuguese.segment_layer(s) == 0 for s in portuguese.channel_segments(screen[1].glow)))

    def test_timing_colours_and_masks_are_kept(self):
        for name, spanish, portuguese, _ in pages():
            for anim in P.BOOKS:
                for line in P.page_lines(spanish, anim):
                    es_frames = [P.segment_frames(spanish, s) for s in spanish.channel_segments(line.channel)]
                    pt_frames = [P.segment_frames(portuguese, s) for s in portuguese.channel_segments(line.channel)]
                    with self.subTest(page=name, anim=anim, channel=line.channel):
                        self.assertEqual(es_frames, pt_frames)
                        for layer in line.layers:
                            es_keys, pt_keys = spanish.layer_keys(layer), portuguese.layer_keys(layer)
                            if not P.letter_keys(portuguese, layer):
                                continue
                            self.assertEqual({k['colour'] for k in es_keys if P.is_letter(k)},
                                             {k['colour'] for k in pt_keys if P.is_letter(k)})
                            es_masks = [(k['raw'][4:10], k['colour']) for k in es_keys if not P.is_letter(k)]
                            pt_masks = [(k['raw'][4:10], k['colour']) for k in pt_keys if not P.is_letter(k)]
                            self.assertEqual(es_masks, pt_masks)
                            letters, extras = P.copies(portuguese, layer)
                            self.assertEqual(extras, [])

    def test_masks_still_cover_the_line(self):
        for name, spanish, portuguese, _ in pages():
            for anim, plans in RESULT.plans[name].items():
                for _, screen_plans in plans:
                    for plan in screen_plans:
                        drawn_in = plan.line.layers[0]
                        letters = [P.box(portuguese, k) for k in P.letter_keys(portuguese, drawn_in)]
                        masks = [P.box(portuguese, k) for k in portuguese.layer_keys(drawn_in) if not P.is_letter(k)]
                        with self.subTest(page=name, text=plan.text):
                            self.assertEqual(len(masks), 4)
                            covered = min(m[0] for m in masks) <= min(b[0] for b in letters) and \
                                max(m[2] for m in masks) >= max(b[2] for b in letters)
                            self.assertTrue(covered)

    def test_layers_inside_the_page(self):
        for name, _, portuguese, _ in pages():
            names = [n for n, _, _ in portuguese.anims()]
            for i in range(portuguese.count(M.ANIMS)):
                raw = portuguese.record(M.ANIMS, i)
                anim = bytes(raw[16:40]).split(b'\0')[0].decode('latin1')
                if anim not in names:
                    continue
                page_box = np.frombuffer(bytes(raw[0:16]), '<f4')
                for line in P.page_lines(portuguese, anim):
                    for layer in line.layers:
                        b = np.frombuffer(bytes(portuguese.record(M.LAYERS, layer)[0:16]), '<f4')
                        with self.subTest(page=name, anim=anim, layer=layer):
                            self.assertTrue(b[0] >= page_box[0] - 1 and b[2] <= page_box[2] + 1)


class Rendering(unittest.TestCase):
    SAMPLES = ('A VALQUÍRIA', 'As aves acreditam que', 'TERRA DAS FADAS', 'fadas sob a proteção',
               'como uma fantasia efêmera', 'quem a elas se opõe', 'alçapão sob os pés', 'Sabe-se que as almas')

    def test_samples_draw_inside_their_boxes(self):
        name, _, portuguese, _ = pages()[0]
        plans = {p.text: p for anim in RESULT.plans[name].values() for _, ps in anim for p in ps}
        with tempfile.TemporaryDirectory() as folder:
            for text in self.SAMPLES:
                plan = plans[text]
                image, (ox, oy) = P.render_layer(portuguese, ATLAS, plan.line.layers[1])
                path = Path(folder) / f'{len(text)}.png'
                Image.fromarray(image).save(path)
                alpha = np.asarray(Image.open(path))[..., 3]
                ys, xs = np.nonzero(alpha > 24)
                boxes = [b for _, b in plan.items]
                with self.subTest(text=text):
                    self.assertGreater(len(xs), 100)
                    self.assertGreaterEqual(xs.min() + ox, min(b[0] for b in boxes) - 1.5)
                    self.assertLessEqual(xs.max() + ox, max(b[2] for b in boxes) + 1.5)
                    self.assertGreaterEqual(ys.min() + oy, min(b[1] for b in boxes) - 1.5)
                    self.assertLessEqual(ys.max() + oy, max(b[3] for b in boxes) + 1.5)
                    columns = np.flatnonzero((alpha > 24).any(0))
                    gaps = np.diff(columns)
                    self.assertLess(gaps.max(), 40)


class Delta(unittest.TestCase):
    def test_classic_and_hd(self):
        names = sorted(t[0] for t in RESULT.targets)
        self.assertEqual(names, sorted(['Installation.mbs', 'Installation.ftx', 'GUI/REHD_Installation.mbs',
                                        'GUI/HD_Installation.ftx']))

    def test_both_european_builds(self):
        for build in EUROPE:
            for layout, atlas, archive in P.PAGES:
                for entry in (layout, atlas):
                    source = PREP.read(build, archive.format('ES'), entry)
                    match = next(t for t in RESULT.targets if t[0] == entry)
                    with self.subTest(build=build, entry=entry):
                        self.assertIsNotNone(source)
                        self.assertEqual(zlib.crc32(source), zlib.crc32(match[1]))
        if (PREPARED / EUROPE[1] / 'Odin2_OR_ES.CPK').is_file():
            other = P.generate(PREP, EUROPE[1])
            self.assertEqual(P.encode(other.targets), P.encode(RESULT.targets))

    def test_deterministic_and_current(self):
        again = P.generate(PREP, EUROPE[0])
        self.assertEqual(P.encode(again.targets), P.encode(RESULT.targets))
        targets, _ = P.generate_all(PREP)
        self.assertEqual(P.encode(targets), P.DEFAULT_OUT.read_bytes())

    def test_only_the_spanish_entries_are_sources(self):
        shipped = oslv.parse(P.DEFAULT_OUT.read_bytes())
        sources = {(t['name'], t['size'], t['crc']) for t in shipped}
        self.assertEqual(len(shipped), 4)
        for build in ('CUSA05262-1.01', 'CUSA05083-1.00', 'CUSA01290-1.02'):
            for layout, atlas, archive in P.PAGES:
                for language in ('UK', 'FR', 'DE', 'IT', 'US', ''):
                    name = archive.format(language).replace('_.CPK', '.CPK')
                    for entry in (layout, atlas):
                        data = PREP.read(build, name, entry)
                        if data is None:
                            continue
                        with self.subTest(build=build, archive=name, entry=entry):
                            self.assertNotIn((entry, len(data), zlib.crc32(data)), sources)

    def test_a_changed_source_is_refused(self):
        for t in oslv.parse(P.DEFAULT_OUT.read_bytes()):
            source = next(s for name, s, _, _ in RESULT.targets if name == t['name'])
            self.assertIsNotNone(oslv.apply(t, source))
            changed = bytearray(source)
            changed[len(changed) // 2] ^= 0xFF
            with self.subTest(entry=t['name']):
                self.assertIsNone(oslv.apply(t, bytes(changed)))
                self.assertIsNone(oslv.apply(t, source[:-16]))


if __name__ == '__main__':
    try:
        outcome = unittest.main(exit=False, verbosity=1).result
    finally:
        PREP.close()
    count = outcome.testsRun
    failed = len(outcome.failures) + len(outcome.errors)
    print(f'prologue pages: {count} tests, {failed} failed, {len(outcome.skipped)} skipped')
    sys.exit(1 if failed or outcome.skipped else 0)
