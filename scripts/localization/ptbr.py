#!/usr/bin/env python3
"""Portuguese (Brazil): the port's own translation of the game's English text. localization/pt-BR/sheets/<kind>.json
holds one entry per distinct English text, bound to it by SHA-256; launcher/game_translation.cpp serves it over the
build's slot and keeps the slot's text for any record whose English differs.

    ptbr.py batches [--size N] [--wave names|rest]   context batches of the untranslated text (cache/localization/pt-BR)
    ptbr.py merge FILE.tsv...                        translations (id<TAB>text, \\n \\t \\\\ escapes) into sheets/
    ptbr.py check FILE.tsv... [--limits]             the merge checks, nothing written
    ptbr.py validate                                every entry against its English; must end with 0 errors
    ptbr.py generate [--build FOLDER]                the containers as the game will read them (cache only), rebuilt
                                                     and parsed back
    ptbr.py parity                                   per build: records served, kept and why
    ptbr.py progress                                 translated and remaining, per kind and resource
"""
import argparse
import csv
import re
import sys
from collections import Counter, OrderedDict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import corpus as C  # noqa: E402
import english  # noqa: E402
import odin_text as ot  # noqa: E402

WORK = C.CACHE / 'pt-BR'
KIND_ORDER = {'msgsheet': 0, 'story': 1, 'archive': 2, 'credits': 3}
NAME_SHEETS = ('msgsheet/ObjName.fms', 'msgsheet/ItemList.fms', 'msgsheet/Ability.fms', 'msgsheet/SystemMsg.fms',
               'msgsheet/BattleScore.fms')


def resource_order(resource):
    head = NAME_SHEETS.index(resource) if resource in NAME_SHEETS else len(NAME_SHEETS)
    return KIND_ORDER[ot.kind_of(resource)], head, resource


def units():
    """OrderedDict sha256 -> {'en', 'where': [(resource, index)]} of every translatable English text, in game order."""
    english_units = C.english_sheets()
    out = OrderedDict()
    for resource in sorted(english_units, key=resource_order):
        for i, text in enumerate(english_units[resource]):
            if C.translatable(text):
                out.setdefault(ot.sha256(text), {'en': text, 'where': []})['where'].append((resource, i))
    return out


def glossary():
    rows = []
    with open(C.PT_DIR / 'glossary.tsv', encoding='utf-8', newline='') as f:
        for row in csv.DictReader(f, delimiter='\t'):
            rows.append(row)
    return rows


def context_slots():
    """{slot: {resource: units}} of the European build's other languages, for context; empty without the game."""
    folder = C.PREPARED / C.CONTEXT_BUILD
    if not folder.is_dir():
        return {}
    build = ot.build_profile(C.CONTEXT_BUILD)
    out = {}
    for slot in C.CONTEXT_SLOTS:
        loaded, _ = ot.load_slot(dict(build, slot=slot), folder)
        out[slot] = {r: v[0] for r, v in loaded.items()}
    return out


def limits_of(resource, index, english_text, others):
    texts = [english_text]
    for slot in others.values():
        u = slot.get(resource)
        if u and index < len(u):
            texts.append(u[index])
    return C.line_limits(texts)


def is_name(text):
    v = ot.visible(text).strip()
    return '\n' not in v and len(v.split()) <= 4 and not ot.is_format(text)


def escape(text):
    return text.replace('\\', '\\\\').replace('\t', '\\t').replace('\n', '\\n')


