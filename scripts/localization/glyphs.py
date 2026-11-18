#!/usr/bin/env python3
"""Portuguese (Brazil) letters for the game's own fonts. Every executable keeps static font descriptors: a 16-byte header
(texture size, texture count, cell size, columns, rows) and 256 code page pointers to 8-byte glyph records (atlas cell
u16, width u8, offset s8, metrics u32). Descriptors with the same code-to-cell map draw from the same atlases (the
BT/BOOT archive's HD_font_*.ftx). This tool finds them, the letters each family lacks, free atlas cells and how each
letter is composed from the font's own glyphs, and writes localization/pt-BR/font/glyphs.json: fingerprints and cell
numbers only, no pixels. launcher/latin_font.cpp draws the letters into the player's atlas; src/latin_glyphs.c adds
their records to the loaded descriptors. Japanese story files are Shift-JIS: unassigned codes of lead byte 0x81 are
given the letters in the executable's Shift-JIS table, for the Portuguese slot only.

    glyphs.py generate        from every prepared game folder present
    glyphs.py check           glyphs.json against the committed corpus: every letter has a recipe on every build listed
Needs numpy and Pillow (the Dragon's Crown .venv, or tools\\setup.ps1 -InstallPython).
"""
import argparse
import hashlib
import json
import struct
import sys
import unicodedata
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'prompts'))
import corpus as C  # noqa: E402
import odin_text as ot  # noqa: E402
import ps4_module  # noqa: E402
from cpk import Cpk  # noqa: E402

OUT = C.PT_DIR / 'font' / 'glyphs.json'
PORTUGUESE = 'áàâãéêíóôõúüçÁÀÂÃÉÊÍÓÔÕÚÜÇ'
MARKS = {'́': 'acute', '̀': 'grave', '̂': 'circumflex', '̃': 'tilde', '̈': 'diaeresis',
         '̧': 'cedilla'}
# Glyphs of the same mark, lowercase then capital, that a font may already hold: the mark is taken from them.
DONORS = {
    'tilde': ['ñn', 'ÑN', 'õo', 'ÕO', 'ãa', 'ÃA'],
    'circumflex': ['êe', 'âa', 'ôo', 'îi', 'ûu', 'ÊE', 'ÂA', 'ÔO', 'ÎI', 'ÛU'],
    'acute': ['ée', 'áa', 'óo', 'úu', 'ÉE', 'ÁA', 'ÓO', 'ÚU'],
    'grave': ['èe', 'àa', 'ùu', 'ÈE', 'ÀA'],
    'diaeresis': ['üu', 'ëe', 'äa', 'öo', 'ÜU', 'ËE', 'ÄA', 'ÖO'],
    'cedilla': ['çc', 'ÇC'],
}
# Otherwise the font's own punctuation: the first code it holds. Japanese fonts draw 0x7E as an overline.
PUNCTUATION = {'tilde': [0x301C, 0xFF5E, 0x7E], 'circumflex': [0x5E, 0xFF3E], 'acute': [0xB4], 'grave': [0x60, 0xFF40],
               'diaeresis': [0xA8], 'cedilla': [0x3001, 0x2C]}
SJIS_LEAD = 0x81


def required_letters():
    """Every accented letter of the corpus, and the Portuguese alphabet."""
    shared, _ = C.pt_entries()
    found = {ch for e in shared.values() for ch in e['pt_br'] if len(unicodedata.normalize('NFD', ch)) == 2}
    return sorted(found | set(PORTUGUESE), key=ord)


