#!/usr/bin/env python3
"""Machine-assisted editorial review of the Portuguese (Brazil) translation: deterministic heuristics that point an
editor at likely problems. Nothing here blocks a merge; ptbr.py validate is the gate.

    review.py [--out FILE.tsv] [--category NAME]... [--show N] [--sheets DIR] [--strict]

Writes cache/localization/pt-BR/review.tsv (category, status, at, id, detail, EN, PT; \\n escaped) and prints flagged
and open counts per category; REVIEWED lists the flags an editor kept, with the reason. Categories:

    english        English function words left in the Portuguese
    spanish        Spanish words, letters or punctuation
    european       European Portuguese forms (a + infinitive progressive, ecrã, utilizador, génio...)
    glossary       an English glossary term whose required Portuguese is missing (exceptions: GLOSSARY_UI_ONLY,
                   GLOSSARY_ALSO; matching folds accents and case and allows inflection, cristal/cristais)
    calque         English false friends rendered by their Portuguese look-alike (actually/atualmente...)
    names          a short name from the name sheets rendered differently inside another text
    expansion      visible length over 1.6x the English (texts of 12+ characters)
    line           story events: a line longer than any line of the event in any shipped language (the box);
                   sysmsg and sheets: a line over 110% (and 3+ characters) of that record's longest in any language
    punctuation    final punctuation differs from the English, or unbalanced quotes/parentheses
    ui_width       a UI label over 110% of the widest shipped language (EN/FR/DE/ES/IT) for that record
    gender         speaker-gendered forms in story dialogue, listed for review against the speaker
    glyphs         characters the game fonts lack (outside ASCII and GLYPHS, unless the English has them)

Story files are named by campaign: a Gwendolyn, b Cornelius, c Mercedes, d Oswald, e Velvet, f Armageddon (shared),
g the Pooka village/forest NPCs, sysmsg the system messages.
"""
import argparse
import re
import sys
import unicodedata
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import corpus as C  # noqa: E402
import odin_text as ot  # noqa: E402
import ptbr  # noqa: E402

OUT = C.CACHE / 'pt-BR' / 'review.tsv'
CAMPAIGN = {'a': 'Gwendolyn', 'b': 'Cornelius', 'c': 'Mercedes', 'd': 'Oswald', 'e': 'Velvet', 'f': 'Armageddon',
            'g': 'NPC', 's': 'system'}
UI_SHEETS = ('msgsheet/SystemMsg.fms', 'msgsheet/ObjName.fms', 'msgsheet/ItemList.fms', 'msgsheet/Ability.fms',
             'msgsheet/BattleScore.fms')

ENGLISH_WORDS = ('the and you your yours of with is are was were this that these those what which have has had will would '
                 'should could don\'t can\'t won\'t i\'m it\'s you\'re they we he she his her him them their our it be '
                 'been not but from here there please yes hey huh well just all if or by an my to in on at for into '
                 'about who where when why how').split()
# Portuguese words (and the laugh "he he") spelled like an English function word.
ENGLISH_SAFE = {'for', 'me', 'no', 'a', 'do', 'as', 'de', 'em', 'se', 'um', 'mas', 'he'}
SPANISH = [r'señor\w*', r'muy', r'pero', r'también', r'usted\w*', r'hasta', r'entonces', r'y', r'el', r'los', r'las',
           r'del', r'con', r'una', r'ahora', r'aquí', r'allí', r'ahí', r'bueno', r'gracias', r'hay', r'esto', r'eso',
           r'mismo', r'puede\w*', r'tiene\w*', r'nosotros', r'ella', r'ellos', r'cuando', r'qué', r'sí', r'él', r'tú',
           r'yo', r'lo', r'le', r'siempre', r'mañana', r'niñ\w+', r'año\w*', r'hombre', r'mujer', r'vamos a', r'dónde',
           r'cómo', r'quién', r'nuestr\w+', r'vuestr\w+', r'hacer', r'tengo', r'quiero', r'mucho', r'muchas', r'todavía']
