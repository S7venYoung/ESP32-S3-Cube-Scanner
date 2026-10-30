from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ConnectedAdvertisingTests(unittest.TestCase):
    def test_production_stop_helper_with_controller_stubs(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        helper = source[source.index('void StopConnectedAdvertising() {'):
                        source.index('void Advertise() {')]
        program = r'''
#include <cassert>
#include <cstdint>
constexpr int BLE_HS_CONN_HANDLE_NONE=0xffff, BLE_HS_EALREADY=2;
int connection=BLE_HS_CONN_HANDLE_NONE, calls=0, stop_result=0;
int64_t now=1000000, advertisement_stop_next=0;
bool active=true,scanner_advertising=false;
struct { int adv_stop_rc=-1; } host_diagnostics;
int64_t esp_timer_get_time(){return now;}
bool ble_gap_adv_active(){return active;}
int ble_gap_adv_stop(){++calls;if(!stop_result)active=false;return stop_result;}
#define ESP_LOGW(...) ((void)0)
''' + helper + r'''
int main(){
 // Initial pairing must remain discoverable without a computer connection.
 StopConnectedAdvertising();assert(active&&calls==0);
 connection=7;
 scanner_advertising=true;
 StopConnectedAdvertising();assert(active&&calls==0);
 scanner_advertising=false;
 StopConnectedAdvertising();assert(!active&&calls==1&&host_diagnostics.adv_stop_rc==0);
 // No recurring stop calls once the advertisement is gone.
 for(int i=0;i<100;++i)StopConnectedAdvertising();
 assert(calls==1);
 // Controller failure retries, without busy-looping on the 5 ms host timer.
 active=true;stop_result=12;now+=250000;
 StopConnectedAdvertising();assert(active&&calls==2&&host_diagnostics.adv_stop_rc==12);
 now+=5000;StopConnectedAdvertising();assert(calls==2);
 now+=245000;stop_result=0;StopConnectedAdvertising();assert(!active&&calls==3);
 // A disconnect must not cancel the next pairing/reconnection advertisement.
 connection=BLE_HS_CONN_HANDLE_NONE;active=true;now+=250000;
 StopConnectedAdvertising();assert(active&&calls==3);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'connected_adv.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'connected_adv')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_stop_on_connect_and_timer_before_advertising_guard(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        advertise = source[source.index('void Advertise() {'):source.index('void SampleRadioPools() {')]
        self.assertLess(advertise.index('StopConnectedAdvertising();'),
                        advertise.index('connection != BLE_HS_CONN_HANDLE_NONE'))
        gap = source[source.index('int Gap(ble_gap_event* event'):source.index('enum Field :')]
        adopted = gap[gap.index('connection = event->connect.conn_handle;'):]
        self.assertLess(adopted.index('StopConnectedAdvertising();'),
                        adopted.index('host_diagnostics.NewConnection();'))
        tick = source[source.index('void CubeDualTick()'):source.index('void CubeDualSubmit(')]
        self.assertIn('Advertise();', tick)
        disconnect = gap[gap.index('} else if (event->type == BLE_GAP_EVENT_DISCONNECT'):]
        self.assertLess(disconnect.index('connection = BLE_HS_CONN_HANDLE_NONE;'),
                        disconnect.index('Advertise();'))
        diagnostics = (ROOT / 'experimental/cube_dongle/host_diagnostics.h').read_text()
        self.assertIn('ADV-STOP-RC=', diagnostics)


if __name__ == '__main__':
    unittest.main()

