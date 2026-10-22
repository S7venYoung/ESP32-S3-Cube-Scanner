from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FightLayoutTests(unittest.TestCase):
    def assertWired(self, source, fragment):
        self.assertIn(''.join(fragment.split()), ''.join(source.split()))

    def test_mirrored_quota_and_reserved_clock_space(self):
        source = (ROOT / 'main/scanner/arcade_theme.cc').read_text()
        self.assertWired(source, 'const int x=i==0?6:142;')
        self.assertWired(source, 'frame=Box(s->root,x,27,92,11')
        self.assertWired(source, 'i==0?8:177,39,57')
        self.assertWired(source, 'i==0?65:142,39,34')
        self.assertWired(source, '"--:--",99,26,42')
        self.assertLessEqual(6 + 92, 99)
        self.assertLessEqual(99 + 42, 142)
        self.assertLessEqual(177 + 57, 240)
        self.assertWired(source, 'FormatCodexReset(c.metrics,c.online')

    def test_staged_motion_uses_strike_clock_and_mirrored_offset(self):
        source = (ROOT / 'main/scanner/arcade_theme.cc').read_text()
        self.assertWired(source, 'm.strike_age/3')
        self.assertWired(source, '(i==0?m.x:-m.x),m.y')
        self.assertWired(source, '},40,s)')

    def test_boot_restores_existing_power_hold_after_preloading_high(self):
        path = 'main/boards/nologo/xingzhi-cube-1.54tft-wifi/xingzhi-cube-1.54tft-wifi.cc'
        source = (ROOT / path).read_text()
        init = source.split('void InitializePowerSaveTimer()', 1)[1].split('#if', 1)[0]
        self.assertLess(init.index('rtc_gpio_set_level(GPIO_NUM_21, 1)'),
                        init.index('rtc_gpio_hold_dis(GPIO_NUM_21)'))


if __name__ == '__main__':
    unittest.main()
