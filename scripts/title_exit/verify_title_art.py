#!/usr/bin/env python3
"""Proves from the shipped launcher/title/title-exit.dcpx that no supported build's active Exit Game title menu shows INSTALL.

For every build of the manifest (written by `osl-tests --title-art`, one line per SupportedBuilds entry) and every text
language of it, the prepared original archives are read, the shipped patch's blocks are applied in memory and the
Refined and Classic labels are read back through the patched menu files. Nothing is written; a build without prepared
title archives is reported DATA UNAVAILABLE, never PASS. Portuguese (Brazil) rows read the build's English title art
through the English layer and localization/pt-BR/visual/title-exit-ptbr.dcpx (matched as the launcher's MatchEntry
does) and expect SAIR DO JOGO and the other rows in Portuguese (title_rows.py); the PT-BR patch must not match the files without the English layer.

    verify_title_art.py manifest.tsv game/prepared

Manifest line (tab separated): label, folder, data files (;), languages (key=name;) or empty for a single text language.
Output lines: TITLEART|build|language|result|source|note
"""
import sys
import zlib
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'prompts'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import cpk
import ftex
import make_dcpx
import make_title_exit as m
import title_credits
import title_secondary
import title_rows

LANGUAGE_KEYS = {'en': 'english', 'de': 'german', 'es': 'spanish', 'fr': 'french', 'it': 'italian'}
PTBR_KEY = 'portuguese_brazil'
UNAVAILABLE = 'DATA UNAVAILABLE'
UNAVAILABLE_NOTE = 'real-file part not run; the Exit Game row stays off for art the patch does not know (structure row)'
ROOT = Path(__file__).resolve().parent.parent.parent
SHIPPED = ROOT / 'launcher' / 'title' / 'title-exit.dcpx'
PREPARED = None
PTBR = ROOT / 'localization' / 'pt-BR' / 'visual' / 'title-exit-ptbr.dcpx'
TITLE_GLYPHS = {}
OLD_ENGLISH = ROOT / 'tests' / 'fixtures' / 'title-exit-0.2.3-english.dcpx'


def row(build, subject, result, source, note=''):
    fields = [str(f).replace('|', '/') for f in (build, subject, result, source, note)]
    print('TITLEART|' + '|'.join(fields), flush=True)


def applied(data, runs):
    out = bytearray(data)
    for offset, _, after in runs:
        out[offset:offset + 16] = after
    return bytes(out)


class Shipped:
    def __init__(self, replacing=None):
        self.targets = {(n.lower(), s, b): (a, runs) for n, s, b, a, runs in make_dcpx.parse(SHIPPED.read_bytes())}
        for n, s, b, a, runs in make_dcpx.parse(replacing.read_bytes()) if replacing else []:
            self.targets[(n.lower(), s, b)] = (a, runs)

    def apply(self, name, data):
        """The patched file, or why the patch has no target for it."""
        crc = zlib.crc32(data)
        found = self.targets.get((name.lower(), len(data), crc))
        if not found:
            return None, f'{name}: no patch target for size {len(data)}, CRC32 {crc:08x}'
        patched = applied(data, found[1])
        if zlib.crc32(patched) != found[0]:
            return None, f'{name}: the patch does not produce its own CRC32 ({crc:08x})'
        return patched, None


def read_groups(prepared, folders):
    """{atlas crc: group} for the folders asked for, as m.collect makes it, with the archives of each."""
    groups = {}
    for folder in folders:
        build_dir = Path(prepared) / folder
        if not build_dir.is_dir():
            continue
        for archive in sorted(build_dir.glob('*.CPK')):
            pack = cpk.Cpk(archive, original=True)
            try:
                found = {}
                for kind, spec in m.KINDS.items():
                    try:
                        atlas, mbs = pack.read(pack.find(spec['atlas'])), pack.read(pack.find(spec['mbs']))
                    except KeyError:
                        continue
                    found[kind] = (atlas, mbs)
            finally:
                pack.f.close()
            for kind, (atlas, mbs) in found.items():
                group = groups.setdefault(zlib.crc32(atlas), dict(atlas=atlas, where=[], files={}))
                group['where'].append((folder, archive.name))
                group['files'].setdefault(kind, set()).add(mbs)
    return groups


