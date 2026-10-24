from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BootTraceTests(unittest.TestCase):
    def test_actual_rtc_trace_functions(self):
        source = (ROOT / 'main/main.cc').read_text()
        start = source.index('namespace {')
        functions = source[start:source.index('#endif', start)]
        shim = r'''
#include <cassert>
#include <string>
#include "boot_trace.h"
#define RTC_NOINIT_ATTR
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
'''
        program = shim + functions + r'''
int main(){
 CubeBootTraceStart(false);CubeBootTraceStage(3);
 assert(CubeBootTraceStatus()=="CUBE-BOOT/1 sequence=1 previous_stage=0 stage=3");
 CubeBootTraceStart(true);assert(CubeBootTraceStatus().find("sequence=2 previous_stage=3 stage=0")!=std::string::npos);
 CubeBootTraceStage(6);CubeBootTraceStart(true);
 assert(CubeBootTraceStatus().find("sequence=3 previous_stage=6 stage=0")!=std::string::npos);
 cube_boot_trace.checksum^=1;CubeBootTraceStart(true);
 assert(CubeBootTraceStatus().find("sequence=1 previous_stage=0 stage=0")!=std::string::npos);
 CubeBootTraceStage(8);CubeBootTraceStart(false);
 assert(CubeBootTraceStatus().find("sequence=1 previous_stage=0 stage=0")!=std::string::npos);
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

    def test_boot_and_diagnostic_wiring(self):
        main = (ROOT / 'main/main.cc').read_text()
        self.assertLess(main.index('CubeBootTraceStart(reset'), main.index('nvs_flash_init()'))
        self.assertIn('reset != ESP_RST_POWERON && reset != ESP_RST_BROWNOUT', main)
        sync = (ROOT / 'main/scanner/codex_sync.cc').read_text()
        self.assertIn('line == "DONGLE BOOT DIAG"', sync)
        self.assertIn('Reply(CubeBootTraceStatus())', sync)
        app = (ROOT / 'main/application.cc').read_text()
        for stage in (3, 4, 6, 7, 8):
            self.assertIn(f'CubeBootTraceStage({stage})', app)


if __name__ == '__main__':
    unittest.main()
