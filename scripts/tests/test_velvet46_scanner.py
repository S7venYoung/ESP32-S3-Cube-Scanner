from pathlib import Path
import json
import unittest

ROOT = Path(__file__).resolve().parents[2]

class Velvet46ScannerTest(unittest.TestCase):
    def test_development_pushes_do_not_consume_actions(self):
        workflow = (ROOT / '.github/workflows/build-nologo-cube-codex.yml').read_text()
        self.assertIn('workflow_dispatch:', workflow)
        self.assertNotIn('  push:', workflow)

    def test_target_config(self):
        config = json.loads((ROOT / 'main/boards/nologo/xingzhi-cube-1.54tft-wifi/config.json').read_text())
        variant = next(v for v in config['builds'] if v['name'] == 'xingzhi-cube-1.54tft-wifi-codex')
        self.assertIn('CONFIG_ZMK_SCANNER_NAME_FILTER="Velvet46"', variant['sdkconfig_append'])
        self.assertIn('CONFIG_BT_NIMBLE_ROLE_CENTRAL=n', variant['sdkconfig_append'])

    def test_name_resolution_precedes_telemetry(self):
        source = (ROOT / 'main/scanner/zmk_scanner.cc').read_text()
        callback = source[source.index('int OnGapEvent'):source.index('void BeginDiscovery() {')]
        self.assertLess(callback.index('names.Resolve('), callback.index('ReceiveModifiers('))
        self.assertIn('scanner_policy::HasStatus(', callback)

    def test_bounded_retry_and_single_start(self):
        source = (ROOT / 'main/scanner/zmk_scanner.cc').read_text()
        self.assertIn('startup_requested.exchange(true)', source)
        self.assertIn('attempt < 8', source)
        self.assertIn('vTaskDelay(pdMS_TO_TICKS(5000))', source)
        self.assertIn('scan_retry.FailureDelayMs()', source)
        self.assertIn('ble_npl_callout_stop(&scan_callout)', source)
        self.assertIn('free >= 64 * 1024 && largest >= 16 * 1024', source)

    def test_idle_broadcast_is_not_expired_after_ten_seconds(self):
        source = (ROOT / 'main/scanner/zmk_scanner.cc').read_text()
        self.assertIn('scanner_policy::kStatusLeaseUs', source)
        self.assertNotIn('keyboard_seen_us > 10000000', source)

    def test_split_wpm_reaches_distinct_fighters(self):
        source = (ROOT / 'main/scanner/arcade_theme.cc').read_text()
        self.assertRegex(source, r'const int speeds\[\]\s*=\s*\{f.left_wpm,\s*f.right_wpm\}')
        self.assertIn('speeds[i]', source)
        scan = (ROOT / 'main/scanner/zmk_scanner.cc').read_text()
        self.assertIn('FightFrameNewer(fight_data, fight, live)', scan)

    def test_radio_gap_keeps_presence_not_input(self):
        source = (ROOT / 'main/scanner/zmk_scanner.cc').read_text()
        self.assertIn('FightSnapshotAt(fight_data, fight_seen_us,', source)
        self.assertIn('modifier_timeline.Get(esp_timer_get_time())', source)
        self.assertIn('GetScannerModifiers()', source)
        self.assertNotIn('60000000', source)
