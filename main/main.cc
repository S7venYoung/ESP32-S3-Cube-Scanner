#include <nvs_flash.h>

#include "cube_display.h"
#include "zmk_scanner.h"

extern "C" void app_main(void) {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);

    CubeDisplay display;
    display.Initialize();
    display.Show("ZMK BLE Scanner", "Starting scan...");
    StartZmkScanner(display);
}
