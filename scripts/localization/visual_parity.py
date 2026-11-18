#!/usr/bin/env python3
"""Official localization parity for Portuguese (Brazil): every resource the European game localizes, and what the port
does for it.

The Spanish archives of CUSA05262 1.01 hold exactly the entries the game localizes (everything else comes from the
English ones), so Spanish is the map: each Spanish entry is an officially localized resource, compared with the
English entry of the same path. Spanish is never a translation source.

    visual_parity.py inventory [--prepared game/prepared]   writes localization/pt-BR/visual/official-resources.tsv
    visual_parity.py check                                  every resource has a status; translated ones have a target

localization/pt-BR/visual/coverage.tsv gives the status of each resource by the first pattern matching its path:
PT_BR_TRANSLATED (a localization/pt-BR/visual patch, or the text corpus, holds the English or the Spanish entry's exact
size and CRC32 as a target), PT_BR_NOT_NEEDED, PT_BR_BLOCKED or DATA_UNAVAILABLE, with the reason. The check fails on a resource
no pattern explains, and on a TRANSLATED claim no patch backs. `report` also writes visual/untranslated.json, the
entries of the BLOCKED resources the launcher reports when a Portuguese (Brazil) run reads them.
"""
import argparse
import csv
import json
import fnmatch
import re
import sys
import zlib
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'prompts'))
import cpk  # noqa: E402
import make_dcpx  # noqa: E402
import odin_text as ot  # noqa: E402
import oslv  # noqa: E402

ROOT = HERE.parent.parent
VISUAL = ROOT / 'localization' / 'pt-BR' / 'visual'
INVENTORY = VISUAL / 'official-resources.tsv'
COVERAGE = VISUAL / 'coverage.tsv'
UNTRANSLATED = VISUAL / 'untranslated.json'
ENGLISH_LAYER = ROOT / 'launcher' / 'title' / 'title-exit.dcpx'
BUILD = 'CUSA05262-1.01'
ARCHIVES = ('Odin2_BT_{}.CPK', 'Odin2_RE_{}.CPK', 'Odin2_OR_{}.CPK', 'Odin2_EU_PATCH1_RE_{}.CPK', 'Odin2_EU_PATCH1_OR_{}.CPK')
STATUSES = ('PT_BR_TRANSLATED', 'PT_BR_NOT_NEEDED', 'PT_BR_BLOCKED', 'DATA_UNAVAILABLE')
COLUMNS = ('archive', 'path', 'format', 'classification', 'group', 'uk_size', 'uk_crc32', 'es_size', 'es_crc32')

GROUPS = [
    (r'(?i)(^|/)(rehd_|hd_)?stagename\w*\.(ftx|mbs)$|^\d+(_\w+)?\.(ftx|mbs)$', 'STAGE_TITLE', 'stage/location cards'),
    (r'(?i)^(other/hd_)?m\d+\.ftx$', 'MANUAL', 'manual/help'),
    (r'(?i)(^|/)(rehd_|hd_)?title\.(ftx|mbs)$|op_press_start|(rehd_|hd_)?logo\.', 'TITLE', 'title'),
    (r'(?i)(rehd_|hd_)?mode_select|(rehd_|hd_)?chrselect|(rehd_|hd_)?installation|butterfly', 'TITLE', 'other menus'),
    (r'(?i)(rehd_|hd_)?interface\.', 'HUD', 'HUD, save/load, confirmation'),
    (r'(?i)archiveframe|archive_00|bookshelf', 'BOOKSHELF', 'Bookshelf'),
    (r'(?i)(rehd_|hd_)?fin\d', 'ENDING', 'ending'),
    (r'(?i)(rehd_|hd_)?kitchin|skill_common', 'HUD', 'generic UI'),
]
PROLOGUE = ('story/a0_2_1', 'story/b0_2_1', 'story/c0_2_1', 'story/d0_2_1', 'story/e0_2_1', 'story/f0_1_0')


def classify(path, uk, es):
    ext = path.rsplit('.', 1)[-1].lower()
    if uk is None:
        return 'UNKNOWN', 'other'
    if uk == es:
        return 'LANGUAGE_INDEPENDENT', 'other'
    resource = ot.resource_of(path)
    if resource in PROLOGUE:
        return 'TEXT_CONTAINER', 'chapter prologue cards'
    if resource or ext in ('fms', 'txt', 'bin'):
        return 'TEXT_CONTAINER', 'text corpus'
    for pattern, kind, group in GROUPS:
        if re.search(pattern, path):
            return ('LAYOUT' if ext == 'mbs' else kind), group
    return ('LAYOUT' if ext == 'mbs' else 'TEXTURE_TEXT' if ext == 'ftx' else 'UNKNOWN'), 'other'


def same_card(english, spanish):
    """The first texture of a card pack (the card itself; the others are shared atlases) shows no visible difference."""
    import ftex
    import visuals
    a, b = ftex.textures(english)[0], ftex.textures(spanish)[0]
    change = visuals.texture_change(english, a, spanish, b)
    return change is None or change['visible'] == 0


