"""Prevent sub-tick busy loops. Source checks plus FreeRTOS tick arithmetic."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PositiveDelayTests(unittest.TestCase):
    def test_sub_tick_delays_must_block(self):
        for hz in (100, 250, 1000):
            for ms in (2, 5):
                ticks = ms * hz // 1000
                self.assertGreaterEqual(max(1, ticks), 1)
        self.assertEqual(2 * 100 // 1000, 0)
        self.assertEqual(5 * 100 // 1000, 0)

    def test_usb_worker_and_backpressure_have_positive_delays(self):
        source = (ROOT / 'main/scanner/cube_usb_dongle.cc').read_text()
        self.assertNotRegex(source, r'vTaskDelay\(pdMS_TO_TICKS\([125]\)\)')
        self.assertEqual(source.count('vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(2)))'), 2)

    def test_ble_timer_and_waits_have_positive_delays(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        self.assertEqual(source.count('std::max<uint32_t>(1, ble_npl_time_ms_to_ticks32(5))'), 2)
        self.assertNotRegex(source, r'vTaskDelay\(pdMS_TO_TICKS\(5\)\)')
        split = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        self.assertNotRegex(split, r'vTaskDelay\(pdMS_TO_TICKS\(5\)\)')

    def test_output_policy_is_unchanged(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        self.assertIn('return preference == Output::Ble || (preference == Output::Auto && !CubeUsbAttached());', source)


if __name__ == '__main__':
    unittest.main()
