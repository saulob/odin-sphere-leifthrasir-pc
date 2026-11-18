"""Glyph tables for layouts that spell words from a glyph font: which atlas sprite is which letter.

A layer that draws a word holds one key per letter (the word may be drawn more than once, as a shadow or a glow, at the
same places). Given what each language's layer reads (its transcription), the distinct sprites of the layer, left to
right, are its letters; a sprite wholly above the letters' tops is an accent mark over the letter below it. Every
language agrees on a sprite's letter or the sprite is left out.
"""
import unicodedata
from collections import Counter, defaultdict
from statistics import median

import layout_text as LT
import mbs as M

MARKS = {'́': 'acute', '̀': 'grave', '̂': 'circumflex', '̈': 'diaeresis', '̃': 'tilde'}


def letters_of(text):
    """[(base, [marks])] of the visible characters of a transcription."""
    out = []
    for ch in unicodedata.normalize('NFD', text):
        if ch in MARKS:
            out[-1][1].append(MARKS[ch])
        elif not ch.isspace():
            out.append([ch, []])
    return out


def layer_sprites(layout, layer, largest):
    """[(sprite key, box)] of the distinct glyph-sized sprites a layer draws, each at its leftmost place."""
    found = []
    for k in layout.layer_keys(layer):
        if k['part'] >= layout.count(M.PARTS) or k['geom'] >= layout.count(M.GEOMS):
            continue
        rect = layout.part_rect(k['part'])
        if max(rect[2] - rect[0], rect[3] - rect[1]) > largest:
            continue
        box = LT.quad_box(layout.geom(k['geom']))
        sprite = (k['texture'],) + rect
        near = 0.3 * (box[2] - box[0])
        if not any(s == sprite and abs(b[0] - box[0]) <= near and abs(b[1] - box[1]) <= near for s, b in found):
            found.append((sprite, box))
    return found


def split_marks(items):
    if not items:
        return [], []
    tops = median(b[1] for _, b in items)
    heights = median(b[3] - b[1] for _, b in items)
    marks = [it for it in items if it[1][3] <= tops + 0.25 * heights and it[1][3] - it[1][1] < 0.75 * heights]
    rest = [it for it in items if it not in marks]
    lines = []
    for it in sorted(rest, key=lambda it: (it[1][1] + it[1][3]) / 2):
        cy = (it[1][1] + it[1][3]) / 2
        if lines and cy - lines[-1][-1] < 0.6 * heights:
            lines[-1][0].append(it)
            lines[-1][-1] = cy
        else:
            lines.append([[it], cy])
    letters = [it for line, _ in lines for it in sorted(line, key=lambda it: it[1][0])]

    def under(mark):
        cx = (mark[1][0] + mark[1][2]) / 2
        below = [i for i, it in enumerate(letters) if it[1][1] >= mark[1][3] - 2]
        return min(below, key=lambda i: abs((letters[i][1][0] + letters[i][1][2]) / 2 - cx), default=len(letters))
    return letters, sorted(marks, key=under)


def align(layout, layer, text, largest):
    """{sprite: char} of one layer read as `text`, or None when its sprites do not spell it."""
    spelled = letters_of(text)
    letters, marks = split_marks(layer_sprites(layout, layer, largest))
    if len(letters) != len(spelled) or len(marks) != sum(len(m) for _, m in spelled):
        return None
    out = {}
    for (sprite, _), (char, _) in zip(letters, spelled):
        out.setdefault(sprite, set()).add(char)
    for (sprite, _), mark in zip(marks, [m for _, ms in spelled for m in ms]):
        out.setdefault(sprite, set()).add('#' + mark)
    return out


def build(layouts, transcriptions, largest=160):
    """({sprite: char}, report) from {language: Mbs} and {language: {layer: text}}. A sprite two languages read as
    different letters is dropped (case aside: the capital wins, the game draws some capitals as small letters)."""
    votes, report = defaultdict(Counter), []
    for language, layers in transcriptions.items():
        layout = layouts[language]
        for layer, text in layers.items():
            found = align(layout, layer, text, largest)
            if found is None:
                report.append(f'{language} layer {layer}: does not read as {text!r}')
                continue
            for sprite, chars in found.items():
                for c in chars:
                    votes[sprite][c] += 1
    table = {}
    for sprite, counter in votes.items():
        by_letter = Counter()
        for c, n in counter.items():
            by_letter[c.lower()] += n
        ranked = by_letter.most_common()
        if len(ranked) > 1 and ranked[0][1] < 2 * ranked[1][1]:
            report.append(f'{sprite}: read as {sorted(counter)}; left out')
            continue
        if len(ranked) > 1:
            report.append(f'{sprite}: read as {sorted(counter)}; {ranked[0][0]!r} by {ranked[0][1]} to {ranked[1][1]}')
        chars = [c for c in counter if c.lower() == ranked[0][0]]
        table[sprite] = max(chars, key=lambda c: (c.isupper(), counter[c]))
    return table, report
