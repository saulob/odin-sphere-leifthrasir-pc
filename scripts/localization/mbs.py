#!/usr/bin/env python3
"""Vanillaware FMBS layouts (.mbs): reading, and growing by appending records to a section.

A layout is a 0x120-byte header, eleven record sections and a 16-byte FEOC footer. The header holds the file size less
304 (u32 at 4), the record count of every section (0x50..0x66) and every section's offset (u64 at 0xb0; 0 when empty).
Records refer to each other by index only, so a record appended at the end of its section moves the later sections
without invalidating anything but the header.

  sprites   parts    48 B: pivot, UV corners (atlas pixels), 2 floats
  quads     geoms    48 B: centre, 4 corners (layout units), 2 floats
  keys      16 B: u32 sprite id, u16@6 texture << 8, u16@10 part, u16@12 colour, u16@14 geom
  layers    28 B: bbox (4 floats), u16@16 first key, u16@22 key count
  segments  32 B: u32 layer, u16@4 transform, u16@6 frames
  transforms 36 B: x, y, ..., sx@24, sy@28
  anims     48 B: bbox, name[24], u16@40 first channel, u8@42 channel count
  channels  24 B: u16 first segment, u16 segment count
"""
import struct

COLOURS, PARTS, GEOMS, EXTRA, KEYS, LINKS, LAYERS, TRANSFORMS, SEGMENTS, ANIMS, CHANNELS = range(11)
RECORD = {COLOURS: 24, PARTS: 48, GEOMS: 48, EXTRA: 80, KEYS: 16, LINKS: 8, LAYERS: 28, TRANSFORMS: 36, SEGMENTS: 32,
          ANIMS: 48, CHANNELS: 24}
COUNT = {KEYS: (0x50, '<I'), COLOURS: (0x54, '<H'), PARTS: (0x56, '<H'), GEOMS: (0x58, '<H'), EXTRA: (0x5a, '<H'),
         LINKS: (0x5c, '<H'), TRANSFORMS: (0x5e, '<H'), SEGMENTS: (0x60, '<H'), LAYERS: (0x62, '<H'), ANIMS: (0x64, '<H'),
         CHANNELS: (0x66, '<H')}
SIZE_BIAS = 304


class MbsError(Exception):
    pass


