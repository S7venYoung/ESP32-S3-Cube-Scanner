"""Run the real computer security pump, including normal in-progress SMP."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ComputerSecurityTests(unittest.TestCase):
    def test_production_security_pump_and_diagnostics(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        start = source.index('void PumpComputerSecurity(')
        production = source[start:source.index('void Pump() {', start)]
        shim = r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include "host_diagnostics.h"
constexpr int BLE_HS_CONN_HANDLE_NONE=65535,BLE_HS_EALREADY=2,
 BLE_HS_EBUSY=15,BLE_HS_EAGAIN=1,BLE_HS_ENOTCONN=7,BLE_ERR_REM_USER_CONN_TERM=19;
cube_dongle::HostDiagnostics host_diagnostics;
int connection=7,result=0,calls=0,terminations=0,last_result=0;
bool security_start_pending=true,security_pending=false,live=true,encrypted=false;
int64_t security_after=100000,security_deadline=30000000;
struct ble_gap_conn_desc {struct {bool encrypted=false;} sec_state;};
int ble_gap_conn_find(int handle,ble_gap_conn_desc* out){assert(handle==7);
 out->sec_state.encrypted=encrypted;return live?0:7;}
int ble_gap_security_initiate(int handle){assert(handle==7);++calls;return result;}
int ble_gap_terminate(int handle,int reason){assert(handle==7&&reason==19);++terminations;return 0;}
void Reset(){connection=7;result=calls=terminations=last_result=0;
 security_start_pending=live=true;security_pending=encrypted=false;security_after=100000;
 security_deadline=30000000;}
'''
        program = shim + production + r'''
int main(){
 Reset();PumpComputerSecurity(99999);assert(!calls);
 PumpComputerSecurity(100000);assert(calls==1&&!security_start_pending&&!terminations);
 PumpComputerSecurity(200000);assert(calls==1);
 Reset();result=BLE_HS_EALREADY;PumpComputerSecurity(100000);
 assert(calls==1&&!security_start_pending&&!terminations&&last_result==0);
 assert(host_diagnostics.security_rc==2);
 for(int busy:{BLE_HS_EBUSY,BLE_HS_EAGAIN}){
  Reset();result=busy;PumpComputerSecurity(100000);
  assert(security_start_pending&&!terminations&&calls==1);
  PumpComputerSecurity(199999);assert(calls==1);
  result=0;PumpComputerSecurity(200000);assert(calls==2&&!security_start_pending);
 }
 Reset();encrypted=true;PumpComputerSecurity(100000);
 assert(!calls&&security_pending&&!security_start_pending&&!terminations);
 Reset();live=false;PumpComputerSecurity(100000);
 assert(!calls&&!security_start_pending&&host_diagnostics.security_rc==7);
 Reset();connection=BLE_HS_CONN_HANDLE_NONE;PumpComputerSecurity(100000);assert(!calls);
 Reset();PumpComputerSecurity(security_deadline);assert(!calls&&!terminations);
 Reset();result=514;PumpComputerSecurity(100000);
 assert(terminations==1&&last_result==514&&!security_start_pending);
 host_diagnostics.flags=1|2|4|8|32;
 auto text=host_diagnostics.Text();
 assert(text.find("LINK=1 ENCRYPTED=1 BONDED=1 AUTHORIZED=1")!=std::string::npos);
 assert(text.find("KBD-SUB=1 BOOT-SUB=0 ADV=0")!=std::string::npos);
 host_diagnostics.security_rc=6;host_diagnostics.auth_reason=4;
 host_diagnostics.NewConnection();
 assert(host_diagnostics.security_rc==-1&&host_diagnostics.auth_reason==0);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'security.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'security')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined',
                            '-I', str(ROOT / 'experimental/cube_dongle'),
                            str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_connect_deferred_and_reset_invalidates_pending_security(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        start = source.index('if (event->type == BLE_GAP_EVENT_CONNECT)')
        end = source.index('} else if (event->type == BLE_GAP_EVENT_DISCONNECT', start)
        connect = source[start:end]
        self.assertNotIn('ble_gap_security_initiate(', connect)
        self.assertIn('security_start_pending = true;', connect)
        self.assertIn('security_after = esp_timer_get_time() + 100000;', connect)
        reset = source[source.index('void CubeBleHostReset()'):source.index('void CubeDualTick()')]
        self.assertIn('security_start_pending = false;', reset)
        tick = source[source.index('void CubeDualTick()'):source.index('bool CubeDualSystemAction(')] if 'bool CubeDualSystemAction(' in source else source[source.index('void CubeDualTick()'):]
        self.assertIn('PumpComputerSecurity(esp_timer_get_time());', tick)
        self.assertIn('esp_timer_get_time() >= security_deadline', tick)
        self.assertIn('BLE_GAP_REPEAT_PAIRING_IGNORE', source)
        self.assertNotIn('nvs_flash_erase', source)


if __name__ == '__main__':
    unittest.main()