def label_failures(kind, original, patched_atlas, patched_mbs, phrase, language):
    """Why the patched menu does not show the whole intended label; empty when it does."""
    problems = []
    if phrase.replace(' ', '') in {t.replace(' ', '') for t in m.LANGUAGES[language]['texts'].values()}:
        problems.append(f'the label is {phrase}, a label the menu already had')
    shown = m.NativeArt(patched_atlas, patched_mbs['refined'], patched_mbs['classic'])
    label = m.find_label(shown.mbs[kind], kind)
    carrier = m.carrier_of(label.parts)
    x0, y0, x1, y1 = carrier.rect
    crop = shown.texture(carrier.texture)[y0:y1, x0:x1]
    try:
        ink = m.ink_box(crop)
    except m.Unsupported:
        return problems + ['the label rectangle is empty']
    if ink[0] < 1 or ink[1] < 1 or ink[2] > x1 - x0 - 1 or ink[3] > y1 - y0 - 1:
        problems.append(f'label touches the edge of its rectangle (ink {ink} in {x1 - x0}x{y1 - y0})')
    letters = len(phrase.replace(' ', ''))
    pieces = len(m.components(m.upright_part(shown, kind, carrier.part)))
    if pieces != letters:
        problems.append(f'expected {phrase} ({letters} glyphs), rendered {pieces} glyph pieces')
    before = original.mbs[kind]
    old = m.carrier_of(m.find_label(before, kind).parts)
    ox0, oy0, ox1, oy1 = old.rect
    if old.texture == carrier.texture and (ox0, oy0, ox1, oy1) == (x0, y0, x1, y1) and \
            np.array_equal(original.texture(old.texture)[oy0:oy1, ox0:ox1], crop):
        problems.append('the label is still the original INSTALL art')
    expected_rows = m.FOUR_ROWS[kind]
    seen = 0
    for name, first, count, match in m.selection_animations(shown.mbs[kind]):
        wanted = expected_rows.get(int(match.group(3) or 0))
        if wanted:
            rows = m.visible_rows(shown.mbs[kind], kind, first, count)
            if [role for _, role in rows] != wanted:
                problems.append(f'{name} shows the rows {[role for _, role in rows]}, expected {wanted}')
            seen += 1
    if seen < 8:
        problems.append(f'only {seen} selection animations carry the four rows')
    parts = {p.part for p in label.parts}
    shapes = {tuple(sorted((k['part'], k['geom']) for k in shown.mbs[kind].keyframes(i) if k['part'] in parts))
              for i in m.KINDS[kind]['install']}
    if len(shapes) != 1:
        problems.append('the selection states do not use the same label art')
    return problems


def sharing_failures(shown):
    labels = {kind: m.carrier_of(m.find_label(shown.mbs[kind], kind).parts) for kind in m.KINDS}
    refined, classic = labels['refined'], labels['classic']
    if refined.texture == classic.texture and m.overlap(refined.rect, classic.rect) and refined.rect != classic.rect:
        return ['the Refined and Classic labels overlap in the atlas without being the same rectangle']
    return []


