"""Letters cut out of the official painted word sprites of one face, and words set from them.

A letter is a connected piece of the sprite's fill (the lettering without its outline). Every other opaque pixel belongs
to the nearest piece, so a cell holds its letter with the outline around it and neighbouring cells join seamlessly
when their outlines are laid over each other. Marks (accents) are pieces above the letter row; they travel with their
letter and are also kept apart so a mark can be put on another base letter.
"""
import unicodedata
from dataclasses import dataclass, field

import numpy as np
from scipy import ndimage

MARGIN = 7
DRAWN_MARKS = {'́': 'acute', '̃': 'tilde', '̧': 'cedilla'}


def bright_fill(level=200):
    return lambda rgba: (rgba[..., 3] > 200) & (rgba[..., :3].min(axis=-1) > level)


def yellow_fill(rgba):
    return (rgba[..., 3] > 200) & (rgba[..., 0] > 200) & (rgba[..., 1] > 170) & (rgba[..., 2] < 140)


@dataclass
class Cell:
    rgba: np.ndarray            # the letter with its outline
    fill: tuple                 # (x0, y0, x1, y1) of the fill inside rgba
    base_row: int               # row of rgba on the line's baseline
    mark: np.ndarray = None     # the mark's own pixels (same size as rgba), when the letter carries one
    mark_fill: tuple = None
    plain: bool = True
    split: bool = False


@dataclass
class Face:
    cells: dict = field(default_factory=dict)       # char -> Cell
    gap: float = 0.0                                # median distance between neighbouring fills
    height: int = 0                                 # median fill height of the capitals
    fill: object = None                             # the fill mask function of the face
    ring: int = 3                                   # outline thickness
    stroke: int = 0                                 # stroke thickness of drawn marks (0: from the height)


def _split(text):
    out = []
    for ch in text:
        if ch.isspace():
            continue
        base, *marks = unicodedata.normalize('NFD', ch)
        out.append((ch, base, marks))
    return out


def _lines_of(boxes, count):
    """Line index of each box when the boxes make `count` lines (clusters of their vertical centres)."""
    centres = sorted((b[1] + b[3]) / 2 for b in boxes)
    if count == 1:
        return lambda b: 0
    gaps = sorted(range(len(centres) - 1), key=lambda i: centres[i] - centres[i + 1])[:count - 1]
    cuts = sorted((centres[i] + centres[i + 1]) / 2 for i in gaps)
    return lambda b: sum((b[1] + b[3]) / 2 > c for c in cuts)


def _stacked(labels, n):
    """Pieces of one letter that sit above each other (a broken s, an i and its dot) become one piece."""
    spans = [(s[1].start, s[1].stop) for s in ndimage.find_objects(labels)]
    parent = list(range(n))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i in range(n):
        for j in range(i + 1, n):
            overlap = min(spans[i][1], spans[j][1]) - max(spans[i][0], spans[j][0])
            if overlap > 0.6 * min(spans[i][1] - spans[i][0], spans[j][1] - spans[j][0]):
                parent[find(j)] = find(i)
    roots = sorted({find(i) for i in range(n)})
    table = np.zeros(n + 1, labels.dtype)
    for i in range(n):
        table[i + 1] = roots.index(find(i)) + 1
    return table[labels], len(roots)


WIDTH = {'I': 0.5, 'J': 0.7, 'L': 0.8, 'M': 1.25, 'W': 1.3, '!': 0.4, '.': 0.3}


def _split_merged(labels, boxes, row, letters):
    """Letters whose fills touch form one piece. The pieces are split into the letters they hold: the number of letters
    per piece comes from the widths, each cut falls on the column with the least fill near its proportional place."""
    weights = [WIDTH.get(c, 1.0) for c in letters]
    widths = [boxes[i][2] - boxes[i][0] for i in row]
    unit = sum(widths) / sum(weights)
    m, k = len(row), len(letters)
    best = {}

    def solve(i, j):
        if i == m:
            return (0.0, ()) if j == k else (float('inf'), ())
        if (i, j) in best:
            return best[i, j]
        out = (float('inf'), ())
        for take in range(1, k - j - (m - i - 1) + 1):
            cost = (widths[i] - unit * sum(weights[j:j + take])) ** 2
            rest = solve(i + 1, j + take)
            if cost + rest[0] < out[0]:
                out = (cost + rest[0], (take,) + rest[1])
        best[i, j] = out
        return out

    counts = solve(0, 0)[1]
    labels = labels.copy()
    boxes = list(boxes)
    out, j, joined = [], 0, set()
    for i, take in zip(row, counts):
        if take == 1:
            out.append(i)
            j += 1
            continue
        piece = labels == i + 1
        columns = piece.sum(axis=0)
        x0, x1, y0, y1 = boxes[i][0], boxes[i][2], boxes[i][1], boxes[i][3]
        total = sum(weights[j:j + take])
        cuts, run = [x0], 0.0
        for t in range(take - 1):
            run += weights[j + t]
            expect = x0 + (x1 - x0) * run / total
            lo, hi = int(expect - 0.3 * unit), int(expect + 0.3 * unit) + 1
            window = range(max(lo, x0 + 1), min(hi, x1 - 1))
            cuts.append(min(window, key=lambda c: (columns[c], abs(c - expect))))
        cuts.append(x1)
        ids = []
        for t in range(take):
            part = piece.copy()
            part[:, :cuts[t]] = False
            part[:, cuts[t + 1]:] = False
            if t == 0:
                labels[part] = i + 1
                ids.append(i)
                boxes[i] = (cuts[0], y0, cuts[1], y1, int(part.sum()))
            else:
                boxes.append((cuts[t], y0, cuts[t + 1], y1, int(part.sum())))
                labels[part] = len(boxes)
                ids.append(len(boxes) - 1)
        out.extend(ids)
        joined.update(ids)
        j += take
    return out, labels, boxes, joined


