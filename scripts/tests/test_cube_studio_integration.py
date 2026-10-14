import json
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[2]

class StudioIntegrationTest(unittest.TestCase):
    def test_only_dual_variant_enables_studio(self):
        builds = json.loads((ROOT / 'main/boards/nologo/xingzhi-cube-1.54tft-wifi/config.json').read_text())['builds']
        self.assertEqual([b['name'] for b in builds if 'CONFIG_CUBE_STUDIO_RPC=y' in b['sdkconfig_append']], ['xingzhi-cube-1.54tft-wifi-dual-dongle-candidate'])
        self.assertIn('if(CONFIG_CUBE_STUDIO_RPC)', (ROOT / 'main/CMakeLists.txt').read_text())
    def test_binary_mux_preserves_text_and_uses_physical_unlock(self):
        sync = (ROOT / 'main/scanner/codex_sync.cc').read_text()
        self.assertIn('CubeStudioFeed', sync)
        self.assertIn('line == "PAIR"', sync)
        self.assertIn('CubeStudioPoll()', sync)
        board = (ROOT / 'main/boards/nologo/xingzhi-cube-1.54tft-wifi/xingzhi-cube-1.54tft-wifi.cc').read_text()
        self.assertIn('boot_button_.OnDoubleClick', board)
        self.assertIn('CubeStudioUnlockUsb()', board)
    def test_nvs_namespace_and_host_owned_map_refresh(self):
        source = (ROOT / 'main/scanner/cube_studio.cc').read_text()
        self.assertIn('nvs_commit(handle)', source)
        self.assertIn('"cube_studio"', source)
        self.assertNotIn('"cube_hosts"', source)
        self.assertNotIn('"cube_split"', source)
        split = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        publish = split[split.index('void PublishKeyboard()'):split.index('ble_npl_callout retry')]
        self.assertLess(publish.index('CubeStudioRefreshKeymap'), publish.index('keyboard.Apply'))

    def test_usb_session_epoch_guards_response_and_reconnect_lock(self):
        source = (ROOT / 'main/scanner/cube_studio.cc').read_text()
        usb = (ROOT / 'main/scanner/cube_usb_dongle.cc').read_text()
        self.assertIn('CubeUsbEpoch(), CubeBleStudioEpoch()', source)
        self.assertIn('session.epoch != epochs[i]', source)
        self.assertIn('CubeCdcWritePacket(bytes.data(), bytes.size(), sessions[transport].epoch)', source)
        self.assertIn('link_epoch.load() != expected_epoch', usb)
