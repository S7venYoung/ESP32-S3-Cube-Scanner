from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SplitDiagnosticsTests(unittest.TestCase):
    def test_snapshot_and_wraparound(self):
        program = r'''
#include "split_diagnostics.h"
#include <cassert>
int main(){
 cube_dongle::SplitDiagnostics d;
 assert(d.Text(10).find("adv_age_ms=-1")!=std::string::npos);
 d.Advertisement(0xfffffff0);d.phase=2;++d.attempts;d.last_error=5;d.failed_phase=2;
 auto s=d.Text(0x10);
 assert(s.find("adv=1 adv_age_ms=32")!=std::string::npos);
 assert(s.find("phase=2")!=std::string::npos);
 assert(s.find("attempts=1 error=5 failed_phase=2")!=std::string::npos);
 assert(d.Text(0x10)==s); // Reading does not clear failures or counters.
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

    def test_read_only_command_is_wired(self):
        sync = (ROOT / 'main/scanner/codex_sync.cc').read_text()
        self.assertIn('line == "DONGLE SPLIT DIAG"', sync)
        self.assertIn('Reply(CubeSplitDiagnostics())', sync)
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        self.assertIn('split_diagnostics[cookie & 1].last_error = event->connect.status;', source)
        self.assertIn('split_diagnostics[cookie & 1].last_error = event->enc_change.status;', source)
        self.assertIn('split_diagnostics[cookie & 1].last_error = event->disconnect.reason;', source)
        self.assertIn('split_diagnostics[Side(peer)].failed_phase', source)


if __name__ == '__main__':
    unittest.main()