def check_group(group, shipped, pool):
    """{kind: [problems]}, language and phrase of one atlas group."""
    if {k: len(v) for k, v in group['files'].items()} != {'refined': 1, 'classic': 1}:
        return None, None, {kind: ['the group holds several or no title menu files'] for kind in m.KINDS}
    files = {kind: next(iter(v)) for kind, v in group['files'].items()}
    art = m.NativeArt(group['atlas'], files['refined'], files['classic'])
    language = m.language_of(art)
    if not language:
        return None, None, {kind: ['the title art is in no known language'] for kind in m.KINDS}
    atlases, error = {}, None
    for spec in m.KINDS.values():
        atlases[spec['atlas']], error = shipped.apply(spec['atlas'], group['atlas'])
        if error:
            return language, None, {kind: [error] for kind in m.KINDS}
    if len({zlib.crc32(a) for a in atlases.values()}) != 1:
        return language, None, {kind: ['the Refined and Classic archives would get different atlases'] for kind in m.KINDS}
    patched = {}
    for kind, spec in m.KINDS.items():
        patched[kind], error = shipped.apply(spec['mbs'], files[kind])
        if error:
            return language, None, {k: [error] for k in m.KINDS}
    metrics = []
    blob, regenerated, status = m.process_group(art, pool, [], metrics)
    problems = {kind: [] for kind in m.KINDS}
    atlas = atlases['GUI/HD_title.ftx']
    for kind in m.KINDS:
        if status.get(kind) != 'ok':
            problems[kind].append(f'the generator finds no label to paint: {status.get(kind)}')
        elif atlas != blob or patched[kind] != regenerated[kind]:
            problems[kind].append('the shipped patch is not what the generator composes for this art')
    phrases = {mt['kind']: mt['phrase'] for mt in metrics}
    for kind in m.KINDS:
        if kind not in phrases:
            continue
        problems[kind] += label_failures(kind, art, atlas, patched, phrases[kind], language)
    shown = m.NativeArt(atlas, patched['refined'], patched['classic'])
    for kind in m.KINDS:
        problems[kind] += sharing_failures(shown)
    return language, phrases, problems


def match_entry(targets, name, data):
    """The launcher's MatchEntry over parsed DCPX targets: ('patched' | 'original' | 'unknown' | 'ambiguous', result)."""
    crc = zlib.crc32(data)
    for n, size, before, after, runs in targets:
        if n.lower() == name.lower() and size == len(data) and after == crc:
            return 'patched', data
    for n, size, before, after, runs in targets:
        if n.lower() == name.lower() and size == len(data) and before == crc:
            return 'original', applied(data, runs)
    originals, patched = [], 0
    for n, size, before, after, runs in targets:
        if n.lower() != name.lower() or size != len(data) or not runs:
            continue
        if all(data[off:off + 16] == old for off, old, _ in runs):
            originals.append(applied(data, runs))
        elif all(data[off:off + 16] == new for off, _, new in runs):
            patched += 1
    if originals and patched or len(set(originals)) > 1:
        return 'ambiguous', None
    if originals:
        return 'original', originals[0]
    return ('patched', data) if patched else ('unknown', None)