def unescape(text):
    out, i = [], 0
    while i < len(text):
        c = text[i]
        if c == '\\' and i + 1 < len(text):
            nxt = text[i + 1]
            out.append({'n': '\n', 't': '\t', '\\': '\\'}.get(nxt, '\\' + nxt))
            i += 2
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def cmd_batches(args):
    table = units()
    shared, _ = C.pt_entries()
    others = context_slots()
    english_units = C.english_sheets()
    terms = [(r['source'], r['pt_br']) for r in glossary() if r['source'] and not r['source'].startswith('#')]
    names = {table[h]['en']: e['pt_br'] for h, e in shared.items() if h in table and is_name(table[h]['en'])}
    pending = [(h, u) for h, u in table.items() if h not in shared]
    if args.wave == 'names':
        pending = [(h, u) for h, u in pending if u['where'][0][0] in NAME_SHEETS and is_name(u['en'])]
    elif args.wave == 'rest':
        pending = [(h, u) for h, u in pending if not (u['where'][0][0] in NAME_SHEETS and is_name(u['en']))]
    out_dir = WORK / 'batches'
    out_dir.mkdir(parents=True, exist_ok=True)
    for old in out_dir.glob(f'{args.wave}-*'):
        old.unlink()
    batches, current, words = [], [], 0
    for h, u in pending:
        current.append((h, u))
        words += len(ot.visible(u['en']).split())
        if words >= args.size:
            batches.append(current)
            current, words = [], 0
    if current:
        batches.append(current)
    for n, batch in enumerate(batches, 1):
        lines = [f'# Batch {args.wave}-{n:03d}: {len(batch)} texts. Write one line per text to {args.wave}-{n:03d}.tsv:',
                 '# <id><TAB><Portuguese>, with \\n for a line break. Keep every control sequence, printf conversion',
                 '# and button tag exactly; do not exceed max_lines. Never translate from the French; it is context.', '']
        for h, u in batch:
            resource, index = u['where'][0]
            source = english_units[resource]
            before = source[index - 1] if index > 0 else ''
            after = source[index + 1] if index + 1 < len(source) else ''
            lim = limits_of(resource, index, u['en'], others)
            fr = others.get('FR', {}).get(resource)
            hits = sorted({f'{s} = {p}' for s, p in terms if re.search(r'\b' + re.escape(s) + r'\b', u['en'])} |
                          {f'{s} = {p}' for s, p in names.items() if len(s) > 3 and s != u['en'] and
                           re.search(r'\b' + re.escape(s) + r'\b', u['en'])})
            lines.append(f'@@ id={C.short(h)} at={resource}#{index} uses={len(u["where"])} max_lines={lim["max_lines"]} '
                         f'longest_line={lim["max_line"]}')
            if before:
                lines.append(f'   before: {escape(before)}')
            lines.append(f'   EN: {escape(u["en"])}')
            if after:
                lines.append(f'   after: {escape(after)}')
            if fr and index < len(fr):
                lines.append(f'   FR (context): {escape(fr[index])}')
            tokens = ot.control_tokens(u['en']) + ot.conversions(u['en'])
            if tokens:
                lines.append(f'   keep: {" ".join(tokens)}')
            if hits:
                lines.append(f'   glossary: {"; ".join(hits)}')
            lines.append('')
        (out_dir / f'{args.wave}-{n:03d}.txt').write_text('\n'.join(lines), encoding='utf-8', newline='\n')
    print(f'{len(pending)} texts in {len(batches)} batches under {out_dir}')
    return 0


def load_entries():
    entries = {kind: {} for kind in ot.KINDS}
    for kind in ot.KINDS:
        path = C.PT_DIR / 'sheets' / f'{kind}.json'
        if path.is_file():
            for e in C.read_json(path)['entries']:
                entries[kind][e['source_sha256'] if not e.get('only') else e['source_sha256'] + '|' + ','.join(e['only'])] = e
    return entries


def save_entries(entries, table):
    order = {h: n for n, h in enumerate(table)}
    for kind, items in entries.items():
        rows = sorted(items.values(), key=lambda e: (order.get(e['source_sha256'], 1 << 30), e.get('only', [])))
        C.write_json(C.PT_DIR / 'sheets' / f'{kind}.json', {'kind': kind, 'language': 'pt-BR', 'entries': rows})


