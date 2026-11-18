#!/usr/bin/env python3
"""The text containers of Odin Sphere Leifthrasir and the control syntax inside them. launcher/game_text.cpp reads and
writes the same layouts.

  FMSB sheets   msgsheet/*.fms: header, a pointer table the game fills in (zeros on disk), NUL-terminated UTF-8 records
                padded to 16 bytes, then the FEOC trailer. A record's position is its ID.
  story text    <area>.txt, EventTxt/<area>.txt, Sysmsg.txt: a 0x40-byte header holding the record count at 0x10, then
                the records as NUL-terminated UTF-8. Record N is message N of the event script of the same name.
  archive pages ArchiveViewer/*.txt: UTF-8 scripts (BOM, CRLF). Only the string of @TITLE("...") and the body of each
                @TEXT( ... ) block are text; the rest is layout.

Resource keys name a container independently of the archive and build: msgsheet/SystemMsg.fms, story/a1_1,
ArchiveViewer/Cafe00.txt.

    odin_text.py roundtrip <prepared build folder>...   parse and rebuild every container, byte for byte
    odin_text.py inventory <prepared build folder> [--slot US] [--json out.json]
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts' / 'prompts'))
from cpk import Cpk  # noqa: E402

FMS_HEADER = 0x20
STORY_HEADER = 0x40
STORY_COUNT_AT = 0x10
BOM = b'\xef\xbb\xbf'

# ------------------------------------------------------------------ containers


@dataclass
class Fms:
    header: bytes
    records: list
    trailer: bytes


def parse_fms(data):
    if len(data) < FMS_HEADER or data[:4] != b'FMSB':
        raise ValueError('not an FMSB sheet')
    header, count = struct.unpack_from('<I', data, 8)[0], struct.unpack_from('<I', data, 0x14)[0]
    trailer = struct.unpack_from('<I', data, 4)[0] + FMS_HEADER
    start = header + count * 8
    if header < FMS_HEADER or start > trailer or trailer + 4 > len(data) or data[trailer:trailer + 4] != b'FEOC':
        raise ValueError('header offsets out of range')
    if any(data[header:start]):
        raise ValueError('pointer table not empty')
    parts = data[start:trailer].split(b'\0')
    if len(parts) <= count or any(parts[count:]):
        raise ValueError('records do not end in padding')
    return Fms(data[:header], [p.decode('utf-8') for p in parts[:count]], data[trailer:])


def build_fms(sheet):
    out = bytearray(sheet.header)
    out += bytes(len(sheet.records) * 8)
    for record in sheet.records:
        out += record.encode('utf-8') + b'\0'
    out += bytes(-len(out) % 16)
    struct.pack_into('<I', out, 4, len(out) - FMS_HEADER)
    struct.pack_into('<I', out, 0x14, len(sheet.records))
    return bytes(out + sheet.trailer)


@dataclass
class Story:
    header: bytes
    records: list
    padding: int = 0
    encoding: str = 'utf-8'  # the Japanese executable reads its story text as Shift-JIS (cp932)


def parse_story(data):
    if len(data) < STORY_HEADER:
        raise ValueError('shorter than the header')
    count = struct.unpack_from('<I', data, STORY_COUNT_AT)[0]
    body = data[STORY_HEADER:]
    parts = body.split(b'\0')
    if len(parts) <= count or any(parts[count:]):
        raise ValueError(f'{count} records do not fill the file')
    raw = parts[:count]
    try:
        records, encoding = [p.decode('utf-8') for p in raw], 'utf-8'
    except UnicodeDecodeError:
        records, encoding = [p.decode('cp932') for p in raw], 'cp932'
    used = sum(len(p) + 1 for p in raw)
    return Story(data[:STORY_HEADER], records, len(body) - used, encoding)


def build_story(story):
    out = bytearray(story.header)
    struct.pack_into('<I', out, STORY_COUNT_AT, len(story.records))
    for record in story.records:
        out += record.encode(story.encoding) + b'\0'
    return bytes(out + bytes(story.padding))


@dataclass
class Page:
    """An ArchiveViewer script: `parts` alternates layout and text, starting and ending with layout."""
    bom: bool
    parts: list
    kinds: list = field(default_factory=list)  # 'title' or 'text', one per text part

    @property
    def texts(self):
        return self.parts[1::2]


_PAGE_UNIT = re.compile(r'(@TITLE\(\s*")([^"\r\n]*)(")|(@TEXT\()(.*?)(\r?\n[ \t]*\))', re.S)


def _strip_comments(text):
    """The text with // comments blanked to spaces, so positions stay the same."""
    return re.sub(r'//[^\r\n]*', lambda m: ' ' * len(m.group(0)), text)


