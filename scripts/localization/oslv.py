#!/usr/bin/env python3
"""OSLV visual deltas: archive entries rebuilt from the player's own entry by copy and add operations.

Unlike a DCPX block patch an OSLV target may change the entry's size (a layout that grows). A target applies only to
the exact source it was made from (size and CRC32) and must rebuild the exact result (size and CRC32).

  'OSLV3\0\0\0', u32 target count, then per target:
  u16 name length, name (UTF-8, as the archive's table of contents spells it),
  u32 source count, per source: u32 size, u32 CRC32, u32 result size, u32 result CRC32,
  u32 operation count, u32 added size, u32 compressed size,
  operations: u8 0 (copy) u32 source offset, u32 length | u8 1 (add) u32 length,
  then the added bytes of every add, in order, as one zlib stream.

Sources that need the same operations (the same change over files that differ elsewhere) share one target.
OSLV2 holds one source per target; OSLV1 is OSLV2 with the added bytes inline after each add, uncompressed.
"""
import struct
import zlib

import mbs as M

MAGIC1 = b'OSLV1\0\0\0'
MAGIC2 = b'OSLV2\0\0\0'
MAGIC3 = b'OSLV3\0\0\0'
COPY, ADD = 0, 1
JOIN = 16


def same_size_ops(source, result):
    """Copy/add operations for two files of one size: the bytes that differ, runs closer than JOIN bytes joined."""
    import numpy as np
    a, b = np.frombuffer(source, np.uint8), np.frombuffer(result, np.uint8)
    idx = np.flatnonzero(a != b)
    if not len(idx):
        return [('copy', 0, len(a))]
    cuts = np.flatnonzero(np.diff(idx) > JOIN) + 1
    starts, ends = np.concatenate([[0], cuts]), np.concatenate([cuts, [len(idx)]])
    ops, pos = [], 0
    for s, e in zip(starts, ends):
        first, last = int(idx[s]), int(idx[e - 1]) + 1
        if first > pos:
            ops.append(('copy', pos, first - pos))
        ops.append(('add', bytes(b[first:last])))
        pos = last
    if pos < len(a):
        ops.append(('copy', pos, len(a) - pos))
    return ops


def diff_ops(source, result, gram=8, minimum=24, candidates=64):
    """Copy/add operations for a rebuilt file: runs of `result` found anywhere in `source` (at least `minimum` bytes) are
    copied, the rest added. Meant for small files such as layouts."""
    index = {}
    for i in range(len(source) - gram + 1):
        bucket = index.setdefault(source[i:i + gram], [])
        if len(bucket) < candidates:
            bucket.append(i)
    ops, pending, n = [], bytearray(), 0
    while n < len(result):
        best = (0, 0)
        for i in index.get(result[n:n + gram], ()):
            length = gram
            while n + length < len(result) and i + length < len(source) and source[i + length] == result[n + length]:
                length += 1
            if length > best[1]:
                best = (i, length)
        if best[1] >= minimum:
            if pending:
                ops.append(('add', bytes(pending)))
                pending = bytearray()
            ops.append(('copy', best[0], best[1]))
            n += best[1]
        else:
            pending.append(result[n])
            n += 1
    if pending:
        ops.append(('add', bytes(pending)))
    return ops