def inventory(prepared):
    rows = []
    for pattern in ARCHIVES:
        uk_path, es_path = Path(prepared) / BUILD / pattern.format('UK'), Path(prepared) / BUILD / pattern.format('ES')
        if not uk_path.is_file() or not es_path.is_file():
            raise SystemExit(f'{BUILD}: {pattern.format("UK")} or {pattern.format("ES")} is not prepared')
        uk, es = cpk.Cpk(uk_path, original=True), cpk.Cpk(es_path, original=True)
        english = {e['path'].lower(): e for e in uk.entries}
        for entry in sorted(es.entries, key=lambda e: e['path'].lower()):
            es_data = es.read(entry)
            other = english.get(entry['path'].lower())
            uk_data = uk.read(other) if other else None
            kind, group = classify(entry['path'], uk_data, es_data)
            if kind == 'STAGE_TITLE' and uk_data is not None and same_card(uk_data, es_data):
                kind, group = 'LANGUAGE_INDEPENDENT', 'stage card that looks the same in English and Spanish'

            rows.append(dict(archive=pattern.format('UK'), path=entry['path'], format=entry['path'].rsplit('.', 1)[-1].lower(),
                             classification=kind, group=group,
                             uk_size='' if uk_data is None else str(len(uk_data)),
                             uk_crc32='' if uk_data is None else f'{zlib.crc32(uk_data):08x}',
                             es_size=str(len(es_data)), es_crc32=f'{zlib.crc32(es_data):08x}'))
    seen = {}
    for row in rows:
        key = (row['uk_size'], row['uk_crc32'], row['path'].rsplit('/', 1)[-1].lower().removeprefix('hd_'))
        if row['classification'] not in ('TEXT_CONTAINER', 'LANGUAGE_INDEPENDENT') and key in seen:
            row['classification'] = 'DUPLICATE'
            row['group'] = f'same English bytes as {seen[key]}'
        seen.setdefault(key, f"{row['archive']}:{row['path']}")
    return rows


def write_tsv(path, rows, columns):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\t'.join(columns) + '\n')
        for row in rows:
            f.write('\t'.join(row[c] for c in columns) + '\n')


def read_tsv(path):
    with open(path, encoding='utf-8', newline='') as f:
        return list(csv.DictReader(f, delimiter='\t'))


def patch_targets():
    """{(name lower, size, crc32): patch file} of every Portuguese visual patch."""
    out = {}
    for file in sorted(VISUAL.glob('*.dcpx')):
        for name, size, before, *_ in make_dcpx.parse(file.read_bytes()):
            out[(name.lower(), size, before)] = file.name
    for file in sorted(VISUAL.glob('*.oslv')):
        for t in oslv.parse(file.read_bytes()):
            out[(t['name'].lower(), t['size'], t['crc'])] = file.name
    for name, size, before, after, _ in make_dcpx.parse(ENGLISH_LAYER.read_bytes()):
        layered = out.get((name.lower(), size, after))
        if layered:
            out.setdefault((name.lower(), size, before), f'{layered} over the Exit Game layer')
    return out


def status_of(row, coverage):
    target = f"{row['archive']}:{row['path']}"
    for rule in coverage:
        if fnmatch.fnmatchcase(target.lower(), rule['pattern'].lower()) or \
                fnmatch.fnmatchcase(row['classification'].lower(), rule['pattern'].lower()):
            return rule
    return None


def check(inventory_rows, coverage, targets, corpus_resources):
    problems, table, rows = [], Counter(), []
    by_name = {f"{r['archive']}:{r['path']}": r for r in inventory_rows}
    for row in inventory_rows:
        origin = by_name.get(row['group'].removeprefix('same English bytes as ')) if row['classification'] == 'DUPLICATE' else None
        rule = status_of(origin or row, coverage)
        if rule is None:
            problems.append(f"{row['archive']}:{row['path']}: no status")
            continue
        if rule['status'] not in STATUSES:
            problems.append(f"{row['archive']}:{row['path']}: unknown status {rule['status']}")
        if rule['status'] == 'PT_BR_TRANSLATED':
            if row['classification'] == 'TEXT_CONTAINER':
                if ot.resource_of(row['path']) not in corpus_resources and row['format'] != 'bin':
                    problems.append(f"{row['archive']}:{row['path']}: text container not in the corpus")
            elif origin is None:
                keys = {(row['path'].lower(), int(row[f'{side}_size']), int(row[f'{side}_crc32'], 16))
                        for side in ('uk', 'es') if row.get(f'{side}_size')}
                if not keys & targets.keys():
                    problems.append(f"{row['archive']}:{row['path']}: TRANSLATED but no patch targets "
                                    f"{row['uk_size']} bytes CRC32 {row['uk_crc32']}")
        group = (origin or row)['group']
        table[(group, rule['status'])] += 1
        rows.append((row, rule))
    return problems, table, rows