EUROPEAN = [r'ecrã\w*', r'rapariga\w*', r'utilizador\w*', r'telemóve\w*', r'comboio\w*', r'autocarro\w*',
            r'pequeno-almoço', r'casa de banho', r'facto\w*', r'connosco', r'registo\w*', r'equipas?', r'fixe',
            r'miúd[oa]s?', r'bué', r'camião', r'acção', r'acções', r'direcção', r'óptim\w+', r'objecto\w*',
            r'exact[oa]s?', r'baptism\w*', r'actual\w*', r'efectiv\w+', r'protecção', r'reacção', r'colecção',
            r'percebeste', r'fizeste', r'disseste',
            r'\w+[éó][mn][aeiouáéíóú]\w*']
# Mid-word é/ó before m/n + vowel is European spelling (génio, demónio, prémio); these Brazilian words share it.
EUROPEAN_SAFE = {'fenômeno', 'crémet'}
PROGRESSIVE = re.compile(r'\b(estou|estás|está|estamos|estão|estava|estavas|estávamos|estavam|esteve|estive|fico|fica|'
                         r'ficam|ficou|continuo|continua|continuam|ando|anda|andam|andava)\s+a\s+(\w+(?:ar|er|ir))\b', re.I)
# Glossary terms with an ordinary meaning in prose; checked only in the UI and name sheets.
GLOSSARY_UI_ONLY = {'King', 'Queen', 'Prince', 'Princess', 'Lord', 'Level', 'Save', 'Load', 'Max', 'Special', 'Material',
                    'Chain', 'Guard', 'Slide', 'Glide', 'Mix', 'Act', 'Verse', 'Notes', 'Classic', 'Refined', 'Gold',
                    'Kitchen', 'Cafe', 'Ingredients', 'Prelude', 'Epilogue', 'Witch', 'Skill', 'Ability', 'Recipe',
                    'Crystal', 'Options', 'Tutorial', 'Level Up', 'Item Box', 'Chapter', 'New Game', 'Exit Game',
                    'Fairies', 'Dwarves'}
# Accepted renderings beyond the glossary column (inflection, pronoun of address, a different but correct reading).
GLOSSARY_ALSO = {'Your Majesty': ['majestade'], 'Demon Lord': ['lorde demônio', 'lordes demônios'],
                 'Fairies': ['fada'], 'Dwarves': ['anã'], 'Pooka Village': ['vila dos pookas', 'vila pooka'],
                 'The Cauldron': ['caldeirão'], 'The Deep Forest': ['floresta profunda'], 'Phozon': ['phozon'],
                 'Psypher': ['psypher'], 'Three Wise Men': ['três sábios'], 'Halja': ['halja'],
                 'Shadow Knight': ['cavaleiro das sombras', 'cavaleiros das sombras'],
                 'Beast of Armageddon': ['besta do armagedom', 'bestas do armagedom'],
                 'Chain': ['corrente'], 'Ability': ['aprimorad']}
# English word stem / Portuguese look-alike that usually means something else.
FALSE_FRIENDS = [('actual', 'atual'), ('realiz', 'realiz'), ('pretend', 'pretend'), ('excit', 'excitad'),
                 ('eventual', 'eventual'), ('sensible', 'sensíve'), ('exquisite', 'esquisit'), ('parents', 'parentes'),
                 ('prejudice', 'prejuízo'), ('ultimately', 'ultimamente'), ('decept', 'decepç'), ('deceiv', 'decepcion'),
                 ('push', 'pux'), ('library', 'livraria'), ('college', 'colégio'), ('costume', 'costume'),
                 ('novel', 'novela'), ('lunch', 'lanche'), ('attend', 'atend'), ('injur', 'injúri'),
                 ('expert', 'espert'), ('sympath', 'simpátic'), ('balcony', 'balcão'), ('assum', 'assumir que')]
