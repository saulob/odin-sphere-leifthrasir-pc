#!/usr/bin/env python3
"""Builds a DCPX block patch from pairs of (original, patched) files: the 16-byte runs that differ,
with CRC32s of both versions so the patcher can tell untouched from already-patched data.
Taken from Dragon's Crown Pro PC.

    make_dcpx.py out.dcpx  archive/path=orig.bin:patched.bin  [...]
"""
import zlib
import struct
import sys
from pathlib import Path


def dcpx_bytes(targets):
    """targets: [(archive path, original bytes, patched bytes)]."""
    buf = bytearray(b'DCPX1\0\0\0' + struct.pack('<I', len(targets)))
    for name, orig, new in targets:
        assert len(orig) == len(new), name
        runs = [(i, new[i:i + 16]) for i in range(0, len(orig), 16) if orig[i:i + 16] != new[i:i + 16]]
        nb = name.encode()
        buf += struct.pack('<H', len(nb)) + nb + struct.pack('<III', len(orig), zlib.crc32(orig), zlib.crc32(new))
        buf += struct.pack('<I', len(runs))
        for off, data in runs:
            buf += struct.pack('<I', off) + data
        print(f'{name} {zlib.crc32(orig):08x}: {len(runs)} changed 16-byte runs of {len(orig) // 16}')
    return bytes(buf)


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
