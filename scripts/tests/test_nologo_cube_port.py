import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / 'main/boards/nologo/xingzhi-cube-1.54tft-wifi'

class NologoCubePortTest(unittest.TestCase):
    def test_stock_and_custom_variants(self):
        config = json.loads((BOARD / 'config.json').read_text())
        stock, custom = config['builds']
        self.assertEqual(stock['sdkconfig_append'], [])
        self.assertEqual(custom['name'], 'xingzhi-cube-1.54tft-wifi-codex')
        self.assertIn('CONFIG_ZMK_SCANNER_MODE=y', custom['sdkconfig_append'])
        self.assertIn('CONFIG_ESP_CONSOLE_UART_DEFAULT=y', custom['sdkconfig_append'])
        self.assertIn('CONFIG_BT_NIMBLE_ENABLED=y', custom['sdkconfig_append'])

    def test_board_and_power_mapping(self):
        source = (BOARD / 'xingzhi-cube-1.54tft-wifi.cc').read_text()
        self.assertEqual(source.count('DECLARE_BOARD('), 1)
        self.assertIn('PowerManager(GPIO_NUM_38)', source)
        self.assertIn('rtc_gpio_init(GPIO_NUM_21)', source)
        self.assertIn('NextDashboardTheme()', source)
        self.assertIn('new PowerSaveTimer(-1, -1, -1)', source)
        self.assertIn('StartCodexSync()', source)
        self.assertIn('StartZmkScanner()', source)
        power = (BOARD / 'power_manager.h').read_text()
        self.assertIn('ADC_UNIT_2', power)
        self.assertIn('ADC_CHANNEL_6', power)
        self.assertLess(power.index('adc_oneshot_config_channel'), power.index('esp_timer_start_periodic'))
        self.assertNotIn('ESP_ERROR_CHECK(adc_oneshot_read', power)

    def test_pins_are_official(self):
        pins = (BOARD / 'config.h').read_text()
        expected = {'BOOT_BUTTON_GPIO':0, 'VOLUME_UP_BUTTON_GPIO':40,
                    'VOLUME_DOWN_BUTTON_GPIO':39, 'DISPLAY_SDA':10,
                    'DISPLAY_SCL':9, 'DISPLAY_DC':8, 'DISPLAY_CS':14,
                    'DISPLAY_RES':18, 'DISPLAY_BACKLIGHT_PIN':13,
                    'AUDIO_I2S_MIC_GPIO_WS':4, 'AUDIO_I2S_MIC_GPIO_SCK':5,
                    'AUDIO_I2S_MIC_GPIO_DIN':6, 'AUDIO_I2S_SPK_GPIO_DOUT':7,
                    'AUDIO_I2S_SPK_GPIO_BCLK':15, 'AUDIO_I2S_SPK_GPIO_LRCK':16}
        for name, pin in expected.items():
            self.assertRegex(pins, rf'#define\s+{name}\s+GPIO_NUM_{pin}\b')

if __name__ == '__main__':
    unittest.main()