# What the game fonts can draw: ASCII plus these letters (and anything the record's English already shows).
GLYPHS = set('áàâãéêíóôõúüçÁÀÂÃÉÊÍÓÔÕÚÜÇ')
# Flags an editor has reviewed and kept, with the reason; they stay in review.tsv as "reviewed".
_ALCHEMY_MIX = {f'story/sysmsg#{i}' for i in range(190, 216)}
REVIEWED = {
    'calque': {'medical care: ser atendido is the right sense': {'story/a5_6#1'}},
    'gender': {
        'form agrees with the speaker or subject in context': {
            'story/a1_2#12', 'story/a3_4#21', 'story/a3_6#7', 'story/a4_2#2', 'story/a4_2#3', 'story/a4_4#5',
            'story/a6_2#20', 'story/a6_2#22', 'story/a6_2#38', 'story/a6_4#5', 'story/a6_4#6', 'story/b1_6#10',
            'story/b2_2#26', 'story/b2_6#19', 'story/b3_2#63', 'story/b4_4#10', 'story/b4_6#28', 'story/b5_2#7',
            'story/b5_6#41', 'story/b6_4#12', 'story/b6_6#33', 'story/b6_6#46', 'story/b7_2#42', 'story/b7_4#0',
            'story/b7_6#8', 'story/c4_2#18', 'story/c4_6#23', 'story/c5_2#16', 'story/c7_2#4', 'story/d0_6_1#21',
            'story/e2_6#30', 'story/e4_6#40', 'story/e5_2#1', 'story/e5_2#49', 'story/e5_4#4', 'story/e5_6#12',
            'story/e6_4#22', 'story/e7_4#22', 'story/e7_4#24', 'story/f1_1_2#17', 'story/f1_2_5#8',
            'story/f2_1_2#17', 'story/f2_2_4#23', 'story/f3_2_0#2', 'story/f3_2_0#4', 'story/sysmsg#802',
            'story/sysmsg#1709', 'story/sysmsg#1779', 'story/sysmsg#1834'},
    },
    'expansion': {
        'glossary terms (Alchemy Mix = Mistura de Alquimia)': _ALCHEMY_MIX,
        'short source; natural Portuguese needs the extra words': {
            'msgsheet/CommonMsg.fms#8', 'story/e0_2_1#5', 'story/e6_4#13', 'story/f7_3#33', 'story/sysmsg#483',
            'story/sysmsg#1905', 'story/sysmsg#1987', 'story/sysmsg#2073', 'story/sysmsg#2143'},
    },
    'line': {
        'item or recipe name, same as its item list record': {
            'story/sysmsg#184', 'story/sysmsg#375', 'story/sysmsg#421', 'story/sysmsg#424', 'story/sysmsg#426',
            'story/sysmsg#427', 'story/sysmsg#428', 'story/sysmsg#430', 'story/sysmsg#434', 'story/sysmsg#441',
            'story/sysmsg#444', 'story/sysmsg#498', 'story/sysmsg#536', 'story/sysmsg#547', 'story/sysmsg#550',
            'story/sysmsg#551', 'story/sysmsg#553', 'story/sysmsg#1368', 'story/sysmsg#1405'},
        'chapter title card or recap line within the lengths other Portuguese recaps use': {
            'msgsheet/SystemMsg.fms#1256', 'story/sysmsg#1421', 'msgsheet/SystemMsg.fms#1455', 'story/sysmsg#1738'},
    },
    'ui_width': {
        'enemy or part name over the HP gauge, which shows longer names': {
            'msgsheet/ObjName.fms#132', 'msgsheet/ObjName.fms#139', 'msgsheet/ObjName.fms#219',
            'msgsheet/ObjName.fms#220', 'msgsheet/ObjName.fms#223', 'msgsheet/ObjName.fms#238',
            'msgsheet/ObjName.fms#255', 'msgsheet/ObjName.fms#262'},
        'item name; the item lists hold names of 27+ characters': {
            'msgsheet/ItemList.fms#21', 'msgsheet/ItemList.fms#33', 'msgsheet/ItemList.fms#37',
            'msgsheet/ItemList.fms#108', 'msgsheet/ItemList.fms#316', 'msgsheet/ItemList.fms#380',
            'msgsheet/ItemList.fms#382', 'msgsheet/ItemList.fms#383', 'msgsheet/ItemList.fms#384',
            'msgsheet/ItemList.fms#386', 'msgsheet/ItemList.fms#414', 'msgsheet/ItemList.fms#673',
            'msgsheet/ItemList.fms#709', 'msgsheet/ItemList.fms#711', 'msgsheet/ItemList.fms#712',
            'msgsheet/ItemList.fms#713', 'msgsheet/ItemList.fms#715', 'msgsheet/ItemList.fms#729',
            'msgsheet/SystemMsg.fms#38', 'msgsheet/SystemMsg.fms#1674'},
        'skill name within 2-5 characters of the widest language': {
            'msgsheet/Ability.fms#44', 'msgsheet/Ability.fms#101', 'msgsheet/Ability.fms#737',
            'msgsheet/Ability.fms#1364', 'msgsheet/Ability.fms#1369', 'msgsheet/Ability.fms#1382',
            'msgsheet/Ability.fms#1392', 'msgsheet/SystemMsg.fms#439'},
        'option value or HUD row; the same column shows Do Not Zoom / Simple Map Display': {
            'msgsheet/SystemMsg.fms#566', 'msgsheet/SystemMsg.fms#567', 'msgsheet/SystemMsg.fms#608',
            'msgsheet/SystemMsg.fms#609'},
        'area title with a glossary term (Demon Lord = Lorde Demônio)': {'msgsheet/SystemMsg.fms#951'},
    },
}
GLOSSARY_SKIP_CATEGORIES = {'##(KEY)'}
# A name sheet record whose words also mean something else in other texts: the dropped-item label vs the command.
NAMES_EXCEPTIONS = {'Drop Item'}
GENDER = re.compile(r'\b(obrigad[oa]s?|(?:estou|sou|fui|fiquei|estava|era|me sinto|me sentia|sinto-me|ando|continuo|'
                    r'eu mesm[oa]|eu sozinh[oa]|estou tão|sou tão|estou muito|sou muito)\s+(?:tão\s+|muito\s+|bem\s+)?'
                    r'(\w+(?:ad|id|ix|uit|ort|ivr|ivo|iva|os|ec|ut|inh|lh|t|nt)[oa]s?))\b', re.I)
