import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class BroadcastTest(unittest.TestCase):
    def test_production_radio_lifecycle(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        body = source[source.index('bool scanner_advertising = false;'):
                      source.index('void StopConnectedAdvertising() {')]
        program = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include "scanner_broadcast.h"
#include "scanner_timing.h"
constexpr int BLE_HS_EALREADY=2, BLE_GAP_EVENT_ADV_COMPLETE=3;
constexpr int BLE_HS_CONN_HANDLE_NONE=65535, ESP_MAC_BT=2, ESP_OK=0;
constexpr int BLE_GAP_CONN_MODE_NON=0, BLE_GAP_DISC_MODE_GEN=2, BLE_HS_FOREVER=0;
struct ble_gap_event { int type; };
struct ble_hs_adv_fields {const uint8_t* name;size_t name_len;int name_is_complete;};
struct ble_gap_adv_params {int conn_mode=0,disc_mode=0,itvl_min=0,itvl_max=0;};
bool synced=true,configured=true,transition=false,active=false,online=true;
enum class Output {Usb,Ble};Output preference=Output::Usb;
int connection=BLE_HS_CONN_HANDLE_NONE,own_type=0,selected=0,starts=0,stops=0,updates=0;
int result=0;int64_t now=1000000;
unsigned input_modifiers=4;uint8_t last_type=0,last_modifiers=0;
[[maybe_unused]] constexpr const char* kTag="test";
#define ESP_LOGW(...) ((void)0)
struct { bool Pending(){return false;} } host_epoch;
struct Status {bool online=true;unsigned active_layer=2;const char* layer="NAV";
 unsigned modifiers=4;int left_battery=40,right_battery=80,wpm=40;bool usb=true,ble=false;};
struct Fight {int left_wpm=10,right_wpm=30;};
Status CubeSplitKeyboardStatus(){Status s;s.online=online;s.modifiers=input_modifiers;return s;}
Fight CubeSplitFightTelemetry(){return {};}
int64_t esp_timer_get_time(){return now;}
bool ble_gap_adv_active(){return active;}
int ble_gap_adv_stop(){++stops;active=false;return 0;}
int esp_read_mac(uint8_t* m,int){m[2]=1;return 0;}
int ble_gap_adv_set_data(const uint8_t* p,size_t n){
 assert(n<=31&&p[0]==2&&p[4]==255);last_type=p[8];if(last_type==0xcd)last_modifiers=p[28];++updates;return result;
}
int ble_gap_adv_rsp_set_fields(const ble_hs_adv_fields* r){assert(r->name_len==15);return result;}
int ble_gap_adv_start(int,void*,int,const ble_gap_adv_params* p,
 int(*)(ble_gap_event*,void*),void*){
 assert(p->conn_mode==BLE_GAP_CONN_MODE_NON);++starts;active=true;return result;
}
''' + body + r'''
int main(){
 cube_dongle::ScannerTiming timing;
 assert(timing.Text(10).find("SCAN-TRY-AGE=-1")!=std::string::npos);
 timing.Poll(100);timing.Poll(105);timing.Attempt(105,4);timing.Result(105,12);
 assert(timing.Text(110).find("SCAN-ERRORS=1")!=std::string::npos);
 timing.Result(115,0);
 assert(timing.Text(120).find("SCAN-OK-AGE=5")!=std::string::npos);
 assert(timing.Text(120).find("SCAN-CALL-GAP=5")!=std::string::npos);
 ScannerAdvertise();assert(starts==1&&scanner_advertising&&updates==1);
 for(int i=0;i<19;++i){now+=5000;ScannerAdvertise();}assert(updates==1);
 now+=5000;ScannerAdvertise();assert(updates==2&&starts==1);
 now+=5000;input_modifiers=0;ScannerAdvertise();
 assert(updates==3&&last_type==0xcd&&last_modifiers==0);
 now+=5000;input_modifiers=4;ScannerAdvertise();
 assert(updates==4&&last_type==0xcd&&last_modifiers==8);
 now+=5000;input_modifiers=0;result=12;ScannerAdvertise();
 int failed_updates=updates;
 now+=5000;result=0;ScannerAdvertise();assert(updates==failed_updates);
 now+=5000;ScannerAdvertise();assert(updates==failed_updates+1&&last_type==0xcd&&last_modifiers==0);
 input_modifiers=4;now+=5000;ScannerAdvertise();assert(last_type==0xcd&&last_modifiers==8);
 StopScannerAdvertising();assert(!active&&!scanner_advertising&&stops==1);
 preference=Output::Ble;now+=100000;ScannerAdvertise();assert(starts==1);
 connection=7;ScannerAdvertise();assert(starts==2&&scanner_advertising);
 ble_gap_event e{BLE_GAP_EVENT_ADV_COMPLETE};
 ScannerGap(&e,reinterpret_cast<void*>(scanner_epoch-1));assert(scanner_advertising);
 online=false;now+=100000;ScannerAdvertise();assert(!scanner_advertising&&!active);
 online=true;transition=true;now+=100000;ScannerAdvertise();assert(starts==2);
 transition=false;result=12;ScannerAdvertise();assert(!scanner_advertising);
 now+=5000;ScannerAdvertise();assert(starts==2);
 result=0;now+=100000;ScannerAdvertise();assert(starts==3);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / 'radio.cc'
            path.write_text(program)
            exe = str(pathlib.Path(tmp) / 'radio')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I',
                            str(ROOT / 'experimental/cube_dongle'), str(path), '-o', exe], check=True)
            subprocess.run([exe], check=True)

    def test_wire_roundtrip(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(pathlib.Path(tmp) / 'broadcast')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tests/scanner_broadcast_test.cc'), '-o', exe], check=True)
            subprocess.run([exe], check=True)

    def test_pairing_priority_and_reset(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        compact = ''.join(source.split())
        for expected in [
                'scanner_advertising || connection == BLE_HS_CONN_HANDLE_NONE',
                'if (preference != Output::Usb && connection == BLE_HS_CONN_HANDLE_NONE) return;',
                'StopScannerAdvertising(); if (scanner_advertising) return;',
                'ScannerAdvertise(); Pump();',
                'scanner_advertising = false; scanner_next = 0;']:
            self.assertIn(''.join(expected.split()), compact)

if __name__ == '__main__':
    unittest.main()
