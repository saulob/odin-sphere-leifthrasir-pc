"""Generates the application icon from game-console-ecommerce-svgrepo-com.svg, no dependencies.

The SVG's controller silhouette, face and buttons are flattened and rendered with even-odd fill at
4x. The launcher's ruby accent and pressed tones are read from src/osl_accent.h; the 16px tile uses
slightly enlarged buttons so it stays readable. Usage: python tools/make-app-icon.py
"""
import math
import pathlib
import re
import struct
import xml.etree.ElementTree as ET
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'packaging' / 'icons'
SVG = ROOT / 'game-console-ecommerce-svgrepo-com.svg'
SIZES = (16, 24, 32, 48, 64, 128, 256)
SS = 4
LIGHT = (244, 234, 240)
NUMBER = r'[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?'


def launcher_accent():
    source = (ROOT / 'src' / 'osl_accent.h').read_text(encoding='utf-8')
    values = []
    for channel in ('Red', 'Green', 'Blue'):
        match = re.search(r'kAccent' + channel + r'\s*=\s*(\d+)', source)
        if not match:
            raise SystemExit(f'kAccent{channel} not found in src/osl_accent.h')
        values.append(int(match.group(1)))
    return tuple(values)


def launcher_pressed():
    source = (ROOT / 'src' / 'osl_accent.h').read_text(encoding='utf-8')
    values = []
    for channel in ('Red', 'Green', 'Blue'):
        match = re.search(r'kAccentPressed' + channel + r'\s*=\s*(\d+)', source)
        if not match:
            raise SystemExit(f'kAccentPressed{channel} not found in src/osl_accent.h')
        values.append(int(match.group(1)))
    return tuple(values)


def identity():
    return (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)


def multiply(first, second):
    a1, b1, c1, d1, e1, f1 = first
    a2, b2, c2, d2, e2, f2 = second
    return (a1 * a2 + c1 * b2, b1 * a2 + d1 * b2, a1 * c2 + c1 * d2, b1 * c2 + d1 * d2,
            a1 * e2 + c1 * f2 + e1, b1 * e2 + d1 * f2 + f1)


def transform(text):
    result = identity()
    for name, body in re.findall(r'([A-Za-z]+)\s*\(([^)]*)\)', text):
        numbers = [float(value) for value in re.split(r'[\s,]+', body.strip()) if value]
        if name == 'matrix':
            local = tuple(numbers[:6])
        elif name == 'translate':
            local = (1.0, 0.0, 0.0, 1.0, numbers[0], numbers[1] if len(numbers) > 1 else 0.0)
        elif name == 'scale':
            local = (numbers[0], 0.0, 0.0, numbers[1] if len(numbers) > 1 else numbers[0], 0.0, 0.0)
        else:
            raise SystemExit(f'unsupported SVG transform {name}')
        result = multiply(result, local)
    return result


def load_svg(path):
    root = ET.parse(path).getroot()
    view = tuple(float(value) for value in re.split(r'[\s,]+', root.attrib['viewBox'].strip()))
    paths = []

    def walk(element, parent):
        matrix = parent
        if element.attrib.get('transform'):
            matrix = multiply(parent, transform(element.attrib['transform']))
        if element.tag.rsplit('}', 1)[-1] == 'path':
            paths.append((element.attrib['d'], matrix))
        for child in element:
            walk(child, matrix)

    walk(root, identity())
    return view, paths


def flatten(d, matrix):
    tokens = re.findall(r'[MCLZ]|' + NUMBER, d.upper())
    subpaths, polygon = [], []
    current = start = None
    command = None
    index = 0

    def point(x, y):
        a, b, c, d_, e, f = matrix
        return a * x + c * y + e, b * x + d_ * y + f

    while index < len(tokens):
        token = tokens[index]
        if token in 'MCLZ':
            command = token
            index += 1
            if command == 'Z':
                if polygon:
                    polygon.append(start)
                    subpaths.append(polygon)
                polygon = []
                current = start
                command = None
                continue
        if command is None:
            raise SystemExit('malformed SVG path')
        if command == 'M':
            x, y = float(tokens[index]), float(tokens[index + 1])
            index += 2
            if polygon:
                polygon.append(start)
                subpaths.append(polygon)
            current = start = (x, y)
            polygon = [current]
            command = 'L'
        elif command == 'L':
            x, y = float(tokens[index]), float(tokens[index + 1])
            index += 2
            current = (x, y)
            polygon.append(current)
        elif command == 'C':
            x1, y1, x2, y2, x, y = (float(value) for value in tokens[index:index + 6])
            index += 6
            x0, y0 = current
            for step in range(1, 17):
                t = step / 16.0
                u = 1.0 - t
                px = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x
                py = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y
                polygon.append((px, py))
            current = (x, y)
        else:
            raise SystemExit('unsupported SVG path command')
    if polygon:
        polygon.append(start)
        subpaths.append(polygon)
    return [[point(x, y) for x, y in polygon] for polygon in subpaths]


