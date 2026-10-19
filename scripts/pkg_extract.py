"""Unpack a PS4 fake PKG (fPKG) into a game folder (eboot.bin, sce_module, sce_sys, ...).

Follows shadPS4's PKG::Extract (src/core/file_format/pkg.cpp, v0.4.0):
  ENTRY_KEYS[3] --RSA(PkgDerivedKey3)--> dk3
  sha256(IMAGE_KEY entry header || dk3) --> iv|key; AES-CBC(IMAGE_KEY) --RSA(Fake)--> ekpfs
  HMAC-SHA256(ekpfs, 1 || pfs seed) --> tweak key | data key
  outer PFS image: AES-XTS, 0x1000 sectors -> contains a PFSC (zlib, 0x10000 blocks) inner PFS
  inner PFS: header, 0xA8-byte inodes, dirents; superroot -> uroot -> game files.

Only fake PKGs (image key index 3) can be unpacked; retail PKGs need console keys.

Usage:
  python scripts/pkg_extract.py <game.pkg> --info
  python scripts/pkg_extract.py <game.pkg> --list
  python scripts/pkg_extract.py <game.pkg> <out_dir>                    # resumable
  python scripts/pkg_extract.py <game.pkg> <out_dir> --only "*.prx" eboot.bin
"""
import argparse
import fnmatch
import hashlib
import hmac
import json
import os
import struct
import sys
import time
import zlib

try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
except ImportError:
    sys.exit("The 'cryptography' package is required (run tools/setup.ps1 -InstallPython).")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pkg_keys  # noqa: E402

PKG_MAGIC = 0x7F434E54
PFSC_MAGIC = b"PFSC"
XTS_SECTOR = 0x1000
PFS_FILE, PFS_DIR, PFS_CUR, PFS_PARENT = 2, 3, 4, 5
INODE_SIZE = 0xA8

ENTRY_NAMES = {
    0x0001: "digests", 0x0010: "entry_keys", 0x0020: "image_key", 0x0080: "general_digests",
    0x0100: "metas", 0x0200: "entry_names", 0x0400: "license.dat", 0x0401: "license.info",
    0x0402: "nptitle.dat", 0x0403: "npbind.dat", 0x0404: "selfinfo.dat", 0x0406: "imageinfo.dat",
    0x0407: "target-deltainfo.dat", 0x0408: "origin-deltainfo.dat", 0x0409: "psreserved.dat",
    0x1000: "param.sfo", 0x1001: "playgo-chunk.dat", 0x1002: "playgo-chunk.sha",
    0x1003: "playgo-manifest.xml", 0x1004: "pronunciation.xml", 0x1005: "pronunciation.sig",
    0x1006: "pic1.png", 0x1007: "pubtoolinfo.dat", 0x1008: "app/playgo-chunk.dat",
    0x1009: "app/playgo-chunk.sha", 0x100A: "app/playgo-manifest.xml", 0x100B: "shareparam.json",
    0x100C: "shareoverlayimage.png", 0x100D: "save_data.png", 0x100E: "shareprivacyguardimage.png",
    0x1200: "icon0.png", 0x1220: "pic0.png", 0x1240: "snd0.at9", 0x1280: "icon0.dds",
    0x12A0: "pic0.dds", 0x12C0: "pic1.dds", 0x1400: "trophy/trophy00.trp",
}


def rsa_pkcs1_decrypt(c: bytes, n: int, d: int) -> bytes:
    m = pow(int.from_bytes(c, "big"), d, n).to_bytes(256, "big")
    if m[0] != 0 or m[1] != 2:
        raise ValueError("RSA padding check failed (not a fake PKG?)")
    return m[m.index(0, 2) + 1:]


def parse_sfo(data: bytes) -> dict:
    magic, ver, key_tab, data_tab, count = struct.unpack_from("<IIIII", data, 0)
    if magic != 0x46535000:
        return {}
    out = {}
    for i in range(count):
        koff, fmt, length, _maxlen, doff = struct.unpack_from("<HHIII", data, 0x14 + i * 16)
        key = data[key_tab + koff:data.index(0, key_tab + koff)].decode()
        raw = data[data_tab + doff:data_tab + doff + length]
        out[key] = int.from_bytes(raw, "little") if fmt == 0x0404 else raw.rstrip(b"\0").decode("utf-8", "replace")
    return out