GENDER_SIMPLE = re.compile(r'\b(obrigad[oa]|eu mesm[oa]|eu sozinh[oa])\b', re.I)


def norm(text):
    return ' '.join(ot.visible(text.replace('\r\n', '\n')).split())


def fold(text):
    return ''.join(c for c in unicodedata.normalize('NFD', text.lower()) if not unicodedata.combining(c))


def words(text):
    return re.findall(r"[A-Za-zÀ-ÿ]+(?:'[A-Za-z]+)?", ot.visible(text))


def stems(term):
    out = []
    for w in re.findall(r'\w+', fold(term)):
        if w.endswith('ao'):
            w = w[:-2]
        out.append(w[:-2] if len(w) >= 6 else w[:-1] if len(w) == 5 else w)
    return out


def contains_term(pt, term):
    """True when every word of `term` starts some word of `pt`, folded, allowing inflection (cristal/cristais)."""
    seen = re.findall(r'\w+', fold(ot.visible(pt)))
    return all(any(w.startswith(s) for w in seen) for s in stems(term))


def campaign(resource):
    name = resource.split('/', 1)[1]
    return CAMPAIGN.get(name[0], '?')


def check_english(pt):
    hits = []
    for w in re.findall(r"(?<![\w'-])[A-Za-z]+(?:'[a-z]+)?(?![\w'-])", ot.visible(pt)):
        if w.lower() in ENGLISH_WORDS and w.lower() not in ENGLISH_SAFE:
            hits.append(w)
    return sorted(set(hits))


