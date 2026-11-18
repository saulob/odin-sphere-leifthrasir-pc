"""The chapter prologue cards (story a0_2_1, b0_2_1, c0_2_1, d0_2_1, e0_2_1, f0_1_0): the Portuguese comes from the
English text, every record is served in Portuguese, and the wording fits the lines the official languages use."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts' / 'localization'))
sys.path.insert(0, str(ROOT / 'scripts' / 'prompts'))
import corpus as C  # noqa: E402
import odin_text as ot  # noqa: E402

CHECKS = FAILURES = 0


def check(condition, what):
    global CHECKS, FAILURES
    CHECKS += 1
    if not condition:
        FAILURES += 1
        print('FAIL', what)


FAMILY = {'story/a0_2_1': 7, 'story/b0_2_1': 7, 'story/c0_2_1': 6,
          'story/d0_2_1': 7, 'story/e0_2_1': 7, 'story/f0_1_0': 7}
REQUIRED = {
    ('story/a0_2_1', 0): '@f(8)@t(8)@X(3)A VALQUÍRIA',
    ('story/a0_2_1', 2): '@f(8)@t(8)@X(3)A alma brilha com orgulho',
    ('story/a0_2_1', 3): '@f(8)@t(8)@X(3)ao cair em batalha',
    ('story/a0_2_1', 4): '@f(8)@t(8)@X(3)As aves acreditam que',
    ('story/a0_2_1', 6): '@f(8)@t(8)@X(3)de ganhar uma alma imortal',
}
BUILD = 'CUSA05262-1.01'
LANGUAGES = ('UK', 'ES', 'FR', 'DE', 'IT')
SPANISH_LINE, SPANISH_ENGLISH = 'Los pájaros poseen', '@f(8)@t(8)@X(3)The birds have a belief:'

english = C.english_sheets()
shared, only = C.pt_entries()


def translated(resource, index):
    return C.pt_for(resource, index, english[resource][index], shared, only)


for resource, count in FAMILY.items():
    check(len(english.get(resource, ())) == count, f'{resource}: {count} records in the English corpus')
    for index, text in enumerate(english.get(resource, ())):
        if not C.translatable(text):
            continue
        pt = translated(resource, index)
        check(pt is not None, f'{resource}#{index}: Portuguese text')
        check(pt != text, f'{resource}#{index}: not the English text')
        check(not C.check_translation(text, pt), f'{resource}#{index}: valid translation')

for (resource, index), text in REQUIRED.items():
    check(translated(resource, index) == text, f'{resource}#{index}: {text!r}')
check(english['story/a0_2_1'][4] == SPANISH_ENGLISH, 'the Spanish line is "The birds have a belief:" in English')
check(translated('story/a0_2_1', 4).endswith('acreditam que'), 'the belief line reads "acreditam que"')

glyphs = C.read_json(C.PT_DIR / 'font' / 'glyphs.json')
build = next((b for b in glyphs['builds'] if b['eboot_sha256'].startswith('572bf21e')), None)
if build:
    letters = None
    for family in build['families']:
        have = {chr(c) for c in family['native']} | {chr(g['code']) for g in family['glyphs']}
        letters = have if letters is None else letters & have
    used = {ch for resource in FAMILY for index in range(FAMILY[resource])
            for ch in (translated(resource, index) or '') if C.needs_glyph(ch)}
    check(letters is not None and used <= letters, 'every accented letter has a glyph in the story font')
else:
    print('note: Europe 1.01 font data not found')

check(C.pt_for('story/a0_2_1', 4, english['story/a0_2_1'][4] + ' ', shared, only) is None,
      'a changed English text gets no Portuguese')

before = (C.PT_DIR / 'sheets' / 'story.json').read_bytes()
C.write_json(C.PT_DIR / 'sheets' / 'story.json', C.read_json(C.PT_DIR / 'sheets' / 'story.json'))
check((C.PT_DIR / 'sheets' / 'story.json').read_bytes() == before, 'the corpus is written deterministically')

import visual_parity as P  # noqa: E402

problems = []
saved = C.pt_for
C.pt_for = lambda resource, index, text, *args: text
P.prologue(problems)
C.pt_for = saved
check(problems == [f'{resource}#{index}: still the English text'
                   for resource in FAMILY for index, text in enumerate(english[resource])
                   if C.translatable(text)], 'visual_parity.prologue refuses records that keep the English')


def official(resource, language):
    import cpk
    pack = cpk.Cpk(ROOT / 'game' / 'prepared' / BUILD / f'Odin2_OR_{language}.CPK', original=True)
    try:
        return ot.parse_story(pack.read(pack.find(resource.rsplit('/', 1)[-1].upper() + '.txt'))).records
    finally:
        pack.f.close()


if (ROOT / 'game' / 'prepared' / BUILD).is_dir():
    spanish = {resource: official(resource, 'ES') for resource in FAMILY}
    check(any(SPANISH_LINE in line for lines in spanish.values() for line in lines), 'Spanish prologue text found')
    for resource in FAMILY:
        official_lines = [official(resource, language) for language in LANGUAGES]
        check(all(len(rows) == len(english[resource]) for rows in official_lines), f'{resource}: official record counts')
        for index, text in enumerate(english[resource]):
            rows = [ot.lines(text)] + [ot.lines(other[index]) for other in official_lines if index < len(other)]
            max_lines = max(len(r) for r in rows)
            max_line = max(len(ot.visible(line)) for r in rows for line in r)
            pt = ot.lines(translated(resource, index))
            check(len(pt) <= max_lines, f'{resource}#{index}: line count within the official lines')
            check(max(len(ot.visible(line)) for line in pt) <= max_line,
                  f'{resource}#{index}: line width within the official lines')
else:
    print('note: prepared Europe 1.01 not found; the official line check was not run')

print(f'chapter prologues: {CHECKS} checks, {FAILURES} failed')
sys.exit(1 if FAILURES else 0)
