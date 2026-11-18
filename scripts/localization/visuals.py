#!/usr/bin/env python3
"""Inventory of the baked (texture) text of the European language slot, for the Portuguese (Brazil) visual patches.

Every texture pack (.ftx) of the German, French, Spanish and Italian archives (Odin2_RE_*, Odin2_OR_* and the 1.01
Odin2_EU_PATCH1_RE_*/OR_*), read as shipped (before any launcher layer), is compared with its English (UK) counterpart
texture by texture and put in one category, first rule that applies:

  B  language-independent art: byte-identical to the English pack
  F  duplicate variant: the same bytes or texture data as a pack already counted for the language, or the 1.01 update
     of a base pack
  G  not actually textual: differs from the English only in invisible or re-encoded pixels
  C  manual / help page (Other/HD_M*, Classic M*)
  D  title / menu label (title, press start, character/mode select)
  E  ending / credits
  A  textual and player-visible: the rest, whose visible pixels differ from the English

    visuals.py inventory game/prepared [--json cache/localization/pt-BR/visual-inventory.json] [--markdown out.md]

The CRILAYLA reader of scripts/prompts/cpk.py is pure Python; crilayla.c next to this file is built into
cache/localization when clang or gcc is on PATH and used instead (same algorithm, checked against the reference).
"""
import argparse
import ctypes
import io
import json
import os
import re
import shutil
import subprocess
import sys
import zlib
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / 'scripts' / 'prompts'))
import cpk
import ftex

LANGUAGES = ('DE', 'FR', 'ES', 'IT')
ARCHIVES = (('CUSA05262-1.00', 'Odin2_RE_{}.CPK'), ('CUSA05262-1.00', 'Odin2_OR_{}.CPK'),
            ('CUSA05262-1.01', 'Odin2_EU_PATCH1_RE_{}.CPK'), ('CUSA05262-1.01', 'Odin2_EU_PATCH1_OR_{}.CPK'))
CATEGORIES = {
    'A': 'textual and player-visible',
    'B': 'language-independent art',
    'C': 'manual / help page',
    'D': 'title / menu label',
    'E': 'ending / credits',
    'F': 'duplicate variant',
    'G': 'not actually textual',
}
MANUAL = re.compile(r'^(?:other/hd_m\d+|m\d+)\.ftx$', re.I)
MENU = re.compile(r'(?:^|/)(?:hd_)?(?:title|op_press_start|chrselect|mode_select)\.ftx$', re.I)
ENDING = re.compile(r'(?:^|/)(?:hd_)?(?:ending\w*|staffroll\w*|credit\w*|fin\d*)\.ftx$', re.I)
VISIBLE_ALPHA = 24
VISIBLE_DELTA = 48
CACHE = ROOT / 'cache' / 'localization'


# ---------------------------------------------------------------- fast reading

def native_crilayla():
    """The C decompressor as a ctypes function, built into the cache on first use; None when no compiler is found."""
    source = Path(__file__).with_name('crilayla.c')
    library = CACHE / ('crilayla.dll' if os.name == 'nt' else 'crilayla.so')
    if not library.is_file() or library.stat().st_mtime < source.stat().st_mtime:
        compiler = shutil.which('clang') or shutil.which('gcc') or next(
            (str(p) for p in (Path(r'C:\msys64\clang64\bin\clang.exe'), Path(r'C:\msys64\mingw64\bin\gcc.exe')) if p.is_file()), None)
        if not compiler:
            return None
        CACHE.mkdir(parents=True, exist_ok=True)
        env = dict(os.environ, PATH=str(Path(compiler).parent) + os.pathsep + os.environ.get('PATH', ''))
        built = subprocess.run([compiler, '-O2', '-shared', '-o', str(library), str(source)], env=env, capture_output=True)
        if built.returncode:
            return None
    if os.name == 'nt' and hasattr(os, 'add_dll_directory'):
        for directory in (Path(r'C:\msys64\clang64\bin'), Path(r'C:\msys64\mingw64\bin')):
            if directory.is_dir():
                os.add_dll_directory(str(directory))
    lib = ctypes.CDLL(str(library))
    lib.crilayla_decompress.argtypes = [ctypes.c_char_p, ctypes.c_int64, ctypes.c_void_p, ctypes.c_int64]
    lib.crilayla_decompress.restype = ctypes.c_int

    def decompress(data):
        out_size = int.from_bytes(data[8:12], 'little')
        out = ctypes.create_string_buffer(out_size + 0x100)
        if lib.crilayla_decompress(bytes(data), len(data), out, len(out)):
            raise ValueError('CRILAYLA stream is damaged')
        return out.raw
    return decompress