def check_spanish(pt):
    text = ot.visible(pt)
    hits = [c for c in '¿¡ñÑ' if c in text]
    for pattern in SPANISH:
        for m in re.finditer(r'(?<![\w-])' + pattern + r'(?![\w-])', text, re.I):
            if not (m.group(0).isupper() and len(m.group(0)) <= 3):
                hits.append(m.group(0))
    return sorted(set(hits))


def check_european(pt):
    text = ot.visible(pt)
    hits = [m.group(0) for m in PROGRESSIVE.finditer(text)]
    for pattern in EUROPEAN:
        for m in re.finditer(r'(?<![\w-])' + pattern + r'(?![\w-])', text, re.I):
            if m.group(0).lower() not in EUROPEAN_SAFE:
                hits.append(m.group(0))
    return sorted(set(hits))


def check_glossary(en, pt, resource, terms):
    label = resource in UI_SHEETS and ptbr.is_name(en)
    out = []
    for source, target in terms:
        if source in GLOSSARY_UI_ONLY and not label:
            continue
        if not re.search(r'(?<![\w-])' + re.escape(source) + r's?(?![\w-])', ot.visible(en), re.I):
            continue
        accepted = [target] + GLOSSARY_ALSO.get(source, [])
        if not any(contains_term(pt, a.removeprefix('o ').removeprefix('a ')) for a in accepted):
            out.append(f'{source} = {target}')
    return out


def check_punctuation(en, pt):
    out = []
    e, p = norm(en), norm(pt)
    e_end = re.sub(r'["”’\')\]\s]+$', '', e)
    p_end = re.sub(r'["”’\')\]\s]+$', '', p)

    def ending(s):
        m = re.search(r'(\.\.\.|…|[?!]+|[.,:;—–-])$', s)
        if not m:
            return ''
        t = m.group(1).replace('…', '...')
        return '?!' if set(t) == {'?', '!'} else t

    ee, pe = ending(e_end), ending(p_end)
    abbreviation = re.search(r'\b(sec|min|etc|approx)\.$', e_end) and re.search(r'\b(s|min|etc)\.?$', p_end)
    question = ee == '.' and pe == '?' and re.search(QUESTION, p, re.I)
    if ee in ('.', '...', '?', '!', '?!') and ee != pe and not abbreviation and not question:
        out.append(f'ends {ee!r} vs {pe!r}')
    marked = ot.TOKEN.sub('\u25c6', pt.replace('\r\n', '\n'))
    if re.search(r'\w (?:[,!?;:]|\.(?!\.))', marked) or re.search(r'[a-zà-ú][,;!?][A-Za-zÀ-ú]', marked):
        out.append('spacing around punctuation')
    for o, c in ('()', '[]', '“”'):
        if p.count(o) - p.count(c) != e.count(o) - e.count(c) and p.count(o) != p.count(c):
            out.append(f'unbalanced {o}{c}')
    if p.count('"') % 2 and not e.count('"') % 2:
        out.append('odd number of "')
    return out


QUESTION = r'\b(será que|por que|como|o que|quem|onde|quando|qual|quanto)\b'


def check_gender(pt):
    return sorted({m.group(0) for m in GENDER.finditer(ot.visible(pt).replace('\n', ' '))} |
                  {m.group(0) for m in GENDER_SIMPLE.finditer(ot.visible(pt))})


def width(text):
    return len(norm(text))


_BOX = {}


