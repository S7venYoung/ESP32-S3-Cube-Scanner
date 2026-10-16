from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ScannerResponsiveness(unittest.TestCase):
    def test_ble_does_not_queue_or_draw(self):
        code = (ROOT / 'main/scanner/zmk_scanner.cc').read_text()
        publish = code.split('void PublishModifiers()',1)[1].split('void ReceiveModifiers',1)[0]
        self.assertIn('priority_update_pending = true', publish)
        self.assertNotIn('Schedule(', code)
        self.assertNotIn('lv_obj_', code)
        self.assertIn('priority_update_pending.exchange(false)', code)
        self.assertIn('scanner_policy::kScanRestartMs', code)
        self.assertIn('scanner_policy::kScanSliceMs', code)

    def test_priority_fields_not_only_modifiers(self):
        code = (ROOT / 'main/scanner/zmk_scanner.cc').read_text()
        for field in ('layer', 'profile', 'usb', 'ble'):
            self.assertIn('keyboard_data.'+field, code)
        self.assertIn('|| state_changed', code)

    def test_fast_timer_and_voice_thermal_budget(self):
        code = (ROOT / 'main/scanner/codex_scanner_display.cc').read_text()
        self.assertIn('PollKeyboardPriority()', code)
        self.assertIn('50, this', code)
        self.assertIn('LV_EVENT_DELETE, input_timer', code)
        poll = code.split('void CodexScannerDisplay::PollKeyboardPriority()',1)[1].split('void CodexScannerDisplay::UpdateKeyboardThemes()',1)[0]
        self.assertNotIn('DisplayLockGuard', poll) # LVGL already owns the lock.
        self.assertNotIn('ArcadeRefresh', poll)
        self.assertIn('TakeScannerPriorityUpdate()', poll)
        self.assertIn('SetScannerVoiceActive(',code)
        self.assertIn('temperature >= 65',code)
        self.assertIn('temperature <= 60',code)


if __name__ == '__main__':
    unittest.main()
