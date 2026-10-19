"""Build the loader's boot image (OSLBOOT1) from a prepared game tree.

Maps the eboot's segments, links the game's own bundled modules after it (libc.prx, ...) and
resolves their exports natively; every other import stays by name for the host runtime.
Writes <out>/boot.bin and <out>/image.json (imports, unresolved imports, module table).

Usage: python scripts/prepare_image.py <game dir> [--out DIR] [--modules libc.prx ...]
"""
import argparse
import collections
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_profiles  # noqa: E402
from ps4_module import (MAX_IMAGE, PT_LOAD, PT_SCE_PROCPARAM, PT_SCE_RELRO, PT_TLS, canonical, nid,  # noqa: E402
                        parse_sfo, procparam_sdk_version, read_module, span, unpack)

MAGIC = b'OSLBOOT1'
FS_LOAD = bytes.fromhex('64488b042500000000')  # mov rax, fs:[0]
KIND_CODE, KIND_DATA = 1, 2
LOADABLE = (PT_LOAD, PT_SCE_RELRO)


def libc_init_env_is_ret(module):
    """The runtime stands in for libc's _init_env, which is only valid when the original is a bare RET."""
    wanted = nid('_init_env')
    ph, elf = module['ph'], module['elf']
    for s in module['symbols']:
        if s['identity'] and s['identity'][0] == wanted and s['section'] and s['type'] == 2:
            p = next(p for p in ph if p['type'] == PT_LOAD and p['flags'] & 1
                     and p['vaddr'] <= s['value'] < p['vaddr'] + p['filesz'])
            return span(elf, p['offset'] + s['value'] - p['vaddr'], s['size']) == b'\xc3'
    return False


def patch_fs_loads(image, ph, base):
    """Rewrite initial-exec `mov rax, fs:[0]` to GS: the host owns FS."""
    patched = 0
    for p in ph:
        if p['type'] != PT_LOAD or not p['flags'] & 1:
            continue
        start, end = base + p['vaddr'], base + p['vaddr'] + p['filesz']
        at = image.find(FS_LOAD, start, end)
        while at >= 0:
            image[at] = 0x65
            patched += 1
            at = image.find(FS_LOAD, at + len(FS_LOAD), end)
    return patched


def map_eboot(main):
    ph, elf = main['ph'], main['elf']
    loads = [p for p in ph if p['type'] in LOADABLE]
    size = max(p['vaddr'] + p['memsz'] for p in loads)
    entry = main['header'][4]

    def mapped(address, length=8):
        return any(p['vaddr'] <= address and address + length <= p['vaddr'] + p['memsz'] for p in loads)
    if size > MAX_IMAGE or not mapped(entry, 1):
        raise ValueError('invalid memory image')
    image = bytearray(size)
    for p in loads:
        if p['filesz'] > p['memsz']:
            raise ValueError('segment file size exceeds memory size')
        image[p['vaddr']:p['vaddr'] + p['filesz']] = span(elf, p['offset'], p['filesz'])
    segments = [(p['vaddr'], p['memsz'], p['flags']) for p in loads]
    relocs, imported = [], collections.OrderedDict()
    for target, kind, sym, addend in main['relocs']:
        if not mapped(target):
            raise ValueError(f'relocation outside mapped segments: {target:#x}')
        if kind == 8:
            relocs.append((target, 0, addend, 0))
        elif kind in (1, 6, 7):
            symbol = main['symbols'][sym]
            if symbol['section']:
                if symbol['section'] == 0xfff1:
                    struct.pack_into('<Q', image, target, symbol['value'] + addend)
                else:
                    relocs.append((target, 0, symbol['value'] + addend, 0))
            else:
                if not symbol['identity']:
                    raise ValueError(f'unlinkable import {symbol["name"]!r}')
                index = imported.setdefault(symbol['identity'], len(imported))
                rk = KIND_DATA if symbol['type'] == 1 else KIND_CODE
                if (rk == KIND_CODE and addend) or not 0 <= addend < 4096:
                    raise ValueError('unsupported import addend')
                relocs.append((target, rk, index, addend))
        else:
            raise ValueError(f'unsupported relocation {kind}')
    return image, segments, relocs, imported, entry, mapped