def parse_page(data):
    bom = data.startswith(BOM)
    text = data[len(BOM) if bom else 0:].decode('utf-8')
    scan = _strip_comments(text)
    parts, kinds, at = [], [], 0
    for m in _PAGE_UNIT.finditer(scan):
        if m.group(1):
            start, end, kind = m.start(2), m.end(2), 'title'
        else:
            start, end, kind = m.start(5), m.end(5), 'text'
        parts.append(text[at:start])
        parts.append(text[start:end])
        kinds.append(kind)
        at = end
    parts.append(text[at:])
    return Page(bom, parts, kinds)


def build_page(page):
    return (BOM if page.bom else b'') + ''.join(page.parts).encode('utf-8')


def page_with(page, texts):
    parts = list(page.parts)
    parts[1::2] = texts
    return Page(page.bom, parts, page.kinds)


_HEADING = re.compile(r'(?m)^([ \t]*@F1)([^\r\n]*)')


def parse_credits(data):
    """StaffRoll/*.txt: one credit per line; only an @F1 line's heading (a role or a character) is text, names in the
    other @F styles stay as they are."""
    bom = data.startswith(BOM)
    text = data[len(BOM) if bom else 0:].decode('utf-8')
    parts, kinds, at = [], [], 0
    for m in _HEADING.finditer(text):
        parts += [text[at:m.start(2)], m.group(2)]
        kinds.append('heading')
        at = m.end(2)
    parts.append(text[at:])
    return Page(bom, parts, kinds)


def page_keys(page):
    """Each text unit's place in the book: page number, frame, kind and its order among units of that place. Two
    languages' pages lay out their frames differently, but a unit with the same key holds the same text."""
    keys, seen = [], {}
    page_no, frame = 0, -1
    for i, kind in enumerate(page.kinds):
        layout = _strip_comments(page.parts[2 * i])
        for m in re.finditer(r'@PAGE_BEGIN|@FRAME\(\s*(\d+)\s*\)', layout):
            if m.group(0) == '@PAGE_BEGIN':
                page_no, frame = page_no + 1, -1
            else:
                frame = int(m.group(1))
        place = (page_no, frame, kind)
        seen[place] = seen.get(place, -1) + 1
        keys.append(place + (seen[place],))
    return keys


def page_layout(page):
    """The script without its text and comments: what must agree before another slot's page takes our text."""
    layout = _strip_comments(''.join(page.parts[0::2]))
    return re.sub(r'\s+', ' ', layout).strip()


# ------------------------------------------------------------------ resources

KINDS = ('msgsheet', 'story', 'archive', 'credits')


def resource_of(path):
    """The resource key of an archive entry, or None when the entry is not text the port translates."""
    p = path.replace('\\', '/')
    low = p.lower()
    name = p.rsplit('/', 1)[-1]
    stem = name.rsplit('.', 1)[0]
    if low.startswith('msgsheet/') and low.endswith('.fms'):
        return 'msgsheet/' + name
    if low.startswith('archiveviewer/') and low.endswith('.txt'):
        return 'ArchiveViewer/' + stem + '.txt'
    if low.startswith('staffroll/') and low.endswith('.txt'):
        return 'StaffRoll/' + stem + '.txt'
    if low.endswith('.txt') and ('/' not in low or low.startswith('eventtxt/')):
        return 'story/' + stem.lower()
    return None


def kind_of(resource):
    if resource.startswith('msgsheet/'):
        return 'msgsheet'
    if resource.startswith('story/'):
        return 'story'
    if resource.startswith('StaffRoll/'):
        return 'credits'
    return 'archive'


