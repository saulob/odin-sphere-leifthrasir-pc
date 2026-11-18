#!/usr/bin/env python3
"""The six chapter prologue story containers as a runtime translation map.

The drama draws these cards from the Spanish story containers, so each official Spanish record is the verification
anchor and the Portuguese corpus record is the replacement. The map is keyed by resource and record index (the source
Spanish string is matched exactly too), emitted as the C++ table the runtime layer includes and checked here against
the prepared Spanish archives (41/41 covered, no Spanish record left unmapped).

    prologue_map.py write   emit launcher/prologue_translation_table.inc
    prologue_map.py check   the emitted table matches the corpus and the Spanish archives
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'prompts'))
import corpus as C  # noqa: E402
import odin_text as ot  # noqa: E402
from cpk import Cpk  # noqa: E402

BUILD = 'CUSA05262-1.01'
ARCHIVE = 'Odin2_OR_ES.CPK'
CARDS = ('A0_2_1', 'B0_2_1', 'C0_2_1', 'D0_2_1', 'E0_2_1', 'F0_1_0')
OUT = Path(__file__).resolve().parent.parent.parent / 'launcher' / 'prologue_translation_table.inc'


def records(prepared):
    shared, only = C.pt_entries()
    english = C.english_sheets()
    pack = Cpk(Path(prepared) / BUILD / ARCHIVE, original=True)
    out = []
    try:
        for name in CARDS:
            story = ot.parse_story(pack.read(pack.find(name + '.txt')))
            resource = 'story/' + name.lower()
            for index, spanish in enumerate(story.records):
                portuguese = C.pt_for(resource, index, english[resource][index], shared, only)
                if portuguese is None:
                    raise ValueError(f'{resource}#{index}: no Portuguese text')
                out.append((name.lower(), index, spanish, portuguese))
    finally:
        pack.f.close()
    return out


def escape(text):
    return text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')


def write(prepared):
    rows = records(prepared)
    lines = ['{"%s", %d, "%s", "%s"},' % (resource, index, escape(spanish), escape(portuguese))
             for resource, index, spanish, portuguese in rows]
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    print(f'{len(rows)} prologue records -> {OUT}')


def check(prepared):
    problems = []
    try:
        rows = records(prepared)
    except (OSError, ValueError, KeyError) as error:
        return [f'prologue map: {error}']
    if len(rows) != 41:
        problems.append(f'prologue map: {len(rows)} records, expected 41')
    seen = set()
    for resource, index, spanish, portuguese in rows:
        if (resource, index) in seen:
            problems.append(f'{resource}#{index}: duplicated')
        seen.add((resource, index))
        if C.translatable(spanish) and ot.visible(spanish) == ot.visible(portuguese):
            problems.append(f'{resource}#{index}: replacement is the Spanish text')
        if ot.visible(spanish) in ('LA VALQUIRIA', 'EL PRÍNCIPE PUKA', 'TIERRA DE HADAS', 'LA ESPADA OSCURA',
                                   'EL DESTINO', 'APOCALIPSIS') and portuguese is None:
            problems.append(f'{resource}#{index}: title unmapped')
    try:
        table = OUT.read_text(encoding='utf-8').splitlines()
    except OSError:
        return problems + [f'{OUT.name}: missing']
    expected = ['{"%s", %d, "%s", "%s"},' % (resource, index, escape(spanish), escape(portuguese))
                for resource, index, spanish, portuguese in rows]
    if table != expected:
        problems.append(f'{OUT.name}: stale')
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('write')
    p.add_argument('--prepared', default=str(C.PREPARED))
    p = sub.add_parser('check')
    p.add_argument('--prepared', default=str(C.PREPARED))
    args = ap.parse_args()
    if args.cmd == 'write':
        write(Path(args.prepared))
        return
    problems = check(Path(args.prepared))
    for problem in problems:
        print('PROBLEM', problem)
    print(f'prologue map: {len(problems)} problems')
    sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