def box_width(resource, english_units, others):
    """The dialogue box of a story event: the longest line of any of its records in any shipped language."""
    if resource not in _BOX:
        texts = list(english_units.get(resource, [])) + [t for slot in others.values() for t in slot.get(resource, [])]
        _BOX[resource] = max((len(line.rstrip()) for t in texts if t for line in ot.lines(t)), default=0)
    return _BOX[resource]


def name_table(table, shared):
    """{English name: Portuguese} of the short single-line records of the name sheets worth checking inside texts."""
    out = {}
    for h, u in table.items():
        resource = u['where'][0][0]
        if resource not in ('msgsheet/ObjName.fms', 'msgsheet/ItemList.fms', 'msgsheet/Ability.fms') or h not in shared:
            continue
        en = norm(u['en'])
        if not ptbr.is_name(u['en']) or len(en) < 5 or not re.fullmatch(r"[A-Z][A-Za-z'\-]*(?: (?:[a-z]{1,3}|[A-Z][A-Za-z'\-]*))*", en):
            continue
        pt = norm(shared[h]['pt_br'])
        if fold(pt) == fold(en) or en in NAMES_EXCEPTIONS:
            continue
        out.setdefault(en, set()).add(pt)
    return {en: pts for en, pts in out.items() if len(pts) == 1}


def name_key(text):
    """A name as matched inside other texts: case matters except in short connectors (Chicken au/Au Gratin)."""
    return ' '.join(w.lower() if len(w) <= 3 else w for w in text.split())


def check_calque(en, pt):
    e, p = ot.visible(en).lower(), ot.visible(pt).lower()
    return [f'{a}/{b}' for a, b in FALSE_FRIENDS if re.search(r'\b' + a, e) and re.search(r'\b' + b, p)]


def check_glyphs(en, pt):
    return sorted({c for c in pt if ord(c) > 0x7e and c not in GLYPHS and c not in en})


def review():
    table = ptbr.units()
    shared, _ = C.pt_entries()
    english_units = C.english_sheets()
    others = ptbr.context_slots()
    terms = [(r['source'], r['pt_br']) for r in ptbr.glossary()
             if r['source'] and r['category'] not in GLOSSARY_SKIP_CATEGORIES and not r['source'].startswith('#')]
    names = name_table(table, shared)
    name_patterns = [(en, next(iter(pts)), re.compile(r'(?<![\w-])' + re.escape(en) + r'(?![\w-])',
                                                      re.I if ' ' in en else 0))
                     for en, pts in sorted(names.items(), key=lambda kv: -len(kv[0]))]
    rows = []
    for h, u in table.items():
        entry = shared.get(h)
        if not entry:
            continue
        en, pt = u['en'], entry['pt_br']
        resource, index = u['where'][0]
        kind = ot.kind_of(resource)
        at = f'{resource}#{index}'

        def flag(category, detail):
            rows.append((category, at, C.short(h), detail, en, pt))

        hits = check_glyphs(en, pt)
        if hits:
            flag('glyphs', ''.join(hits))
        if norm(pt) == norm(en) and ptbr.is_name(en):
            continue
        for category, check in (('english', check_english), ('spanish', check_spanish), ('european', check_european)):
            hits = check(pt)
            if hits:
                flag(category, ' '.join(hits))
        hits = check_glossary(en, pt, resource, terms)
        if hits:
            flag('glossary', '; '.join(hits))
        hits = check_calque(en, pt)
        if hits:
            flag('calque', ' '.join(hits))
        seen_en = ot.visible(en)
        taken = [False] * len(seen_en)
        for name, target, pattern in name_patterns:
            for m in pattern.finditer(seen_en):
                if any(taken[m.start():m.end()]) or name_key(m.group(0)) != name_key(name):
                    continue
                taken[m.start():m.end()] = [True] * (m.end() - m.start())
                if name != norm(en) and not contains_term(pt, target):
                    flag('names', f'{name} = {target}')
                break
        e_len, p_len = width(en), width(pt)
        if e_len >= 12 and p_len > 1.6 * e_len and not (resource in UI_SHEETS and ptbr.is_name(en)):
            flag('expansion', f'{p_len}/{e_len} = {p_len / e_len:.2f}x')
        if kind != 'archive':
            longest = max(len(line.rstrip()) for line in ot.lines(pt))
            if kind == 'story' and resource != 'story/sysmsg':
                budget = box_width(resource, english_units, others)
                if longest > budget:
                    flag('line', f'longest line {longest}, longest line of {resource} in any shipped language {budget}')
            else:
                lim = ptbr.limits_of(resource, index, en, others)
                multi = resource not in UI_SHEETS or len(ot.lines(pt)) > 1
                if multi and longest - lim['max_line'] >= 3 and longest > 1.1 * lim['max_line']:
                    flag('line', f'longest line {longest}, shipped languages {lim["max_line"]}')
            hits = check_punctuation(en, pt)
            if hits:
                flag('punctuation', '; '.join(hits))
        if resource in UI_SHEETS and '\n' not in ot.visible(en).strip() and ptbr.is_name(en):
            widths = {'EN': width(en)}
            for slot, sheets in others.items():
                units_ = sheets.get(resource)
                if units_ and index < len(units_) and units_[index] and ot.shape(units_[index])[0] == 'text':
                    widths[slot] = width(units_[index])
            budget = max(widths.values())
            if width(pt) > budget * 1.1 and width(pt) - budget >= 2:
                flag('ui_width', f'{width(pt)} > budget {budget} ({" ".join(f"{k}{v}" for k, v in widths.items())})')
        if kind == 'story':
            hits = check_gender(pt)
            if hits:
                flag('gender', f'{campaign(resource)}: {", ".join(hits)}')
    return rows


