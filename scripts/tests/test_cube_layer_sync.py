from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LayerSyncTests(unittest.TestCase):
    def test_latest_state_heartbeat_and_reconnect(self):
        program = r'''
#include <cassert>
#include "layer_sync.h"
int main(){
 cube_dongle::LayerSync sync;
 cube_dongle::LayerSync::Update u;
 sync.Observe(0,true,1,0,0);sync.Observe(1,true,2,0,0);
 assert(sync.Take(u)&&u.side==0&&u.layer==0&&u.generation==1);
 assert(sync.Take(u)&&u.side==1&&u.layer==0);
 assert(!sync.Take(u));
 // Layer change bypasses the 1s heartbeat and full ordinary RPC queue.
 sync.Observe(0,true,1,1,5);assert(sync.Take(u)&&u.layer==1);
 sync.Observe(0,true,1,1,10);assert(!sync.Take(u));
 // A rapid release replaces an unsent layer: never replay stale transitions.
 sync.Observe(0,true,1,2,15);sync.Observe(0,true,1,0,20);
 assert(sync.Take(u)&&u.layer==0);assert(!sync.Take(u));
 sync.Observe(0,true,1,0,1019);assert(!sync.Take(u));
 sync.Observe(0,true,1,0,1020);assert(sync.Take(u)&&u.layer==0);
 // Reconnect resends even the same layer, with the new connection generation.
 sync.Observe(0,true,3,0,1021);assert(sync.Take(u)&&u.generation==3);
 sync.Observe(0,true,3,1,1022);sync.Observe(0,false,3,1,1023);
 assert(!sync.Take(u));
 sync.Observe(0,true,3,1,1024);assert(sync.Take(u)&&u.layer==1);
 sync.Observe(1,true,4,255,1025);assert(!sync.Take(u));
 // Round robin prevents one rapidly changing half from starving the other.
 for(int i=0;i<1000;++i){
  sync.Observe(0,true,3,uint8_t(i%3),2000+i);
  sync.Observe(1,true,4,uint8_t(i%3),2000+i);
 }
 assert(sync.Take(u)&&u.side==1&&u.layer==0);
 assert(sync.Take(u)&&u.side==0&&u.layer==0);assert(!sync.Take(u));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'layer.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'layer')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I',
                            str(ROOT / 'experimental/cube_dongle'), str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_production_scheduling_is_separate_and_fair(self):
        source = (ROOT / 'main/scanner/cube_split_bench.cc').read_text()
        dashboard = source[source.index('void PublishKeyboardDashboard() {'):
                           source.index('void Discover(Peer& peer);')]
        self.assertNotIn('"vlcat"', dashboard)
        tick = source[source.index('void CubeSplitInputTick() {'):
                      source.index('void CubeSplitCancelHoldTaps()')]
        self.assertLess(tick.index('PublishKeyboardLayers();'), tick.index('PumpRelayJobs();'))
        keyboard = source[source.index('void PublishKeyboard() {'):
                          source.index('ble_npl_callout retry;')]
        self.assertLess(keyboard.index('publish(filtered, extra_actions);'),
                        keyboard.index('PublishKeyboardLayers();'))
        pump = source[source.index('void PumpRelayJobs() {'):source.index('void Fail(Peer& peer,')]
        self.assertIn('prefer_layer || relay_jobs.empty()', pump)
        self.assertIn('prefer_layer = false;', pump)
        self.assertIn('prefer_layer = true;', pump)
        self.assertIn('if (chunks.empty())', pump)
        self.assertIn('peer.generation != job.generation', pump)


if __name__ == '__main__':
    unittest.main()
