from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class TapDanceIntegration(unittest.TestCase):
    def test_production_filter_preserves_pulse_order_and_modifiers(self):
        text=(ROOT/'main/scanner/cube_studio.cc').read_text()
        fn=text[text.index('wire::ComboResult CubeStudioFilterPositions('):text.index('bool CubeStudioCombosNeedTick()')]
        program=r'''
#include <cassert>
#include <atomic>
#include <mutex>
#include "experimental/cube_dongle/tapdance_engine.h"
#include "experimental/cube_dongle/runtime_macros.h"
namespace wire=cube_dongle::studio;
using namespace cube_dongle;
using namespace cube_dongle::studio;
std::mutex combo_mutex,tapdance_mutex,tapdance_output_mutex,macro_mutex;
ComboEngine combo_engine;ComboModel combo_cache;unsigned combo_revision=1,combo_seen=0;
TapDanceEngine tapdance_engine;DanceModel tapdance_cache;
ReportFrame tapdance_output;std::atomic<unsigned> tapping_term{200};
ReportFrame TapDanceOutput(){std::lock_guard<std::mutex> lock(tapdance_output_mutex);return tapdance_output;}
struct {uint8_t Layers(uint32_t){return 0;}} pointer_engine;
struct {ReportFrame report;uint8_t layers=0;} sensor_output;
MacroPlayer macro_player;MacroModel macro_cache;
uint64_t current_time=0;uint64_t esp_timer_get_time(){return current_time*1000;}
''' + fn + r'''
int main(){
 KeyboardState<61,4>::Keymap map{};map[0][0]={Kind::RuntimeTapDance,0,0};map[0][1]={Kind::Key,0x1b,0};
 map[0][2]={Kind::Key,0xe1,0};map[1][1]={Kind::Key,0x1c,0};
 KeyboardState<61,4> keyboard(map);tapdance_cache.next_id=1;
 auto key=[](unsigned u){return DanceAction{kKp,0x70000|u,0};};
 tapdance_cache.entries[0]={"ctrl",{key(0xe0),key(5),{}}};
 SplitState::Snapshot raw{};ComboResult result;
 auto step=[&](unsigned now){current_time=now;result=CubeStudioFilterPositions(raw,keyboard);keyboard.SetExternalLayers(result.layers);keyboard.Apply(result.filtered,now);return MergeReports({keyboard.Report(),keyboard.MediaReport()},result.report);};
 raw[0]=1;step(0);raw[0]=0;step(10);raw[0]=2;auto final=step(50);
 assert(result.prefix.size()==2&&result.prefix[0].keyboard[0]==1&&result.prefix[1].keyboard[0]==0);
 assert(!result.prefix[0].keyboard[2]&&!result.prefix[1].keyboard[2]);
 assert(final.keyboard[0]==0&&final.keyboard[2]==0x1b); // No accidental Ctrl+X.
 raw[0]=0;step(60);
 tapdance_cache.entries[0].actions={key(4),key(5),{}};
 raw[0]=1;step(100);raw[0]=0;step(110);step(300);
 assert(result.prefix.size()==2&&result.prefix[0].keyboard[2]==4&&!result.prefix[1].keyboard[2]);
 // A shift released in this frame is absent from a deferred tap's pulse base.
 raw[0]=5;step(400);raw[0]=4;step(410);raw[0]=2;final=step(450);
 assert(result.prefix.size()==2&&result.prefix[0].keyboard[0]==0&&final.keyboard[0]==0);
 raw[0]=0;step(460);
 tapdance_cache.entries[0].actions={DanceAction{kModTap,0x700e0,0x70004},{},{}};
 raw[0]=1;step(500);raw[0]=3;final=step(550);
 assert(final.keyboard[0]==1&&final.keyboard[2]==0x1b); // Nested mod-tap holds on interrupt.
 raw[0]=0;step(560);
 tapdance_cache.entries[0].actions={DanceAction{kLayerTap,1,0x70004},{},{}};
 raw[0]=1;step(600);raw[0]=3;final=step(650);
 assert(keyboard.ActiveLayer()==1&&final.keyboard[2]==0x1c);
 raw[0]=0;step(660);assert(keyboard.ActiveLayer()==0);
 raw[0]=1;step(700);raw[0]=0;step(710);
 assert(result.prefix.size()==2&&result.prefix[0].keyboard[2]==4&&!result.prefix[1].keyboard[2]);
 assert(keyboard.ActiveLayer()==0); // Short layer-tap emits A, not a transient layer leak.
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'test.cc';src.write_text(program);binary=str(Path(d)/'test')
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT),str(src),'-o',binary],check=True)
            subprocess.run([binary],check=True)
    def test_registration_persistence_and_usb_only_timers(self):
        text=(ROOT/'main/scanner/cube_studio.cc').read_text()
        for token in ['"cormoran__runtime_tapdance"','"tapdance_v1"','tapdance_cache = tapdances.model','tapdance_engine.Cancel()']:
            self.assertIn(token,text)
        split=(ROOT/'main/scanner/cube_split_bench.cc').read_text()
        self.assertIn('CubeDualSubmit(prefix, 0)',split)
        self.assertIn('CubeUsbSubmit(prefix)',split)
        self.assertIn('InputTimer',split)
        self.assertIn('ble_npl_callout_stop(&input_timer)',split)
        reset=split[split.index('void OnReset('):split.index('void Host(')]
        self.assertIn('CubeStudioCancelMacros()',reset)
if __name__=='__main__':unittest.main()
