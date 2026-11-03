#!/usr/bin/env python3
"""Builds a DCPX2 block patch from pairs of (original, patched) files: the 16-byte runs that differ, each with its
original and its replacement block, and CRC32s of both versions so the patcher can tell untouched from already-patched
data. The original blocks let the patcher accept a file of another CRC32 when every block it replaces matches.
Adapted from Dragon's Crown Pro PC (whose DCPX1 runs hold only the replacement).

    make_dcpx.py out.dcpx  archive/path=orig.bin:patched.bin  [...]
"""
import zlib
import struct
import sys
from pathlib import Path

MAGIC = b'DCPX2\0\0\0'


def dcpx_bytes(targets):
    """targets: [(archive path, original bytes, patched bytes)]."""
    buf = bytearray(MAGIC + struct.pack('<I', len(targets)))
    seen = set()
    for name, orig, new in targets:
        assert len(orig) == len(new) and len(orig) % 16 == 0, name
        key = (name.lower(), len(orig), zlib.crc32(orig))
        assert key not in seen, f'{name} {key[2]:08x}: two targets for the same original'
        seen.add(key)
        runs = [(i, orig[i:i + 16], new[i:i + 16]) for i in range(0, len(orig), 16) if orig[i:i + 16] != new[i:i + 16]]
        assert runs, f'{name} {key[2]:08x}: the patch changes nothing'
        assert zlib.crc32(orig) != zlib.crc32(new), name
        nb = name.encode()
        buf += struct.pack('<H', len(nb)) + nb + struct.pack('<III', len(orig), zlib.crc32(orig), zlib.crc32(new))
        buf += struct.pack('<I', len(runs))
        for off, before, after in runs:
            buf += struct.pack('<I', off) + before + after
        print(f'{name} {zlib.crc32(orig):08x}: {len(runs)} changed 16-byte runs of {len(orig) // 16}')
    verify(bytes(buf), {(n.lower(), len(o), zlib.crc32(o)): (o, p) for n, o, p in targets})
    return bytes(buf)


def parse(blob):
    """[(name, size, crc_before, crc_after, [(offset, original or None, replacement)])] of a DCPX1 or DCPX2 patch."""
    assert blob[:4] == b'DCPX' and blob[4:5] in (b'1', b'2') and blob[5:8] == b'\0\0\0', 'not a DCPX patch'
    source = blob[4:5] == b'2'
    count, pos = struct.unpack_from('<I', blob, 8)[0], 12
    out = []
    for _ in range(count):
        n = struct.unpack_from('<H', blob, pos)[0]; pos += 2
        name = blob[pos:pos + n].decode(); pos += n
        size, before, after, nruns = struct.unpack_from('<IIII', blob, pos); pos += 16
        runs = []
        for _ in range(nruns):
            off = struct.unpack_from('<I', blob, pos)[0]
            if source:
                runs.append((off, blob[pos + 4:pos + 20], blob[pos + 20:pos + 36])); pos += 36
            else:
                runs.append((off, None, blob[pos + 4:pos + 20])); pos += 20
        out.append((name, size, before, after, runs))
    assert pos == len(blob), 'trailing bytes'
    return out


def verify(blob, files):
    """Checks a DCPX2 patch against the files it was made from: runs inside the file, in order, not overlapping, each one
    changing its block; the original blocks and the replacements rebuild exactly the files the CRC32s name."""
    assert blob[:5] == b'DCPX2'
    targets = parse(blob)
    keys = set()
    for name, size, before, after, runs in targets:
        key = (name.lower(), size, before)
        assert key not in keys, f'{name} {before:08x}: duplicate target'
        keys.add(key)
        orig, new = files[key]
        assert runs and zlib.crc32(new) == after and before != after, name
        end = 0
        out = bytearray(orig)
        for off, original, replacement in runs:
            assert off >= end and off + 16 <= size, f'{name} {before:08x}: run at {off:#x} overlaps or leaves the file'
            assert original != replacement, f'{name} {before:08x}: run at {off:#x} changes nothing'
            assert orig[off:off + 16] == original, f'{name} {before:08x}: run at {off:#x} has the wrong original block'
            out[off:off + 16] = replacement
            end = off + 16
        assert bytes(out) == new, f'{name} {before:08x}: the runs do not rebuild the patched file'


def main():
    out = Path(sys.argv[1])
    targets = []
    for spec in sys.argv[2:]:
        name, files = spec.split('=', 1)
        orig_p, new_p = files.split(':', 1)
        targets.append((name, Path(orig_p).read_bytes(), Path(new_p).read_bytes()))
    buf = dcpx_bytes(targets)
    out.write_bytes(buf)
    print(f'-> {out} ({len(buf)} bytes)')


if __name__ == '__main__':
    main()
