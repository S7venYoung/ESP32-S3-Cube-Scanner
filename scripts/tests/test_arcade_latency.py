from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ArcadeLatency(unittest.TestCase):
    def test_shared_modifier_source(self):
        for path in ('arcade_theme.cc', 'white_theme.cc', 'codex_scanner_display.cc'):
            code = (ROOT / 'main/scanner' / path).read_text()
            self.assertIn('GetScannerModifiers()', code)
        code = (ROOT / 'main/scanner/zmk_scanner.cc').read_text()
        self.assertNotIn('if (!fight_active', code)
        self.assertIn('modifier_timeline.Observe(0, fight.modifiers, now)', code)
        self.assertIn('modifier_timeline.Observe(1, keyboard.modifiers, now)', code)
        self.assertNotIn('[mods]', code)
        self.assertNotIn('[fight]', code)

    def test_25fps_and_strike_relative_effects(self):
        code = (ROOT / 'main/scanner/arcade_theme.cc').read_text()
        self.assertRegex(code, r'\},\s*40,\s*s\)')
        self.assertIn('m.strike_age', code)
        self.assertIn('m.x', code)
        self.assertIn('m.y', code)


if __name__ == '__main__':
    unittest.main()
