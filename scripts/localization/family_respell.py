#!/usr/bin/env python3
"""Portuguese (Brazil) words of the layouts that spell them from a glyph font every language shares (Mode Select's
illuminated letters, the Classic butterfly screen): localization/pt-BR/visual/<family>-ptbr.oslv.

Each family names its layouts, what every European language's layers read (TEXTS, the evidence for which sprite is
which letter, glyph_table.py) and the Portuguese of each English line. The rest is make_visuals.py's: the font is learned
from the five layouts, each line may be as wide as the widest official line of its slot, and the atlas is not touched.

    family_respell.py mode_select [--prepared game/prepared] [--report out.tsv]
"""
import argparse
import struct
from collections import Counter
import sys
import zlib
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'prompts'))
import glyph_table  # noqa: E402
import layout_text as T  # noqa: E402
import make_visuals as V  # noqa: E402
import mbs as M  # noqa: E402
import oslv  # noqa: E402
import stage_acts  # noqa: E402
import visuals  # noqa: E402

ROOT = HERE.parent.parent
VISUAL = ROOT / 'localization' / 'pt-BR' / 'visual'


def _mode_texts():
    genre = dict(UK='RPG ACTION', ES='RPG ACCIÓN', FR='JDR ACTION', DE='RPG AKTION')
    names = dict(UK=('Easy', 'Normal', 'Hard', 'Heroic'), ES=('Fácil', 'Normal', 'Difícil', 'Heroica'),
                 FR=('Facile', 'Normal', 'Difficile', 'Héroïque'), DE=('Leicht', 'Normal', 'Schwer', 'Heldenhaft'),
                 IT=('Facile', 'Normale', 'Difficile', 'Infernale'))
    out = {}
    for language in V.LANGUAGES:
        layers = {layer: 'RPG' for layer in (16, 17, 18, 20)}
        if language in genre:
            layers.update({layer: genre[language] for layer in range(21, 26)})
        for n, name in enumerate(names[language]):
            layers.update({layer: name for layer in range(28 + 3 * n, 31 + 3 * n)})
        out[language] = layers
    return out


FAMILIES = {
    'mode_select': dict(
        layouts=(('GUI/REHD_Mode_Select.mbs', 'GUI/HD_Mode_Select.ftx'), ('Mode_Select.mbs', 'Mode_Select.ftx')),
        texts=_mode_texts(), largest=100,
        labels={'ACTION': 'AÇÃO', 'Easy': 'Fácil', 'Hard': 'Difícil', 'Heroic': 'Heroico'},
        hook=((0, 1668, 758, 1686, 817), 'bottom', 0.4)),
    'kitchen': dict(
        layouts=(('GUI/REHD_Kitchin.mbs', 'GUI/HD_Kitchin.ftx'),),
        texts=dict(ES={6: 'Receta', 7: 'Precio'}, FR={6: 'Recette', 7: 'Prix'}, DE={6: 'Rezept', 7: 'Preis'},
                   IT={6: 'Ricetta', 7: 'Prezzo'}),
        largest=70, sprites={(0, 1470, 270, 1586, 336): 'Recipe', (0, 1462, 898, 1582, 958): 'Price'},
        labels={'Recipe': 'Receita', 'Price': 'Preço'}, peer_line='ES', slots='kitchen',
        hook=((0, 1444, 294, 1468, 328), 'top', 0.3)),
}


def bare(text):
    """Letters without accents and with i for a dotless stem: what a composed mark leaves for the reader."""
    import unicodedata
    return ''.join(c for c in unicodedata.normalize('NFD', text) if not unicodedata.combining(c)).replace('l', 'i').replace(' ', '')


def cedilla_glyph(template, sprite, end, rows):
    """A cedilla cut from a glyph of the face (`sprite`, upright in the atlas): its top or bottom `rows` (the lower
    hook of a closing parenthesis, the dot of a script i), standing under the letter."""
    t, x0, y0, x1, y1 = sprite
    if end == 'top':
        y1 = y0 + (y1 - y0) * rows
    top = y1 - (y1 - y0) * (1.0 if end == 'top' else rows)
    corners = [(x0, top), (x0, y1), (x1, y1), (x1, top)]
    part = struct.pack('<12f', (x0 + x1) / 2, (top + y1) / 2, *[v for c in corners for v in c], *corners[0])
    return T.Glyph(',', t, T.corner_pattern([(0, 0), (0, 1), (1, 1), (1, 0)]), part, template.key, x1 - x0, y1 - top,
                   (y1 - top) * 0.6)


