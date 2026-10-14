from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

class DownloadModeTest(unittest.TestCase):
    def test_long_boot_is_gated_and_keeps_wifi_fallback(self):
        board = (ROOT / 'main/boards/nologo/xingzhi-cube-1.54tft-wifi/xingzhi-cube-1.54tft-wifi.cc').read_text()
        block = board[board.index('boot_button_.OnLongPress'):board.index('volume_up_button_.OnClick')]
        self.assertIn('CONFIG_CUBE_USB_DONGLE_OUTPUT', block)
        self.assertIn('CubeUsbEnterDownload()', block)
        self.assertIn('EnterWifiConfigMode()', block)
        self.assertIn('Schedule', block)

    def test_restart_is_deferred_without_flash_or_nvs_writes(self):
        usb = (ROOT / 'main/scanner/cube_usb_dongle.cc').read_text()
        self.assertIn('esp_register_shutdown_handler(DownloadShutdown)', usb)
        self.assertIn('RTC_CNTL_FORCE_DOWNLOAD_BOOT', usb)
        self.assertIn('tud_disconnect()', usb)
        self.assertIn('ReleaseLocked()', usb)
        self.assertNotIn('nvs_erase', usb)
        self.assertNotIn('esp_efuse_write', usb)
