"""Read-only parsing of plaintext PS4 SELF executables and modules (eboot.bin, *.prx, *.elf).

Only dumps that are already decrypted (fake PKG contents) are supported; nothing is decrypted
here and every offset is range-checked before use.
"""
import base64
import hashlib
import struct

SELF_MAGIC = b'O\x15=\x1d'
PT_LOAD, PT_DYNAMIC, PT_TLS = 1, 2, 7
PT_SCE_DYNLIBDATA, PT_SCE_PROCPARAM, PT_SCE_MODULEPARAM, PT_SCE_RELRO = 0x61000000, 0x61000001, 0x61000002, 0x61000010
DT_NEEDED = 1
ID_TAGS = {'library': (0x61000013, 0x61000015), 'module': (0x6100000d, 0x6100000f)}
ID_ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-'
NID_SALT = bytes.fromhex('518d64a635ded8c1e6b039b1c3e55230')
MAX_IMAGE = 512 * 1024 * 1024


def span(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(f'out of bounds: {offset:#x}+{size:#x} / {len(data):#x}')
    return data[offset:offset + size]


def unpack(fmt, data, offset):
    return struct.unpack(fmt, span(data, offset, struct.calcsize(fmt)))


def nid(name):
    digest = hashlib.sha1(name.encode('ascii') + NID_SALT).digest()[:8][::-1]
    return base64.b64encode(digest).decode().rstrip('=').replace('/', '-')


def encode_id(value):
    result = ID_ALPHABET[value & 63]
    value >>= 6
    while value:
        result = ID_ALPHABET[value & 63] + result
        value >>= 6
    return result


def parse_self(data):
    """Returns (elf image, ELF header tuple, program headers, SELF segments, missing header indexes)."""
    if span(data, 0, 4) != SELF_MAGIC:
        raise ValueError('expected PS4 SELF')
    count, = unpack('<H', data, 24)
    base = 32 + count * 32
    header = unpack('<16sHHIQQQIHHHHHH', data, base)
    if header[0][:7] != b'\x7fELF\x02\x01\x01' or header[2] != 62:
        raise ValueError('expected little-endian x86-64 ELF')
    if header[9] != 56 or not 0 < header[10] < 256:
        raise ValueError('unsupported program headers')
    ph = [dict(zip(('type', 'flags', 'offset', 'vaddr', 'paddr', 'filesz', 'memsz', 'align'),
                   unpack('<IIQQQQQQ', data, base + header[5] + i * 56)))
          for i in range(header[10])]
    end = max(p['offset'] + p['filesz'] for p in ph)
    if end > MAX_IMAGE:
        raise ValueError('image exceeds 512 MiB limit')
    elf = bytearray(end)
    header_end = header[5] + header[10] * 56
    elf[:header_end] = span(data, base, header_end)
    covered = [(0, header_end)]
    segments = []
    for i in range(count):
        flags, off, size, mem = unpack('<QQQQ', data, 32 + i * 32)
        segments.append(dict(flags=hex(flags), offset=off, size=size))
        if not flags & 0x800:
            continue
        if flags & 10:
            raise ValueError('encrypted/compressed SELF segment is unsupported')
        index = (flags >> 20) & 4095
        if index >= len(ph):
            raise ValueError('invalid segment index')
        p = ph[index]
        if size != p['filesz'] or size != mem:
            raise ValueError('unsupported blocked segment layout')
        elf[p['offset']:p['offset'] + size] = span(data, off, size)
        covered.append((p['offset'], p['offset'] + size))
    missing = []
    for i, p in enumerate(ph):
        if p['filesz'] and not any(a <= p['offset'] and p['offset'] + p['filesz'] <= z for a, z in covered):
            missing.append(i)
            if p['type'] in (PT_LOAD, PT_DYNAMIC, PT_SCE_PROCPARAM, PT_SCE_RELRO):
                raise ValueError(f'required segment {i} is unavailable')
    return bytes(elf), header, ph, segments, missing


def parse_sfo(data):
    magic, _version, keys, values, count = unpack('<4sIIII', data, 0)
    if magic != b'\0PSF':
        raise ValueError('bad SFO signature')
    result = {}
    for i in range(count):
        key, fmt, size, _capacity, off = unpack('<HHIII', data, 20 + i * 16)
        name = data[keys + key:data.index(0, keys + key)].decode('ascii')
        raw = span(data, values + off, size)
        result[name] = struct.unpack('<I', raw)[0] if fmt == 0x404 else raw.rstrip(b'\0').decode('utf8', 'replace')
    return result


def read_module(path):
    """Parse a module's dynamic data: imports, exports, relocations and library identities."""
    source = path.read_bytes()
    elf, header, ph, _, missing = parse_self(source)
    dp = next(p for p in ph if p['type'] == PT_DYNAMIC)
    dynamic = []
    for pos in range(dp['offset'], dp['offset'] + dp['filesz'], 16):
        tag, value = unpack('<QQ', elf, pos)
        if not tag:
            break
        dynamic.append((tag, value))
    tags = dict(dynamic)
    lp = next(p for p in ph if p['type'] == PT_SCE_DYNLIBDATA)
    blob = span(elf, lp['offset'], lp['filesz'])
    strings = span(blob, tags[0x61000035], tags[0x61000037])

    def string(offset):
        span(strings, offset, 1)
        return strings[offset:strings.index(0, offset)].decode('ascii')
    libraries, modules = {}, {}
    for tag, value in dynamic:
        for kind, table in (('library', libraries), ('module', modules)):
            if tag in ID_TAGS[kind]:
                key = encode_id(value >> 48)
                identity = (string(value & 0xffffffff), (value >> 32) & 65535)
                if key in table and table[key] != identity:
                    raise ValueError('conflicting module/library IDs')
                table[key] = identity

    def identity(name):
        parts = name.split('#')
        if len(parts) != 3:
            return None  # plain names such as module_start are not linkable
        n, lib, mod = parts
        return (n, libraries[lib], modules[mod])
    symbols = []
    table = span(blob, tags[0x61000039], tags[0x6100003f])
    for pos in range(0, len(table), 24):
        n, info, _other, section, value, size = unpack('<IBBHQQ', table, pos)
        name = string(n)
        symbols.append(dict(name=name, type=info & 15, binding=info >> 4, section=section,
                            value=value, size=size, identity=identity(name) if name else None))
    relocations = []
    for offset_tag, size_tag in ((0x61000029, 0x6100002d), (0x6100002f, 0x61000031)):
        table = span(blob, tags[offset_tag], tags[size_tag])
        if len(table) % 24:
            raise ValueError('bad relocation table size')
        for pos in range(0, len(table), 24):
            target, info, addend = unpack('<QQq', table, pos)
            relocations.append((target, info & 0xffffffff, info >> 32, addend))
    needed = [string(v) for t, v in dynamic if t == DT_NEEDED]
    return dict(elf=elf, header=header, ph=ph, tags=tags, symbols=symbols, relocs=relocations,
                identity=identity, libraries=libraries, modules=modules, needed=needed,
                sha256=hashlib.sha256(source).hexdigest(), size=len(source), missing=missing)


def canonical(identity, fallback=None):
    """NID#library for an import identity (NID, (library, version), module)."""
    return f'{identity[0]}#{identity[1][0]}' if identity else fallback


def procparam_sdk_version(module):
    p = next((p for p in module['ph'] if p['type'] == PT_SCE_PROCPARAM), None)
    if not p:
        return None
    return unpack('<Q', module['elf'], p['offset'] + 16)[0]
