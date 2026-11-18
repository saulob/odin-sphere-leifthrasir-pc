"""scripts/localization: container round trips on synthetic data, the control syntax, the translation checks and the
batch escapes. No game files needed."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts' / 'localization'))
import corpus as C  # noqa: E402
import odin_text as ot  # noqa: E402
import ptbr  # noqa: E402

checks = failures = 0


def check(condition, what):
    global checks, failures
    checks += 1
    if not condition:
        failures += 1
        print('FAIL', what)


def fms_bytes(records):
    sheet = ot.Fms(b'FMSB' + bytes(4) + struct.pack('<I', 0x20) + bytes(0x14), records, b'FEOC' + bytes(12))
    return ot.build_fms(sheet)


data = fms_bytes(['Opções', '%d HP'])
check(ot.build_fms(ot.parse_fms(data)) == data, 'fms round trip')
check(ot.parse_fms(data).records == ['Opções', '%d HP'], 'fms records')

story = bytearray(0x40)
struct.pack_into('<I', story, 0x10, 2)
story += 'あい'.encode('cp932') + b'\0' + b'@w(6)abc\0'
parsed = ot.parse_story(bytes(story))
check(parsed.encoding == 'cp932' and ot.build_story(parsed) == bytes(story), 'shift-jis story round trip')

page = '﻿@TITLE( "Notas" ) // t\r\n@TEXT(\r\nLinha @b um@/b\r\n)\r\n@TEXT(\r\ndois\r\n  )\r\n'.encode('utf-8')
p = ot.parse_page(page)
check(p.texts == ['Notas', '\r\nLinha @b um@/b', '\r\ndois'] and ot.build_page(p) == page, 'page round trip')

check(ot.resource_of('EventTxt/A1_1.txt') == 'story/a1_1' and ot.resource_of('StaffRoll/x.txt') == 'StaffRoll/x.txt' and ot.resource_of('GUI/a.ftx') is None, 'resource keys')
check(ot.control_tokens('@#(106) @c(6)x@/c ##(JUMP) @CThe') == ['@#(106)', '@c(6)', '@/c', '##(JUMP)', '@C'], 'tokens')
check(ot.conversions('20% or lower') == [] and ot.conversions('%g%%, %02d') == ['%g', '%%', '%02d'], 'conversions')

en = 'Press @#(106) to @c(6)attack@/c %d times.'
check(not C.check_translation(en, 'Pressione @#(106) para @c(6)atacar@/c %d vezes.'), 'valid translation')
for bad, why in [('Pressione para @c(6)atacar@/c %d vezes.', 'missing button'),
                 ('Pressione @#(106) para @c(6)atacar@/c %s vezes.', 'changed conversion'),
                 ('Pressione @#(106) para @c(6)atacar %d vezes.', 'malformed colour'),
                 ('Pressione @#(106) para @c(6)atacar@/c %d vezes, señor.', 'Spanish letter'),
                 ('Pressione @#(106) para @c(6)atacar@/c %d vezes Ã©.', 'mojibake'),
                 ('Pressione @#(106) para @c(6)atacar@/c %d vezes �.', 'replacement character'),
                 ('TODO', 'placeholder'), ('', 'empty')]:
    check(bool(C.check_translation(en, bad)), why)
check(bool(C.check_translation('One line', 'Uma\nlinha', {'max_lines': 1, 'max_line': 8})), 'too many lines')
check(not C.check_translation('Text', 'não informações opções coração ÃÕÇ ãõç'), 'Portuguese letters')
check(not hasattr(C, 'fold') and C.needs_glyph('ã') and C.needs_glyph('º') and not C.needs_glyph('×'), 'no folding, glyph letters')
check(ptbr.unescape(ptbr.escape('a\nb\tc\\d')) == 'a\nb\tc\\d', 'batch escapes')

print(f'localization scripts: {"FAIL" if failures else "OK"} ({checks} checks, {failures} failed)')
sys.exit(1 if failures else 0)
