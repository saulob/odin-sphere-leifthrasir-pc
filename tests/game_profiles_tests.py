"""scripts/game_profiles.py: builds that share a package identity, told apart by eboot.bin (issue #19).

Synthetic param.sfo files in a temporary folder; no game data. Usage: python -I tests/game_profiles_tests.py
"""
from pathlib import Path
import hashlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'scripts'))
import game_profiles  # noqa: E402

HK_SHA = '8c483dc2c06d958ccd84bdbf726df6d23e31592c7c1bb39eb5425718b5bc1eb3'
ASIA_SHA = '85ea2e4396b94a7b228d11e35a267f401cb2957bd116b3a5d23f5b3cb15a2fb2'
HK_SFO = {'TITLE_ID': 'CUSA03873', 'CONTENT_ID': 'HP0177-CUSA03873_00-ODINSPHERELE0000', 'CATEGORY': 'gd',
          'APP_VER': '01.00', 'VERSION': '01.00'}


def sfo_bytes(values):
    keys, data, entries = b'', b'', []
    for key, value in values.items():
        raw = value.encode() + b'\0'
        entries.append(struct.pack('<HHIII', len(keys), 0x0204, len(raw), len(raw), len(data)))
        keys += key.encode() + b'\0'
        data += raw
    key_table = 20 + 16 * len(entries)
    keys += b'\0' * (-len(keys) % 4)
    header = b'\0PSF' + struct.pack('<IIII', 0x101, key_table, key_table + len(keys), len(entries))
    return header + b''.join(entries) + keys + data


class SamePackageReleases(unittest.TestCase):
    def setUp(self):
        self.versions = game_profiles.PROFILES['CUSA03873']['versions']
        self.identity = ('CUSA03873', '01.00', '01.00', False)

    def game(self, eboot):
        root = Path(self.enterContext(tempfile.TemporaryDirectory()))
        (root / 'sce_sys').mkdir()
        (root / 'sce_sys' / 'param.sfo').write_bytes(sfo_bytes(HK_SFO))
        (root / 'eboot.bin').write_bytes(eboot)
        return root

    def test_both_releases_and_their_folders(self):
        hk, asia = self.versions['1.00'], self.versions['1.00-Asia']
        self.assertEqual(hk['eboot_sha256'], HK_SHA)
        self.assertEqual(asia['eboot_sha256'], ASIA_SHA)
        self.assertEqual((hk['folder'], asia['folder']), ('CUSA03873-1.00', 'CUSA03873-1.00-Asia'))
        self.assertEqual(hk['data_files'], ('Odin2_BT_CN.CPK', 'Odin2_OR_CN.CPK', 'Odin2_RE_CN.CPK'))
        self.assertEqual(asia['data_files'], ('Odin2_BT_CN.CPK', 'Odin2_OR_CN.CPK', 'Odin2_RE_CN.CPK',
                                              'Odin2_BT_KO.CPK', 'Odin2_OR_KO.CPK', 'Odin2_RE_KO.CPK'))
        self.assertEqual(hk['layers'], asia['layers'])
        self.assertEqual(game_profiles.profile_version('1.00', hk), '1.00')
        self.assertEqual(game_profiles.profile_version('1.00-Asia', asia), '1.00')
        self.assertEqual(asia['region'], 'Asia')
        folders = [b['folder'] for p in game_profiles.PROFILES.values() for b in p['versions'].values()]
        self.assertEqual(len(folders), len(set(folders)))

    def test_eboot_decides(self):
        self.assertEqual(game_profiles.finishing_versions('CUSA03873', self.identity), ['1.00', '1.00-Asia'])
        self.assertIsNone(game_profiles.finishing_version('CUSA03873', self.identity))
        self.assertEqual(game_profiles.finishing_version('CUSA03873', self.identity, HK_SHA), '1.00')
        self.assertEqual(game_profiles.finishing_version('CUSA03873', self.identity, ASIA_SHA), '1.00-Asia')
        self.assertIsNone(game_profiles.finishing_version('CUSA03873', self.identity, '0' * 64))

    def test_package_layers(self):
        info = {'param_sfo': dict(HK_SFO)}
        self.assertEqual(game_profiles.package_layers(info), [('CUSA03873', '1.00', 0), ('CUSA03873', '1.00-Asia', 0)])
        self.assertIsNone(game_profiles.package_layer(info))
        self.assertEqual(game_profiles.package_layer(info, ASIA_SHA), ('CUSA03873', '1.00-Asia', 0))
        self.assertEqual(game_profiles.package_layer(info, HK_SHA), ('CUSA03873', '1.00', 0))
        self.assertIsNone(game_profiles.package_layer(info, '0' * 64))

    def test_version_of_reads_the_eboot(self):
        eboot = b'synthetic eboot'
        sha = hashlib.sha256(eboot).hexdigest()
        original = self.versions['1.00-Asia']['eboot_sha256']
        try:
            self.versions['1.00-Asia']['eboot_sha256'] = sha
            self.assertEqual(game_profiles.version_of(self.game(eboot)), '1.00-Asia')
        finally:
            self.versions['1.00-Asia']['eboot_sha256'] = original
        self.assertIsNone(game_profiles.version_of(self.game(b'another asian eboot')))

    def test_other_titles_unchanged(self):
        usa = {'TITLE_ID': 'CUSA05083', 'CONTENT_ID': 'UP0177-CUSA05083_00-ODINSPHERELE0000', 'CATEGORY': 'gd',
               'APP_VER': '01.00', 'VERSION': '01.01'}
        self.assertEqual(game_profiles.package_layer({'param_sfo': usa}), ('CUSA05083', '1.00', 0))
        update = dict(usa, CATEGORY='gp', APP_VER='01.01', VERSION='01.00')
        self.assertEqual(game_profiles.package_layer({'param_sfo': update}), ('CUSA05083', '1.01', 1))
        europe = ('CUSA05262', '01.01', '01.00', True)
        self.assertEqual(game_profiles.finishing_version('CUSA05262', europe), '1.01')


if __name__ == '__main__':
    unittest.main(verbosity=2)
