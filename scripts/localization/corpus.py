"""The committed localization corpus (localization/en, localization/pt-BR) and the rules that bind it to the game's own
records. launcher/game_translation.cpp applies the same rules at run time."""
import json
import re
import unicodedata
from pathlib import Path

import odin_text as ot

ROOT = ot.ROOT
LOCALIZATION = ROOT / 'localization'
EN_DIR = LOCALIZATION / 'en'
PT_DIR = LOCALIZATION / 'pt-BR'
CACHE = ROOT / 'cache' / 'localization'
PREPARED = ROOT / 'game' / 'prepared'
CANONICAL_BUILD = 'CUSA05083-1.00'
CONTEXT_BUILD = 'CUSA05262-1.01'
CONTEXT_SLOTS = ('FR', 'DE', 'ES', 'IT')

PT_LETTERS = set('áàâãéêíóôõúüçÁÀÂÃÉÊÍÓÔÕÚÜÇ')
# Letters that never belong in Brazilian Portuguese text; their presence means another language leaked in.
FOREIGN_LETTERS = set('ñÑìòùÌÒÙèÈëËïÏîÎûÛäÄöÖßœŒæÆøØåÅÿ¿¡')


def short(digest):
    return digest[:16]


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(value, ensure_ascii=False, indent=1, sort_keys=False)
    path.write_text(text + '\n', encoding='utf-8', newline='\n')


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def translatable(text):
    """Units the port translates: text with letters that is not a bare placeholder."""
    seen = ot.visible(text).strip()
    return bool(re.search(r'[A-Za-z]', seen)) and seen not in ('---', 'DUMMY', 'dummy')


# ------------------------------------------------------------------ the English corpus


def english_sheets():
    """{resource: [units]} from localization/en/sheets."""
    out = {}
    for kind in ot.KINDS:
        path = EN_DIR / 'sheets' / f'{kind}.json'
        if path.is_file():
            for item in read_json(path)['resources']:
                out[item['resource']] = item['units']
    return out


def english_records():
    """[(resource, index, text)] of every translatable English unit, in game order."""
    out = []
    for resource, units in english_sheets().items():
        for i, text in enumerate(units):
            if text is not None and translatable(text):
                out.append((resource, i, text))
    return out


# ------------------------------------------------------------------ Portuguese (Brazil)


def pt_entries():
    """{source_sha256: entry} of localization/pt-BR/sheets/*.json, and per-record overrides {(resource, index): entry}."""
    shared, only = {}, {}
    for kind in ot.KINDS:
        path = PT_DIR / 'sheets' / f'{kind}.json'
        if not path.is_file():
            continue
        for entry in read_json(path)['entries']:
            if entry.get('only'):
                for at in entry['only']:
                    resource, index = at.rsplit('#', 1)
                    only[(resource, int(index))] = entry
            else:
                shared[entry['source_sha256']] = entry
    return shared, only


def pt_for(resource, index, english, shared, only):
    entry = only.get((resource, index)) or shared.get(ot.sha256(english))
    if entry and entry['source_sha256'] == ot.sha256(english):
        return entry['pt_br']
    return None


# ------------------------------------------------------------------ rules


def needs_glyph(ch):
    """A letter the game's fonts must hold a glyph for: accented Latin, ª and º."""
    return (0xC0 <= ord(ch) <= 0x24F and ch not in '×÷') or ch in 'ªº'


def check_translation(english, pt, limits=None):
    """Problems of `pt` as the translation of `english`; empty when it may be served."""
    problems = []
    if not pt or not pt.strip():
        return ['empty translation']
    if '�' in pt or re.search('Ã[\u0080-¿]|Â[\u0080-¿]|â€', pt):
        problems.append('replacement character or mojibake')
    crlf = '\r\n' in english
    if any(ord(c) < 0x20 and c != '\n' and not (crlf and c == '\r') for c in pt):
        problems.append('control character')
    if crlf and pt.count('\n') != pt.count('\r\n'):
        problems.append('line breaks must be CRLF like the English')
    if re.search(r'\b(TODO|FIXME|XXX)\b', pt):
        problems.append('placeholder value')
    foreign = sorted(set(pt) & FOREIGN_LETTERS - set(english))
    if foreign:
        problems.append('letters foreign to Portuguese: ' + ''.join(foreign))
    allowed = set(english) | PT_LETTERS
    odd = sorted({c for c in pt if ord(c) > 0x7e and c not in allowed and c not in '…“”‘’—–«»ºª°'})
    if odd:
        problems.append('unsupported characters: ' + ''.join(odd))
    se, sp = ot.signature(english), ot.signature(pt)
    if se[0] != sp[0]:
        problems.append(f'control tokens differ: {list(se[0])} vs {list(sp[0])}')
    if se[1] != sp[1]:
        problems.append(f'printf conversions differ: {list(se[1])} vs {list(sp[1])}')
    if not ot.is_format(english) and ot.conversions(pt):
        problems.append('adds a printf conversion')
    if se[2] != sp[2]:
        problems.append('lone % count differs in a format record')
    if limits:
        n = len(ot.lines(pt))
        if n > limits['max_lines']:
            problems.append(f'{n} lines, at most {limits["max_lines"]}')
    return problems


def match_line_breaks(english, pt):
    """The translation with the English's line break convention (archive pages are CRLF)."""
    if '\r\n' in english:
        return pt.replace('\r\n', '\n').replace('\n', '\r\n')
    return pt


def line_limits(texts):
    """The most lines and the longest line among the shipped languages' texts of one record."""
    rows = [ot.lines(t) for t in texts if t]
    return {'max_lines': max(len(r) for r in rows), 'max_line': max(len(line) for r in rows for line in r)}