def encode(targets):
    """targets: [(name, source bytes, result bytes, inserts)] where inserts are (source offset, bytes) the result added;
    an empty list for a result of the source's size, None for a rebuilt result of any size. Targets with the same
    operations share one table."""
    seen, groups = set(), {}
    for name, source, result, inserts in sorted(targets, key=lambda t: (t[0].lower(), len(t[1]), zlib.crc32(t[1]))):
        key = (name.lower(), len(source), zlib.crc32(source))
        assert key not in seen, f'{name}: two targets for one source'
        seen.add(key)
        if inserts is None:
            ops = diff_ops(source, result)
        elif len(source) == len(result) and not inserts:
            ops = same_size_ops(source, result)
        else:
            ops = M.delta_ops(source, result, inserts)
        assert M.apply_ops(source, ops) == result, name
        shape = (name.lower(), tuple((o[0], o[1], o[2]) if o[0] == 'copy' else ('add', zlib.crc32(o[1]), len(o[1])) for o in ops))
        groups.setdefault(shape, (name, ops, []))[2].append((len(source), zlib.crc32(source), len(result), zlib.crc32(result)))
    out = bytearray(MAGIC3 + struct.pack('<I', len(groups)))
    for name, ops, sources in groups.values():
        added = b''.join(op[1] for op in ops if op[0] == 'add')
        packed = zlib.compress(added, 9)
        raw = name.encode()
        out += struct.pack('<H', len(raw)) + raw + struct.pack('<I', len(sources))
        for src in sources:
            out += struct.pack('<IIII', *src)
        out += struct.pack('<III', len(ops), len(added), len(packed))
        for op in ops:
            out += struct.pack('<BII', COPY, op[1], op[2]) if op[0] == 'copy' else struct.pack('<BI', ADD, len(op[1]))
        out += packed
    assert len(parse(bytes(out))) == len(targets)
    return bytes(out)


def parse(blob):
    """[dict(name, size, crc, result_size, result_crc, ops)] of an OSLV file, one per source (sources of one target share
    its operations)."""
    version = {MAGIC3: 3, MAGIC2: 2, MAGIC1: 1}.get(blob[:8], 0)
    if not version:
        raise ValueError('not an OSLV file')
    count, pos = struct.unpack_from('<I', blob, 8)[0], 12
    out = []
    for _ in range(count):
        n = struct.unpack_from('<H', blob, pos)[0]
        name = blob[pos + 2:pos + 2 + n].decode()
        pos += 2 + n
        sources = []
        if version == 3:
            nsrc = struct.unpack_from('<I', blob, pos)[0]
            sources = [struct.unpack_from('<IIII', blob, pos + 4 + 16 * i) for i in range(nsrc)]
            pos += 4 + 16 * nsrc
            nops, added_size, packed_size = struct.unpack_from('<III', blob, pos)
            pos += 12
        elif version == 2:
            *src, nops, added_size, packed_size = struct.unpack_from('<IIIIIII', blob, pos)
            sources = [tuple(src)]
            pos += 28
        else:
            *src, nops = struct.unpack_from('<IIIII', blob, pos)
            sources = [tuple(src)]
            pos += 20
        ops = []
        for _ in range(nops):
            kind = blob[pos]
            if kind == COPY:
                ops.append(['copy'] + list(struct.unpack_from('<II', blob, pos + 1)))
                pos += 9
            elif kind == ADD:
                length = struct.unpack_from('<I', blob, pos + 1)[0]
                if version == 1:
                    ops.append(['add', blob[pos + 5:pos + 5 + length]])
                    pos += 5 + length
                else:
                    ops.append(['add', length])
                    pos += 5
            else:
                raise ValueError(f'{name}: unknown operation {kind}')
        if version >= 2:
            added = zlib.decompress(blob[pos:pos + packed_size])
            if len(added) != added_size:
                raise ValueError(f'{name}: added bytes have the wrong size')
            pos += packed_size
            at = 0
            for op in ops:
                if op[0] == 'add':
                    op[1], at = added[at:at + op[1]], at + op[1]
        shared = [tuple(o) for o in ops]
        for size, crc, rsize, rcrc in sources:
            out.append(dict(name=name, size=size, crc=crc, result_size=rsize, result_crc=rcrc, ops=shared))
    if pos != len(blob):
        raise ValueError('trailing bytes')
    return out


def apply(target, source):
    """The result of a target over `source`, or None when `source` is not the target's exact source."""
    if len(source) != target['size'] or zlib.crc32(source) != target['crc']:
        return None
    result = M.apply_ops(source, target['ops'])
    if len(result) != target['result_size'] or zlib.crc32(result) != target['result_crc']:
        return None
    return result