def use_native_reader():
    fast = native_crilayla()
    if not fast:
        print('note: no C compiler found; reading compressed entries in pure Python (slow)', file=sys.stderr)
        return False
    probe = cpk._crilayla_literals(bytes(range(256)) * 3 + b'odin sphere' * 40)
    assert fast(probe) == cpk.crilayla_decompress(probe)
    cpk.crilayla_decompress = fast
    return True


_OFFSETS = {}


def tiled_offsets(bw, bh, pitch_blocks):
    """Byte offset of every block (row major) in a Thin_1dThin surface of 16-byte elements (ftex.tiled_offset, vectorised)."""
    key = (bw, bh, pitch_blocks)
    if key not in _OFFSETS:
        by, bx = np.mgrid[0:bh, 0:bw]
        x, y = bx & 7, by & 7
        micro = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)
        tile = (by // 8) * (pitch_blocks // 8) + (bx // 8)
        _OFFSETS[key] = (tile * 64 * 16 + micro * 16).ravel()
    return _OFFSETS[key]


def blocks(blob, tex):
    """The texture's 16-byte blocks in row-major order: uint8 [bh, bw, 16]."""
    bw, bh = tex['width'] // 4, tex['height'] // 4
    raw = np.frombuffer(blob, np.uint8, tex['size'], tex['data'])
    offsets = tiled_offsets(bw, bh, tex['pitch'] // 4)
    idx = offsets[:, None] + np.arange(16)
    return raw[idx].reshape(bh, bw, 16)


def decode(blob, tex):
    """RGBA uint8 of a BC texture, through Pillow's DDS reader."""
    linear = blocks(blob, tex).tobytes()
    image = Image.open(io.BytesIO(ftex.dds_wrap(linear, tex['width'], tex['height'], tex['dfmt'])))
    return np.asarray(image.convert('RGBA'))


# ---------------------------------------------------------------- comparison

def texture_change(a_blob, a_tex, b_blob, b_tex):
    """How a language texture differs from the English one: None when the bytes are equal, else a dict with the changed
    block count, the visibly changed pixel count and their box."""
    if (a_tex['width'], a_tex['height'], a_tex['dfmt']) != (b_tex['width'], b_tex['height'], b_tex['dfmt']):
        return dict(size=f"{a_tex['width']}x{a_tex['height']} vs {b_tex['width']}x{b_tex['height']}", visible=-1, blocks=-1)
    ab, bb = blocks(a_blob, a_tex), blocks(b_blob, b_tex)
    differ = (ab != bb).any(axis=-1)
    if not differ.any():
        return None
    a, b = decode(a_blob, a_tex).astype(np.int16), decode(b_blob, b_tex).astype(np.int16)
    mask = np.repeat(np.repeat(differ, 4, 0), 4, 1)
    alpha = np.maximum(a[..., 3], b[..., 3])
    colour = np.abs(a[..., :3] - b[..., :3]).max(axis=-1) * alpha // 255
    visible = mask & (alpha > VISIBLE_ALPHA) & ((np.abs(a[..., 3] - b[..., 3]) > VISIBLE_DELTA) | (colour > VISIBLE_DELTA))
    out = dict(blocks=int(differ.sum()), visible=int(visible.sum()))
    if visible.any():
        ys, xs = np.nonzero(visible)
        out['box'] = [int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1]
    return out


def sibling_mbs(path, names):
    """The layout (.mbs) that draws a pack, as the archive spells it: GUI/HD_X.ftx -> GUI/REHD_X.mbs (every
    HD_StageNameNN -> REHD_StageName.mbs), X.ftx -> X.mbs; None when the archive has none."""
    folder, _, name = path.rpartition('/')
    stem = name[:-4]
    if stem.lower().startswith('hd_'):
        stem = 'REHD_' + re.sub(r'(?i)^(StageName)\d.*$', r'\1', stem[3:])
    wanted = (folder + '/' if folder else '') + stem + '.mbs'
    return names.get(wanted.lower())


def classify(path, record, seen, bases):
    """(category, reason) of one language pack."""
    if record['same_as_english']:
        return 'B', 'byte-identical to the English pack'
    if record['crc32'] in seen:
        return 'F', f'same bytes as {seen[record["crc32"]]}'
    if record['pixels_crc32'] in seen:
        return 'F', f'same texture data as {seen[record["pixels_crc32"]]}'
    if record['archive'].startswith('Odin2_EU_PATCH1_') and path.lower() in bases:
        return 'F', f'1.01 update of {bases[path.lower()]}'
    if record['english'] is None:
        return 'A' if not MANUAL.search(path) else 'C', 'no English counterpart'
    shared = record['visible_pixels'] == 0
    if shared and not record['layout_differs']:
        return 'G', 'no visible pixel differs from the English and the layout is the same'
    how = ('every language\'s sprites in one atlas; the language\'s layout ' + record['layout'] + ' picks its own') if shared else \
        f'{record["visible_pixels"]} visible pixels differ in {len(record["differing"])} texture(s)'
    if any(d.get('missing') for d in record['differing']):
        how = 'atlas laid out differently from the English'
    elif any(d['visible'] < 0 for d in record['differing']):
        how = 'texture sizes differ from the English'
    if MANUAL.search(path):
        return 'C', f'manual page: {how}'
    if MENU.search(path):
        return 'D', f'title / menu art: {how}'
    if ENDING.search(path):
        return 'E', f'ending art: {how}'
    return 'A', how


def inventory(prepared):
    records = []
    for language in LANGUAGES:
        seen, bases = {}, {}
        for build, pattern in ARCHIVES:
            path = Path(prepared) / build / pattern.format(language)
            english_path = Path(prepared) / build / pattern.format('UK')
            if not path.is_file() or not english_path.is_file():
                print(f'{language}: {path.name} not prepared', file=sys.stderr)
                continue
            pack, english = cpk.Cpk(path, original=True), cpk.Cpk(english_path, original=True)
            by_path = {e['path'].lower(): e for e in english.entries}
            layouts = {e['path'].lower(): e for e in pack.entries if e['path'].lower().endswith('.mbs')}
            for entry in pack.entries:
                if not entry['path'].lower().endswith('.ftx'):
                    continue
                data = pack.read(entry)
                other = by_path.get(entry['path'].lower())
                english_data = english.read(other) if other else None
                textures = ftex.textures(data)
                pixels = 0
                for tex in textures:
                    pixels = zlib.crc32(data[tex['data']:tex['data'] + tex['size']], pixels)
                record = dict(language=language, build=build, archive=path.name, path=entry['path'], size=len(data),
                              crc32=f'{zlib.crc32(data):08x}', pixels_crc32=f'p{pixels:08x}', textures=len(textures),
                              english=None if other is None else dict(archive=english_path.name, size=len(english_data),
                                                                      crc32=f'{zlib.crc32(english_data):08x}'),
                              same_as_english=data == english_data, differing=[], visible_pixels=0)
                if english_data is not None and data != english_data:
                    theirs = {t['name']: t for t in ftex.textures(english_data)}
                    for tex in textures:
                        mine = theirs.get(tex['name'])
                        change = dict(missing=True, visible=-1) if mine is None else texture_change(data, tex, english_data, mine)
                        if change:
                            record['differing'].append(dict(texture=tex['name'], width=tex['width'], height=tex['height'], **change))
                    record['visible_pixels'] = sum(max(d['visible'], 0) for d in record['differing'])
                    if any(d['visible'] < 0 for d in record['differing']):
                        record['visible_pixels'] = max(record['visible_pixels'], 1)
                layout = sibling_mbs(entry['path'], layouts)
                record['layout'], record['layout_differs'] = None, False
                if layout:
                    theirs = by_path.get(layout['path'].lower())
                    record['layout'] = layout['path']
                    record['layout_differs'] = theirs is None or pack.read(layout) != english.read(theirs)
                record['category'], record['reason'] = classify(entry['path'], record, seen, bases)
                seen.setdefault(record['crc32'], f'{path.name}:{entry["path"]}')
                seen.setdefault(record['pixels_crc32'], f'{path.name}:{entry["path"]}')
                if not path.name.startswith('Odin2_EU_PATCH1_'):
                    bases.setdefault(entry['path'].lower(), f'{path.name}:{entry["path"]}')
                records.append(record)
                print(f'{language} {path.name:28s} {entry["path"]:36s} {record["category"]} {record["reason"]}', flush=True)
            pack.f.close()
            english.f.close()
    return records


def counts(records):
    table = {}
    for language in LANGUAGES:
        mine = [r for r in records if r['language'] == language]
        table[language] = dict(Counter(r['category'] for r in mine), total=len(mine))
    return table


def markdown(table):
    lines = ['| Language | ' + ' | '.join(sorted(CATEGORIES)) + ' | Total |',
             '| --- | ' + ' | '.join('---:' for _ in CATEGORIES) + ' | ---: |']
    for language, row in table.items():
        lines.append(f'| {language} | ' + ' | '.join(str(row.get(k, 0)) for k in sorted(CATEGORIES)) + f' | {row["total"]} |')
    return '\n'.join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('inventory')
    p.add_argument('prepared')
    p.add_argument('--json', default=str(CACHE / 'pt-BR' / 'visual-inventory.json'))
    args = ap.parse_args()
    use_native_reader()
    records = inventory(args.prepared)
    out = dict(categories=CATEGORIES, counts=counts(records), packs=records)
    Path(args.json).parent.mkdir(parents=True, exist_ok=True)
    Path(args.json).write_text(json.dumps(out, indent=1), encoding='utf-8')
    print(markdown(out['counts']))
    print(f'-> {args.json}')


if __name__ == '__main__':
    main()
