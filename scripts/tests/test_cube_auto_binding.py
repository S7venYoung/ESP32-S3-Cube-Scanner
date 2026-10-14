import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

class AutoBindingIntegrationTest(unittest.TestCase):
    def test_feature_only_enabled_in_dual_candidate(self):
        config = json.loads((ROOT / 'main/boards/nologo/xingzhi-cube-1.54tft-wifi/config.json').read_text())
        variants = config['builds']
        enabled = [v['name'] for v in variants if 'CONFIG_CUBE_SPLIT_AUTO_BIND=y' in v['sdkconfig_append']]
        self.assertEqual(enabled, ['xingzhi-cube-1.54tft-wifi-dual-dongle-candidate'])

    def test_verify_before_save_and_release_before_publish(self):
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        save = source[source.index('bool SaveAutoBinding'):source.index('int OnRead')]
        for check in ('identity_verified', 'Encrypted(peer)', 'ours.sc', 'theirs.sc', 'binding_mutex', 'ESP_ERR_NVS_NOT_FOUND'):
            self.assertIn(check, save)
        read = source[source.index('int OnRead'):source.index('int OnSubscribed')]
        self.assertLess(read.index('SaveAutoBinding'), read.index('CubeDualRequireNeutral'))
        self.assertLess(read.index('CubeDualRequireNeutral'), read.index('input.Apply'))

    def test_known_directed_advertisements_do_not_require_uuid(self):
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        gap = source[source.index('int OnGap(ble_gap_event* event, void* arg) {'):]
        self.assertLess(gap.index('Connect(peer)'), gap.index('cube_dongle::AutoSide'))
        self.assertNotIn('AdvertisesSplit', gap)