def link_modules(game, main, image, segments, relocs, imported, module_names):
    """Appends the bundled modules to the image; returns the module table, exports and TLS ids."""
    exports, by_nid, table = {}, collections.defaultdict(list), []
    base = (len(image) + 65535) & ~65535
    fs_patched = patch_fs_loads(image, main['ph'], 0)
    tls_module = 2
    for filename in module_names:
        m = read_module(game / 'sce_module' / filename)
        loads = [p for p in m['ph'] if p['type'] in LOADABLE]
        modsize = max(p['vaddr'] + p['memsz'] for p in loads)
        if base + modsize > MAX_IMAGE:
            raise ValueError('linked image exceeds the 512 MiB limit')
        image.extend(bytes(base + modsize - len(image)))

        def mapped(address, length=8, loads=loads):
            return any(p['vaddr'] <= address and address + length <= p['vaddr'] + p['memsz'] for p in loads)
        for p in loads:
            if p['filesz'] > p['memsz']:
                raise ValueError(f'{filename}: invalid segment size')
            image[base + p['vaddr']:base + p['vaddr'] + p['filesz']] = span(m['elf'], p['offset'], p['filesz'])
            segments.append((base + p['vaddr'], p['memsz'], p['flags']))
        tls = next((p for p in m['ph'] if p['type'] == PT_TLS), None)
        module_id = 0
        if tls:
            if tls['filesz'] > tls['memsz'] or not mapped(tls['vaddr'], tls['memsz']):
                raise ValueError(f'{filename}: unsupported TLS layout')
            module_id = tls_module
            tls_module += 1
        tls_relocations = 0
        for target, kind, sym, addend in m['relocs']:
            if not mapped(target):
                raise ValueError(f'{filename}: relocation outside image {target:#x}')
            target += base
            if kind == 8:
                relocs.append((target, 0, base + addend, 0))
            elif kind == 16:
                symbol = m['symbols'][sym]
                if symbol['name'] or symbol['type'] != 3 or addend or not module_id:
                    raise ValueError(f'{filename}: unsupported external TLS relocation')
                struct.pack_into('<Q', image, target, module_id)
                tls_relocations += 1
            elif kind in (1, 6, 7):
                symbol = m['symbols'][sym]
                if symbol['section']:
                    if symbol['section'] == 0xfff1:
                        struct.pack_into('<Q', image, target, symbol['value'] + addend)
                    else:
                        relocs.append((target, 0, base + symbol['value'] + addend, 0))
                elif not symbol['identity'] and symbol['binding'] == 2:
                    struct.pack_into('<Q', image, target, 0)  # weak undefined (module_start/stop)
                else:
                    rk = KIND_DATA if symbol['type'] == 1 else KIND_CODE
                    if (rk == KIND_CODE and addend) or not 0 <= addend < 4096:
                        raise ValueError(f'{filename}: unsupported import addend')
                    index = imported.setdefault(symbol['identity'], len(imported))
                    relocs.append((target, rk, index, addend))
            else:
                raise ValueError(f'{filename}: unsupported relocation type {kind}')
        count = 0
        for s in m['symbols']:
            if s['section'] and s['binding'] in (1, 2) and s['identity'] and s['type'] in (1, 2):
                if not mapped(s['value'], max(1, s['size'])):
                    raise ValueError(f'{filename}: export outside load segments')
                if s['identity'] in exports:
                    raise ValueError(f'{filename}: ambiguous export')
                value = (base + s['value'], KIND_DATA if s['type'] == 1 else KIND_CODE, filename)
                exports[s['identity']] = value
                by_nid[(s['identity'][0], s['identity'][1][0])].append(value)
                count += 1
        fs_patched += patch_fs_loads(image, m['ph'], base)
        table.append(dict(file=filename, base=base, size=modsize, init=base + m['tags'].get(12, 0),
                          tls_address=base + tls['vaddr'] if tls else 0, tls_memsz=tls['memsz'] if tls else 0,
                          tls_filesz=tls['filesz'] if tls else 0, tls_module=module_id,
                          tls_relocations=tls_relocations, exports=count, sha256=m['sha256']))
        base = (base + modsize + 65535) & ~65535
    return table, exports, by_nid, fs_patched


