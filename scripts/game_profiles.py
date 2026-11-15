"""Per-title facts for the games this port knows, keyed by TITLE_ID in sce_sys/param.sfo.

A version's layers are its packages, the base first and then the updates applied over it; the
last layer's param.sfo is the finished game's. launcher/game_profile.cpp mirrors these values,
and tests/game_tests.cpp checks that the two agree.

Each key of 'versions' is one build. A release whose packages have the same param.sfo identity
as another build's has a key of its own, with the profile 'version' it shares and its own
'region'; only its eboot.bin tells them apart.
"""
import hashlib
from pathlib import Path

from ps4_module import parse_sfo

PROFILES = {
    'CUSA05083': {
        'name': 'Odin Sphere Leifthrasir',
        'region': 'USA',
        'content_id': 'UP0177-CUSA05083_00-ODINSPHERELE0000',
        'versions': {
            '1.00': {
                'layers': ({'app_ver': '01.00', 'sfo_version': '01.01', 'update': False},),
                'eboot_sha256': '7c517194aba4e5eea55d9197efae47d1567bd785b8c0dced7d1b0f4028d3eed6',
                'folder': 'CUSA05083-1.00', 'extra_files': (),
                'data_files': ('Odin2_BT_US.CPK', 'Odin2_OR_US.CPK', 'Odin2_RE_US.CPK'),
            },
            # The 1.01 patch is laid over the base package; it replaces eboot.bin and adds a patch archive.
            '1.01': {
                'layers': ({'app_ver': '01.00', 'sfo_version': '01.01', 'update': False},
                           {'app_ver': '01.01', 'sfo_version': '01.00', 'update': True}),
                'merged_sfo_version': '01.01',
                'eboot_sha256': '2865b6cec33f2515def173c1ed0c03afc27a1cd5fad1b0d0f516b4527aba9a88',
                'folder': 'CUSA05083-1.01', 'extra_files': ('sce_discmap_patch.plt',),
                'data_files': ('Odin2_BT_US.CPK', 'Odin2_OR_US.CPK', 'Odin2_RE_US.CPK', 'Odin2_US_PATCH1_RE_US.CPK'),
            },
        },
        'entry': 'eboot.bin',
        'extra_executables': (),
        # Linked after the eboot in this order (libc.prx needs libSceFios2.prx, as in the other ports).
        'modules': ('libc.prx', 'libSceFios2.prx'),
        # What the loader needs; the game data (Odin2_*.CPK) is only unpacked with --scope full.
        'core_patterns': ('eboot.bin', 'sce_module/*', 'sce_sys/*', '*.plt'),
    },
    'CUSA01290': {
        'name': 'Odin Sphere Leifthrasir',
        'region': 'Japan',
        'content_id': 'JP0005-CUSA01290_00-ODINSPHERELE0000',
        'versions': {
            '1.00': {
                'layers': ({'app_ver': '01.00', 'sfo_version': '01.00', 'update': False},),
                'eboot_sha256': 'd0add7e35bc5f58d5c9017fdce0878006c32f944fa8eb3d7ce62671257f5bcc7',
                'folder': 'CUSA01290-1.00', 'extra_files': (),
                'data_files': ('Odin2_BOOT.CPK', 'Odin2_OR.CPK', 'Odin2_ORHD.CPK', 'Odin2_RE.CPK'),
            },
            # The 1.02 patch is laid over the base package; it replaces eboot.bin and adds the patch archives.
            '1.02': {
                'layers': ({'app_ver': '01.00', 'sfo_version': '01.00', 'update': False},
                           {'app_ver': '01.02', 'sfo_version': '01.00', 'update': True}),
                'eboot_sha256': 'db96b839fe31adba4b3eb3547202f761d34881325c2fea650c4c020eded5d071',
                'folder': 'CUSA01290-1.02', 'extra_files': ('sce_discmap_patch.plt',),
                'data_files': ('Odin2_BOOT.CPK', 'Odin2_OR.CPK', 'Odin2_ORHD.CPK', 'Odin2_RE.CPK',
                               'Odin2_PATCH1_OR.CPK', 'Odin2_PATCH1_RE.CPK'),
            },
        },
        'entry': 'eboot.bin',
        'extra_executables': (),
        'modules': ('libc.prx', 'libSceFios2.prx'),
        'core_patterns': ('eboot.bin', 'sce_module/*', 'sce_sys/*', '*.plt'),
    },
    'CUSA05262': {
        'name': 'Odin Sphere Leifthrasir',
        'region': 'Europe',
        'content_id': 'EP1063-CUSA05262_00-ODINSPHERELE0000',
        'versions': {
            '1.00': {
                'layers': ({'app_ver': '01.00', 'sfo_version': '01.00', 'update': False},),
                'eboot_sha256': '3e6b1741e8a404d0f8fbe10bce93c6887974566f5be09ef1bddc22a00ba29feb',
                'folder': 'CUSA05262-1.00', 'extra_files': (),
                'data_files': ('Odin2_BT_DE.CPK', 'Odin2_BT_ES.CPK', 'Odin2_BT_FR.CPK', 'Odin2_BT_IT.CPK',
                               'Odin2_BT_UK.CPK', 'Odin2_OR_DE.CPK', 'Odin2_OR_ES.CPK', 'Odin2_OR_FR.CPK',
                               'Odin2_OR_IT.CPK', 'Odin2_OR_UK.CPK', 'Odin2_RE_DE.CPK', 'Odin2_RE_ES.CPK',
                               'Odin2_RE_FR.CPK', 'Odin2_RE_IT.CPK', 'Odin2_RE_UK.CPK'),
            },
            # The 1.01 patch is laid over the base package; it replaces eboot.bin, adds the patch archives and
            # completes the non-English menu and subtitle data.
            '1.01': {
                'layers': ({'app_ver': '01.00', 'sfo_version': '01.00', 'update': False},
                           {'app_ver': '01.01', 'sfo_version': '01.00', 'update': True}),
                'eboot_sha256': '572bf21e57febee415d739cded5516def9ca81e867ad3184c989367248edcd7e',
                'folder': 'CUSA05262-1.01', 'extra_files': ('sce_discmap_patch.plt',),
                'data_files': ('Odin2_BT_DE.CPK', 'Odin2_BT_ES.CPK', 'Odin2_BT_FR.CPK', 'Odin2_BT_IT.CPK',
                               'Odin2_BT_UK.CPK', 'Odin2_OR_DE.CPK', 'Odin2_OR_ES.CPK', 'Odin2_OR_FR.CPK',
                               'Odin2_OR_IT.CPK', 'Odin2_OR_UK.CPK', 'Odin2_RE_DE.CPK', 'Odin2_RE_ES.CPK',
                               'Odin2_RE_FR.CPK', 'Odin2_RE_IT.CPK', 'Odin2_RE_UK.CPK', 'Odin2_EU_PATCH1_OR_DE.CPK',
                               'Odin2_EU_PATCH1_OR_ES.CPK', 'Odin2_EU_PATCH1_OR_FR.CPK', 'Odin2_EU_PATCH1_OR_IT.CPK',
                               'Odin2_EU_PATCH1_OR_UK.CPK', 'Odin2_EU_PATCH1_RE_DE.CPK', 'Odin2_EU_PATCH1_RE_ES.CPK',
                               'Odin2_EU_PATCH1_RE_FR.CPK', 'Odin2_EU_PATCH1_RE_IT.CPK', 'Odin2_EU_PATCH1_RE_UK.CPK'),
            },
        },
        'entry': 'eboot.bin',
        'extra_executables': (),
        'modules': ('libc.prx', 'libSceFios2.prx'),
        'core_patterns': ('eboot.bin', 'sce_module/*', 'sce_sys/*', '*.plt'),
    },
    'CUSA03873': {
        'name': 'Odin Sphere Leifthrasir',
        'region': 'HK',
        'content_id': 'HP0177-CUSA03873_00-ODINSPHERELE0000',
        'versions': {
            '1.00': {
                'layers': ({'app_ver': '01.00', 'sfo_version': '01.00', 'update': False},),
                'eboot_sha256': '8c483dc2c06d958ccd84bdbf726df6d23e31592c7c1bb39eb5425718b5bc1eb3',
                'folder': 'CUSA03873-1.00', 'extra_files': (),
                'data_files': ('Odin2_BT_CN.CPK', 'Odin2_OR_CN.CPK', 'Odin2_RE_CN.CPK'),
            },
            '1.00-Asia': {
                'version': '1.00', 'region': 'Asia',
                'layers': ({'app_ver': '01.00', 'sfo_version': '01.00', 'update': False},),
                'eboot_sha256': '85ea2e4396b94a7b228d11e35a267f401cb2957bd116b3a5d23f5b3cb15a2fb2',
                'folder': 'CUSA03873-1.00-Asia', 'extra_files': (),
                'data_files': ('Odin2_BT_CN.CPK', 'Odin2_OR_CN.CPK', 'Odin2_RE_CN.CPK',
                               'Odin2_BT_KO.CPK', 'Odin2_OR_KO.CPK', 'Odin2_RE_KO.CPK'),
            },
        },
        'entry': 'eboot.bin',
        'extra_executables': (),
        'modules': ('libc.prx', 'libSceFios2.prx'),
        'core_patterns': ('eboot.bin', 'sce_module/*', 'sce_sys/*', '*.plt'),
    },
}


