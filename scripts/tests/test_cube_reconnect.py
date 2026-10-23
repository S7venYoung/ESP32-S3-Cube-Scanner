"""Execute production scan setup and bonded-peer matching with a host BLE shim."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ReconnectTests(unittest.TestCase):
    def test_scan_and_cooldown_reports(self):
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        scan = source[source.index('void Scan(ble_npl_event*) {'):source.index('bool Configure(')]
        start = source.index('        // Known bonded halves')
        end = source.index('        // Learn only undirected', start)
        match = source[start:end]
        shim = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iterator>
struct ble_npl_event {};
enum class Phase { Idle, Ready, Connecting };
struct Address { int type=0; uint8_t val[6]{}; };
struct Peer { bool configured=true, auto_retry=false, auto_allowed=false;
 Phase phase=Phase::Idle; int64_t retry_after=0; Address address; } peers[2];
struct Candidate {} candidates[2];
bool connecting=false, active=false; int64_t now=0; int retry_count=0, connected=-1;
int own_address_type=0;
int64_t esp_timer_get_time(){return now;}
bool ble_gap_disc_active(){return active;}
void Retry(){++retry_count;}
void Connect(Peer& p){connected=&p-peers;connecting=true;p.phase=Phase::Connecting;}
struct Event { struct { int event_type=0; Address addr; } disc; };
struct ble_gap_disc_params { int passive=0,itvl=0,window=0,filter_duplicates=1; } saved;
int OnGap(Event*,void*);
int ble_gap_disc(int,int duration,ble_gap_disc_params* p,int(*)(Event*,void*),void*){
 assert(duration==5000);saved=*p;return 0;
}
'''
        program = shim + scan + '\nint OnGap(Event* event,void*) {\n' + match + r'''
 return 0;
}
int main(){
 peers[0].address.val[0]=1;peers[1].address.val[0]=2;
 now=1000000;Scan(nullptr);
 assert(saved.passive==1&&saved.filter_duplicates==0);
 assert(saved.itvl==0x60&&saved.window==0x48);
 Event event;event.disc.addr=peers[1].address;
 peers[1].retry_after=2000000;
 OnGap(&event,nullptr);assert(connected==-1);
 // Same advertiser within the same scan must be seen after the cooldown.
 now=2100000;OnGap(&event,nullptr);assert(connected==1);
 connecting=false;peers[1].phase=Phase::Ready;connected=-1;
 event.disc.addr=peers[0].address;OnGap(&event,nullptr);assert(connected==0);
 connecting=false;peers[0].phase=Phase::Idle;connected=-1;
 event.disc.addr.val[0]=99;OnGap(&event,nullptr);assert(connected==-1);
 event.disc.addr=peers[0].address;event.disc.event_type=1;
 OnGap(&event,nullptr);assert(connected==0); // Directed bonded advertising.
 connecting=false;peers[0].phase=Phase::Idle;
 now=60000000;Scan(nullptr);assert(saved.itvl==0x100&&saved.window==0x30);
 active=true;saved.itvl=123;Scan(nullptr);assert(saved.itvl==123);
 active=false;peers[0].phase=peers[1].phase=Phase::Ready;
 Scan(nullptr);assert(saved.itvl==123);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'test.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_retry_and_security_preserved(self):
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        self.assertIn('ble_npl_time_ms_to_ticks32(250)', source)
        self.assertEqual(source.count('peer.retry_after = esp_timer_get_time() + 1000000;'), 4)
        self.assertIn('BLE_GAP_REPEAT_PAIRING_IGNORE', source)
        self.assertIn('security failed; bonds NOT deleted', source)
        self.assertIn('!Encrypted(peer)', source)


if __name__ == '__main__':
    unittest.main()