def parse_resource(resource, data):
    """The resource's text units, in order, and the parsed container."""
    kind = kind_of(resource)
    if kind == 'msgsheet':
        sheet = parse_fms(data)
        return sheet.records, sheet
    if kind == 'story':
        story = parse_story(data)
        return story.records, story
    page = parse_credits(data) if kind == 'credits' else parse_page(data)
    return page.texts, page


def rebuild_resource(resource, container, units):
    kind = kind_of(resource)
    if kind == 'msgsheet':
        return build_fms(Fms(container.header, list(units), container.trailer))
    if kind == 'story':
        return build_story(Story(container.header, list(units), container.padding, container.encoding))
    return build_page(page_with(container, list(units)))


def sha256(text):
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def stored_sha256(text, container):
    """The hash of a record as its container stores it (Shift-JIS story text is hashed as Shift-JIS)."""
    return hashlib.sha256(text.encode(getattr(container, 'encoding', 'utf-8'))).hexdigest()


# ------------------------------------------------------------------ builds and slots

sys.path.insert(0, str(ROOT / 'scripts'))
from game_profiles import PROFILES  # noqa: E402

# The slot each build's English text lives in: launcher/game_profile.cpp's TextLanguage::slot. Europe serves
# Portuguese (Brazil) over the Spanish slot at runtime; this map is the English slot only.
SLOTS = {'USA': 'US', 'Japan': '', 'Europe': 'UK', 'HK': 'CN', 'Asia': 'CN'}
NATIVE_ENGLISH = {'USA', 'Europe'}


def builds():
    for title, profile in PROFILES.items():
        for key, version in profile['versions'].items():
            region = version.get('region', profile['region'])
            number = version.get('version', key)
            yield {'title': title, 'version': number, 'region': region, 'folder': version['folder'],
                   'label': f'{title} {number} {region}', 'data_files': list(version['data_files']),
                   'slot': SLOTS[region], 'native_english': region in NATIVE_ENGLISH}


def build_profile(folder):
    for build in builds():
        if build['folder'] == folder:
            return build
    raise KeyError(folder)


def slot_archives(build, folder_path):
    """The archives of the build's text slot present in folder_path, lowest priority first (an update's PATCH archives
    after the archives they patch)."""
    suffix = build['slot']
    names = [n for n in build['data_files'] if not suffix or n.upper().endswith('_' + suffix + '.CPK')]
    names.sort(key=lambda n: ('PATCH' in n.upper(), n))
    return [folder_path / n for n in names if (folder_path / n).is_file()]


def slot_resources(build, folder_path):
    """{resource: [(archive path, entry path), ...]}: every copy of every text resource of the slot, in priority order.
    The game reads the last one; the port serves all of them."""
    found = {}
    for archive in slot_archives(build, folder_path):
        cpk = Cpk(archive, original=True)
        for entry in cpk.entries:
            resource = resource_of(entry['path'])
            if resource:
                found.setdefault(resource, []).append((archive, entry['path']))
    return found


def read_resource(copies, cache=None):
    """The data of the copy the game reads (the last), from the archive's original table of contents."""
    archive, path = copies[-1]
    cpk = cache.get(archive) if cache is not None else None
    if cpk is None:
        cpk = Cpk(archive, original=True)
        if cache is not None:
            cache[archive] = cpk
    return cpk.read(cpk.find(path))


def load_slot(build, folder_path):
    """{resource: (units, container, data)} of the slot; resources that fail to parse are left out and reported."""
    cache, out, problems = {}, {}, []
    for resource, copies in sorted(slot_resources(build, folder_path).items()):
        data = read_resource(copies, cache)
        try:
            units, container = parse_resource(resource, data)
        except (ValueError, UnicodeDecodeError) as e:
            problems.append(f'{resource}: {e}')
            continue
        out[resource] = (units, container, data)
    return out, problems


# ------------------------------------------------------------------ control syntax

