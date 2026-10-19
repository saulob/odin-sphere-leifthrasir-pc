"""Read-only report on the executables of an extracted game folder (plaintext SELF check, SDK, modules, imports).

Usage: python scripts/game_report.py <game dir>
"""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import game_profiles  # noqa: E402
from ps4_module import procparam_sdk_version, read_module  # noqa: E402


def describe(path):
    module = read_module(path)
    imports = sum(1 for s in module['symbols'] if not s['section'] and s['identity'])
    exports = sum(1 for s in module['symbols'] if s['section'] and s['identity'])
    sdk = procparam_sdk_version(module)
    return module, imports, exports, sdk


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    args = parser.parse_args()
    game = args.game
    profile = game_profiles.profile(game)
    status = 0
    names = ['eboot.bin'] + [f'sce_module/{m.name}' for m in sorted((game / 'sce_module').glob('*.prx'))]
    for name in names:
        path = game / name
        try:
            module, imports, exports, sdk = describe(path)
        except (OSError, ValueError, StopIteration, KeyError) as error:
            print(f'  {name}: not a readable plaintext SELF ({error})')
            status = 1
            continue
        extra = f', SDK {sdk:#x}, NEEDED {len(module["needed"])}' if sdk else ''
        print(f'  {name}: plaintext SELF, {path.stat().st_size:,} bytes, imports {imports}, exports {exports}, '
              f'relocations {len(module["relocs"])}{extra}')
        print(f'    sha256 {module["sha256"]}')
        if profile and name == profile['entry']:
            version = game_profiles.version_of(game)
            known = profile['versions'].get(version or '', {}).get('eboot_sha256')
            print(f'    profile: version {version or "unknown"}, entry hash '
                  f'{"matches" if known == module["sha256"] else "DOES NOT match"} the profile')
            if known != module['sha256']:
                status = 1
    if profile:
        missing = [m for m in profile['modules'] if not (game / 'sce_module' / m).is_file()]
        print(f'  profile modules: {", ".join(profile["modules"])}' + (f'  MISSING: {", ".join(missing)}' if missing else ''))
        status = status or int(bool(missing))
    return status


if __name__ == '__main__':
    sys.exit(main())
