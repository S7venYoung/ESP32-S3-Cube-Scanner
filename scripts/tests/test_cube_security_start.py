from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SecurityStartTests(unittest.TestCase):
    def test_in_progress_does_not_disconnect(self):
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        start = source.index('                const int security_rc = ble_gap_security_initiate(')
        end = source.index('\n            }', start)
        production = source[start:end]
        shim = r'''
#include <cassert>
#include "split_diagnostics.h"
constexpr int BLE_HS_EALREADY=2;
int result=0,failures=0;
struct Peer {int connection=7;} peer;
cube_dongle::SplitDiagnostics split_diagnostics[2];
int ble_gap_security_initiate(int connection){assert(connection==7);return result;}
void Fail(Peer&,const char*){++failures;}
void Execute(unsigned cookie){
'''
        program = shim + production + r'''
}
int main(){
 for(unsigned side=0;side<2;++side){
  failures=0;result=0;Execute(side);assert(failures==0);
  assert(split_diagnostics[side].security_start==0);
  result=BLE_HS_EALREADY;Execute(side);assert(failures==0);
  assert(split_diagnostics[side].security_start==BLE_HS_EALREADY);
  result=5;Execute(side);assert(failures==1&&split_diagnostics[side].last_error==5);
 }
 split_diagnostics[1].encryption_status=520;
 split_diagnostics[1].last_error=19;
 auto text=split_diagnostics[1].Text(1);
 assert(text.find("encryption_status=520")!=std::string::npos);
 assert(text.find("error=19")!=std::string::npos);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'test.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I' + str(ROOT / 'experimental/cube_dongle'),
                            str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_security_guard_and_timeout_unchanged(self):
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        self.assertIn('if (event->enc_change.status || !Encrypted(peer))', source)
        self.assertIn('Fail(peer, "security failed; bonds NOT deleted")', source)
        self.assertIn('ble_npl_time_ms_to_ticks32(15000)', source)
        self.assertIn('split_diagnostics[cookie & 1].encryption_status = event->enc_change.status;', source)


if __name__ == '__main__':
    unittest.main()
