from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BondPersistenceTests(unittest.TestCase):
    def test_production_update_and_power_loss_recovery(self):
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        functions = source[source.index('struct PendingBondUpdate'):source.index('const ble_uuid128_t kService')]
        shim = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
constexpr int ESP_OK=0,ESP_ERR_NVS_NOT_FOUND=1,NVS_READWRITE=1,NVS_READONLY=0;
constexpr int BLE_HS_ESTORE_FAIL=28,BLE_HS_ENOENT=5;
constexpr int BLE_STORE_OBJ_TYPE_PEER_SEC=1,BLE_STORE_OBJ_TYPE_OUR_SEC=2;
struct ble_addr_t{uint8_t type,val[6];};
struct ble_store_value_sec{ble_addr_t peer_addr;uint8_t ltk[16];};
union ble_store_value{ble_store_value_sec sec;};
struct ble_store_key{struct {ble_addr_t peer_addr;} sec;};
using nvs_handle_t=int;
std::map<int,std::map<int,ble_store_value>> ram,disk;
std::vector<uint8_t> journal,staged;
bool fail_commit=false,fail_write=false,fail_delete=false,fail_open=false;
int nvs_open(const char*,int mode,int* handle){if(fail_open)return 99;
 if(mode==NVS_READONLY&&journal.empty())return ESP_ERR_NVS_NOT_FOUND;
 *handle=mode;staged=journal;return 0;}
int nvs_set_blob(int,const char*,const void* p,size_t n){staged.assign((const uint8_t*)p,(const uint8_t*)p+n);return 0;}
int nvs_get_blob(int,const char*,void* p,size_t* n){if(journal.empty())return ESP_ERR_NVS_NOT_FOUND;
 if(*n<journal.size())return 99;*n=journal.size();std::memcpy(p,journal.data(),*n);return 0;}
int nvs_erase_key(int,const char*){staged.clear();return 0;}
int nvs_commit(int){if(fail_commit)return 99;journal=staged;return 0;}
void nvs_close(int){}
int ble_store_config_read(int t,const ble_store_key* k,ble_store_value* out){
 auto it=ram[t].find(k->sec.peer_addr.val[0]);if(it==ram[t].end())return BLE_HS_ENOENT;
 *out=it->second;return 0;}
int ble_store_config_delete(int t,const ble_store_key* k){if(fail_delete)return BLE_HS_ESTORE_FAIL;
 ram[t].erase(k->sec.peer_addr.val[0]);if(disk[t].size()>ram[t].size())disk[t]=ram[t];return 0;}
int ble_store_config_write(int t,const ble_store_value* v){if(fail_write)return BLE_HS_ESTORE_FAIL;
 ram[t][v->sec.peer_addr.val[0]]=*v;
 // Reproduce the pinned SDK's equal-count update omission.
 if(disk[t].size()<ram[t].size())disk[t]=ram[t];return 0;}
void Reset(){ram.clear();disk.clear();journal.clear();staged.clear();
 fail_commit=fail_write=fail_delete=fail_open=false;}
ble_store_value Key(int id,int byte){ble_store_value v{};v.sec.peer_addr.val[0]=id;v.sec.ltk[0]=byte;return v;}
'''
        program = shim + functions + r'''
int main(){for(int type:{1,2}){
 Reset();auto old=Key(1,10),next=Key(1,20),other=Key(2,30);
 assert(!ble_store_config_write(type,&old));assert(!ble_store_config_write(type,&other));
 assert(!ble_store_config_write(type,&next));ram=disk;
 assert(ram[type][1].sec.ltk[0]==10); // Baseline reproduces stale reboot key.
 assert(!PersistBondUpdate(type,&next));ram=disk;
 assert(ram[type][1].sec.ltk[0]==20&&ram[type][2].sec.ltk[0]==30&&journal.empty());
 ram[type][1].sec.ltk[0]=25;
 assert(!PersistBondUpdate(type,&ram[type][1]));ram=disk;
 assert(ram[type][1].sec.ltk[0]==25); // Input alias survives removal of RAM entry.
 fail_commit=true;auto newer=Key(1,40);assert(PersistBondUpdate(type,&newer));
 assert(disk[type][1].sec.ltk[0]==25);fail_commit=false;
 fail_delete=true;assert(PersistBondUpdate(type,&newer));assert(!journal.empty());
 fail_delete=false;ram=disk;assert(!RecoverBondJournal());
 assert(disk[type][1].sec.ltk[0]==40&&journal.empty());
 auto newest=Key(1,50);fail_write=true;assert(PersistBondUpdate(type,&newest));
 assert(!journal.empty()&&!disk[type].count(1)); // Simulate loss after deletion.
 fail_write=false;ram=disk;assert(!RecoverBondJournal());
 assert(disk[type][1].sec.ltk[0]==50&&disk[type][2].sec.ltk[0]==30);
 assert(!RecoverBondJournal());assert(journal.empty());
 PendingBondUpdate deletion;deletion.type=type;deletion.security=Key(1,55).sec;
 assert(!SaveBondJournal(&deletion));ble_store_key key{};key.sec.peer_addr=deletion.security.peer_addr;
 assert(!DeleteBondWithRecovery(type,&key));ram=disk;assert(!RecoverBondJournal());
 assert(!disk[type].count(1)&&journal.empty());
 PendingBondUpdate pending;pending.type=type;pending.security=Key(1,60).sec;
 assert(!SaveBondJournal(&pending));ble_store_value value{};value.sec=pending.security;
 assert(!ReplaceBondRecord(type,&value)); // Power loss before journal clearance.
 ram=disk;assert(!RecoverBondJournal());assert(disk[type][1].sec.ltk[0]==60);
 pending.version=99;assert(!SaveBondJournal(&pending));
 assert(RecoverBondJournal()&&!journal.empty()&&disk[type][1].sec.ltk[0]==60);
 }
 Reset();auto fresh=Key(3,70);assert(!PersistBondUpdate(1,&fresh));ram=disk;
 assert(ram[1][3].sec.ltk[0]==70&&journal.empty());
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'test.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_install_before_host_start(self):
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        start = source.index('    ble_store_config_init();')
        tail = source[start:]
        self.assertLess(tail.index('RecoverBondJournal()'), tail.index('nimble_port_freertos_init(Host)'))
        self.assertLess(tail.index('ble_hs_cfg.store_write_cb = PersistBondUpdate'), tail.index('nimble_port_freertos_init(Host)'))
        self.assertLess(tail.index('ble_hs_cfg.store_delete_cb = DeleteBondWithRecovery'), tail.index('nimble_port_freertos_init(Host)'))
        self.assertNotIn('nvs_flash_erase', tail)


if __name__ == '__main__':
    unittest.main()