def untranslated_json(rows):
    """visual/untranslated.json of the checked rows: every BLOCKED visual entry path, once."""
    paths = sorted({row['path'] for row, rule in rows
                    if rule['status'] == 'PT_BR_BLOCKED' and row['classification'] != 'TEXT_CONTAINER'}, key=str.lower)
    return json.dumps({'entries': paths}, indent=1, ensure_ascii=False) + '\n'


TOTALS_OPEN, TOTALS_CLOSE = '<!-- totals -->', '<!-- /totals -->'
REPORT = VISUAL / 'INVENTORY.md'


def totals_table(table):
    groups = sorted({g for g, _ in table})
    lines = ['| Group | Translated | Not needed | Blocked |', '| --- | ---: | ---: | ---: |']
    for g in groups:
        lines.append(f"| {g} | " + ' | '.join(str(table[(g, s)]) for s in STATUSES[:3]) + ' |')
    lines.append('| **Total** | ' + ' | '.join(f"**{sum(v for (_, st), v in table.items() if st == s)}**" for s in STATUSES[:3]) + ' |')
    return '\n'.join(lines)


def read_totals():
    text = REPORT.read_text(encoding='utf-8').replace('\r\n', '\n')
    if TOTALS_OPEN not in text:
        return None
    return text.split(TOTALS_OPEN, 1)[1].split(TOTALS_CLOSE, 1)[0].strip()


def write_totals(current):
    raw = REPORT.read_bytes().decode('utf-8')
    crlf = '\r\n' in raw
    text = raw.replace('\r\n', '\n')
    head, rest = text.split(TOTALS_OPEN, 1)
    tail = rest.split(TOTALS_CLOSE, 1)[1]
    out = head + TOTALS_OPEN + '\n' + current + '\n' + TOTALS_CLOSE + tail
    REPORT.write_bytes((out.replace('\n', '\r\n') if crlf else out).encode('utf-8'))


def corpus():
    import corpus as C
    return set(C.english_sheets())


def text_containers(problems):
    """Every translatable record of every text container must have a Portuguese entry the corpus accepts."""
    import corpus as C
    shared, only = C.pt_entries()
    for resource, units in C.english_sheets().items():
        for index, text in enumerate(units):
            if not C.translatable(text):
                continue
            pt = C.pt_for(resource, index, text, shared, only)
            if pt is None:
                problems.append(f'{resource}#{index}: no Portuguese text')
            else:
                problems += [f'{resource}#{index}: {why}' for why in C.check_translation(text, pt)]


def prologue(problems):
    """Every record of the six chapter prologue cards must have a Portuguese text that is not the English one."""
    import corpus as C
    english = C.english_sheets()
    shared, only = C.pt_entries()
    for resource in PROLOGUE:
        for index, text in enumerate(english.get(resource, ())):
            if not C.translatable(text):
                continue
            pt = C.pt_for(resource, index, text, shared, only)
            if pt is None:
                problems.append(f'{resource}#{index}: no Portuguese text')
            elif pt == text:
                problems.append(f'{resource}#{index}: still the English text')
            else:
                problems += [f'{resource}#{index}: {why}' for why in C.check_translation(text, pt)]


def prologue_map(problems):
    """The runtime map of the six prologue cards covers every Spanish record with a Portuguese one."""
    import corpus as C
    import prologue_map as P
    if not (C.PREPARED / P.BUILD / P.ARCHIVE).is_file():
        return
    problems += P.check(C.PREPARED)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('inventory')
    p.add_argument('--prepared', default=str(ROOT / 'game' / 'prepared'))
    sub.add_parser('check')
    sub.add_parser('report')
    args = ap.parse_args()
    if args.cmd == 'inventory':
        import visuals
        visuals.use_native_reader()
        rows = inventory(args.prepared)
        write_tsv(INVENTORY, rows, COLUMNS)
        print(f'{len(rows)} officially localized entries -> {INVENTORY}')
        return
    problems, table, checked = check(read_tsv(INVENTORY), read_tsv(COVERAGE), patch_targets(), corpus())
    text_containers(problems)
    prologue(problems)
    prologue_map(problems)
    current = totals_table(table)
    if args.cmd == 'report':
        write_totals(current)
        UNTRANSLATED.write_text(untranslated_json(checked), encoding='utf-8', newline='\n')
        print(current)
        return
    if read_totals() != current:
        problems.append('INVENTORY.md totals are stale (visual_parity.py report rewrites them)')
    if not UNTRANSLATED.is_file() or UNTRANSLATED.read_text(encoding='utf-8') != untranslated_json(checked):
        problems.append(f'{UNTRANSLATED.name} is stale (visual_parity.py report rewrites it)')
    groups = sorted({g for g, _ in table})
    print('group'.ljust(34) + ''.join(s.replace('PT_BR_', '').ljust(14) for s in STATUSES))
    for g in groups:
        print(g[:33].ljust(34) + ''.join(str(table[(g, s)]).ljust(14) for s in STATUSES))
    print('total'.ljust(34) + ''.join(str(sum(v for (g, s), v in table.items() if s == st)).ljust(14) for st in STATUSES))
    for problem in problems:
        print('PROBLEM', problem)
    print(f'{len(problems)} unexplained or unbacked')
    sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
