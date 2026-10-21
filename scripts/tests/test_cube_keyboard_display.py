"""Source wiring checks; these do not replace LVGL/hardware validation."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class KeyboardDisplayWiring(unittest.TestCase):
    def setUp(self):
        self.source = (ROOT / "main/scanner/codex_scanner_display.cc").read_text()

    def test_periodic_metrics_refresh_updates_keyboard(self):
        body = self.source.split("void CodexScannerDisplay::UpdateMetrics()", 1)[1]
        body = body.split("void CodexScannerDisplay::UpdateKeyboard()", 1)[0]
        self.assertIn("UpdateKeyboard();", body)

    def test_labels_are_retained_instead_of_static_placeholders(self):
        for name in ("keyboard_layer_", "keyboard_wpm_", "keyboard_battery_[i]",
                     "mac_layer_", "mac_keyboard_battery_[i]", "mac_profile_", "mac_link_"):
            self.assertIn(name + " =", self.source)

    def test_reads_real_snapshot_and_preserves_unknowns(self):
        body = self.source.split("void CodexScannerDisplay::UpdateKeyboard()", 1)[1]
        self.assertIn("GetKeyboardStatus()", body)
        self.assertIn("kb.online && kb.layer[0]", body)
        self.assertIn("kb.online && kb.wpm >= 0", body)
        self.assertIn("level >= 0 && level <= 100", body)
        self.assertIn('"--%%"', body)

    def test_refresh_does_not_reenter_display_lock(self):
        body = self.source.split("void CodexScannerDisplay::UpdateKeyboard()", 1)[1]
        body = body.split("void CodexScannerDisplay::UpdateAssistantOverlay()", 1)[0]
        self.assertNotIn("DisplayLockGuard", body)
        self.assertNotIn("SetMacModifierState(", body)


if __name__ == "__main__":
    unittest.main()
