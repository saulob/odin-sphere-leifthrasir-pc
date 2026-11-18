#!/usr/bin/env python3
"""English on every build. The USA and European builds play their own English slot; on Japan, Hong Kong and Asia the
port serves the English of localization/en/sheets over the native slot (launcher/game_translation.cpp).

    english.py generate [--game DIR]   localization/en/sheets and sources/US.json from the USA 1.00 game (default
                                       game/prepared/CUSA05083-1.00); the archives are only read
    english.py sources                 localization/en/sources/<slot>.json: which records of each local non-English
                                       slot take the English record of the same index, and why the others do not
    english.py validate                the committed corpus and maps, without game files
    english.py parity                  per build: records served, kept and why
"""
import argparse
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import corpus as C  # noqa: E402
import odin_text as ot  # noqa: E402

MAPPED_BUILDS = ('CUSA01290-1.00', 'CUSA01290-1.02', 'CUSA03873-1.00', 'CUSA03873-1.00-Asia')
SLOT_FILE = {'': 'JP', 'CN': 'CN'}


def cmd_generate(args):
    build = ot.build_profile(C.CANONICAL_BUILD)
    slot, problems = ot.load_slot(build, args.game)
    sheets = {kind: [] for kind in ot.KINDS}
    hashes = {}
    excluded = {}
    for resource, (units, container, _) in sorted(slot.items()):
        if getattr(container, 'encoding', 'utf-8') != 'utf-8':
            excluded[resource] = 'Shift-JIS development leftover, not English'
            continue
        sheets[ot.kind_of(resource)].append({'resource': resource, 'units': units})
        hashes[resource] = [C.short(ot.sha256(u)) for u in units]
    for kind, items in sheets.items():
        C.write_json(C.EN_DIR / 'sheets' / f'{kind}.json', {'kind': kind, 'source': C.CANONICAL_BUILD, 'resources': items})
    C.write_json(C.EN_DIR / 'sources' / 'US.json', {'source': C.CANONICAL_BUILD, 'excluded': excluded, 'resources': hashes})
    total = sum(len(i['units']) for items in sheets.values() for i in items)
    print(f'{sum(len(v) for v in sheets.values())} resources, {total} units, {len(C.english_records())} translatable; '
          f'excluded {len(excluded)}: {", ".join(sorted(excluded))}')
    for p in problems:
        print('not parsed:', p)
    return 0


def map_slot(english, slot_units, container=None):
    """(accepted {index: slot hash}, excluded {index: reason}) for one resource with the same unit count in both.
    The record number alone proves nothing: a record is taken only when its printf conversions, button glyphs and
    numbers agree and the records around it have the same shape (empty, placeholder or text, and their arguments)."""
    accepted, excluded = {}, {}
    n = len(english)
    for i, text in enumerate(english):
        if not C.translatable(text):
            continue
        native = slot_units[i]
        if ot.arguments(text) != ot.arguments(native):
            excluded[i] = 'printf conversions or button glyphs differ'
        elif ot.digits(text) != ot.digits(native):
            excluded[i] = 'numbers differ'
        elif any(ot.shape(english[k]) != ot.shape(slot_units[k]) for k in range(max(0, i - 2), min(n, i + 3))):
            excluded[i] = 'neighbouring records differ in shape'
        else:
            accepted[i] = C.short(ot.stored_sha256(native, container))
    return accepted, excluded


