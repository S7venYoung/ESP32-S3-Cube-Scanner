from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AudioDiagnosticsTests(unittest.TestCase):
    def test_actual_enable_function(self):
        source = (ROOT / 'main/audio/audio_service.cc').read_text()
        function = source[source.index('void AudioService::EnableWakeWordDetection('):source.index('void AudioService::ReleaseWakeWordResources(')]
        shim = r'''
#include <atomic>
#include <cassert>
#include <mutex>
#define CONFIG_CUBE_SPLIT_BENCH 1
#define CONFIG_IDF_TARGET_ESP32S3 1
#define ESP_LOGD(...) ((void)0)
constexpr unsigned AS_EVENT_WAKE_WORD_RUNNING=2;
void xEventGroupClearBits(unsigned* p,unsigned b){*p&=~b;}
void xEventGroupSetBits(unsigned* p,unsigned b){*p|=b;}
void esp_ae_rate_cvt_reset(void*){}
struct Engine{bool model=true,enabled=false;bool HasWakeWord(){return model;}
 void EnableWakeWordDetection(bool value){enabled=value;}};
struct AudioService{
 unsigned bits=0;unsigned* event_group_=&bits;
 bool initialized=true,audio_engine_initialized_=true;
 Engine engine;Engine* audio_engine_=&engine;
 void* input_resampler_=nullptr;std::mutex input_resampler_mutex_;
 std::atomic<int> wake_diagnostic_status_{0};
 bool InitializeAudioEngine(){return initialized;}
 void EnableWakeWordDetection(bool enable);
};
'''
        program = shim + function + r'''
int main(){
 AudioService s;s.initialized=false;s.EnableWakeWordDetection(true);
 assert(s.wake_diagnostic_status_==-1&&s.bits==0);
 s.initialized=true;s.engine.model=false;s.EnableWakeWordDetection(true);
 assert(s.wake_diagnostic_status_==-2&&s.bits==0);
 s.engine.model=true;s.EnableWakeWordDetection(true);
 assert(s.wake_diagnostic_status_==1&&s.bits==2&&s.engine.enabled);
 s.EnableWakeWordDetection(false);
 assert(s.wake_diagnostic_status_==0&&s.bits==0&&!s.engine.enabled);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'test.cc'
            path.write_text(program)
            binary = str(Path(tmp) / 'test')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path), '-o', binary], check=True)
            subprocess.run([binary], check=True)

    def test_observational_wiring(self):
        audio = (ROOT / 'main/audio/audio_service.cc').read_text()
        read = audio[audio.index('bool AudioService::ReadAudioData('):audio.index('void AudioService::AudioInputTask(')]
        self.assertEqual(read.count('++input_diagnostic_errors_;'), 2)
        self.assertEqual(read.count('++input_diagnostic_frames_;'), 1)
        sync = (ROOT / 'main/scanner/codex_sync.cc').read_text()
        command = sync.split('line == "DONGLE AUDIO DIAG"', 1)[1].split('#if CONFIG_CUBE_BLE_DONGLE_OUTPUT', 1)[0]
        self.assertIn('audio.IsWakeWordRunning()', command)
        self.assertNotIn('EnableWakeWordDetection(', command)


if __name__ == '__main__':
    unittest.main()