TOKEN = re.compile(r'##\([^)]*\)|\$\$\([^)]*\)|@/[A-Za-z]|@[A-Za-z#$](?:\([^)\n]*\))?')
CONVERSION = re.compile(r'%%|%[-+0#]*\d*(?:\.\d+)?[hlLzjt]*[diouxXeEfgGcsp]')
BUTTON = re.compile(r'@#\(\d+\)|##\([^)]*\)')


def control_tokens(text):
    return TOKEN.findall(text)


def conversions(text):
    return CONVERSION.findall(text)


def is_format(text):
    return bool(conversions(text))


def lone_percents(text):
    return CONVERSION.sub('', text).count('%')


def signature(text):
    """What a translation must keep: its tokens in any order, its conversions in order and its lone % count."""
    return (tuple(sorted(control_tokens(text))), tuple(conversions(text)), lone_percents(text) if is_format(text) else 0)


def arguments(text):
    """What a slot in another language must share with the English record: conversions in order and button glyphs."""
    return tuple(conversions(text)), tuple(sorted(BUTTON.findall(text)))


def visible(text):
    """The text without its control sequences, for length checks."""
    return TOKEN.sub('', text)


_WIDE_DIGITS = str.maketrans('０１２３４５６７８９', '0123456789')


def digits(text):
    """The numbers the text shows, in any order and any width."""
    return sorted(re.findall(r'\d+', visible(text).translate(_WIDE_DIGITS)))


def shape(text):
    """empty, placeholder or text, with its arguments: what neighbouring records of two languages must share."""
    seen = visible(text).strip()
    kind = 'empty' if not seen else 'placeholder' if set(seen) <= set('-－ー') else 'text'
    return kind, arguments(text)


def lines(text):
    return visible(text.replace('\r\n', '\n')).split('\n')


# ------------------------------------------------------------------ commands


def cmd_roundtrip(args):
    total = failed = 0
    for folder in args.folders:
        build = build_profile(folder.name)
        for resource, copies in sorted(slot_resources(build, folder).items()):
            for archive, path in copies:
                cpk = Cpk(archive, original=True)
                data = cpk.read(cpk.find(path))
                total += 1
                try:
                    units, container = parse_resource(resource, data)
                    same = rebuild_resource(resource, container, units) == data
                    why = 'rebuild differs'
                except (ValueError, UnicodeDecodeError) as e:
                    same, why = False, str(e)
                if not same:
                    failed += 1
                    print(f'{folder.name} {archive.name} {path}: {why}')
    print(f'{total} containers, {total - failed} rebuilt byte-identical, {failed} not')
    return 0 if total else 1


def cmd_inventory(args):
    build = build_profile(args.folder.name)
    if args.slot is not None:
        build = dict(build, slot=args.slot)
    slot, problems = load_slot(build, args.folder)
    rows = []
    for resource, (units, _, data) in slot.items():
        rows.append({'resource': resource, 'kind': kind_of(resource), 'units': len(units), 'bytes': len(data),
                     'textual': sum(1 for u in units if re.search(r'[^\W\d_]', visible(u))),
                     'formats': sum(1 for u in units if is_format(u))})
    summary = {}
    for row in rows:
        s = summary.setdefault(row['kind'], {'resources': 0, 'units': 0, 'textual': 0})
        s['resources'] += 1
        s['units'] += row['units']
        s['textual'] += row['textual']
    for kind, s in summary.items():
        print(f'{kind:9} {s["resources"]:4} resources {s["units"]:6} units {s["textual"]:6} with text')
    for p in problems:
        print('not parsed:', p)
    if args.json:
        args.json.write_text(json.dumps({'build': build['folder'], 'slot': build['slot'], 'resources': rows,
                                         'not_parsed': problems}, indent=1), encoding='utf-8')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('roundtrip')
    p.add_argument('folders', type=Path, nargs='+')
    p = sub.add_parser('inventory')
    p.add_argument('folder', type=Path)
    p.add_argument('--slot')
    p.add_argument('--json', type=Path)
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    return {'roundtrip': cmd_roundtrip, 'inventory': cmd_inventory}[args.command](args)


if __name__ == '__main__':
    sys.exit(main())