def check_ptbr(group, shipped, ptbr):
    """Portuguese (Brazil) over one English atlas group: {kind: [problems]} and the label. The PT-BR patch must turn the
    files the English layer leaves into what the generator composes, and leave the files without that layer alone."""
    problems = {kind: [] for kind in m.KINDS}
    files = {kind: next(iter(v)) for kind, v in group['files'].items()}
    art = m.NativeArt(group['atlas'], files['refined'], files['classic'])
    metrics = []
    blob, regenerated, status = m.process_group(art, {}, [], metrics, ptbr=True)
    exited = m.NativeArt(blob, regenerated['refined'], regenerated['classic'])
    blob, regenerated, _ = title_rows.paint_rows(exited, regenerated, TITLE_GLYPHS)
    donors = None
    try:
        donors = m.donor_layouts(PREPARED, [f'{folder}/{archive}' for folder, archive in group['where']], files)
        blob, regenerated = title_secondary.apply(blob, regenerated, donors)
    except (title_secondary.SecondaryError, m.Unsupported, KeyError, StopIteration, FileNotFoundError):
        pass
    if donors and donors['refined']['ES'] != files['refined']:
        try:
            blob, _ = title_credits.apply(blob, regenerated, donors)
        except (title_credits.CreditError, KeyError, StopIteration):
            pass
    expected = {}
    for kind, spec in m.KINDS.items():
        expected[spec['atlas']], expected[spec['mbs']] = blob, regenerated[kind]
        if status.get(kind) != 'ok':
            problems[kind].append(f'the generator finds no Portuguese label to paint: {status.get(kind)}')
    sources = {}
    for kind, spec in m.KINDS.items():
        sources[spec['atlas']], sources[spec['mbs']] = group['atlas'], files[kind]
    shown = {}
    for name, original in sources.items():
        kinds = [k for k, s in m.KINDS.items() if name in (s['atlas'], s['mbs'])]
        layered, error = shipped.apply(name, original)
        if error:
            for kind in kinds:
                problems[kind].append(error)
            continue
        state, result = match_entry(ptbr, name, layered)
        if state != 'original' and not (state == 'patched' and layered == expected[name]):
            for kind in kinds:
                problems[kind].append(f'{name}: the PT-BR patch finds the English layer {state}')
            continue
        if result != expected[name]:
            for kind in kinds:
                problems[kind].append(f'{name}: the PT-BR patch is not what the generator composes')
        if match_entry(ptbr, name, original)[0] != 'unknown':
            for kind in kinds:
                problems[kind].append(f'{name}: the PT-BR patch would also change the file without the English layer')
        if match_entry(ptbr, name, expected[name])[0] != 'patched':
            for kind in kinds:
                problems[kind].append(f'{name}: the PT-BR result is not recognised as patched')
        shown[name] = result
    phrase = next((mt['phrase'] for mt in metrics), '')
    if any(problems.values()) or len(shown) != len(sources):
        return phrase, problems
    patched = {kind: shown[spec['mbs']] for kind, spec in m.KINDS.items()}
    for kind, spec in m.KINDS.items():
        if phrase != m.PTBR_PHRASES[0]:
            problems[kind].append(f'the label is {phrase}, not {m.PTBR_PHRASES[0]}')
        problems[kind] += label_failures(kind, art, shown[spec['atlas']], patched, phrase, 'en')
        problems[kind] += sharing_failures(m.NativeArt(shown[spec['atlas']], patched['refined'], patched['classic']))
    return phrase, problems


def main():
    manifest =[line.rstrip('\n').split('\t') for line in Path(sys.argv[1]).read_text(encoding='utf-8').splitlines() if line]
    prepared = Path(sys.argv[2])
    global PREPARED
    PREPARED = prepared
    shipped = Shipped()
    builds = [dict(label=l, folder=f, files=set(d.split(';')) if d else set(),
                   languages=[tuple(x.split('=', 1)) for x in langs.split(';') if x]) for l, f, d, langs in manifest]
    groups = read_groups(prepared, [b['folder'] for b in builds])
    arts = {}
    for crc, group in groups.items():
        if {k: len(v) for k, v in group['files'].items()} == {'refined': 1, 'classic': 1}:
            arts[crc] = m.NativeArt(group['atlas'], next(iter(group['files']['refined'])), next(iter(group['files']['classic'])))
    if not arts:
        for build in builds:
            for key, name in build['languages'] or [('', 'English')]:
                for kind in m.KINDS:
                    row(build['label'], f'{name} / {kind.capitalize()}', UNAVAILABLE, 'no prepared title archives', UNAVAILABLE_NOTE)
        return 0
    _, reference = m.find_reference(arts)
    pool = m.build_pool(arts, reference)
    by_language = {}
    for crc in sorted(arts):
        by_language.setdefault(m.language_of(arts[crc]), arts[crc])
    TITLE_GLYPHS.update(title_rows.pool(by_language))
    checked, failed = {}, 0
    ptbr_checked = {}
    for build in builds:
        mine = {crc: g for crc, g in groups.items()
                if any(folder == build['folder'] and name in build['files'] for folder, name in g['where'])}
        by_language = {}
        for crc, group in mine.items():
            if crc not in checked:
                checked[crc] = check_group(group, shipped, pool)
            language, phrases, problems = checked[crc]
            archives = sorted({name for folder, name in group['where'] if folder == build['folder'] and name in build['files']})
            by_language.setdefault(language, []).append((crc, archives, phrases, problems))
        wanted = [(key, name) for key, name in build['languages']] or [(None, 'English')]
        covered = set()
        for key, name in wanted:
            if key == PTBR_KEY:
                failed += ptbr_rows(build, name, by_language.get('en', []), groups, shipped, ptbr_checked, bool(mine))
                continue
            code = next((c for c, k in LANGUAGE_KEYS.items() if k == key), 'en')
            entries = by_language.get(code, [])
            covered.add(code)
            if not entries:
                reason = 'no prepared title archives' if not mine else f'no prepared archive holds {name} title art'
                for kind in m.KINDS:
                    row(build['label'], f'{name} / {kind.capitalize()}', UNAVAILABLE, reason, UNAVAILABLE_NOTE)
                continue
            results, archives, phrase = {}, [], ''
            for crc, names, phrases, problems in entries:
                archives += names
                phrase = next(iter(phrases.values()), '') if phrases else phrase
                for kind in m.KINDS:
                    results.setdefault(kind, []).extend(f'{p}' for p in problems.get(kind, []))
            for kind in m.KINDS:
                reasons = list(dict.fromkeys(results[kind]))
                failed += bool(reasons)
                row(build['label'], f'{name} / {kind.capitalize()}', 'PASS' if not reasons else 'FAIL: ' + '; '.join(reasons),
                    f'real files: {len(archives)} archive(s)', f'label "{phrase}"')
        for code in sorted(set(by_language) - covered - {None}):
            row(build['label'], code, 'note', f'prepared archives of language {code} the build does not list')
        for crc, names, phrases, problems in by_language.get(None, []):
            failed += 1
            row(build['label'], f'archives {",".join(names)}', 'FAIL: ' + '; '.join(problems['refined']), 'real files')
    failed += old_patch_is_rejected(checked, groups, pool)
    return 1 if failed else 0


