from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ExtendedIntegrationTest(unittest.TestCase):
    def test_actual_dya_identifiers(self):
        source = (ROOT / 'main/scanner/cube_studio.cc').read_text()
        for identifier in ('cormoran_rip', 'cormoran_rsr', 'cormoran_custom_settings',
                           'cormoran__physical_layouts', 'cormoran__fast_keymap',
                           'cormoran__default_layer', 'cormoran__os_detection',
                           'zmk__input_stream'):
            self.assertIn('"' + identifier + '"', source)
        self.assertNotIn('"zmk__runtime_input_processor"', source)

    def test_pointer_transport_and_no_replay(self):
        usb = (ROOT / 'main/scanner/cube_usb_dongle.cc').read_text()
        ble = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        queue = (ROOT / 'experimental/cube_dongle/report_queue.h').read_text()
        self.assertIn('PointerBytes', usb)
        self.assertIn('PointerBytes', ble)
        self.assertIn('pointer_handle', ble)
        self.assertIn('physical_input.pointer.x = physical_input.pointer.y = 0', ble)
        self.assertIn('!frame.Motion()', queue)

    def test_native_split_paths_are_identity_guarded(self):
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        self.assertIn('CubeStudioPointerInput', source)
        self.assertIn('CubeStudioSensorInput', source)
        self.assertIn('CubeStudioRelayEvent', source)
        self.assertIn('peer.generation != job.generation', source)
        self.assertIn('Encrypted(peer)', source)
        self.assertIn('optional_pending', source)

    def test_unlock_and_session_scoped_stream(self):
        source = (ROOT / 'main/scanner/cube_studio.cc').read_text()
        self.assertIn('session.stream && session.unlocked', source)
        self.assertIn('session.epoch == message.epoch', source)
        self.assertIn('Input stream queue overflow', source)
        self.assertIn('initialized_for_notifications', source)

    def test_cdc_close_invalidates_studio_without_releasing_hid(self):
        usb = (ROOT / 'main/scanner/cube_usb_dongle.cc').read_text()
        callback = usb[usb.index('void OnCdcLineState'):usb.index('void ReleaseLocked')]
        self.assertIn('cdc_epoch', callback)
        self.assertNotIn('++link_epoch', callback)
        self.assertNotIn('ReleaseLocked()', callback)


if __name__ == '__main__':
    unittest.main()