class Pkg:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        h = self.f.read(0x1000)
        be32 = lambda o: struct.unpack_from(">I", h, o)[0]
        be64 = lambda o: struct.unpack_from(">Q", h, o)[0]
        if be32(0) != PKG_MAGIC:
            raise ValueError("not a PS4 PKG")
        self.content_id = h[0x40:0x64].rstrip(b"\0").decode()
        self.entry_count = be32(0x10)
        self.table_offset = be32(0x18)
        self.pfs_offset = be64(0x410)
        self.pfs_size = be64(0x418)

        self.f.seek(self.table_offset)
        table = self.f.read(self.entry_count * 32)
        self.entries = []
        for i in range(self.entry_count):
            raw = table[i * 32:(i + 1) * 32]
            eid, _fn, fl1, fl2, off, size = struct.unpack_from(">IIIIII", raw)
            self.entries.append(dict(id=eid, flags1=fl1, flags2=fl2, offset=off, size=size, raw=raw))
        self._derive_keys()
        self._open_pfsc()

    def read_raw(self, off, size):
        self.f.seek(off)
        return self.f.read(size)

    def entry_data(self, e):
        return self.read_raw(e["offset"], e["size"])

    def _derive_keys(self):
        by_id = {e["id"]: e for e in self.entries}
        ek = self.entry_data(by_id[0x10])
        key1_3 = ek[32 + 7 * 32 + 3 * 256:32 + 7 * 32 + 4 * 256]
        self.dk3 = rsa_pkcs1_decrypt(key1_3, pkg_keys.PKGDERIVEDKEY3KEYSET_N, pkg_keys.PKGDERIVEDKEY3KEYSET_D)[:32]
        ik = by_id[0x20]
        if (ik["flags2"] >> 12) & 0xF != 3:
            raise ValueError("image key index %d: only fake PKGs (index 3) can be unpacked" % ((ik["flags2"] >> 12) & 0xF))
        self.entry_ivkey = lambda e: hashlib.sha256(e["raw"] + self.dk3).digest()
        ivkey = self.entry_ivkey(ik)
        dec = Cipher(algorithms.AES(ivkey[16:32]), modes.CBC(ivkey[:16])).decryptor()
        imgkey = dec.update(self.entry_data(ik)) + dec.finalize()
        self.ekpfs = rsa_pkcs1_decrypt(imgkey, pkg_keys.FAKEKEYSET_N, pkg_keys.FAKEKEYSET_D)[:32]
        seed = self.read_raw(self.pfs_offset + 0x370, 16)
        d = hmac.new(self.ekpfs, struct.pack("<I", 1) + seed, hashlib.sha256).digest()
        self.tweak_key, self.data_key = d[:16], d[16:32]
        self.xts_key = self.data_key + self.tweak_key

    def decrypt_entry(self, e):
        data = self.entry_data(e)
        if not (e["flags1"] & 0x80000000):
            return data
        ivkey = self.entry_ivkey(e)
        n = len(data) & ~15
        dec = Cipher(algorithms.AES(ivkey[16:32]), modes.CBC(ivkey[:16])).decryptor()
        return dec.update(data[:n]) + dec.finalize() + data[n:]

    # Outer PFS image: AES-XTS with 0x1000-byte sectors numbered from the image start.
    def read_pfs(self, off, size):
        start = off & ~(XTS_SECTOR - 1)
        end = (off + size + XTS_SECTOR - 1) & ~(XTS_SECTOR - 1)
        enc = self.read_raw(self.pfs_offset + start, end - start)
        out = bytearray(len(enc))
        sector = start // XTS_SECTOR
        for i in range(0, len(enc), XTS_SECTOR):
            d = Cipher(algorithms.AES(self.xts_key), modes.XTS((sector + i // XTS_SECTOR).to_bytes(16, "little"))).decryptor()
            out[i:i + XTS_SECTOR] = d.update(enc[i:i + XTS_SECTOR])
        return bytes(out[off - start:off - start + size])

    def _open_pfsc(self):
        self.pfsc_offset = None
        for o in range(0x20000, min(self.pfs_size, 0x4000000), 0x10000):
            if self.read_pfs(o, 4) == PFSC_MAGIC:
                self.pfsc_offset = o
                break
        if self.pfsc_offset is None:
            raise ValueError("PFSC image not found (wrong keys?)")
        hdr = self.read_pfs(self.pfsc_offset, 0x30)
        _m, _u4, _u8, _bs, self.block_size, block_offsets, _data_start, data_length = struct.unpack_from("<iiiiqqQq", hdr)
        self.num_blocks = data_length // self.block_size
        raw = self.read_pfs(self.pfsc_offset + block_offsets, (self.num_blocks + 1) * 8)
        self.sector_map = struct.unpack("<%dQ" % (self.num_blocks + 1), raw)

    def _inflate_block(self, comp):
        if len(comp) == self.block_size:
            return comp
        out = zlib.decompress(comp)
        return out.ljust(self.block_size, b"\0")

    def read_blocks(self, first, count):
        """Inner PFS blocks [first, first+count), decompressed."""
        lo, hi = self.sector_map[first], self.sector_map[first + count]
        comp = self.read_pfs(self.pfsc_offset + lo, hi - lo)
        parts = []
        for b in range(first, first + count):
            s, e = self.sector_map[b] - lo, self.sector_map[b + 1] - lo
            parts.append(self._inflate_block(comp[s:e]))
        return b"".join(parts)

    # Inner PFS
    def load_fs(self):
        h = self.read_blocks(0, 1)
        mode, = struct.unpack_from("<H", h, 0x1C)
        self.fs_block = struct.unpack_from("<i", h, 0x20)[0]
        dinode_count, = struct.unpack_from("<q", h, 0x30)
        dinode_blocks, = struct.unpack_from("<q", h, 0x40)
        self.superroot, = struct.unpack_from("<q", h, 0x48)
        if self.fs_block != self.block_size:
            raise ValueError("unexpected PFS block size %#x" % self.fs_block)
        # Unsigned 32-bit dinodes (0x64 header + 12 direct + 5 indirect u32 = 0xA8); fPKG inner PFS.
        if mode & 0x3:
            raise ValueError("unexpected PFS mode %#x (expected unsigned 32-bit inodes)" % mode)
        self.inodes = []
        data = self.read_blocks(1, dinode_blocks)
        for b in range(dinode_blocks):
            blk = data[b * self.block_size:(b + 1) * self.block_size]
            for p in range(0, self.block_size - INODE_SIZE + 1, INODE_SIZE):
                m, _nl, flags, size = struct.unpack_from("<HHIq", blk, p)
                if m == 0:
                    break
                blocks, loc = struct.unpack_from("<II", blk, p + 0x60)
                self.inodes.append(dict(mode=m, flags=flags, size=size, blocks=blocks, loc=loc))
        if len(self.inodes) < dinode_count:
            raise ValueError("read %d of %d inodes" % (len(self.inodes), dinode_count))

    def read_dir(self, ino):
        node = self.inodes[ino]
        data = self.read_blocks(node["loc"], node["blocks"])
        out = []
        for b in range(0, len(data), self.block_size):
            p = b
            while p + 16 <= b + self.block_size:
                d_ino, d_type, nlen, esize = struct.unpack_from("<iiii", data, p)
                if d_ino == 0 or esize <= 0:
                    break
                name = data[p + 16:p + 16 + nlen].decode("utf-8", "replace")
                out.append((name, d_ino, d_type))
                p += esize
        return out

    def walk(self):
        """Yield (relative_path, inode, type) for everything under uroot."""
        root = dict((n, (i, t)) for n, i, t in self.read_dir(self.superroot))
        if "uroot" not in root:
            raise ValueError("no uroot in PFS")
        stack = [("", root["uroot"][0])]
        while stack:
            prefix, ino = stack.pop()
            for name, child, t in self.read_dir(ino):
                if t in (PFS_CUR, PFS_PARENT):
                    continue
                rel = prefix + "/" + name if prefix else name
                yield rel, child, t
                if t == PFS_DIR:
                    stack.append((rel, child))


def human(n):
    for u in ("B", "KB", "MB", "GB"):
        if n < 1024 or u == "GB":
            return "%.1f %s" % (n, u)
        n /= 1024


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pkg")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--info", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--only", nargs="+", metavar="PATTERN", help="extract only paths matching these globs (case-insensitive)")
    args = ap.parse_args()

    pkg = Pkg(args.pkg)
    sfo = {}
    for e in pkg.entries:
        if e["id"] == 0x1000:
            sfo = parse_sfo(pkg.entry_data(e))
    print("content id :", pkg.content_id)
    for k in ("TITLE", "TITLE_ID", "APP_VER", "VERSION", "CATEGORY", "SYSTEM_VER"):
        if k in sfo:
            v = sfo[k]
            print("%-11s: %s" % (k.lower(), hex(v) if k == "SYSTEM_VER" else v))
    print("pfs image  : %s at %#x, PFSC at +%#x, %d blocks" % (human(pkg.pfs_size), pkg.pfs_offset, pkg.pfsc_offset, pkg.num_blocks))
    if args.info:
        return

    pkg.load_fs()
    items = list(pkg.walk())
    total = sum(pkg.inodes[i]["size"] for _, i, t in items if t == PFS_FILE)
    nfiles = sum(1 for _, _, t in items if t == PFS_FILE)
    print("files      : %d (%s)" % (nfiles, human(total)))
    if args.list or not args.out:
        for rel, ino, t in sorted(items):
            print("%s %12d  %s" % ("d" if t == PFS_DIR else "-", pkg.inodes[ino]["size"] if t == PFS_FILE else 0, rel))
        return

    last = [-1]

    def report(done, total, rate):
        percent = 100 * done // max(total, 1)
        if percent == last[0]:
            return
        last[0] = percent
        end = "\r" if sys.stdout.isatty() else "\n"
        sys.stdout.write("  %3d%%  %s / %s  %.0f MB/s%s" % (percent, human(done), human(total), rate, end))
        sys.stdout.flush()
    extract(pkg, args.out, report, items, args.only)
    print("\ndone: %s -> %s" % (args.pkg, os.path.abspath(args.out)))


def matches(rel, patterns):
    rel = rel.lower()
    return any(fnmatch.fnmatchcase(rel, p.lower()) for p in patterns)


def extract(pkg, out, progress=None, items=None, only=None):
    """Unpacks `pkg` (a path or Pkg) into `out`; resumable (complete files are kept).
    `only`: case-insensitive globs on the PFS-relative path (None = everything).
    progress(done_bytes, total_bytes, mb_per_s) is called as data is written."""
    if not isinstance(pkg, Pkg):
        pkg = Pkg(pkg)
    if items is None:
        pkg.load_fs()
        items = list(pkg.walk())
    if only:
        items = [(rel, ino, t) for rel, ino, t in items if t == PFS_FILE and matches(rel, only)]
    total = sum(pkg.inodes[i]["size"] for _, i, t in items if t == PFS_FILE)
    out = os.path.abspath(out)
    os.makedirs(os.path.join(out, "sce_sys"), exist_ok=True)
    # PKG metadata entries -> sce_sys (as shadPS4 does); the PFS copy wins where both exist.
    for e in pkg.entries:
        if e["id"] < 0x400:
            continue
        name = ENTRY_NAMES.get(e["id"], str(e["id"]))
        dst = os.path.join(out, "sce_sys", name)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as fo:
            fo.write(pkg.decrypt_entry(e))

    done = 0
    t0 = time.time()
    chunk_blocks = 256  # 16 MB of decompressed data per read
    for rel, ino, t in sorted(items):
        dst = os.path.join(out, *rel.split("/"))
        if t == PFS_DIR:
            os.makedirs(dst, exist_ok=True)
            continue
        node = pkg.inodes[ino]
        if node["flags"] & 0x1:
            raise ValueError("%s: PFS-compressed file not supported" % rel)
        if os.path.isfile(dst) and os.path.getsize(dst) == node["size"]:
            done += node["size"]
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        tmp = dst + ".part"
        remaining = node["size"]
        with open(tmp, "wb") as fo:
            b = 0
            while remaining > 0:
                n = min(chunk_blocks, node["blocks"] - b)
                data = pkg.read_blocks(node["loc"] + b, n)
                w = data[:remaining]
                fo.write(w)
                remaining -= len(w)
                done += len(w)
                b += n
                if progress:
                    progress(done, total, done / 1048576 / max(time.time() - t0, 1e-6))
        os.replace(tmp, dst)
    write_marker(pkg.path, out, only)
    return out


def marker_path(out):
    return os.path.join(out, ".pkg_extracted")


def write_marker(pkg_path, out, only):
    with open(marker_path(out), "w", encoding="utf-8") as fo:
        json.dump({"pkg": os.path.abspath(pkg_path), "size": os.path.getsize(pkg_path),
                   "only": sorted(only) if only else None}, fo)


def is_extracted(pkg_path, out, only=None):
    """True when `out` holds a finished unpack of this PKG that covers `only`
    (a full unpack covers every subset)."""
    try:
        with open(marker_path(out), encoding="utf-8") as fi:
            marker = json.load(fi)
    except (OSError, ValueError):
        return False
    if marker.get("size") != os.path.getsize(pkg_path):
        return False
    done = marker.get("only")
    return done is None or (only is not None and set(map(str.lower, only)) <= set(map(str.lower, done)))


if __name__ == "__main__":
    main()