def build(game, out, module_names):
    main = read_module(game / 'eboot.bin')
    libc = read_module(game / 'sce_module' / 'libc.prx')
    init_env_ret = libc_init_env_is_ret(libc)
    image, segments, relocs, imported, entry, _ = map_eboot(main)
    eboot_size = len(image)
    table, exports, by_nid, fs_patched = link_modules(game, main, image, segments, relocs, imported, module_names)

    names = [None] * len(imported)
    for identity, index in imported.items():
        names[index] = canonical(identity)
    bindings, unresolved = [], []
    for identity, index in imported.items():
        found = exports.get(identity)
        if not found and identity[1][0] == 'libSceLibcInternal':
            candidates = by_nid.get((identity[0], 'libc'), [])
            found = candidates[0] if len(candidates) == 1 else None
        if found:
            bindings.append((index, found[0], found[1]))
        else:
            unresolved.append(names[index])

    main_tls = next((p for p in main['ph'] if p['type'] == PT_TLS), None)
    tls_values = (main_tls['vaddr'], main_tls['filesz'], main_tls['memsz'], main_tls['align']) if main_tls else (0, 0, 0, 0)
    if main_tls and (main_tls['filesz'] > main_tls['memsz'] or main_tls['memsz'] > 1024 * 1024
                     or main_tls['vaddr'] + main_tls['filesz'] > eboot_size):
        raise ValueError('unsupported eboot TLS layout')
    procparam = next(p for p in main['ph'] if p['type'] == PT_SCE_PROCPARAM)
    out.mkdir(parents=True, exist_ok=True)
    with (out / 'boot.bin').open('wb') as f:
        f.write(struct.pack('<8s6Q', MAGIC, len(image), entry, len(segments), len(relocs), len(names), int(init_env_ret)))
        f.write(struct.pack('<Q', procparam['vaddr']))
        f.write(struct.pack('<4Q', *tls_values))
        f.write(struct.pack('<Q', len(table)))
        for t in table:
            f.write(struct.pack('<7Q', t['base'], t['size'], t['init'], t['tls_address'], t['tls_memsz'],
                                t['tls_filesz'], t['tls_module']))
        f.write(struct.pack('<Q', len(bindings)))
        for binding in bindings:
            f.write(struct.pack('<3Q', *binding))
        for segment in segments:
            f.write(struct.pack('<3Q', *segment))
        for name in names:
            if len(name.encode()) >= 128:
                raise ValueError('import name too long')
            f.write(name.encode().ljust(128, b'\0'))
        for relocation in relocs:
            f.write(struct.pack('<QQqq', *relocation))
        f.write(image)

    sfo = parse_sfo((game / 'sce_sys' / 'param.sfo').read_bytes())
    libraries = collections.Counter(n.split('#')[1] for n in names)
    report = dict(
        title_id=sfo.get('TITLE_ID'), app_ver=sfo.get('APP_VER'), eboot_sha256=main['sha256'],
        entry=hex(entry), image_bytes=len(image), eboot_image_bytes=eboot_size,
        sdk_version=hex(procparam_sdk_version(main) or 0), needed_modules=main['needed'],
        init_env_is_ret=init_env_ret, fs_loads_patched=fs_patched,
        modules=[{k: (hex(v) if k in ('base', 'init', 'tls_address') else v) for k, v in t.items()} for t in table],
        main_tls=dict(zip(('vaddr', 'filesz', 'memsz', 'align'), tls_values)),
        imports=len(names), native_bindings=len(bindings), unresolved=len(unresolved),
        imports_by_library=dict(libraries.most_common()),
        unresolved_by_library=dict(collections.Counter(n.split('#')[1] for n in unresolved).most_common()),
        unresolved_imports=unresolved, import_list=names)
    (out / 'image.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('game', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--modules', nargs='*')
    args = parser.parse_args()
    sys.stdout.reconfigure(errors='replace')
    modules = args.modules
    if modules is None:
        profile = game_profiles.profile(args.game)
        modules = list(profile['modules']) if profile else ['libc.prx']
    if 'libc.prx' not in modules:
        parser.error('libc.prx must be linked (the runtime stands in for its start-up contract)')
    try:
        report = build(args.game, args.out, modules)
    except (ValueError, OSError, StopIteration, KeyError, IndexError) as error:
        print(f'prepare_image failed: {error}', file=sys.stderr)
        return 1
    print(f"{report['title_id']} {report['app_ver']} | entry {report['entry']} | image {report['image_bytes']:,} bytes | "
          f"modules {', '.join(m['file'] for m in report['modules'])}")
    print(f"{report['imports']} imports: {report['native_bindings']} bound natively, "
          f"{report['unresolved']} left to the host runtime; libc _init_env is RET: {report['init_env_is_ret']}")
    print(f'Output: {args.out.resolve()}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
