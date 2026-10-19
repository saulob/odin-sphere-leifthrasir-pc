"""Generates the application icon (packaging/icons/app-icon-*.png and app-icon.ico), no dependencies.

The design is the port's own: a dark field, a gold ring and a teal drop, drawn by distance functions
and supersampled. Usage: python tools/make-app-icon.py
"""
import math
import pathlib
import struct
import zlib

OUT = pathlib.Path(__file__).resolve().parent.parent / 'packaging' / 'icons'
SIZES = (16, 24, 32, 48, 64, 128, 256)
SS = 4
BG_TOP = (14, 18, 36)
BG_BOTTOM = (6, 8, 16)
GOLD = (224, 187, 50)
GOLD_DARK = (150, 112, 24)
TEAL = (72, 199, 191)
TEAL_DARK = (24, 104, 108)


def mix(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def background(x, y):
    t = y
    return mix(BG_TOP, BG_BOTTOM, t)


def shade(x, y):
    """RGBA of one supersampled point in unit coordinates."""
    cx, cy = x - 0.5, y - 0.5
    r = math.hypot(cx, cy * 1.08)
    ring = abs(r - 0.33)
    if ring < 0.045:
        edge = min(1.0, max(0.0, (0.045 - ring) / 0.012))
        gold = mix(GOLD_DARK, GOLD, min(1.0, max(0.0, (cy + 0.4) / 0.8)))
        return (*gold, edge)
    # Drop: a teardrop centred a little below the ring's centre.
    dx, dy = cx, cy + 0.06
    drop = math.hypot(dx * 1.25, dy * 1.6) - (0.115 + 0.06 * max(0.0, -dy))
    if drop < 0.0:
        teal = mix(TEAL, TEAL_DARK, min(1.0, max(0.0, (dy + 0.12) / 0.24)))
        return (*teal, 1.0)
    return (*background((y), y), 1.0)


def render(size):
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(SS):
                for sx in range(SS):
                    x = (px + (sx + 0.5) / SS) / size
                    y = (py + (sy + 0.5) / SS) / size
                    r, g, b, a = shade(x, y)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = SS * SS
            alpha = acc[3] / n
            if alpha <= 0.0:
                row += bytes(4)
            else:
                row += bytes((round(acc[0] / acc[3]), round(acc[1] / acc[3]), round(acc[2] / acc[3]),
                              round(alpha * 255)))
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


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    payloads = []
    for size in SIZES:
        path = OUT / f'app-icon-{size}.png'
        write_png(path, size, render(size))
        payloads.append((size, path.read_bytes()))
    entries, offset = [], 6 + 16 * len(payloads)
    for size, data in payloads:
        entries.append(struct.pack('<BBBBHHII', size if size < 256 else 0, size if size < 256 else 0, 0, 0, 1, 32,
                                   len(data), offset))
        offset += len(data)
    (OUT / 'app-icon.ico').write_bytes(struct.pack('<HHH', 0, 1, len(payloads)) + b''.join(entries) +
                                       b''.join(data for _, data in payloads))
    print(f'wrote {OUT / "app-icon.ico"} ({len(SIZES)} sizes)')


if __name__ == '__main__':
    main()
