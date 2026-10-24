from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RadioPoolTests(unittest.TestCase):
    def test_production_pool_sampling_and_admission_thresholds(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        start = source.index('void SampleRadioPools() {')
        production = source[start:source.index('void PumpComputerSecurity(', start)]
        shim = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include "host_diagnostics.h"
#define MYNEWT_VAL(x) 4
cube_dongle::HostDiagnostics host_diagnostics;
struct os_mempool { const char* name;int free,total; };
struct os_mempool_info {char omi_name[32]{};int omi_num_free,omi_num_blocks;};
std::vector<os_mempool> registry;
int visits=0;int64_t now=0;
int64_t esp_timer_get_time(){return now;}
os_mempool* os_mempool_info_get_next(os_mempool* cursor,os_mempool_info* out){
 ++visits;
 size_t i=cursor?size_t(cursor-registry.data())+1:0;
 if(i>=registry.size()){return nullptr;}
 auto* p=&registry[i];std::strncpy(out->omi_name,p->name,31);out->omi_name[31]=0;
 out->omi_num_free=p->free;out->omi_num_blocks=p->total;return p;
}
'''
        program = shim + production + r'''
int main(){
 using cube_dongle::RadioPoolCount;using cube_dongle::RadioPools;
 RadioPools p;
 assert(p.Missing()==7&&p.Blocked()==0);
 p.Observe("ble_hs_conn_pool",1,3);p.Observe("ble_l2cap_chan_pool",3,9);
 p.Observe("ble_gatts_clt_cfg_pool",1,4);assert(p.Missing()==0&&p.Blocked()==0);
 p.Observe("ble_l2cap_chan_pool",2,9);assert(p.Blocked()==2);
 p.Observe("ble_hs_conn_pool",0,3);p.Observe("ble_gatts_clt_cfg_pool",0,4);
 assert(p.Blocked()==7);
 RadioPools unknown;unknown.Observe("wrong",0,3);unknown.Observe(nullptr,0,3);
 unknown.Observe("ble_hs_conn_pool",4,3);assert(unknown.Missing()==7);
 auto missing=RadioPoolCount::Unpack(RadioPoolCount{}.Packed());
 assert(missing.free==-1&&missing.total==-1);
 registry={{"ble_hs_conn_pool",1,3},{"ble_l2cap_chan_pool",2,9},
           {"ble_gatts_clt_cfg_pool",1,4},{"unrelated",0,1}};
 SampleRadioPools();assert(visits==5&&host_diagnostics.pool_scan_complete);
 auto text=host_diagnostics.Text();
 assert(text.find("POOL-CONN=1/3 POOL-L2CAP=2/9 POOL-GATT=1/4")!=std::string::npos);
 assert(text.find("POOL-BLOCKED=2 POOL-MISSING=0")!=std::string::npos);
 assert(host_diagnostics.nimble_limit==4);
 visits=0;now=999999;SampleRadioPools();assert(visits==0);
 registry[1].free=3;now=1000000;SampleRadioPools();assert(visits==5);
 assert(host_diagnostics.Text().find("POOL-BLOCKED=0")!=std::string::npos);
 registry.assign(100,{"unrelated",0,1});now=2000000;visits=0;SampleRadioPools();
 assert(visits==64&&!host_diagnostics.pool_scan_complete);
 assert(host_diagnostics.Text().find("POOL-BLOCKED=0 POOL-MISSING=7")!=std::string::npos);
 registry.clear();now=3000000;visits=0;SampleRadioPools();
 assert(visits==1&&host_diagnostics.pool_scan_complete);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'pools.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'pools')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I',
                            str(ROOT / 'experimental/cube_dongle'), str(path), '-o', binary],
                           check=True)
            subprocess.run([binary], check=True)

    def test_sample_only_on_host_advertising_attempt(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        advertise = source[source.index('void Advertise() {'):source.index('void SampleRadioPools() {')]
        self.assertIn('SampleRadioPools();', advertise)
        self.assertLess(advertise.index('host_diagnostics.adv_rc = rc;'),
                        advertise.index('SampleRadioPools();'))
        serial_status = source[source.index('std::string CubeDualStatus()'):]
        self.assertNotIn('os_mempool_info_get_next', serial_status)
        self.assertNotIn('ble_gap_conn_find', serial_status)


if __name__ == '__main__':
    unittest.main()