def extract(rgba, lines, fill, face=None, join=0, stack=False):
    """Adds the letters of `lines` (one string per text line, top first) read off a sprite to `face`."""
    face = face or Face()
    face.fill = fill
    mask = fill(rgba)
    if join:
        mask = ndimage.binary_dilation(mask, structure=np.ones((3, 3)), iterations=join)
    labels, n = ndimage.label(mask, structure=np.ones((3, 3)))
    if stack:
        labels, n = _stacked(labels, n)
    if n == 0:
        raise ValueError('no lettering in the sprite')
    boxes = []
    for i, sl in enumerate(ndimage.find_objects(labels)):
        boxes.append((sl[1].start, sl[0].start, sl[1].stop, sl[0].stop, int((labels[sl] == i + 1).sum())))
    median_area = float(np.median([b[4] for b in boxes]))
    big = [i for i, b in enumerate(boxes) if b[4] >= 0.3 * median_area]
    if big:
        middle = float(np.median([(boxes[i][1] + boxes[i][3]) / 2 for i in big]))
        solid = list(big)
        big += [i for i in range(n) if i not in big and (boxes[i][1] + boxes[i][3]) / 2 > middle and not any(
            min(boxes[i][2], boxes[j][2]) - max(boxes[i][0], boxes[j][0]) > 0.5 * (boxes[i][2] - boxes[i][0]) for j in solid)]
    small = [i for i in range(n) if i not in big]
    expected = [sum(1 for _ in _split(line)) for line in lines]
    line_of = _lines_of([boxes[i] for i in big], len(lines))
    rows = [[] for _ in lines]
    for i in big:
        rows[line_of(boxes[i])].append(i)
    rows = [sorted(r, key=lambda i: boxes[i][0]) for r in rows]
    from_split = set()
    for index, (r, text) in enumerate(zip(rows, lines)):
        letters = [base for _, base, _ in _split(text)]
        if len(r) > len(letters):
            raise ValueError(f'a line has {len(r)} pieces for {len(letters)} letters')
        if len(r) < len(letters):
            rows[index], labels, boxes, joined = _split_merged(labels, boxes, r, letters)
            from_split |= joined
    big = [i for r in rows for i in r]
    near = ndimage.distance_transform_edt(labels == 0, return_indices=True)
    dist, (iy, ix) = near
    owner = np.where((rgba[..., 3] > 0) & (dist <= MARGIN), labels[iy, ix], 0)
    marks_of = {}
    for i in small:
        cx = (boxes[i][0] + boxes[i][2]) / 2
        under = [j for j in big if boxes[j][0] - 2 <= cx <= boxes[j][2] + 2 and boxes[j][1] >= boxes[i][3] - 2]
        if under:
            marks_of.setdefault(min(under, key=lambda j: boxes[j][1] - boxes[i][3]), []).append(i)
    heights = []
    gaps = []
    for text, row in zip(lines, rows):
        base_bottom = int(np.median([boxes[i][3] for i in row]))
        letters = _split(text)
        for (ch, base, marks), i in zip(letters, row):
            ids = [i] + marks_of.get(i, [])
            detached = len(ids) - 1 == len(marks)
            cell_mask = np.isin(owner, [j + 1 for j in ids])
            ys, xs = np.nonzero(cell_mask)
            x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
            cell = np.where(cell_mask[..., None], rgba, 0)[y0:y1, x0:x1].astype(np.uint8)
            bx0, by0, bx1, by1, _ = boxes[i]
            entry = Cell(cell, (bx0 - x0, by0 - y0, bx1 - x0, by1 - y0), base_bottom - y0, split=i in from_split)
            if marks and not detached:
                entry.plain = False
            if marks and detached:
                mark_mask = np.isin(owner, [j + 1 for j in marks_of.get(i, [])])
                m = np.where(mark_mask[..., None], rgba, 0)[y0:y1, x0:x1].astype(np.uint8)
                entry.mark, entry.plain = m, False
                mx = [boxes[j] for j in marks_of[i]]
                entry.mark_fill = (min(b[0] for b in mx) - x0, min(b[1] for b in mx) - y0,
                                   max(b[2] for b in mx) - x0, max(b[3] for b in mx) - y0)
            if ch not in face.cells or (entry.plain and not face.cells[ch].plain) or (face.cells[ch].split and not entry.split and entry.plain == face.cells[ch].plain):
                face.cells[ch] = entry
            if ch.isupper() or ch.isdigit():
                heights.append(by1 - by0)
        for a, b in zip(row, row[1:]):
            gaps.append(boxes[b][0] - boxes[a][2])
    if gaps:
        face.gap = float(np.median(gaps)) if not face.gap else (face.gap + float(np.median(gaps))) / 2
    if heights:
        face.height = int(np.median(heights))
    return face