def cmd_sources(args):
    english = C.english_sheets()
    maps = {}
    for folder in MAPPED_BUILDS:
        path = C.PREPARED / folder
        if not path.is_dir():
            print(f'{folder}: DATA UNAVAILABLE (no prepared game)')
            continue
        build = ot.build_profile(folder)
        slot, _ = ot.load_slot(build, path)
        out = maps.setdefault(SLOT_FILE[build['slot']], {'builds': [], 'resources': {}})
        out['builds'].append(folder)
        for resource, units in english.items():
            entry = out['resources'].setdefault(resource, {'accept': {}, 'excluded': {}, 'whole': {}})
            if resource not in slot:
                entry['whole'][folder] = 'not in the slot'
                continue
            native, container, _ = slot[resource]
            if ot.kind_of(resource) == 'archive':
                accepted, excluded, sources = map_page(units, english_page(resource), container)
                if not accepted:
                    entry['whole'][folder] = 'no text block of the page has a counterpart in the English page'
                for i, j in sources.items():
                    if i != j:
                        entry.setdefault('from', {})[str(i)] = j
            elif len(native) != len(units):
                entry['whole'][folder] = f'{len(native)} units, English has {len(units)}'
                continue
            else:
                accepted, excluded = map_slot(units, native, container)
            for i, h in accepted.items():
                hs = entry['accept'].setdefault(str(i), [])
                if h not in hs:
                    hs.append(h)
            for i, why in excluded.items():
                entry['excluded'].setdefault(str(i), why)
    for name, value in maps.items():
        for entry in value['resources'].values():
            for i in list(entry['excluded']):
                if i in entry['accept']:
                    del entry['excluded'][i]
        C.write_json(C.EN_DIR / 'sources' / f'{name}.json', value)
        accepted = sum(len(e['accept']) for e in value['resources'].values())
        print(f'{name}.json ({", ".join(value["builds"])}): {accepted} records mapped')
    return 0


_PAGES = {}


def english_page(resource):
    if not _PAGES:
        build = ot.build_profile(C.CANONICAL_BUILD)
        slot, _ = ot.load_slot(build, C.PREPARED / C.CANONICAL_BUILD)
        for r, (_, container, _) in slot.items():
            if ot.kind_of(r) == 'archive':
                _PAGES[r] = container
    return _PAGES[resource]


def map_page(units, english, page):
    """An archive page of another language: each text block takes the English block at the same page, frame, kind
    and order; the page keeps its own layout commands."""
    by_key = {k: j for j, k in enumerate(ot.page_keys(english))}
    accepted, excluded, sources = {}, {}, {}
    for i, key in enumerate(ot.page_keys(page)):
        j = by_key.get(key)
        if j is None:
            excluded[i] = 'no English block at this page, frame and place'
        elif not C.translatable(units[j]):
            continue
        elif ot.arguments(units[j]) != ot.arguments(page.texts[i]):
            excluded[i] = 'printf conversions or button glyphs differ'
        else:
            accepted[i] = C.short(ot.stored_sha256(page.texts[i], page))
            sources[i] = j
    return accepted, excluded, sources


def cmd_validate(args):
    errors = []
    english = C.english_sheets()
    hashes = C.read_json(C.EN_DIR / 'sources' / 'US.json')['resources']
    for resource, units in english.items():
        if resource not in hashes or len(hashes[resource]) != len(units):
            errors.append(f'{resource}: sources/US.json does not list its {len(units)} units')
            continue
        for i, u in enumerate(units):
            if C.short(ot.sha256(u)) != hashes[resource][i]:
                errors.append(f'{resource}#{i}: hash differs from sources/US.json')
            if '�' in u:
                errors.append(f'{resource}#{i}: replacement character')
    for path in sorted((C.EN_DIR / 'sources').glob('*.json')):
        if path.name == 'US.json':
            continue
        for resource, entry in C.read_json(path)['resources'].items():
            if resource not in english:
                errors.append(f'{path.name}: {resource} is not in the English corpus')
                continue
            for i in entry['accept']:
                if not 0 <= int(i) < len(english[resource]):
                    errors.append(f'{path.name}: {resource}#{i} out of range')
    for e in errors[:50]:
        print('error:', e)
    print(f'English corpus: {len(english)} resources, {sum(len(u) for u in english.values())} units, '
          f'{len(C.english_records())} translatable; {len(errors)} errors')
    return 1 if errors else 0


