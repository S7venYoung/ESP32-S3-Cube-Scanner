#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#if CONFIG_CUBE_SPLIT_BENCH
#include <esp_attr.h>
#include <esp_system.h>
#include "scanner/cube_boot_trace.h"
#include "../experimental/cube_dongle/boot_trace.h"
namespace {
RTC_NOINIT_ATTR cube_dongle::BootTrace cube_boot_trace;
portMUX_TYPE boot_trace_lock = portMUX_INITIALIZER_UNLOCKED;
unsigned previous_boot_stage = 0, boot_sequence = 1;
}
void CubeBootTraceStart(bool retain) {
    previous_boot_stage = 0;
    boot_sequence = 1;
    if (retain && cube_boot_trace.Valid()) {
        previous_boot_stage = cube_boot_trace.stage;
        boot_sequence = cube_boot_trace.boot + 1;
    }
    cube_boot_trace.Set(boot_sequence, 0);
}
void CubeBootTraceStage(unsigned stage) {
    portENTER_CRITICAL(&boot_trace_lock);
    cube_boot_trace.Set(boot_sequence, stage);
    portEXIT_CRITICAL(&boot_trace_lock);
}
std::string CubeBootTraceStatus() {
    unsigned stage;
    portENTER_CRITICAL(&boot_trace_lock);
    stage = cube_boot_trace.stage;
    portEXIT_CRITICAL(&boot_trace_lock);
    return "CUBE-BOOT/1 sequence=" + std::to_string(boot_sequence) +
           " previous_stage=" + std::to_string(previous_boot_stage) +
           " stage=" + std::to_string(stage);
}
#endif

#define TAG "main"

extern "C" void app_main(void)
{
#if CONFIG_CUBE_SPLIT_BENCH
    const auto reset = esp_reset_reason();
    CubeBootTraceStart(reset != ESP_RST_UNKNOWN && reset != ESP_RST_POWERON && reset != ESP_RST_BROWNOUT);
    CubeBootTraceStage(1);  // NVS init.
#endif
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize and run the application
#if CONFIG_CUBE_SPLIT_BENCH
    CubeBootTraceStage(2);  // Board/display/audio and application initialization.
#endif
    auto& app = Application::GetInstance();
    app.Initialize();
#if CONFIG_CUBE_SPLIT_BENCH
    CubeBootTraceStage(5);  // Main event loop ready.
#endif
    app.Run();  // This function runs the main event loop and never returns
}
