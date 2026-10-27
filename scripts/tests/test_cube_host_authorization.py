"""Run the production computer authorization function with deterministic SMP timing."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HostAuthorizationTests(unittest.TestCase):
    def test_identity_and_persistence_races(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        production = source[source.index('bool UnresolvedPrivate('):source.index('void SampleRadioPools();')]
        shim = r'''
#include <cassert>
#include <atomic>
#include <cstdint>
#include <cstring>
#include "host_diagnostics.h"
cube_dongle::HostDiagnostics host_diagnostics;
#define BLE_ADDR_RANDOM 1
#define BLE_ERR_REM_USER_CONN_TERM 19
#define ESP_LOGI(...) ((void)0)
struct ble_addr_t { uint8_t type=0,val[6]{}; };
struct ble_gap_conn_desc { struct { bool encrypted=true,bonded=true; } sec_state;
 ble_addr_t peer_id_addr; } desc;
struct ble_store_key_sec { ble_addr_t peer_addr; };
struct ble_store_value_sec { bool ltk_present=true,sc=true; int key_size=16; };
struct Profile { bool bound=false;ble_addr_t identity; } profiles[5];
unsigned selected=0;int connection=1;int64_t now=100,security_deadline=30000;
bool security_pending=true,authorized=false,routed_ble=false;
bool keys_ready=true,save_ok=true,split=false;int terminated=0,saves=0,key_size=16;
std::atomic<int> last_result{0};const char* kTag="test";
int64_t esp_timer_get_time(){return now;}
int ble_gap_conn_find(int,ble_gap_conn_desc* out){*out=desc;return 0;}
bool ValidIdentity(const ble_addr_t& a){return a.val[0]!=0;}
bool CubeSplitIsKeyboard(const uint8_t*,int){return split;}
int ble_store_read_our_sec(ble_store_key_sec*,ble_store_value_sec* out){out->key_size=key_size;return keys_ready?0:1;}
int ble_store_read_peer_sec(ble_store_key_sec*,ble_store_value_sec* out){out->key_size=key_size;return keys_ready?0:1;}
bool Same(const ble_addr_t& a,const ble_addr_t& b){return (a.type&1)==(b.type&1)&&!std::memcmp(a.val,b.val,6);}
bool SaveProfile(unsigned,const ble_addr_t*){++saves;return save_ok;}
void ReleaseBle(){}struct {void Begin(){}} barrier;
bool NotifyReady(){return false;}void Notice(int){}
void ble_gap_terminate(int,int){++terminated;}
void Reset(){for(auto& p:profiles)p={};desc={};desc.peer_id_addr.val[0]=7;
 now=100;security_deadline=30000;security_pending=true;
 authorized=false;keys_ready=true;save_ok=true;split=false;terminated=saves=0;key_size=16;last_result=0;}
'''
        program = shim + production + r'''
int main(){
 Reset();desc.peer_id_addr.type=1;desc.peer_id_addr.val[5]=0x40;
 assert(UnresolvedPrivate(desc.peer_id_addr));AuthorizeAfterPersistence();
 assert(security_pending&&!authorized&&!terminated&&!saves);
 assert(host_diagnostics.auth_reason==2);
 desc.peer_id_addr.type=0;desc.peer_id_addr.val[5]=0;
 keys_ready=false;AuthorizeAfterPersistence();assert(security_pending&&!terminated&&!saves);
 assert(host_diagnostics.auth_reason==3&&host_diagnostics.keys==0);
 keys_ready=true;AuthorizeAfterPersistence();assert(authorized&&!security_pending&&saves==1);
 assert(host_diagnostics.auth_reason==0&&host_diagnostics.keys==3);
 assert(profiles[0].bound);
 Reset();profiles[0].bound=true;profiles[0].identity=desc.peer_id_addr;
 AuthorizeAfterPersistence();assert(authorized&&saves==0);
 Reset();profiles[0].bound=true;profiles[0].identity.val[0]=8;
 AuthorizeAfterPersistence();assert(!authorized&&terminated==1&&saves==0);
 assert(host_diagnostics.auth_reason==5);
 Reset();profiles[1].bound=true;profiles[1].identity=desc.peer_id_addr;
 AuthorizeAfterPersistence();assert(!authorized&&terminated==1&&saves==0);
 assert(host_diagnostics.auth_reason==4);
 Reset();key_size=12;AuthorizeAfterPersistence();assert(!authorized&&security_pending&&!terminated);
 now=security_deadline;AuthorizeAfterPersistence();assert(!authorized&&terminated==1&&!saves);
 // An empty slot is still pairable after a day of USB operation. The security
 // exchange still has its own finite deadline, independent of slot openness.
 Reset();now=86400000000LL;security_deadline=now+30000000;
 AuthorizeAfterPersistence();assert(authorized&&!terminated&&saves==1);
 Reset();save_ok=false;AuthorizeAfterPersistence();assert(!authorized&&terminated==1&&!profiles[0].bound);
 Reset();split=true;now=security_deadline;AuthorizeAfterPersistence();assert(!authorized&&terminated==1&&!saves);
 Reset();desc.sec_state.encrypted=false;AuthorizeAfterPersistence();assert(!authorized&&!saves);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'test.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'experimental/cube_dongle'),
                            '-fsanitize=address,undefined', str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)


if __name__ == '__main__':
    unittest.main()