def _mark_template(face, mark):
    for ch, cell in face.cells.items():
        if cell.mark is not None and unicodedata.normalize('NFD', ch)[1:] == mark:
            return ch, cell
    return None, None


def with_mark(face, char):
    """`char` as its plain base letter with the face's own mark of the same kind laid over it, centred and at the same
    height above the lettering as on the letter the mark was cut from."""
    base, *marks = unicodedata.normalize('NFD', char)
    plain = face.cells.get(base)
    if plain is None or len(marks) != 1:
        return None
    donor_char, donor = _mark_template(face, marks[0])
    if donor is None:
        kind = DRAWN_MARKS.get(marks[0])
        return _drawn_mark(face, plain, kind) if kind else None
    dx = round((plain.fill[0] + plain.fill[2]) / 2 - (donor.fill[0] + donor.fill[2]) / 2)
    dy = plain.fill[1] - donor.fill[1]
    return _stack(plain, donor.mark, dx, dy)


def _drawn_mark(face, plain, kind, gap=2):
    """`plain` with a mark drawn in the face's fill and outline colours: a one-period wave above the letter (tilde) or a
    small hooked stem under it (cedilla), as thick as the face's strokes."""
    cell = plain.rgba
    fill = face.fill(cell)
    fill_colour = np.median(cell[fill][:, :3], axis=0)
    ring_pixels = (cell[..., 3] > 240) & ~fill
    outline = np.median(cell[ring_pixels][:, :3], axis=0) if ring_pixels.any() else np.zeros(3)
    thick = face.stroke or max(2, int(round(face.height * 0.14)))
    pad = face.ring + 2
    cx = (plain.fill[0] + plain.fill[2]) / 2
    width = max(6, int(round((plain.fill[2] - plain.fill[0]) * 0.8)))
    amp = max(2.0, face.height * 0.13)
    above = int(thick + 2 * amp + 2 * pad + gap) if kind in ('tilde', 'acute') else 0
    below = int(thick * 2 + 2 + pad) if kind == 'cedilla' else 0
    h, w = cell.shape[0] + above + below, cell.shape[1]
    yy, xx = np.mgrid[0:h, 0:w]
    mask = np.zeros((h, w), bool)
    if kind == 'acute':
        rise, lean = max(4, round(face.height * 0.28)), max(3, round(width * 0.4))
        bottom = plain.fill[1] + above - gap
        for t in np.linspace(0, 1, 40):
            x, y = cx - lean / 2 + t * lean, bottom - thick / 2 - t * rise
            mask |= (xx - x) ** 2 + (yy - y) ** 2 <= (thick / 2) ** 2
    elif kind == 'tilde':
        bottom = plain.fill[1] + above - gap
        for t in np.linspace(0, 1, width * 4):
            x = cx - width / 2 + t * width
            y = bottom - thick / 2 - amp - amp * np.sin(2 * np.pi * t)
            mask |= (xx - x) ** 2 + (yy - y) ** 2 <= (thick / 2) ** 2
    else:
        top = plain.fill[3] + above + 1
        stem = int(thick * 1.5)
        mask |= (abs(xx - cx) <= thick / 2) & (yy >= top) & (yy < top + stem)
        mask |= (xx >= cx - thick) & (xx <= cx + thick / 2) & (yy >= top + stem - thick / 2) & (yy < top + stem + thick / 2)
    ring = (ndimage.distance_transform_edt(~mask) <= face.ring) & ~mask
    mark = np.zeros((h, w, 4), np.uint8)
    mark[ring, :3] = outline.astype(np.uint8)
    mark[ring, 3] = 255
    mark[mask, :3] = fill_colour.astype(np.uint8)
    mark[mask, 3] = 255
    canvas = np.zeros((h, w, 4), np.uint8)
    _over(canvas, cell, 0, above)
    _over(canvas, mark, 0, 0)
    rows = np.nonzero(canvas[..., 3].any(axis=1))[0]
    top, bottom = int(rows.min()), int(rows.max()) + 1
    canvas = canvas[top:bottom]
    shift = above - top
    return Cell(canvas, (plain.fill[0], plain.fill[1] + shift, plain.fill[2], plain.fill[3] + shift), plain.base_row + shift,
                plain=False)