def coverage(subpaths, view, size):
    width = height = size * SS
    vx, vy, vw, vh = view
    edges = []
    for polygon in subpaths:
        points = [((x - vx) / vw * width, (y - vy) / vh * height) for x, y in polygon]
        for i in range(len(points) - 1):
            x0, y0 = points[i]
            x1, y1 = points[i + 1]
            if y0 == y1:
                continue
            if y0 > y1:
                x0, y0, x1, y1 = x1, y1, x0, y0
            edges.append((y0, y1, x0, (x1 - x0) / (y1 - y0)))
    mask = bytearray(width * height)
    for row in range(height):
        y = row + 0.5
        crossings = [x0 + (y - y0) * slope for y0, y1, x0, slope in edges if y0 <= y < y1]
        crossings.sort()
        for i in range(0, len(crossings) - 1, 2):
            x0, x1 = crossings[i], crossings[i + 1]
            first = max(0, math.ceil(x0 - 0.5))
            last = min(width - 1, math.floor(x1 - 0.5))
            if first <= last:
                start = row * width + first
                mask[start:start + last - first + 1] = b'\xff' * (last - first + 1)
    return mask


def scaled_about_center(subpaths, factor):
    result = []
    for subpath in subpaths:
        xs = [point[0] for point in subpath]
        ys = [point[1] for point in subpath]
        cx = (min(xs) + max(xs)) * 0.5
        cy = (min(ys) + max(ys)) * 0.5
        result.append([(cx + (x - cx) * factor, cy + (y - cy) * factor) for x, y in subpath])
    return result


def render(size, view, layers, accent, pressed):
    width = size * SS
    total = SS * SS
    body = coverage([layers[1][0]], view, size)
    details = coverage(scaled_about_center(layers[1][2:], 1.8) if size <= 16 else layers[1][2:], view, size)
    face = None if size <= 16 else coverage([layers[0][0]], view, size)
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            body_alpha = details_alpha = face_alpha = 0
            for sy in range(SS):
                base = (py * SS + sy) * width + px * SS
                body_alpha += sum(body[base:base + SS])
                details_alpha += sum(details[base:base + SS])
                if face is not None:
                    face_alpha += sum(face[base:base + SS])
            body_alpha /= total * 255.0
            details_alpha /= total * 255.0
            face_alpha /= total * 255.0
            alpha = face_alpha + body_alpha * (1.0 - face_alpha)
            body_colour = accent if face is None else pressed
            colour = [face_alpha * accent[i] + (1.0 - face_alpha) * body_alpha * body_colour[i] for i in range(3)]
            if alpha <= 0.0:
                row += bytes(4)
                continue
            colour = [(details_alpha * LIGHT[i] + (1.0 - details_alpha) * colour[i]) / alpha for i in range(3)]
            alpha = details_alpha + alpha * (1.0 - details_alpha)
            row += bytes((min(255, round(colour[0])), min(255, round(colour[1])), min(255, round(colour[2])),
                          min(255, round(alpha * 255))))
        rows.append(bytes(row))
    return rows


def write_png(path, size, rows):
    raw = b''.join(b'\x00' + row for row in rows)

    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(raw, 9)) +
                     chunk(b'IEND', b''))


# A 32-bit BGRA bitmap frame, bottom-up, with its AND mask: the usual icon format below 256px.
def dib_frame(size, rows):
    pixels, mask = b'', b''
    for row in reversed(rows):
        pixels += b''.join(bytes((row[i + 2], row[i + 1], row[i], row[i + 3])) for i in range(0, size * 4, 4))
        bits = ''.join('1' if row[i + 3] == 0 else '0' for i in range(0, size * 4, 4)).ljust((size + 31) // 32 * 32, '0')
        mask += int(bits, 2).to_bytes(len(bits) // 8, 'big')
    header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, len(pixels) + len(mask), 0, 0, 0, 0)
    return header + pixels + mask


def main():
    accent = launcher_accent()
    pressed = launcher_pressed()
    accent_hex = '#%02X%02X%02X' % accent
    if accent_hex.lower() not in SVG.read_text(encoding='utf-8').lower():
        raise SystemExit(f'{SVG.name} does not use the launcher accent {accent_hex}')
    view, paths = load_svg(SVG)
    if len(paths) != 2:
        raise SystemExit(f'{SVG.name}: expected two paths, found {len(paths)}')
    layers = [flatten(d, matrix) for d, matrix in paths]
    OUT.mkdir(parents=True, exist_ok=True)
    payloads = []
    for size in SIZES:
        path = OUT / f'app-icon-{size}.png'
        rows = render(size, view, layers, accent, pressed)
        write_png(path, size, rows)
        payloads.append((size, path.read_bytes() if size == 256 else dib_frame(size, rows)))
    entries, offset = [], 6 + 16 * len(payloads)
    for size, data in payloads:
        entries.append(struct.pack('<BBBBHHII', size if size < 256 else 0, size if size < 256 else 0, 0, 0, 1, 32,
                                   len(data), offset))
        offset += len(data)
    (OUT / 'app-icon.ico').write_bytes(struct.pack('<HHH', 0, 1, len(payloads)) + b''.join(entries) +
                                       b''.join(data for _, data in payloads))
    print(f'wrote {OUT / "app-icon.ico"} ({len(SIZES)} sizes, accent {accent_hex})')


if __name__ == '__main__':
    main()
