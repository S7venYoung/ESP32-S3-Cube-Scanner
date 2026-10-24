from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ComputerBondRepairTests(unittest.TestCase):
    def test_exact_recent_target_and_other_bonds_preserved(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        start = source.index('bool RepairCandidateValid() {')
        production = source[start:source.index('bool WantBle()', start)]
        shim = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <set>
constexpr int BLE_HS_ENOENT=5;
struct ble_addr_t {uint8_t type=0,val[6]{};};
struct Profile {bool bound=false;ble_addr_t identity;} profiles[5];
struct {bool valid=false;unsigned profile=0;int64_t expires=0;ble_addr_t identity;} repair_candidate;
struct ble_gap_conn_desc {ble_addr_t peer_id_addr;} desc;
unsigned selected=0;int connection=7,last_result=0,deletes=0,clears=0;
int64_t now=100;bool live=true,fail_delete=false;
std::set<int> records;
int64_t esp_timer_get_time(){return now;}
bool ValidIdentity(const ble_addr_t& a){return a.type<=1&&a.val[0]!=0;}
bool UnresolvedPrivate(const ble_addr_t& a){return a.type==1&&(a.val[5]&0xc0)==0x40;}
bool CubeSplitIsKeyboard(const uint8_t* a,int){return a[0]==1||a[0]==2;}
bool Same(const ble_addr_t& a,const ble_addr_t& b){return (a.type&1)==(b.type&1)&&!std::memcmp(a.val,b.val,6);}
int ble_gap_conn_find(int,ble_gap_conn_desc* out){*out=desc;return live?0:7;}
int ble_store_util_delete_peer(const ble_addr_t* a){++deletes;if(fail_delete){return 6;}
 records.erase(a->val[0]);return 0;}
bool ClearSlot(unsigned slot){++clears;assert(slot==selected);
 if(ble_store_util_delete_peer(&profiles[slot].identity)){return false;}
 profiles[slot]={};return true;}
void Reset(unsigned slot){selected=slot;for(auto& p:profiles)p={};repair_candidate={};
 desc={};desc.peer_id_addr.val[0]=7;records={1,2,7,9};
 now=100;live=true;fail_delete=false;deletes=clears=last_result=0;}
void Protected(){assert(records.count(1)&&records.count(2)&&records.count(9));}
'''
        program = shim + production + r'''
int main(){for(unsigned slot=0;slot<5;++slot){
 Reset(slot);assert(!RepairComputerBond()&&!deletes);
 RememberRepairCandidate(connection);assert(RepairCandidateValid()&&!deletes);
 assert(RepairComputerBond()&&deletes==1&&!clears&&!records.count(7));Protected();
 assert(!RepairComputerBond()&&deletes==1); // One-shot; never choose another peer.
 Reset(slot);profiles[slot].bound=true;profiles[slot].identity=desc.peer_id_addr;
 RememberRepairCandidate(connection);assert(RepairComputerBond());
 assert(clears==1&&!profiles[slot].bound);Protected();
 Reset(slot);auto other=(slot+1)%5;profiles[other].bound=true;
 profiles[other].identity=desc.peer_id_addr;RememberRepairCandidate(connection);
 assert(!RepairComputerBond()&&!deletes&&profiles[other].bound);Protected();
 Reset(slot);RememberRepairCandidate(connection);selected=other;
 assert(!RepairComputerBond()&&!deletes);
 Reset(slot);RememberRepairCandidate(connection);now=repair_candidate.expires;
 assert(!RepairComputerBond()&&!deletes);
 Reset(slot);desc.peer_id_addr.val[0]=1;RememberRepairCandidate(connection);
 assert(!RepairComputerBond()&&!deletes);Protected();
 Reset(slot);desc.peer_id_addr.type=1;desc.peer_id_addr.val[5]=0x40;
 RememberRepairCandidate(connection);assert(!RepairComputerBond()&&!deletes);
 Reset(slot);live=false;RememberRepairCandidate(connection);assert(!RepairComputerBond()&&!deletes);
 Reset(slot);RememberRepairCandidate(connection+1);assert(!RepairComputerBond()&&!deletes);
 Reset(slot);RememberRepairCandidate(connection);fail_delete=true;
 assert(!RepairComputerBond()&&repair_candidate.valid&&records.count(7));Protected();
 fail_delete=false;assert(RepairComputerBond());Protected();
}}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'repair.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'repair')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_explicit_command_only_and_disconnect_before_delete(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        gap = source[source.index('int Gap(ble_gap_event* event'):source.index('enum Field :')]
        self.assertIn('RememberRepairCandidate(connection)', gap)
        self.assertIn('BLE_GAP_REPEAT_PAIRING_IGNORE', gap)
        self.assertNotIn('RepairComputerBond()', gap)
        self.assertNotIn('ble_store_util_delete_peer', gap)
        reset = source[source.index('void CubeBleHostReset()'):source.index('void CubeDualTick()')]
        self.assertIn('repair_candidate.valid = false', reset)
        apply = source[source.index('void ApplyPending()'):source.index('bool CubeBleHostInit()')]
        self.assertLess(apply.index('ble_gap_terminate('), apply.index('RepairComputerBond()'))
        control = (ROOT / 'experimental/cube_dongle/host_control.h').read_text()
        self.assertIn('ClearSlot,\n    Repair', control)
        self.assertIn('line == "DONGLE BT REPAIR"', control)


if __name__ == '__main__':
    unittest.main()
