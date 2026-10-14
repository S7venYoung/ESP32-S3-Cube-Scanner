from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]

class DualDongleIntegrationTest(unittest.TestCase):
    def test_one_host_and_gated_server(self):
        ble = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        self.assertNotIn('nimble_port_init()', ble)
        self.assertIn('CubeSplitIsKeyboard', ble)
        self.assertNotIn('ble_store_util_delete_oldest_peer', ble)
        self.assertNotIn('ble_store_util_delete_all', ble)
        self.assertIn('ble_store_util_delete_peer(&profiles[slot].identity)', ble)
        self.assertIn('desc.sec_state.encrypted', ble)
        self.assertIn('desc.sec_state.bonded', ble)
        self.assertIn('physical_neutral', ble)
        self.assertIn('flight_revision != revision', ble)

    def test_build_checks_roles_and_bonds(self):
        script = (ROOT / 'scripts/build-cube-dual-candidate.sh').read_text()
        for flag in ('CONFIG_CUBE_BLE_DONGLE_OUTPUT=y', 'CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y', 'CONFIG_BT_NIMBLE_ROLE_CENTRAL=y', 'CONFIG_BT_NIMBLE_MAX_BONDS=7'):
            self.assertIn(flag, script)
        cmake = (ROOT / 'main/CMakeLists.txt').read_text()
        self.assertIn('if(CONFIG_CUBE_BLE_DONGLE_OUTPUT)', cmake)
        self.assertIn('scanner/cube_ble_dongle.cc', cmake)

    def test_async_commands_are_not_success_claims(self):
        ble = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        self.assertIn('CUBE-DUAL-CANDIDATE QUEUED', ble)
        self.assertIn('ERR HOST-BUSY', ble)
        self.assertIn('SWITCHING=', ble)
        self.assertIn('RESULT=', ble)
