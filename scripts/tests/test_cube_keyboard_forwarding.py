import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class CubeKeyboardForwardingTests(unittest.TestCase):
    def test_wire_and_freshness(self):
        with tempfile.TemporaryDirectory() as d:
            binary = str(pathlib.Path(d) / 'test')
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'experimental/cube_dongle'),
                str(ROOT/'experimental/cube_dongle/keyboard_dashboard_test.cc'),'-o',binary],check=True)
            subprocess.run([binary],check=True)

    def test_production_scheduler_on_host(self):
        source=(ROOT/'main/scanner/cube_split_bench.cc').read_text()
        start = source.index('void PublishKeyboardDashboard()')
        # Stop at this top-level function's closing brace, not the next named
        # function: unrelated helpers may legitimately be inserted between them.
        end = source.index('\n}', start) + 2
        production = source[start:end]
        self.assertNotIn('PumpPeerSecurity', production)
        self.assertTrue(production.rstrip().endswith('}'))
        program=r'''
#include <cassert>
#include <mutex>
#include <deque>
#include <vector>
#include <string>
#include <atomic>
#include "keyboard_dashboard.h"
#define CONFIG_CUBE_BLE_DONGLE_OUTPUT 1
#define CONFIG_CUBE_USB_DONGLE_OUTPUT 1
std::mutex relay_jobs_mutex,dashboard_mutex;
std::deque<int> relay_jobs;
std::atomic<unsigned> relay_ready{3};
cube_dongle::KeyboardDashboard dashboard;
struct {unsigned layer=0;unsigned ActiveLayer(){return layer;}} keyboard;
uint64_t now=0;
uint64_t esp_timer_get_time(){return now*1000;}
bool enabled=true,ble=false,ble_ready=false,usb=true;
bool CubeDualEnabled(){return enabled;}
bool CubeDualUsesBle(){return ble;}
bool CubeDualBleReady(){return ble_ready;}
int CubeDualProfile(){return 4;}
bool CubeUsbConnected(){return usb;}
bool CubeUsbEnabled(){return enabled;}
struct Sent {unsigned side;std::string name;std::vector<uint8_t> data;};
std::vector<Sent> sent;
bool CubeSplitQueueRelay(unsigned side,const std::string& name,const std::vector<uint8_t>& data){sent.push_back({side,name,data});return true;}
''' + production + r'''
int main(){
 PublishKeyboardDashboard();assert(sent.size()==3);
 assert(sent[0].side==1&&sent[0].name=="vlcat"&&sent[0].data[1]==0);
 assert(sent[2].side==2&&sent[2].name=="vldash"&&sent[2].data[2]==(CONFIG_CUBE_USB_DONGLE_OUTPUT?1:0));
 sent.clear();now=500;PublishKeyboardDashboard();assert(sent.empty());
 now=1000;relay_jobs.push_back(1);PublishKeyboardDashboard();assert(sent.empty());
 relay_jobs.clear();relay_ready=2;keyboard.layer=3;ble=ble_ready=true;
 PublishKeyboardDashboard();assert(sent.size()==2);
 assert(sent[0].side==2&&sent[0].data[1]==3);
 assert(sent[1].data[2]==(CONFIG_CUBE_BLE_DONGLE_OUTPUT?2:CONFIG_CUBE_USB_DONGLE_OUTPUT?1:0));
 assert(sent[1].data[3]==(CONFIG_CUBE_BLE_DONGLE_OUTPUT?5:0));
 sent.clear();now=2000;enabled=false;PublishKeyboardDashboard();
 assert(sent.size()==2&&sent[1].data[2]==0&&sent[1].data[3]==0);
 sent.clear();now=3000;relay_ready=0;PublishKeyboardDashboard();assert(sent.empty());
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            source=pathlib.Path(d)/'test.cc';binary=str(pathlib.Path(d)/'test')
            for ble,usb in [(1,1),(0,1),(0,0)]:
                case=program.replace('#define CONFIG_CUBE_BLE_DONGLE_OUTPUT 1',f'#define CONFIG_CUBE_BLE_DONGLE_OUTPUT {ble}').replace('#define CONFIG_CUBE_USB_DONGLE_OUTPUT 1',f'#define CONFIG_CUBE_USB_DONGLE_OUTPUT {usb}')
                source.write_text(case)
                subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                    '-I'+str(ROOT/'experimental/cube_dongle'),str(source),'-o',binary],check=True)
                subprocess.run([binary],check=True)

    def test_usb_wifi_and_timer_are_wired(self):
        sync=(ROOT/'main/scanner/codex_sync.cc').read_text()
        self.assertIn('CubeSplitUpdateHostMetrics(data.data(), data.size())',sync)
        self.assertIn('if (!Apply(body, false))',sync)
        self.assertIn('line.rfind("HOST1 ", 0) == 0',sync)
        split=(ROOT/'main/scanner/cube_split_bench.cc').read_text()
        timer=split[split.index('void TelemetryTick('):split.index('bool SaveAutoBinding(')]
        self.assertIn('PublishKeyboardDashboard();',timer)
        self.assertIn('PumpRelayJobs();',timer)

if __name__=='__main__':unittest.main()
