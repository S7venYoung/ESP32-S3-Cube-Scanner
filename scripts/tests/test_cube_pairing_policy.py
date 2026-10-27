from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class PairingPolicyTests(unittest.TestCase):
    def test_production_advertising_after_usb_idle_and_disconnect(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        start = source.index('void Advertise() {')
        body = source[start:source.index('void SampleRadioPools() {', start)]
        program = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include "host_epoch.h"
#define CONFIG_CUBE_STUDIO_RPC 0
constexpr int BLE_HS_CONN_HANDLE_NONE=0xffff, BLE_HS_ADV_F_DISC_GEN=2,
 BLE_HS_ADV_F_BREDR_UNSUP=4, BLE_GAP_CONN_MODE_UND=2, BLE_GAP_DISC_MODE_GEN=2,
 BLE_HS_FOREVER=0;
struct ble_uuid16_t {int value;};
#define BLE_UUID16_INIT(x) {x}
struct ble_hs_adv_fields {
 int flags=0,appearance=0,appearance_is_present=0,num_uuids16=0,uuids16_is_complete=0;
 const ble_uuid16_t* uuids16=nullptr;
 const uint8_t* name=nullptr;size_t name_len=0;int name_is_complete=0;
};
struct ble_gap_adv_params {int conn_mode=0,disc_mode=0;};
struct {int adv_stage=0,adv_rc=0;} host_diagnostics;
bool synced=true,configured=true,transition=false,active=false;
enum class Output {Auto,Usb,Ble};Output preference=Output::Auto;
int connection=BLE_HS_CONN_HANDLE_NONE,own_type=0,last_result=0,starts=0,stops=0;
int64_t now=86400000000LL,advertisement_next=0;
cube_dongle::HostEpoch host_epoch;
int64_t esp_timer_get_time(){return now;}
bool ble_gap_adv_active(){return active;}
void StopConnectedAdvertising(){if(connection!=BLE_HS_CONN_HANDLE_NONE&&active){active=false;++stops;}}
int ble_gap_adv_set_fields(const ble_hs_adv_fields*){return 0;}
int ble_svc_gap_device_name_set(const char*){return 0;}
int ble_gap_adv_rsp_set_fields(const ble_hs_adv_fields*){return 0;}
int Gap(int*,void*){return 0;}
int ble_gap_adv_start(int,void*,int,const ble_gap_adv_params* p,int(*)(int*,void*),void*){
 assert(p->conn_mode==BLE_GAP_CONN_MODE_UND&&p->disc_mode==BLE_GAP_DISC_MODE_GEN);
 active=true;++starts;return 0;
}
void SampleRadioPools(){}
''' + body + r'''
int main(){
 // AUTO/BLE keep open-slot pairing after a day's idle, without a window timer.
 Advertise();assert(active&&starts==1);
 Advertise();assert(starts==1); // No redundant start while already advertising.
 // Adopting a computer stops surplus advertising; keep both split links alone.
 connection=7;Advertise();assert(!active&&stops==1&&starts==1);
 // An already-bound computer can reconnect after disconnect, without PAIR.
 connection=BLE_HS_CONN_HANDLE_NONE;now+=250000;
 Advertise();assert(active&&starts==2);
 transition=true;active=false;now+=250000;Advertise();assert(starts==2);
 transition=false;synced=false;Advertise();assert(starts==2);
 synced=true;Advertise();assert(active&&starts==3);
 // Explicit USB is now exclusive, unlike AUTO with USB attached.
 preference=Output::Usb;active=false;now+=250000;
 Advertise();assert(!active&&starts==3);
 preference=Output::Ble;Advertise();assert(active&&starts==4);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'pair.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'pair')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I',
                            str(ROOT / 'experimental/cube_dongle'), str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_open_profile_not_timeout_and_security_gates_preserved(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        self.assertNotIn('pairing_until', source)
        self.assertIn('(!profiles[selected].bound ? 256u : 0u)', source)
        start = source.index('void AuthorizeAfterPersistence() {')
        auth = source[start:source.index('void SampleRadioPools();', start)]
        for gate in ['desc.sec_state.bonded', 'ValidIdentity(', '!UnresolvedPrivate(',
                     '!CubeSplitIsKeyboard(', 'sc', 'key_size', 'SaveProfile(']:
            self.assertIn(gate, auth)
        self.assertIn('esp_timer_get_time() < security_deadline', auth)
        self.assertIn('esp_timer_get_time() >= security_deadline', source)
        self.assertIn('BLE_GAP_REPEAT_PAIRING_IGNORE', source)
        self.assertIn('RepairCandidateValid()', source)
        self.assertIn('repair_candidate.expires', source)
        # Clearing computer slots must still use exact peer deletion, not all bonds.
        self.assertNotIn('ble_store_clear(', source)
        disconnect = source[source.index('event->type == BLE_GAP_EVENT_DISCONNECT'):]
        self.assertLess(disconnect.index('connection = BLE_HS_CONN_HANDLE_NONE;'),
                        disconnect.index('Advertise();'))


if __name__ == '__main__':
    unittest.main()