def coverage(folder, language):
    """{'state', 'native', 'total', 'served', 'kept', 'reasons'} of one language on one build, from the real files."""
    import json  # noqa: F401
    build = ot.build_profile(folder)
    path = C.PREPARED / folder
    if not path.is_dir():
        return {'state': 'DATA UNAVAILABLE'}
    slot, problems = ot.load_slot(build, path)
    english = C.english_sheets()
    shared, only = C.pt_entries() if language == 'pt' else ({}, {})
    source_map = None
    if not build['native_english']:
        map_path = C.EN_DIR / 'sources' / f'{SLOT_FILE[build["slot"]]}.json'
        source_map = C.read_json(map_path)['resources'] if map_path.is_file() else {}
    letters = glyph_letters(path, language)
    total = served = 0
    reasons = Counter()
    for resource, units in english.items():
        entry = source_map.get(resource, {}) if source_map is not None else {}
        slot_of = {j: int(i) for i, j in entry.get('from', {}).items()}
        for i, text in enumerate(units):
            if not C.translatable(text):
                continue
            total += 1
            if resource not in slot:
                reasons['not in the slot'] += 1
                continue
            native, container = slot[resource][0], slot[resource][1]
            if build['native_english']:
                if len(native) <= i or native[i] != text:
                    reasons['installed English differs from the corpus'] += 1
                    continue
            else:
                k = slot_of.get(i, i)
                page = ot.kind_of(resource) == 'archive'
                h = C.short(ot.stored_sha256(native[k], container)) if k < len(native) and (page or len(native) == len(units)) else None
                if h is None or h not in entry.get('accept', {}).get(str(k), []):
                    reasons[entry.get('whole', {}).get(folder) or entry.get('excluded', {}).get(str(k), 'not mapped')] += 1
                    continue
            shown = text
            if language == 'pt':
                shown = C.pt_for(resource, i, text, shared, only)
                if shown is None:
                    reasons['not translated yet'] += 1
                    continue
                if C.check_translation(text, shown):
                    reasons['translation refused by validation'] += 1
                    continue
            elif build['native_english']:
                reasons['native'] += 1
                served += 1
                continue
            missing = next((ch for ch in shown if C.needs_glyph(ch) and ch not in letters), None)
            if missing:
                reasons[f'no glyph for {missing} U+{ord(missing):04X}'] += 1
                continue
            served += 1
    return {'state': 'PASS', 'total': total, 'served': served, 'kept': total - served, 'reasons': reasons,
            'not_parsed': problems}


def glyph_letters(path, language):
    """Accented letters every text font of the prepared game draws for the language (localization/pt-BR/font)."""
    import hashlib
    data = C.read_json(C.PT_DIR / 'font' / 'glyphs.json')
    sha = hashlib.sha256((path / 'eboot.bin').read_bytes()).hexdigest()
    build = next((b for b in data['builds'] if b['eboot_sha256'] == sha), None)
    if not build:
        return set()
    out = None
    for f in build['families']:
        have = {chr(c) for c in f['native']} | ({chr(g['code']) for g in f['glyphs']} if language == 'pt' else set())
        out = have if out is None else out & have
    return out or set()


def cmd_parity(args):
    for build in ot.builds():
        result = coverage(build['folder'], 'en')
        if result['state'] != 'PASS':
            print(f'{build["label"]:24} English: {result["state"]}')
            continue
        how = 'native' if build['native_english'] else f'port corpus over the {build["slot"] or "Japanese"} slot'
        print(f'{build["label"]:24} English ({how}): {result["served"]}/{result["total"]} records, '
              f'{result["kept"]} keep the slot text')
        for why, n in result['reasons'].most_common():
            if why != 'native':
                print(f'{"":26}{n:6}  {why}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('generate')
    p.add_argument('--game', type=Path, default=C.PREPARED / C.CANONICAL_BUILD)
    for name in ('sources', 'validate', 'parity'):
        sub.add_parser(name)
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    return {'generate': cmd_generate, 'sources': cmd_sources, 'validate': cmd_validate, 'parity': cmd_parity}[args.command](args)


if __name__ == '__main__':
    sys.exit(main())
