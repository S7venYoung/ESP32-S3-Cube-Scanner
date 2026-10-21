from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ResetCauseTests(unittest.TestCase):
    def test_device_info_uses_same_bitmask_as_watchdog_log(self):
        source = (ROOT / 'main/scanner/cube_studio.cc').read_text()
        self.assertIn('wire::Number(hardware, 2, ResetBits());', source)
        self.assertNotIn('wire::Number(hardware, 2, esp_reset_reason());', source)
        self.assertIn('watchdog_log.Boot(ResetBits());', source)

    def test_task_watchdog_is_not_software_or_brownout(self):
        source = (ROOT / 'main/scanner/cube_studio.cc').read_text()
        block = source.split('uint32_t ResetBits()', 1)[1].split('wire::MacroPlayer', 1)[0]
        self.assertIn('case ESP_RST_TASK_WDT:', block)
        self.assertIn('return 1u << 4;', block)
        self.assertEqual((1 << 4) & ((1 << 1) | (1 << 2)), 0)


if __name__ == '__main__':
    unittest.main()
