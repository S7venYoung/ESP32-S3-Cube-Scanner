import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class KeyboardThemeBindings(unittest.TestCase):
    def test_live_widgets(self):
        code = (ROOT / "main/scanner/codex_scanner_display.cc").read_text()
        for name in ("codex_layer_", "codex_wpm_", "codex_keyboard_transport_",
                     "codex_kb_battery_text_[i]", "mac_layer_", "mac_profile_",
                     "mac_ble_", "mac_kb_battery_text_[i]"):
            self.assertIn(name + " =", code)
            self.assertGreaterEqual(code.count(name), 2)
        self.assertIn("UpdateKeyboardThemes();", code)
        self.assertIn("KeyboardThemeSnapshot(kb, fight)", code)

    def test_modifier_lock_not_recursive(self):
        code = (ROOT / "main/scanner/codex_scanner_display.cc").read_text()
        refresh = code.split("void CodexScannerDisplay::UpdateKeyboardThemes()", 1)[1]
        refresh = refresh.split("void CodexScannerDisplay::UpdateMetrics()", 1)[0]
        self.assertIn("ApplyMacModifierState(GetScannerModifiers())", refresh)
        self.assertNotIn("SetMacModifierState(", refresh)


if __name__ == "__main__":
    unittest.main()