class Image:
    def __init__(self, eboot):
        mod = ps4_module.read_module(Path(eboot))
        self.elf = mod['elf']
        self.relocs = {t: a for t, _, _, a in mod['relocs']}
        ph, n = struct.unpack_from('<Q', self.elf, 32)[0], struct.unpack_from('<H', self.elf, 56)[0]
        self.segments = []
        for i in range(n):
            kind, _, off, vaddr, _, size, _, _ = struct.unpack_from('<IIQQQQQQ', self.elf, ph + i * 56)
            if kind in (1, 0x61000010):
                self.segments.append((off, vaddr, size))

    def read(self, va, size):
        for off, vaddr, length in self.segments:
            if vaddr <= va and va + size <= vaddr + length:
                return self.elf[off + va - vaddr:off + va - vaddr + size]
        return None

    def glyphs(self, desc):
        out = {}
        for page in range(256):
            va = self.relocs.get(desc + 0x10 + 8 * page)
            data = self.read(va, 2048) if va else None
            if not data:
                continue
            for c in range(256):
                cell, width = struct.unpack_from('<HB', data, 8 * c)
                if cell or width:
                    out[page * 256 + c] = cell
        return out

    def descriptors(self):
        found = []
        for t in sorted(self.relocs):
            head = self.read(t - 0x10, 16)
            if not head or head[:4] != bytes(4) or head[15] or not 1 <= head[8] <= 8:
                continue
            width, height = struct.unpack_from('<HH', head, 4)
            if width != height or width not in (0x400, 0x800, 0x1000) or head[9] != head[10] or not head[13]:
                continue
            page0 = self.read(self.relocs.get(t, 0), 2048)
            if not page0:
                continue
            a = [struct.unpack_from('<H', page0, 8 * c)[0] for c in range(0x41, 0x5B)]
            if a[0] and all(a[k] == a[0] + k for k in range(26)):
                found.append({'va': t - 0x10, 'header': head.hex(), 'size': width, 'textures': head[8],
                              'cell': head[9], 'cols': head[13], 'rows': head[14]})
        return found

    def sjis_table(self, code_size):
        """The page pointer table of the Shift-JIS decoder: lea rdx,[rip+X]; mov rcx,[rdx+rcx*8]; test rcx,rcx."""
        code = self.elf[0x4000:0x4000 + code_size]
        hits = []
        at = code.find(b'\x48\x8b\x0c\xca\x48\x85\xc9')
        while at != -1:
            if code[at - 7:at - 4] == b'\x48\x8d\x15':
                hits.append(at + struct.unpack_from('<i', code, at - 4)[0])
            at = code.find(b'\x48\x8b\x0c\xca\x48\x85\xc9', at + 1)
        return hits