class Mbs:
    def __init__(self, data):
        data = bytes(data)
        if data[:4] != b'FMBS' or len(data) < 0x130 or data[-16:-12] != b'FEOC':
            raise MbsError('not an FMBS layout')
        if struct.unpack_from('<I', data, 4)[0] != len(data) - SIZE_BIAS:
            raise MbsError('size field does not match')
        self.original = data
        self.data = bytearray(data)
        self.inserts = []  # (offset in the original, bytes)
        for s, (at, fmt) in COUNT.items():
            count = struct.unpack_from(fmt, data, at)[0]
            if count and self.offset(s) + count * RECORD[s] > len(data) - 16:
                raise MbsError(f'section {s} overruns the file')

    # ------------------------------------------------------------ sections
    def offset(self, s):
        return struct.unpack_from('<Q', self.data, 0xb0 + 8 * s)[0]

    def count(self, s):
        at, fmt = COUNT[s]
        return struct.unpack_from(fmt, self.data, at)[0]

    def record(self, s, i):
        if not 0 <= i < self.count(s):
            raise MbsError(f'record {i} of section {s} out of range')
        at = self.offset(s) + RECORD[s] * i
        return self.data[at:at + RECORD[s]]

    def put(self, s, i, raw):
        assert len(raw) == RECORD[s]
        at = self.offset(s) + RECORD[s] * i
        self.data[at:at + RECORD[s]] = raw

    def append(self, s, records):
        """Appends raw records to section s; returns the index of the first."""
        raw = b''.join(records)
        assert raw and len(raw) % RECORD[s] == 0 and len(raw) % 16 == 0
        first = self.count(s)
        if first == 0:
            raise MbsError(f'section {s} is empty')
        end = self.offset(s) + RECORD[s] * first
        self.inserts.append((self._original_offset(end), raw))
        self.data[end:end] = raw
        for t in range(11):
            if self.count(t) and self.offset(t) >= end and t != s:
                struct.pack_into('<Q', self.data, 0xb0 + 8 * t, self.offset(t) + len(raw))
        at, fmt = COUNT[s]
        if first + len(raw) // RECORD[s] >= (1 << (8 * struct.calcsize(fmt))):
            raise MbsError(f'section {s} would overflow its count')
        struct.pack_into(fmt, self.data, at, first + len(raw) // RECORD[s])
        struct.pack_into('<I', self.data, 4, len(self.data) - SIZE_BIAS)
        return first

    def replace(self, s, records):
        """Section s holds exactly `records` afterwards; the sections stored after it move. Not combinable with append's
        insert tracking: the result is a new layout, not an edit of the original."""
        raw = b''.join(records)
        assert len(raw) % RECORD[s] == 0
        count = len(raw) // RECORD[s]
        at, fmt = COUNT[s]
        if count >= (1 << (8 * struct.calcsize(fmt))) or not records:
            raise MbsError(f'section {s} cannot hold {count} records')
        if self.count(s) == 0:
            raise MbsError(f'section {s} is empty')
        start = self.offset(s)
        end = start + RECORD[s] * self.count(s)
        moved = [t for t in range(11) if t != s and self.count(t) and self.offset(t) >= end]
        self.data[start:end] = raw
        for t in moved:
            struct.pack_into('<Q', self.data, 0xb0 + 8 * t, self.offset(t) + len(raw) - (end - start))
        struct.pack_into(fmt, self.data, at, count)
        struct.pack_into('<I', self.data, 4, len(self.data) - SIZE_BIAS)
        self.inserts = None

    def _original_offset(self, at):
        """The original offset of a current one that lies on a boundary between original bytes."""
        shift = 0
        for where, raw in sorted(self.inserts, key=lambda i: i[0]):
            if where + shift + len(raw) <= at:
                shift += len(raw)
        return at - shift

    def bytes(self):
        return bytes(self.data)

    # ------------------------------------------------------------ records
    def part_uv(self, p):
        f = struct.unpack_from('<12f', self.record(PARTS, p))
        return [(f[2 + 2 * i], f[3 + 2 * i]) for i in range(4)]

    def part_rect(self, p):
        uv = self.part_uv(p)
        return (int(min(u for u, _ in uv)), int(min(v for _, v in uv)), int(max(u for u, _ in uv)), int(max(v for _, v in uv)))

    def geom(self, g):
        f = struct.unpack_from('<12f', self.record(GEOMS, g))
        return [(f[2 + 2 * i], f[3 + 2 * i]) for i in range(4)]

    def key(self, k):
        raw = self.record(KEYS, k)
        sid, w4, w6, w8, part, colour, geom = struct.unpack_from('<IHHHHHH', raw)
        return dict(index=k, sprite=sid, texture=w6 >> 8, part=part, colour=colour, geom=geom, raw=bytes(raw))

    def layer_range(self, layer):
        raw = self.record(LAYERS, layer)
        return struct.unpack_from('<H', raw, 16)[0], struct.unpack_from('<H', raw, 22)[0]

    def set_layer(self, layer, first, count, bbox=None):
        raw = bytearray(self.record(LAYERS, layer))
        struct.pack_into('<H', raw, 16, first)
        struct.pack_into('<H', raw, 22, count)
        if bbox is not None:
            struct.pack_into('<4f', raw, 0, *bbox)
        self.put(LAYERS, layer, bytes(raw))

    def layer_keys(self, layer):
        first, count = self.layer_range(layer)
        return [self.key(k) for k in range(first, first + count) if k < self.count(KEYS)]

    def anims(self):
        out = []
        for i in range(self.count(ANIMS)):
            raw = self.record(ANIMS, i)
            name = bytes(raw[16:40]).split(b'\0')[0].decode('latin1')
            if name and name.isprintable():
                out.append((name, struct.unpack_from('<H', raw, 40)[0], raw[42]))
        return out

    def channel_segments(self, channel):
        first, count = struct.unpack_from('<HH', self.record(CHANNELS, channel))
        return range(first, first + count)

    def segment_layer(self, segment):
        return struct.unpack_from('<I', self.record(SEGMENTS, segment))[0]

    def segment_transform(self, segment):
        t = struct.unpack_from('<H', self.record(SEGMENTS, segment), 4)[0]
        f = struct.unpack_from('<9f', self.record(TRANSFORMS, t))
        return f[0], f[1], f[6], f[7]

    def anim_layers(self, first, count):
        """[(channel, layer of the settled segment, (x, y, sx, sy))] of an animation."""
        out = []
        for ch in range(first, first + count):
            segments = list(self.channel_segments(ch))
            if not segments:
                continue
            layer = self.segment_layer(segments[-1])
            if layer < self.count(LAYERS):
                out.append((ch, layer, self.segment_transform(segments[-1])))
        return out


def delta_ops(original, new, inserts):
    """Copy/add operations turning `original` into `new`, given the byte strings inserted at original offsets."""
    ops = []
    o = n = 0
    for where, raw in sorted(inserts, key=lambda i: i[0]) + [(len(original), b'')]:
        _aligned_ops(original, o, new, n, where - o, ops)
        n += where - o
        o = where
        if raw:
            ops.append(('add', bytes(new[n:n + len(raw)])))
            n += len(raw)
    assert n == len(new)
    return _merge(ops)


def _aligned_ops(a, ao, b, bo, length, ops, slack=16):
    i = 0
    while i < length:
        j = i
        while j < length and a[ao + j] == b[bo + j]:
            j += 1
        if j > i:
            ops.append(('copy', ao + i, j - i))
        if j >= length:
            break
        k = j
        same = 0
        while k < length and same < slack:
            same = same + 1 if a[ao + k] == b[bo + k] else 0
            k += 1
        end = k - same if same >= slack else k
        ops.append(('add', bytes(b[bo + j:bo + end])))
        i = end


def _merge(ops):
    out = []
    for op in ops:
        if out and op[0] == 'add' and out[-1][0] == 'add':
            out[-1] = ('add', out[-1][1] + op[1])
        elif out and op[0] == 'copy' and out[-1][0] == 'copy' and out[-1][1] + out[-1][2] == op[1]:
            out[-1] = ('copy', out[-1][1], out[-1][2] + op[2])
        else:
            out.append(op)
    return out


def apply_ops(original, ops):
    out = bytearray()
    for op in ops:
        out += original[op[1]:op[1] + op[2]] if op[0] == 'copy' else op[1]
    return bytes(out)
