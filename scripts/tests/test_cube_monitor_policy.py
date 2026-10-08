"""Source/config regression checks; actual brightness requires a device test."""
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "main/boards/zhengchen/1.54tft-wifi"


class CubeMonitorPolicyTest(unittest.TestCase):
    def test_monitor_disables_both_idle_timeouts(self):
        source = (BOARD / "zhengchen-1.54tft-wifi.cc").read_text()
        policy = re.search(
            r"#if CONFIG_ZMK_SCANNER_MODE\s*"
            r"(?P<monitor>(?:(?!#else).)*PowerSaveTimer\(-1, -1, -1\);)"
            r"\s*#else\s*(?P<stock>.*?)#endif",
            source,
            re.S,
        )
        self.assertIsNotNone(policy)
        self.assertIn("PowerSaveTimer(-1, 60, 300)", policy.group("stock"))

    def test_codex_variant_selects_monitor_policy(self):
        config = json.loads((BOARD / "config.json").read_text())
        builds = {build["name"]: build["sdkconfig_append"] for build in config["builds"]}
        self.assertIn("CONFIG_ZMK_SCANNER_MODE=y", builds["zhengchen-1.54tft-wifi-codex"])
        self.assertNotIn("CONFIG_ZMK_SCANNER_MODE=y", builds["zhengchen-1.54tft-wifi"])

    def test_minus_one_is_disabled_in_shared_timer(self):
        source = (ROOT / "main/boards/common/power_save_timer.cc").read_text()
        self.assertIn("seconds_to_sleep_ != -1 && ticks_ >= seconds_to_sleep_", source)
        self.assertIn("seconds_to_shutdown_ != -1 && ticks_ >= seconds_to_shutdown_", source)


if __name__ == "__main__":
    unittest.main()