def ptbr_rows(build, name, entries, groups, shipped, checked, prepared):
    """The Portuguese (Brazil) rows of a build: its English title art through the English layer and the PT-BR patch."""
    if not entries:
        reason = 'no prepared title archives' if not prepared else 'no prepared archive holds English title art'
        for kind in m.KINDS:
            row(build['label'], f'{name} / {kind.capitalize()}', UNAVAILABLE, reason, UNAVAILABLE_NOTE)
        return 0
    if not PTBR.is_file():
        for kind in m.KINDS:
            row(build['label'], f'{name} / {kind.capitalize()}', f'FAIL: {PTBR.name} is missing', 'real files')
        return len(m.KINDS)
    if 'targets' not in checked:
        checked['targets'] = make_dcpx.parse(PTBR.read_bytes())
    results, archives, phrase = {kind: [] for kind in m.KINDS}, [], ''
    for crc, names, _, _ in entries:
        if crc not in checked:
            checked[crc] = check_ptbr(groups[crc], shipped, checked['targets'])
        phrase, problems = checked[crc]
        archives += names
        for kind in m.KINDS:
            results[kind] += problems[kind]
    failed = 0
    for kind in m.KINDS:
        reasons = list(dict.fromkeys(results[kind]))
        failed += bool(reasons)
        row(build['label'], f'{name} / {kind.capitalize()}', 'PASS' if not reasons else 'FAIL: ' + '; '.join(reasons),
            f'real files: {len(archives)} archive(s)', f'label "{phrase}" ({PTBR.name} over the English layer)')
    return failed


def old_patch_is_rejected(checked, groups, pool):
    """The 0.2.3 English targets, put back in place of the shipped ones, must fail the same checks (EXIT GAM)."""
    old = Shipped(OLD_ENGLISH)
    english = [crc for crc, (language, _, _) in checked.items() if language == 'en']
    if not english:
        row('0.2.3 English patch', 'English', UNAVAILABLE, 'no prepared English title archives', 'negative control not run')
        return 0
    failed = 0
    for crc in english:
        problems = check_group(groups[crc], old, pool)[2]['refined']
        reasons = [p for p in problems if 'glyph pieces' in p]
        failed += not reasons
        row('0.2.3 English patch', f'atlas {crc:08x}', f'PASS: rejected, {reasons[0]}' if reasons else
            'FAIL: the old broken English patch passes the checks', 'negative control', '')
    return failed


if __name__ == '__main__':
    sys.exit(main())