def cmd_merge(args):
    table = units()
    by_short = {}
    for h in table:
        by_short.setdefault(C.short(h), []).append(h)
    entries = load_entries()
    others = context_slots() if args.limits else {}
    merged = refused = 0
    for path in args.files:
        for number, line in enumerate(Path(path).read_text(encoding='utf-8-sig').splitlines(), 1):
            if not line.strip() or line.startswith('#'):
                continue
            ident, _, text = line.partition('\t')
            ident = ident.strip().removeprefix('id=')
            full = by_short.get(ident)
            if not full or len(full) != 1:
                print(f'{path}:{number}: unknown id {ident}')
                refused += 1
                continue
            h = full[0]
            u = table[h]
            pt = C.match_line_breaks(table[full[0]]['en'], unescape(text.strip('\r')))
            resource, index = u['where'][0]
            limits = limits_of(resource, index, u['en'], others) if others else None
            problems = C.check_translation(u['en'], pt, limits)
            if problems:
                print(f'{path}:{number}: {resource}#{index}: {"; ".join(problems)}\n   EN: {escape(u["en"])}\n   PT: {escape(pt)}')
                refused += 1
                continue
            entries[ot.kind_of(resource)][h] = {'source_sha256': h, 'at': f'{resource}#{index}', 'pt_br': pt}
            merged += 1
    if args.command == 'merge':
        save_entries(entries, table)
    print(f'{"merged" if args.command == "merge" else "valid"} {merged}, refused {refused}')
    return 1 if refused else 0


def cmd_validate(args):
    table = units()
    errors = []
    seen = {}
    others = context_slots() if args.limits else {}
    english_units = C.english_sheets()
    for kind in ot.KINDS:
        path = C.PT_DIR / 'sheets' / f'{kind}.json'
        if not path.is_file():
            continue
        raw = path.read_bytes()
        try:
            raw.decode('utf-8')
        except UnicodeDecodeError as e:
            errors.append(f'{path.name}: not UTF-8 ({e})')
            continue
        for e in C.read_json(path)['entries']:
            h = e['source_sha256']
            key = (h, tuple(e.get('only', [])))
            if key in seen and seen[key] != e['pt_br']:
                errors.append(f'{path.name}: conflicting translations of {C.short(h)}')
            seen[key] = e['pt_br']
            if e.get('only'):
                targets = []
                for at in e['only']:
                    resource, index = at.rsplit('#', 1)
                    u = english_units.get(resource)
                    if not u or int(index) >= len(u) or ot.sha256(u[int(index)]) != h:
                        errors.append(f'{path.name}: {at}: source hash mismatch')
                    targets.append((resource, int(index)))
            elif h not in table:
                errors.append(f'{path.name}: {e.get("at", "?")}: source hash {C.short(h)} is not an English record')
                continue
            else:
                targets = table[h]['where'][:1]
            english_text = table[h]['en'] if h in table else english_units[targets[0][0]][targets[0][1]]
            if ot.kind_of(targets[0][0]) != kind:
                errors.append(f'{path.name}: {e.get("at")}: belongs in {ot.kind_of(targets[0][0])}.json')
            limits = limits_of(targets[0][0], targets[0][1], english_text, others) if others else None
            for p in C.check_translation(english_text, e['pt_br'], limits):
                errors.append(f'{targets[0][0]}#{targets[0][1]}: {p}\n   EN: {escape(english_text)}\n   PT: {escape(e["pt_br"])}')
    for e in errors[:args.show]:
        print('error:', e)
    print(f'{len(seen)} Portuguese entries for {len(table)} distinct English texts; {len(errors)} errors')
    return 1 if errors else 0


def served_units(resource, english_units_list, shared, only):
    out, n = [], 0
    for i, text in enumerate(english_units_list):
        pt = C.pt_for(resource, i, text, shared, only) if C.translatable(text) else None
        if pt is not None and not C.check_translation(text, pt):
            out.append(pt)
            n += 1
        else:
            out.append(text)
    return out, n


