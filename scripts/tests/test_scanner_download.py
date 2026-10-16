from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / 'main/boards/nologo/xingzhi-cube-1.54tft-wifi/xingzhi-cube-1.54tft-wifi.cc'


class ScannerDownload(unittest.TestCase):
    def test_button_routes_and_target_scope(self):
        code = BOARD.read_text()
        self.assertIn('kBootLongPressMs = 3000', code)
        self.assertIn('boot_button_(BOOT_BUTTON_GPIO, false, kBootLongPressMs)', code)
        double = code.split('boot_button_.OnDoubleClick', 1)[1].split('boot_button_.OnClick', 1)[0]
        self.assertIn('EnterWifiConfigMode()', double)
        long = code.split('boot_button_.OnLongPress', 1)[1].split('volume_up_button_', 1)[0]
        self.assertIn('#if CONFIG_ZMK_SCANNER_MODE', long)
        self.assertIn('EnterDownloadMode()', long)
        self.assertIn('#else', long)
        self.assertIn('EnterWifiConfigMode()', long)
        self.assertNotIn('CubeUsbEnterDownload', code)

    def test_exact_download_method_with_sdk_mocks(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        code = BOARD.read_text()
        method = code.split('static constexpr uint16_t kBootLongPressMs = 3000;', 1)[1].split('#else', 1)[0]
        prefix = r'''
#include <cassert>
#include <cstdint>
#include <functional>
#include <vector>
using esp_err_t = int;
constexpr int ESP_OK = 0;
using esp_timer_handle_t = void*;
struct esp_timer_create_args_t { void (*callback)(void*); const char* name; };
int failure=0, creates=0, deletes=0, registered=0, unregistered=0, restarts=0;
int RTC_CNTL_OPTION1_REG=0;
constexpr int RTC_CNTL_FORCE_DOWNLOAD_BOOT=1;
void (*shutdown_fn)() = nullptr;
void (*timer_fn)(void*) = nullptr;
#define TAG "mock"
#define ESP_LOGE(...) ((void)0)
#define SET_PERI_REG_MASK(reg, mask) ((reg) |= (mask))
const char* esp_err_to_name(int) { return "mock"; }
int esp_timer_create(const esp_timer_create_args_t* args, void** handle) {
    ++creates;
    if(failure==1) return -1;
    timer_fn=args->callback; *handle=reinterpret_cast<void*>(1); return ESP_OK;
}
int esp_timer_delete(void*) { ++deletes; return ESP_OK; }
int esp_register_shutdown_handler(void (*fn)()) {
    if(failure==2) return -1;
    ++registered; shutdown_fn=fn; return ESP_OK;
}
int esp_unregister_shutdown_handler(void (*)()) { ++unregistered; shutdown_fn=nullptr; return ESP_OK; }
int esp_timer_start_once(void*, uint64_t us) { assert(us==600000); return failure==3?-1:ESP_OK; }
void esp_restart() { ++restarts; if(shutdown_fn) shutdown_fn(); }
struct Application {
    std::vector<std::function<void()>> queue;
    static Application& GetInstance() { static Application app; return app; }
    void Schedule(std::function<void()> fn) { queue.push_back(fn); }
};
struct Display { void ShowNotification(const char*, int=3000) {} };
struct Rig { Display display; Display* GetDisplay() { return &display; }
'''
        suffix = r'''
};
int main() {
    Rig ok;
    ok.EnterDownloadMode(); ok.EnterDownloadMode();
    assert(creates==1 && registered==1 && !restarts && !RTC_CNTL_OPTION1_REG);
    timer_fn(nullptr);
    assert(!restarts && Application::GetInstance().queue.size()==1);
    Application::GetInstance().queue[0]();
    assert(restarts==1 && RTC_CNTL_OPTION1_REG==RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    for(int mode=1;mode<=3;++mode) {
        failure=mode; creates=deletes=registered=unregistered=0;
        Rig bad; bad.EnterDownloadMode();
        assert(!bad.download_timer_ && creates==1);
        assert(deletes==(mode==1?0:1));
        assert(unregistered==(mode==3?1:0));
        failure=0; bad.EnterDownloadMode(); // Every failure permits retry.
        assert(bad.download_timer_ && creates==2);
    }
}
'''
        with tempfile.TemporaryDirectory(prefix='scanner-download-test-') as tmp:
            exe = str(Path(tmp) / 'test')
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-x', 'c++', '-', '-o', exe], input=prefix + method + suffix,
                           text=True, check=True, capture_output=True)
            subprocess.run([exe], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