CATEGORIES = ('glyphs', 'english', 'spanish', 'european', 'glossary', 'calque', 'names', 'expansion', 'line',
              'punctuation', 'ui_width', 'gender')


def status(row):
    category, at = row[0], row[1]
    for reason, ats in REVIEWED.get(category, {}).items():
        if at in ats:
            return 'reviewed: ' + reason
    return 'review' if category == 'gender' else 'open'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', type=Path, default=OUT)
    parser.add_argument('--category', action='append', choices=CATEGORIES)
    parser.add_argument('--show', type=int, default=0, help='print the first N open rows of each category')
    parser.add_argument('--sheets', type=Path, help='review another copy of localization/pt-BR (sheets/ and glossary.tsv)')
    parser.add_argument('--strict', action='store_true', help='exit 1 when any flag is open')
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    if args.sheets:
        C.PT_DIR = args.sheets.resolve()
    rows = review()
    if args.category:
        rows = [r for r in rows if r[0] in args.category]
    rows.sort(key=lambda r: (CATEGORIES.index(r[0]), ptbr.resource_order(r[1].rsplit('#', 1)[0]), int(r[1].rsplit('#', 1)[1])))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('category\tstatus\tat\tid\tdetail\ten\tpt_br\n')
        for row in rows:
            f.write('\t'.join(ptbr.escape(str(v)) for v in (row[0], status(row)) + row[1:]) + '\n')
    total, still = Counter(), Counter()
    for r in rows:
        total[r[0]] += 1
        still[r[0]] += status(r) in ('open', 'review')
    for category in CATEGORIES:
        if args.category and category not in args.category:
            continue
        print(f'{category:12} {total[category]:5} flagged {still[category]:5} open')
        if args.show:
            for r in [r for r in rows if r[0] == category and status(r) in ('open', 'review')][:args.show]:
                print(f'   {r[1]}  [{r[3]}]\n      EN: {ptbr.escape(r[4])}\n      PT: {ptbr.escape(r[5])}')
    print(f'{"total":12} {sum(total.values()):5} flagged {sum(still.values()):5} open; {args.out}')
    return 1 if args.strict and sum(still.values()) else 0


if __name__ == '__main__':
    sys.exit(main())