def title_id(game):
    """TITLE_ID of a game folder, or None."""
    try:
        return parse_sfo((Path(game) / 'sce_sys' / 'param.sfo').read_bytes()).get('TITLE_ID')
    except (OSError, ValueError, KeyError):
        return None


def profile(game_or_title):
    """The profile for a TITLE_ID or a game folder, or None for an unknown title."""
    key = str(game_or_title)
    if key not in PROFILES:
        key = title_id(game_or_title) or ''
    return PROFILES.get(key)


def finished_layer(build):
    """The layer whose param.sfo the installed game has."""
    return build['layers'][-1]


def profile_version(key, build):
    """The version a build's preparation marker records: its own key unless it shares another's."""
    return build.get('version', key)


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def _one(versions, keys, eboot_sha256):
    """The only key, or among keys that share a package identity the one whose eboot.bin has eboot_sha256."""
    if len(keys) > 1:
        keys = [key for key in keys if versions[key]['eboot_sha256'] == eboot_sha256]
    return keys[0] if len(keys) == 1 else None


def layer_identity(title, layer):
    """(TITLE_ID, APP_VER, VERSION, is an update) of the package a layer is."""
    return title, layer['app_ver'], layer['sfo_version'], layer['update']


def package_identity(info):
    """The identity of a package, as layer_identity() names it, or None when it is not a package of a supported title."""
    sfo = info['param_sfo']
    found = PROFILES.get(sfo.get('TITLE_ID', ''))
    if not found or sfo.get('CONTENT_ID') != found['content_id']:
        return None
    return sfo['TITLE_ID'], sfo.get('APP_VER'), sfo.get('VERSION'), sfo.get('CATEGORY') == 'gp'


