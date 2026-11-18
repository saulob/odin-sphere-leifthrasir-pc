"""The runtime translation map of the six chapter prologue cards: every translatable Spanish record has a Portuguese
replacement keyed by resource and index, none keeps its Spanish text, and the emitted C++ table is in sync."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts' / 'localization'))
sys.path.insert(0, str(ROOT / 'scripts' / 'prompts'))
import corpus as C  # noqa: E402
import odin_text as ot  # noqa: E402
import prologue_map as P  # noqa: E402

CHECKS = FAILURES = 0


def check(condition, what):
    global CHECKS, FAILURES
    CHECKS += 1
    if not condition:
        FAILURES += 1
        print('FAIL', what)


PREPARED = C.PREPARED / P.BUILD
if not (PREPARED / P.ARCHIVE).is_file():
    print('prologue runtime map: DATA UNAVAILABLE (CUSA05262-1.01 is not prepared)')
    sys.exit(0)

rows = P.records(C.PREPARED)
check(len(rows) == 41, '41 records in the family')

spanish_left = []
for resource, index, spanish, portuguese in rows:
    check(spanish != portuguese, f'{resource}#{index}: replacement differs')
    if C.translatable(spanish) and ot.visible(spanish) == ot.visible(portuguese):
        spanish_left.append(f'{resource}#{index}')
check(spanish_left == [], f'no Spanish text left: {spanish_left}')

by_key = {(resource, index): portuguese for resource, index, _, portuguese in rows}
check(len(by_key) == 41, 'resource and index uniquely identify every record')

# Gwendolyn anchors.
check(ot.visible(by_key[('a0_2_1', 0)]) == 'A VALQUÍRIA', 'a0_2_1#0 -> A VALQUÍRIA')
check(ot.visible(by_key[('a0_2_1', 2)]) == 'A alma brilha com orgulho', 'a0_2_1#2 -> A alma brilha com orgulho')
check(ot.visible(by_key[('a0_2_1', 4)]) == 'As aves acreditam que', 'a0_2_1#4 -> As aves acreditam que')
check(ot.visible(by_key[('a0_2_1', 5)]) == 'este é o único meio', 'a0_2_1#5 -> este é o único meio')
check(ot.visible(by_key[('a0_2_1', 6)]) == 'de ganhar uma alma imortal', 'a0_2_1#6 -> de ganhar uma alma imortal')

for unwanted in ('LA VALQUIRIA', 'El alma refulge con orgullo', 'Los pájaros poseen', 'este es el único camino',
                 'VALKYRIE', 'The soul shines proudly', 'The birds have a belief'):
    present = any(unwanted in ot.visible(portuguese) for _, _, _, portuguese in rows)
    check(not present, f'no {unwanted!r} in any replacement')

check(P.check(C.PREPARED) == [], 'the emitted table is current')

print(f'prologue runtime map: {CHECKS} checks, {FAILURES} failed')
sys.exit(1 if FAILURES else 0)