def _stack(plain, mark, dx, dy):
    """`plain` with the `mark` layer moved by (dx, dy); the canvas grows to hold both."""
    dx, dy = int(dx), int(dy)
    left, top = max(0, -dx), max(0, -dy)
    w = max(plain.rgba.shape[1], mark.shape[1] + dx) + left
    h = max(plain.rgba.shape[0], mark.shape[0] + dy) + top
    canvas = np.zeros((h, w, 4), np.uint8)
    _over(canvas, plain.rgba, left, top)
    _over(canvas, mark, left + dx, top + dy)
    return Cell(canvas, (plain.fill[0] + left, plain.fill[1] + top, plain.fill[2] + left, plain.fill[3] + top),
                plain.base_row + top, plain=False)


def _over(canvas, tile, x, y):
    h, w = tile.shape[:2]
    y0, x0 = max(y, 0), max(x, 0)
    y1, x1 = min(y + h, canvas.shape[0]), min(x + w, canvas.shape[1])
    if y1 <= y0 or x1 <= x0:
        return
    src = tile[y0 - y:y1 - y, x0 - x:x1 - x].astype(np.float32)
    dst = canvas[y0:y1, x0:x1].astype(np.float32)
    a = src[..., 3:4] / 255
    b = dst[..., 3:4] / 255
    out_a = a + b * (1 - a)
    rgb = np.where(out_a > 0, (src[..., :3] * a + dst[..., :3] * b * (1 - a)) / np.maximum(out_a, 1e-6), 0)
    canvas[y0:y1, x0:x1, :3] = np.clip(rgb + 0.5, 0, 255).astype(np.uint8)
    canvas[y0:y1, x0:x1, 3] = np.clip(out_a[..., 0] * 255 + 0.5, 0, 255).astype(np.uint8)


def cell_of(face, char):
    cell = face.cells.get(char)
    if cell is None:
        cell = with_mark(face, char)
        if cell is not None:
            face.cells[char] = cell
    if cell is None:
        raise KeyError(char)
    return cell


def set_line(face, text, gap=None, space=None, kern=None):
    """(rgba of the line, baseline row): the letters' fills `gap` apart, drawn in order."""
    gap = face.gap if gap is None else gap
    space = space if space is not None else max(face.height * 0.3, 3)
    cells, pen = [], 0.0
    for ch in text:
        if ch.isspace():
            pen += space
            continue
        cell = cell_of(face, ch)
        cells.append((cell, pen - cell.fill[0]))
        pen += (cell.fill[2] - cell.fill[0]) + gap + (kern or {}).get(ch, 0)
    up = max(c.base_row for c, _ in cells)
    down = max(c.rgba.shape[0] - c.base_row for c, _ in cells)
    left = min(x for _, x in cells)
    width = int(round(max(x + c.rgba.shape[1] for c, x in cells) - min(0, left))) + 1
    canvas = np.zeros((up + down, width, 4), np.uint8)
    shift = -min(0, left)
    for cell, x in cells:
        _over(canvas, cell.rgba, int(round(x + shift)), up - cell.base_row)
    return canvas, up


def set_block(face, lines, gap=None, line_gap=None, align='center', kern=None):
    """The lines stacked and aligned on one canvas; `line_gap` is the distance between baselines (default: the lettering
    height plus the face's outline)."""
    rendered = [set_line(face, t, gap, kern=kern) for t in lines]
    step = line_gap if line_gap is not None else face.height + 2 * 3
    width = max(r.shape[1] for r, _ in rendered)
    first_up = rendered[0][1]
    height = int(first_up + step * (len(lines) - 1) + max(r.shape[0] - up for r, up in rendered)) + 1
    canvas = np.zeros((height, width, 4), np.uint8)
    for i, (r, up) in enumerate(rendered):
        x = {'left': 0, 'right': width - r.shape[1]}.get(align, (width - r.shape[1]) // 2)
        _over(canvas, r, x, int(round(first_up + step * i - up)))
    return canvas
