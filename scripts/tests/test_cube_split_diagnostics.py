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

    def test_drop_history_counts_recovery_and_reset_are_distinct(self):
        program = r'''
#include "split_diagnostics.h"
#include <cassert>
int main(){
 cube_dongle::SplitDiagnostics d;
 for(int i=0;i<63;++i)++d.attempts;
 for(int i=0;i<62;++i)d.ConnectFailed(50,525,1,1);
 assert(d.disconnects==0&&d.History()=="{}");
 d.Connected(100);d.Ready(150);d.Ready(160);d.Position(170);
 assert(d.ready_sessions==1&&d.ready_latency==50);
 assert(d.connect_failure_error==525&&d.connect_failures==62);
 d.Disconnected(300,520,8);
 assert(d.disconnects==1&&d.ready_disconnects==1);
 assert(d.History()=="{1:300:520:8:0}");
 d.Connected(600);d.Disconnected(650,520,2);
 assert(d.disconnects==2&&d.ready_disconnects==1);
 d.Connected(800);d.Ready(900);
 assert(d.reconnect_latency==600); // Whole outage, not last retry duration.
 assert(d.ready_latency==100);
 d.LocalClose(1000,8,2);d.Disconnected(1010,534,9);
 assert(d.local_closes==1&&d.History().find("3:1010:534:9:2")!=std::string::npos);
 d.HostReset(1100,7);
 assert(d.host_resets==1&&d.disconnects==3&&d.reset_reason==7);
 assert(!d.connected_now&&!d.ready_now);
 auto before=d.Text(1200);assert(before==d.Text(1200));
 assert(before.find("drop_age_ms=190")!=std::string::npos);
 assert(before.find("position_age_ms=1030")!=std::string::npos);
 d.LinkAlreadyGone();assert(d.missing_disconnects==1&&d.disconnects==3);
 // Fixed-size history retains the newest eight real disconnects only.
 for(int i=4;i<=12;++i){d.Connected(i*1000);d.Ready(i*1000+1);d.Disconnected(i*1000+2,520,8);}
 assert(d.History().find("{5:")==0);
 assert(d.History().find("12:12002:520:8:0")!=std::string::npos);
 assert(d.History().find("4:4002")==std::string::npos);
 cube_dongle::SplitDiagnostics wrap;
 wrap.Connected(0xfffffff0);wrap.Ready(0x10);assert(wrap.ready_latency==32);
 wrap.Disconnected(0xfffffff0,520,8);wrap.Connected(0);wrap.Ready(0x10);
 assert(wrap.reconnect_latency==32);
 assert(wrap.Text(0x10).find("drop_age_ms=32")!=std::string::npos);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'drops.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'drops')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I' + str(ROOT / 'experimental/cube_dongle'),
                            str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_events_are_recorded_without_changing_link_policy(self):
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        drop = source[source.index('} else if (event->type == BLE_GAP_EVENT_DISCONNECT'):]
        self.assertLess(drop.index('.Disconnected('), drop.index('peer.phase = Phase::Idle;'))
        self.assertIn('.Connected(esp_timer_get_time() / 1000)', source)
        self.assertIn('.Ready(esp_timer_get_time() / 1000)', source)
        self.assertIn('.HostReset(esp_timer_get_time() / 1000, reason)', source)
        self.assertIn('std::strcmp(reason, "handshake timeout") == 0 ? 2 : 1', source)
        self.assertIn('.LinkAlreadyGone();', source)
        self.assertIn('static_cast<unsigned>(peer.phase), 3)', source)
        self.assertIn('split_diagnostics[Side(peer)].ConnectFailed(', source)
        self.assertIn('split_diagnostics[cookie & 1].ConnectFailed(', source)


if __name__ == '__main__':
    unittest.main()