def cmd_generate(args):
    """The native-English slot of a build served in Portuguese: rebuild, parse back, compare unit by unit."""
    build = ot.build_profile(args.build)
    folder = C.PREPARED / args.build
    if not folder.is_dir():
        print(f'{args.build}: DATA UNAVAILABLE')
        return 1
    slot, _ = ot.load_slot(build, folder)
    shared, only = C.pt_entries()
    out_dir = WORK / 'generated' / args.build
    total = failures = 0
    for resource, (native, container, _) in slot.items():
        served, n = served_units(resource, native, shared, only)
        if not n:
            continue
        data = ot.rebuild_resource(resource, container, served)
        back, _ = ot.parse_resource(resource, data)
        if back != served:
            failures += 1
            print(f'{resource}: parse after rebuild differs')
        total += n
        target = out_dir / resource.replace('/', '_')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    print(f'{args.build}: {total} records served in Portuguese, {failures} containers failed; files in {out_dir}')
    return 1 if failures else 0


def cmd_parity(args):
    for build in ot.builds():
        result = english.coverage(build['folder'], 'pt')
        if result['state'] != 'PASS':
            print(f'{build["label"]:24} Portuguese (Brazil): {result["state"]}')
            continue
        slot = build['slot'] or 'Japanese'
        print(f'{build["label"]:24} Portuguese (Brazil) over the {slot} slot: {result["served"]}/{result["total"]} records '
              f'translated, {result["kept"]} keep the slot text')
        for why, n in result['reasons'].most_common(8):
            print(f'{"":26}{n:6}  {why}')
    return 0


def cmd_progress(args):
    table = units()
    shared, only = C.pt_entries()
    per = Counter()
    done = Counter()
    for h, u in table.items():
        kind = ot.kind_of(u['where'][0][0])
        per[kind] += 1
        done[kind] += h in shared
    records = Counter()
    records_done = Counter()
    for h, u in table.items():
        for resource, _ in u['where']:
            kind = ot.kind_of(resource)
            records[kind] += 1
            records_done[kind] += h in shared
    for kind in ot.KINDS:
        print(f'{kind:9} distinct texts {done[kind]:6}/{per[kind]:6}   records {records_done[kind]:6}/{records[kind]:6}')
    print(f'{"total":9} distinct texts {sum(done.values()):6}/{sum(per.values()):6}   '
          f'records {sum(records_done.values()):6}/{sum(records.values()):6}')
    if args.resources:
        rows = Counter()
        rows_done = Counter()
        for h, u in table.items():
            for resource, _ in u['where']:
                rows[resource] += 1
                rows_done[resource] += h in shared
        for resource in sorted(rows, key=resource_order):
            if rows_done[resource] < rows[resource]:
                print(f'  {resource:40} {rows_done[resource]:5}/{rows[resource]:5}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('batches')
    p.add_argument('--size', type=int, default=1200, help='English words per batch')
    p.add_argument('--wave', choices=('names', 'rest', 'all'), default='all')
    for name in ('merge', 'check'):
        p = sub.add_parser(name)
        p.add_argument('files', nargs='+')
        p.add_argument('--limits', action='store_true', help='check line counts against the shipped languages')
    p = sub.add_parser('validate')
    p.add_argument('--show', type=int, default=40)
    p.add_argument('--limits', action='store_true')
    p = sub.add_parser('generate')
    p.add_argument('--build', default=C.CONTEXT_BUILD)
    sub.add_parser('parity')
    p = sub.add_parser('progress')
    p.add_argument('--resources', action='store_true')
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    return {'batches': cmd_batches, 'merge': cmd_merge, 'check': cmd_merge, 'validate': cmd_validate, 'generate': cmd_generate,
            'parity': cmd_parity, 'progress': cmd_progress}[args.command](args)


if __name__ == '__main__':
    sys.exit(main())