def ink_cells(textures, cell, cols, rows):
    import numpy as np
    per = cols * rows
    found = set()
    for t, alpha in enumerate(textures):
        a = alpha > 8
        for k in range(per):
            y, x = (k // cols) * cell, (k % cols) * cell
            if a[y:y + cell, x:x + cell].any():
                found.add(t * per + k)
    return found


def atlas_alpha(data):
    import numpy as np
    import ftex
    return [np.array(ftex.decode(data, t))[..., 3] for t in ftex.textures(data)]


def free_cells(atlases, used, count):
    """Cells no glyph uses whose 4x4 blocks hold no other glyph's pixels: not in the row of the family's last glyph
    or the row under it, in every atlas of the family."""
    top = max(used)
    out, c = [], top + 1
    while len(out) < count:
        ok = True
        for a in atlases:
            per = a['cols'] * a['rows']
            if c >= per * a['textures']:
                ok = False
                break
            if c // per == top // per and (c % per) // a['cols'] <= (top % per) // a['cols'] + 1:
                ok = False
        if ok:
            out.append(c)
        c += 1
        if c > 70000:
            raise ValueError('no free cells')
    return out


def same_cells(family, other):
    """A descriptor of the family's atlas with at most two glyphs of its own: every shared code has the same cell."""
    shared = family.keys() & other.keys()
    return len(shared) >= 0.99 * len(family) and len(other.keys() - family.keys()) <= 2 and         all(family[c] == other[c] for c in shared)


def recipe(letter, glyphs):
    base, mark = unicodedata.normalize('NFD', letter)[0], MARKS[unicodedata.normalize('NFD', letter)[1]]
    capital = base.isupper()
    for pair in DONORS[mark]:
        donor, donor_base = pair
        if donor != letter and ord(donor) in glyphs and ord(donor_base) in glyphs and donor_base.isupper() == capital:
            return {'mark': mark, 'donor': ord(donor), 'donor_base': ord(donor_base)}
    for pair in DONORS[mark]:
        donor, donor_base = pair
        if donor != letter and ord(donor) in glyphs and ord(donor_base) in glyphs:
            return {'mark': mark, 'donor': ord(donor), 'donor_base': ord(donor_base)}
    for code in PUNCTUATION[mark]:
        if code in glyphs:
            return {'mark': mark, 'punctuation': code}
    return None


def build_entry(folder):
    build = ot.build_profile(folder)
    path = C.PREPARED / folder
    eboot = path / 'eboot.bin'
    image = Image(eboot)
    descriptors = image.descriptors()
    families = []
    for d in sorted(descriptors, key=lambda d: -d['size']):
        g = image.glyphs(d['va'])
        key = hashlib.sha256(json.dumps(sorted(g.items())).encode()).hexdigest()
        fam = next((f for f in families if f['map'] == key or same_cells(f['glyphs'], g)), None)
        if not fam:
            fam = {'map': key, 'glyphs': g, 'descriptors': []}
            families.append(fam)
        fam['descriptors'].append(d)
    fonts = {}
    for archive in ot.slot_archives(build, path):
        cpk = Cpk(archive)
        for e in cpk.entries:
            name = e['path'].lower()
            if name.startswith('hd_font_') and name.endswith('.ftx'):
                data = cpk.read(e)
                fonts[e['path']] = (archive.name, data, atlas_alpha(data))
    letters = required_letters()
    out = {'eboot_sha256': hashlib.sha256(eboot.read_bytes()).hexdigest(), 'label': build['label'], 'families': []}
    for fam in families:
        used = {cell for d in fam['descriptors'] for cell in image.glyphs(d['va']).values()}
        atlases = []
        for d in fam['descriptors']:
            for entry, (archive, data, textures) in fonts.items():
                if len(textures) != d['textures'] or textures[0].shape[0] != d['size']:
                    continue
                ink = ink_cells(textures, d['cell'], d['cols'], d['rows'])
                if any(a['entry'] == entry for a in atlases):
                    continue
                if len(ink & used) >= 0.97 * len(used) and len(ink - used) <= 0.03 * len(ink):
                    atlases.append({'archive': archive, 'entry': entry, 'size': len(data),
                                    'crc32': f'{zlib.crc32(data):08x}', 'textures': d['textures'],
                                    'cell': d['cell'], 'cols': d['cols'], 'rows': d['rows']})
        if not atlases:
            continue
        missing = [ch for ch in letters if ord(ch) not in fam['glyphs']]
        cells = free_cells(atlases, used, len(missing))
        glyphs = []
        for ch, cell in zip(missing, cells):
            r = recipe(ch, fam['glyphs'])
            if r is None:
                raise ValueError(f'{folder}: no mark for {ch}')
            base = unicodedata.normalize('NFD', ch)[0]
            r.update(code=ord(ch), cell=cell, template=ord(base), base_cell=fam['glyphs'][ord(base)])
            for key in ('donor', 'donor_base', 'punctuation'):
                if key in r:
                    r[key + '_cell'] = fam['glyphs'][r[key]]
            glyphs.append(r)
        out['families'].append({'map_sha256': fam['map'],
                                'descriptors': [{'va': f"{d['va']:#x}", 'header': d['header']} for d in fam['descriptors']],
                                'atlases': atlases, 'glyphs': glyphs,
                                'native': sorted(c for c in fam['glyphs'] if 0x7F < c < 0x250)})
    if not build['native_english']:
        tables = image.sjis_table(len(image.elf))
        if len(tables) != 1:
            raise ValueError(f'{folder}: Shift-JIS decoder table not found once ({len(tables)})')
        page = image.read(image.relocs[tables[0] + 8 * SJIS_LEAD], 512)
        free = [t for t in range(0x40, 0xFD) if t != 0x7F and not struct.unpack_from('<H', page, 2 * t)[0]]
        if len(free) < len(letters):
            raise ValueError(f'{folder}: too few free Shift-JIS codes')
        out['sjis'] = {'table_va': f'{tables[0]:#x}', 'lead': SJIS_LEAD,
                       'letters': {str(ord(ch)): trail for ch, trail in zip(letters, free)}}
    return out


def cmd_generate(args):
    builds = []
    for build in ot.builds():
        if not (C.PREPARED / build['folder'] / 'eboot.bin').is_file():
            print(f'{build["label"]:24} DATA UNAVAILABLE')
            continue
        entry = build_entry(build['folder'])
        builds.append(entry)
        print(f'{build["label"]:24} {len(entry["families"])} families, '
              f'{sum(len(f["glyphs"]) for f in entry["families"])} letters to compose'
              f'{", Shift-JIS codes " + str(len(entry["sjis"]["letters"])) if "sjis" in entry else ""}')
    C.write_json(OUT, {'letters': ''.join(required_letters()), 'builds': builds})
    return 0


def cmd_check(args):
    data = C.read_json(OUT)
    letters = required_letters()
    errors = []
    if set(letters) - set(data['letters']):
        errors.append('letters missing from glyphs.json: ' + ''.join(sorted(set(letters) - set(data['letters']))))
    for b in data['builds']:
        for f in b['families']:
            have = set(f['native']) | {g['code'] for g in f['glyphs']}
            lack = [ch for ch in letters if ord(ch) not in have]
            if lack:
                errors.append(f'{b["label"]}: family {f["map_sha256"][:8]} lacks {"".join(lack)}')
    for e in errors:
        print('error:', e)
    print(f'{len(data["builds"])} builds, {len(letters)} letters; {len(errors)} errors')
    return 1 if errors else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('generate')
    sub.add_parser('check')
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    return {'generate': cmd_generate, 'check': cmd_check}[args.command](args)


if __name__ == '__main__':
    sys.exit(main())