def slot_layout(family, archives, data, layout_name):
    """The English layout with sprite_slots.py's key swaps of `family` (its words painted into other languages' slots)."""
    import sprite_slots
    spec = sprite_slots.FAMILIES[family]
    out = data
    for word in spec['words']:
        if word.slot != word.english:
            other = archives.read(V.EUROPE, [a.format(word.slot_language) for a in V.EUROPE_ARCHIVES], layout_name)
            out = sprite_slots.swap_keys(out, other, word)
    return out


def on_peer_line(plan, peer_layout, reader):
    """A word sprite's plan set on the line the peer language spells in the same layer: its scale, baseline and left
    edge (an English word sprite says nothing about letter size)."""
    lines = T.lines(peer_layout, reader, [plan.line.layer])
    if len(lines) != 1:
        return plan
    peer = lines[0]
    plan.line = T.Line(plan.line.layer, plan.line.keys, plan.line.text, peer.box, peer.scale, 1.0, peer.baseline,
                       plan.line.colour)
    plan.width = (peer.box[2] - peer.box[0]) * 1.4
    plan.left = peer.box[0]
    plan.lean = T.shear(peer_layout, T.passes(peer_layout, peer, reader)[0])
    return plan


def own_gap(layout, line, reader):
    """The median gap between the letters of a line as the game sets it, in atlas pixels; None for a single letter."""
    first = T.passes(layout, line, reader)[0]
    boxes = sorted(T.quad_box(layout.geom(layout.key(k)['geom'])) for k in first
                   if not T.is_mark(reader.char(layout, k) or ''))
    gaps = [(b[0] - a[2]) / line.scale for a, b in zip(boxes, boxes[1:])]
    return sorted(gaps)[len(gaps) // 2] if gaps else None


def build(family, prepared, out, report_path):
    spec = FAMILIES[family]
    archives = V.Archives(prepared)
    targets, report = [], []
    for mbs_name, ftx_name in spec['layouts']:
        europe = {}
        for language in V.LANGUAGES:
            data = archives.read(V.EUROPE, [a.format(language) for a in V.EUROPE_ARCHIVES], mbs_name)
            if data is not None:
                europe[language] = M.Mbs(data)
        if set(europe) != set(V.LANGUAGES):
            report.append((mbs_name, 'layout', '', '', 'DATA UNAVAILABLE: not in every European language'))
            continue
        table, notes = glyph_table.build(europe, spec['texts'], spec['largest'])
        report += [(mbs_name, 'glyphs', n, '', '') for n in notes]
        glyphs = {k: v for k, v in table.items() if not v.startswith('#')}
        variants = {}
        for sprite, mark in table.items():
            if mark.startswith('#'):
                glyphs[sprite] = '#stroke'
                for t in ((-1, -1), (1, -1)), ((-1, 1), (1, 1)), T.UPRIGHT:
                    variants[('#stroke', t)] = mark[1:]
        reader = V.Reader(glyphs, variants)
        font = T.learn(list(europe.values()), reader)
        eu_pixels = V.atlas(archives.read(V.EUROPE, [a.format('UK') for a in V.EUROPE_ARCHIVES], ftx_name))
        others = [europe[L] for L in V.LANGUAGES[1:]]
        layouts = {id(m): m for m in others}
        readers = {id(m): reader for m in others}
        labels = {V.compact(k): dict(pt_br=v) for k, v in spec['labels'].items()}
        for build_name, names in V.ENGLISH.items():
            for archive in names:
                c = archives.get(build_name, archive)
                if c is None:
                    report.append((build_name, 'build', archive, '', 'DATA UNAVAILABLE'))
                    continue
                hit = next((e for e in c.entries if e['path'].lower() == mbs_name.lower()), None)
                if hit is None:
                    continue
                data = c.read(hit)
                pixels = V.atlas(archives.read(build_name, [archive] + list(names), ftx_name))
                start = data
                if spec.get('slots'):
                    try:
                        start = slot_layout(spec['slots'], archives, data, mbs_name)
                    except ValueError as e:
                        report.append((f'{build_name} {archive}', 'layout', mbs_name, '', f'BLOCKED: {e}'))
                        continue
                layout = M.Mbs(start)
                hook, hook_end, hook_rows = spec.get('hook') or (None, None, None)
                wanted = {**glyphs, **({hook: '#hook'} if hook else {}), **{k: '#sprite' for k in spec.get('sprites', {})}}
                found = V.matches(wanted, eu_pixels, layout, pixels)
                for key in sorted(set(wanted) - set(found)):
                    t, x0, y0, x1, y1 = key
                    for q in range(4):
                        sprite = np.rot90(eu_pixels[t][y0:y1, x0:x1, 3], q) if t < len(pixels) else None
                        at = None if sprite is None else stage_acts.find_sprite(pixels[t][..., 3], sprite)
                        if at:
                            found[key] = ((t, at[0], at[1], at[0] + sprite.shape[1], at[1] + sprite.shape[0]), q)
                            break
                local = V.Reader({found[k][0]: ch for k, ch in glyphs.items() if k in found}, variants)
                drawn = V.target_font(font, found)
                if hook in found and found[hook][1] == 0 and drawn.glyphs:
                    drawn.glyphs[','] = cedilla_glyph(next(iter(drawn.glyphs.values())), found[hook][0], hook_end, hook_rows)
                elif hook:
                    report.append((build_name, 'glyphs', archive, '', 'BLOCKED: no parenthesis in the atlas for the cedilla'))
                    continue
                peers = V.layer_peers(layout, others)
                where = f'{build_name} {archive} {mbs_name}'
                sprites = {found[k][0]: name for k, name in spec.get('sprites', {}).items() if k in found}
                plans = V.plan_layout(layout, local, drawn, labels, peers, layouts, readers, sprites, report, where)
                if spec.get('peer_line'):
                    plans = [on_peer_line(p, europe[spec['peer_line']], reader) for p in plans]
                if not plans:
                    report.append((where, 'layout', mbs_name, '', 'nothing to respell'))
                    continue
                for plan in plans:
                    if not spec.get('peer_line'):
                        plan.gap = own_gap(layout, plan.line, local)
                T.respell(layout, drawn, local, plans)
                new = layout.bytes()
                check = M.Mbs(new)
                written = Counter(''.join(bare(T.line_text(check, l, local)) for l in T.lines(check, local)))
                missing = sorted({p.text for p in plans if Counter(bare(p.text)) - written})
                if missing:
                    report.append((where, 'verify', ', '.join(missing[:8]), '', 'reads back differently'))
                targets.append((mbs_name, data, new, layout.inserts))
                print(f'{where}: {len(plans)} lines, {len(data)} -> {len(new)} bytes')
    unique = {}
    for name, data, new, inserts in targets:
        unique.setdefault((name.lower(), len(data), zlib.crc32(data)), (name, data, new, inserts))
    blob = oslv.encode(list(unique.values()))
    Path(out).write_bytes(blob)
    print(f'-> {out} ({len(blob)} bytes, {len(unique)} targets)')
    if report_path:
        with open(report_path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('where\tkind\tenglish\tpt_br\tnote\n' + ''.join('\t'.join(r) + '\n' for r in report))
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('family', choices=sorted(FAMILIES))
    ap.add_argument('--prepared', default=str(ROOT / 'game' / 'prepared'))
    ap.add_argument('--out')
    ap.add_argument('--report')
    args = ap.parse_args()
    visuals.use_native_reader()
    out = args.out or VISUAL / f'{args.family.replace("_", "-")}-ptbr.oslv'
    report = build(args.family, args.prepared, out, args.report)
    blocked = [r for r in report if 'BLOCKED' in r[4] or 'reads back' in r[4]]
    for r in blocked:
        print(*r)
    return 1 if blocked else 0


if __name__ == '__main__':
    sys.exit(main())
