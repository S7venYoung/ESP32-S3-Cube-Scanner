from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HidReconnectTests(unittest.TestCase):
    def compile_run(self, program):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'reconnect.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'reconnect')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I',
                            str(ROOT / 'experimental/cube_dongle'), str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_restore_lifetime_and_bounded_storage(self):
        self.compile_run(r'''
#include <cassert>
#include "early_subscriptions.h"
int main(){
 using Cache=cube_dongle::EarlySubscriptions;Cache cache;
 assert(cache.Observe(1,Cache::Keyboard,true));
 assert(cache.Observe(1,Cache::Media,true));
 assert(cache.Observe(1,Cache::Keyboard,false));
 assert(cache.Take(1)==Cache::Media);assert(cache.Take(1)==0);
 assert(cache.Observe(1,Cache::Boot,true));
 assert(cache.Observe(2,Cache::Pointer,true));
 assert(cache.Observe(3,Cache::Studio,true));
 assert(!cache.Observe(4,Cache::Keyboard,true));
 assert(cache.Take(2)==Cache::Pointer);
 assert(cache.Observe(4,Cache::Keyboard,true));
 cache.Clear();assert(!cache.Take(1)&&!cache.Take(3)&&!cache.Take(4));
 assert(!cache.Observe(0xffff,Cache::Keyboard,true));
 assert(!cache.Observe(1,0,true));assert(!cache.Observe(1,3,true));
 assert(!cache.Observe(1,32,true));
}
''')

    def test_production_early_event_and_connect_order(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        start = source.index('int Gap(ble_gap_event* event, void* arg) {')
        end = source.index('        ReleaseBle();\n        ble_gap_conn_desc desc{};', start)
        # Execute the actual early-event staging and CONNECT initialization.
        production = source[start:end] + 'return 0;\n}\nreturn 0;\n}\n'
        shim = r'''
#include <cassert>
#include <cstdint>
#include "early_subscriptions.h"
#define CONFIG_CUBE_STUDIO_RPC 0
enum {BLE_GAP_EVENT_DISCONNECT=1,BLE_GAP_EVENT_SUBSCRIBE=2,BLE_GAP_EVENT_CONNECT=3,
 BLE_GAP_ROLE_SLAVE=1,BLE_HS_CONN_HANDLE_NONE=0xffff,BLE_ERR_REM_USER_CONN_TERM=19};
enum class Output {Usb,Ble};Output preference=Output::Ble;
enum class HostCommand {None,Usb};struct {HostCommand command=HostCommand::None;} pending;
struct ble_gap_conn_desc {int role=1;struct {uint8_t val[6]{};int type=0;}peer_id_addr;};
struct ble_gap_event {
 int type=0;struct {struct {uint16_t conn_handle=0;}conn;}disconnect;
 struct {uint16_t conn_handle=0,attr_handle=0;bool cur_notify=false,cur_indicate=false;}subscribe;
 struct {int status=0;uint16_t conn_handle=0;}connect;
};
cube_dongle::EarlySubscriptions early_subscriptions;
struct {bool Accept(uintptr_t x){return x==1;}} host_epoch;
struct {int early_sub_events=0,early_sub_replays=0,connect_rc=0;void NewConnection(){}}host_diagnostics;
uint16_t connection=BLE_HS_CONN_HANDLE_NONE,keyboard_handle=10,media_handle=11,boot_handle=12,pointer_handle=13;
bool transition=false,authorized=false,security_pending=false,security_start_pending=false,
 suspended=false,keyboard_sub=false,media_sub=false,boot_sub=false,pointer_sub=false;
int protocol=1;int64_t advertisement_stop_next=0;
bool live=true,split=false;int role=1,terminated=0;
int ble_gap_conn_find(uint16_t,ble_gap_conn_desc* p){p->role=role;return live?0:1;}
bool CubeSplitIsKeyboard(const uint8_t*,int){return split;}
void Advertise(){}void StopConnectedAdvertising(){}
int ble_gap_terminate(uint16_t,int){++terminated;return 0;}
'''
        self.compile_run(shim + production + r'''
void Sub(int h,int a,bool on){ble_gap_event e;e.type=BLE_GAP_EVENT_SUBSCRIBE;
 e.subscribe.conn_handle=h;e.subscribe.attr_handle=a;e.subscribe.cur_notify=on;Gap(&e,(void*)1);}
void Connect(int h){ble_gap_event e;e.type=BLE_GAP_EVENT_CONNECT;e.connect.conn_handle=h;Gap(&e,(void*)1);}
int main(){
 Sub(7,10,true);Sub(7,11,true);assert(!keyboard_sub&&!authorized);
 Connect(7);assert(connection==7&&keyboard_sub&&media_sub&&!authorized);
 assert(host_diagnostics.early_sub_replays==1);
 connection=BLE_HS_CONN_HANDLE_NONE;early_subscriptions.Clear();
 role=0;Sub(8,10,true);role=1;Connect(8);assert(!keyboard_sub);
 connection=BLE_HS_CONN_HANDLE_NONE;split=true;Sub(9,10,true);split=false;
 Connect(9);assert(!keyboard_sub);
 connection=BLE_HS_CONN_HANDLE_NONE;Sub(10,10,true);Sub(10,10,false);
 Connect(10);assert(!keyboard_sub);
 connection=BLE_HS_CONN_HANDLE_NONE;Sub(11,10,true);
 ble_gap_event d;d.type=BLE_GAP_EVENT_DISCONNECT;d.disconnect.conn.conn_handle=11;
 Gap(&d,(void*)1);Connect(11);assert(!keyboard_sub);
 connection=BLE_HS_CONN_HANDLE_NONE;Sub(12,12,true);Connect(12);assert(boot_sub&&!keyboard_sub);
 connection=BLE_HS_CONN_HANDLE_NONE;preference=Output::Usb;Sub(13,10,true);Connect(13);
 assert(connection==BLE_HS_CONN_HANDLE_NONE&&terminated==1);
 assert(early_subscriptions.Take(13)==0);
 preference=Output::Ble;Sub(14,10,true);
 ble_gap_event fail;fail.type=BLE_GAP_EVENT_CONNECT;fail.connect.conn_handle=14;fail.connect.status=8;
 Gap(&fail,(void*)1);assert(early_subscriptions.Take(14)==0);
}
''')
        reset = source[source.index('void CubeBleHostReset()'):source.index('void CubeDualTick()')]
        self.assertIn('early_subscriptions.Clear();', reset)
        ready = source[source.index('bool NotifyReady()'):source.index('#if CONFIG_CUBE_STUDIO_RPC', source.index('bool NotifyReady()'))]
        self.assertIn('authorized && Encrypted()', ready)

    def test_old_host_absent_and_suspended_usb_does_not_block_switch(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        helper = source[source.index('bool OldReleased() {'):source.index('void BeginTransition(')]
        self.compile_run(r'''
#include <cassert>
constexpr int BLE_HS_CONN_HANDLE_NONE=0xffff;
bool old_ble=false,neutral_sent=false,usb_connected=false,usb_neutral=false;
int connection=BLE_HS_CONN_HANDLE_NONE;
bool CubeUsbConnected(){return usb_connected;}
bool CubeUsbNeutralDelivered(){return usb_neutral;}
''' + helper + r'''
int main(){
 assert(OldReleased()); // USB absent or suspended: do not wait for an ACK.
 usb_connected=true;assert(!OldReleased()); // Live host still needs release.
 usb_neutral=true;assert(OldReleased());
 old_ble=true;assert(OldReleased()); // No BLE peer: switching remains local.
 connection=7;assert(!OldReleased());neutral_sent=true;assert(OldReleased());
}
''')
        tick = source[source.index('void CubeDualTick()'):source.index('void CubeDualSubmit(')]
        self.assertIn('transition && OldReleased() && physical_neutral', tick)
        self.assertNotIn('!CubeUsbAttached() || CubeUsbNeutralDelivered()', tick)
        self.assertIn('!CubeUsbConnected() || CubeUsbNeutralDelivered()', tick)
        submit = source[source.index('void CubeDualSubmit('):source.index('void CubeDualSetNeutral(')]
        self.assertLess(submit.index('if (actions)'), submit.index('CubeDualTick();'))
        self.assertNotIn('if (!NotifyReady())', submit)


if __name__ == '__main__':
    unittest.main()