def finishing_versions(title, identity):
    """The keys of the builds of `title` whose finished game is the package with this identity."""
    return [key for key, build in PROFILES[title]['versions'].items()
            if layer_identity(title, finished_layer(build)) == identity]


def finishing_version(title, identity, eboot_sha256=None):
    """The key of the build of `title` whose finished game is the package with this identity, or None.

    Builds that share the identity need the package's eboot.bin SHA-256 to be told apart.
    """
    return _one(PROFILES[title]['versions'], finishing_versions(title, identity), eboot_sha256)


def _finished_identity(build, sfo):
    finished = finished_layer(build)
    versions = {finished['sfo_version'], build.get('merged_sfo_version')} - {None}
    return sfo.get('APP_VER') == finished['app_ver'] and sfo.get('VERSION') in versions


def version_of(game):
    """The key of the build whose finished param.sfo (and, when shared, eboot.bin) the game folder has, or None."""
    try:
        sfo = parse_sfo((Path(game) / 'sce_sys' / 'param.sfo').read_bytes())
    except (OSError, ValueError):
        return None
    found = PROFILES.get(sfo.get('TITLE_ID', ''))
    if not found:
        return None
    keys = [key for key, build in found['versions'].items() if _finished_identity(build, sfo)]
    sha = None
    if len(keys) > 1:
        try:
            sha = file_sha256(Path(game) / 'eboot.bin')
        except OSError:
            return None
    return _one(found['versions'], keys, sha)


def package_layers(info):
    """(TITLE_ID, build key, layer index) of every build whose finished game the package is; several when they
    share its identity. `info` is what scripts/pkg_info.py reads. The index is the package's place in the layers."""
    identity = package_identity(info)
    if identity is None:
        return []
    title = identity[0]
    return [(title, key, len(PROFILES[title]['versions'][key]['layers']) - 1)
            for key in finishing_versions(title, identity)]


def package_layer(info, eboot_sha256=None):
    """The one entry of package_layers(info), chosen by the package's eboot.bin SHA-256 when several; else None."""
    layers = package_layers(info)
    if not layers:
        return None
    key = _one(PROFILES[layers[0][0]]['versions'], [key for _, key, _ in layers], eboot_sha256)
    return next((layer for layer in layers if layer[1] == key), None)
