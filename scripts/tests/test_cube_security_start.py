from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SecurityStartTests(unittest.TestCase):
    def test_deferred_production_security_pump(self):
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        production = source[source.index('void PumpPeerSecurity('):source.index('void TelemetryTick(')]
        shim = r'''
#include <cassert>
#include <cstdint>
constexpr int BLE_HS_EALREADY=2,BLE_HS_EBUSY=15,BLE_HS_EAGAIN=1,BLE_HS_ENOTCONN=7;
enum class Phase{Idle,Security,Service,Ready,Closing};
struct Peer{Phase phase=Phase::Security;bool security_started=false;
 int64_t security_after=100000;int connection=7;} peers[2];
struct Diag{int security_start=-1,last_error=0;} split_diagnostics[2];
int calls=0,failures=0,discoveries=0,result=0;bool live=true,encrypted=false;
struct ble_gap_conn_desc{};
int ble_gap_conn_find(int,ble_gap_conn_desc*){return live?0:1;}
unsigned Side(Peer& p){return &p-peers;}
bool Encrypted(Peer&){return encrypted;}
int ble_gap_security_initiate(int conn){assert(conn==7);++calls;return result;}
void Fail(Peer& p,const char*){++failures;p.phase=Phase::Closing;}
void Discover(Peer& p){++discoveries;p.phase=Phase::Service;}
void Reset(unsigned side){peers[side]={};calls=failures=discoveries=0;
 result=0;live=true;encrypted=false;split_diagnostics[side]={};}
'''
        program = shim + production + r'''
int main(){for(unsigned side=0;side<2;++side){
 Reset(side);auto& p=peers[side];PumpPeerSecurity(p,99999);assert(!calls);
 PumpPeerSecurity(p,100000);assert(calls==1&&p.security_started&&!failures);
 PumpPeerSecurity(p,200000);assert(calls==1);
 Reset(side);result=BLE_HS_EALREADY;PumpPeerSecurity(p,100000);
 assert(p.security_started&&!failures&&calls==1);
 for(int busy:{BLE_HS_EBUSY,BLE_HS_EAGAIN}){
  Reset(side);result=busy;PumpPeerSecurity(p,100000);assert(!p.security_started&&!failures);
  PumpPeerSecurity(p,150000);assert(calls==1);
  result=0;PumpPeerSecurity(p,200000);assert(calls==2&&p.security_started);
 }
 Reset(side);live=false;PumpPeerSecurity(p,100000);
 assert(!calls&&failures==1&&split_diagnostics[side].security_start==BLE_HS_ENOTCONN);
 Reset(side);encrypted=true;PumpPeerSecurity(p,100000);
 assert(!calls&&discoveries==1&&p.phase==Phase::Service);
 Reset(side);result=514;PumpPeerSecurity(p,100000);
 assert(failures==1&&split_diagnostics[side].last_error==514);
 Reset(side);p.phase=Phase::Idle;PumpPeerSecurity(p,100000);assert(!calls&&!discoveries);
 Reset(side);p.phase=Phase::Ready;PumpPeerSecurity(p,100000);assert(!calls&&!discoveries);
}}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'test.cc'
            path.write_text('#include <initializer_list>\n' + program)
            binary = str(Path(tmp) / 'test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_native_parameters_and_security_guard(self):
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        compact = ''.join(source.split())
        for fragment in ['params.itvl_min=params.itvl_max=6;', 'params.latency=30;',
                         'params.supervision_timeout=400;', 'PumpPeerSecurity(peer,now);']:
            self.assertIn(fragment, compact)
        connect = source[source.index('if (event->type == BLE_GAP_EVENT_CONNECT)'):source.index('} else if (event->type == BLE_GAP_EVENT_DISCONNECT')]
        self.assertNotIn('ble_gap_security_initiate(', connect)
        self.assertIn('if (event->enc_change.status || !Encrypted(peer))', source)
        self.assertIn('Fail(peer, "security failed; bonds NOT deleted")', source)
        self.assertIn('ble_npl_time_ms_to_ticks32(15000)', source)


if __name__ == '__main__':
    unittest.main()
