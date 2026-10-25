from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HostEpochTests(unittest.TestCase):
    def test_delayed_connect_survives_failed_advertising_retries(self):
        program = r'''
#include <cassert>
#include "host_epoch.h"
int main(){
 // Reproduce the old rule: a retry invalidates a delayed successful CONNECT.
 uintptr_t old_epoch=1;const auto established=++old_epoch;
 ++old_epoch;assert(established!=old_epoch);
 cube_dongle::HostEpoch epoch;
 const auto advertising=epoch.Begin();
 assert(epoch.Accept(advertising)); // Callback may arrive before start returns.
 assert(epoch.Begin()==0); // Reentrant advertising is not allowed.
 epoch.Complete(0);assert(epoch.Accept(advertising));
 // SDK has already allocated the third connection, but is still obtaining
 // remote features. Timer retries advertising and receives ENOMEM.
 for(int i=0;i<100;++i){
  const auto retry=epoch.Begin();
  assert(retry==advertising&&epoch.Accept(advertising)&&epoch.Accept(retry));
  epoch.Complete(6);
  assert(epoch.Accept(advertising)&&epoch.Accept(retry));
 }
 // The original CONNECT (and subsequent encryption/disconnect) remains valid.
 assert(epoch.Accept(advertising));
 const auto replacement=epoch.Begin();epoch.Complete(0);
 assert(replacement==advertising&&epoch.Accept(advertising));
 epoch.Invalidate();assert(!epoch.Accept(replacement)&&!epoch.Accept(0));
 const auto after_reset=epoch.Begin();assert(after_reset!=replacement);
 epoch.Invalidate();epoch.Complete(0);assert(!epoch.Accept(after_reset));
 const auto failed=epoch.Begin();epoch.Complete(12);assert(!epoch.Accept(failed));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'epoch.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'epoch')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I',
                            str(ROOT / 'experimental/cube_dongle'), str(path), '-o', binary],
                           check=True)
            subprocess.run([binary], check=True)

    def test_delayed_connect_survives_successful_advertising_retries(self):
        program = r'''
#include <cassert>
#include "host_epoch.h"
int main(){
 cube_dongle::HostEpoch epoch;
 const auto connection_cookie=epoch.Begin();epoch.Complete(0);
 // With fewer than two halves connected the SDK still has room to advertise.
 // Repeated successful starts must not discard the allocated link's cookie.
 for(int i=0;i<100;++i){
  const auto retry=epoch.Begin();
  assert(retry==connection_cookie);
  assert(epoch.Accept(connection_cookie));
  epoch.Complete(0);
  assert(epoch.Accept(connection_cookie));
 }
 // Only host reset expires the cookie; delayed callbacks from that host must
 // never match a new host lifetime, including a reset inside adv_start.
 epoch.Invalidate();assert(!epoch.Accept(connection_cookie));
 const auto next=epoch.Begin();assert(next!=connection_cookie);
 epoch.Complete(0);assert(epoch.Accept(next)&&!epoch.Accept(connection_cookie));
 epoch.Begin();epoch.Invalidate();epoch.Complete(0);
 assert(!epoch.Accept(next));
 const auto failed=epoch.Begin();epoch.Complete(6);
 assert(!epoch.Accept(failed));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'epoch.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'epoch')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I',
                            str(ROOT / 'experimental/cube_dongle'), str(path), '-o', binary],
                           check=True)
            subprocess.run([binary], check=True)

    def test_production_commits_epoch_only_on_success(self):
        source = (ROOT / 'main/scanner/cube_ble_dongle.cc').read_text()
        advertise = source[source.index('void Advertise() {'):source.index('void SampleRadioPools() {')]
        self.assertIn('host_epoch.Pending()', advertise)
        self.assertLess(advertise.index('host_epoch.Begin()'), advertise.index('ble_gap_adv_start('))
        self.assertLess(advertise.index('ble_gap_adv_start('), advertise.index('host_epoch.Complete(rc)'))
        self.assertNotIn('++host_epoch', source)
        gap = source[source.index('int Gap(ble_gap_event* event'):source.index('enum Field :')]
        self.assertIn('host_epoch.Accept(reinterpret_cast<uintptr_t>(arg))', gap)
        self.assertLess(gap.index('if (connection != BLE_HS_CONN_HANDLE_NONE)'),
                        gap.index('connection = event->connect.conn_handle;'))
        self.assertIn('ble_gap_terminate(event->connect.conn_handle,', gap)
        reset = source[source.index('void CubeBleHostReset()'):source.index('void CubeDualTick()')]
        self.assertIn('host_epoch.Invalidate()', reset)


if __name__ == '__main__':
    unittest.main()
