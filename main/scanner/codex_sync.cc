#include "codex_metrics.h"
#if CONFIG_CUBE_STUDIO_RPC
#include "cube_studio.h"
#endif
#if CONFIG_CUBE_USB_DONGLE_OUTPUT
#include "cube_usb_dongle.h"
#if CONFIG_CUBE_BLE_DONGLE_OUTPUT
#include "cube_ble_dongle.h"
#endif
#endif
#if CONFIG_CUBE_SPLIT_BENCH
#include "cube_split_bench.h"
#endif
#include <driver/uart.h>
#include "settings.h"
#if CONFIG_BOARD_TYPE_XINGZHI_CUBE_1_54TFT_WIFI
#include <driver/usb_serial_jtag.h>
#endif
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <string>

namespace {
std::mutex mutex;
struct Sample {
    CodexMetrics metrics;
    int64_t at = -1;
} usb, wifi;
std::string token;
httpd_handle_t server = nullptr;
bool uart_ready = false;

bool Fresh(const Sample& s, int64_t now) {
    return s.at >= 0 && now - s.at < static_cast<int64_t>(s.metrics.ttl_seconds) * 1000000;
}

bool Apply(const char* line, bool is_usb) {
    CodexMetrics metrics;
    if (!ParseCodexFrame(line, metrics))
        return false;
    std::lock_guard<std::mutex> lock(mutex);
    auto& sample = is_usb ? usb : wifi;
    sample.metrics = metrics;
    sample.at = esp_timer_get_time();
    return true;
}

std::string IpAddress() {
    auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {};
    if (netif == nullptr || esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0)
        return "0.0.0.0";
    char text[20];
    std::snprintf(text, sizeof(text), IPSTR, IP2STR(&ip.ip));
    return text;
}

esp_err_t Info(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "CUBE-CODEX/2\n");
}

esp_err_t Update(httpd_req_t* req) {
    char authorization[64] = {};
    if (httpd_req_get_hdr_value_str(req, "X-Cube-Token", authorization, sizeof(authorization)) !=
            ESP_OK ||
        token != authorization) {
        httpd_resp_set_status(req, "403 Forbidden");
        return httpd_resp_sendstr(req, "DENIED\n");
    }
    if (req->content_len <= 0 || req->content_len >= 160) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid frame length");
    }
    char body[160] = {};
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0)
            return ESP_FAIL;
        received += n;
    }
    while (received > 0 && (body[received - 1] == '\n' || body[received - 1] == '\r'))
        body[--received] = 0;
    if (!Apply(body, false))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid CODEX frame");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "OK\n");
}

void Reply(const std::string& line) {
    // Leading newline separates replies from the existing diagnostic console.
    const std::string output = "\n" + line + "\n";
#if CONFIG_CUBE_USB_DONGLE_OUTPUT
    CubeCdcWrite(output.data(), output.size());
#elif CONFIG_BOARD_TYPE_XINGZHI_CUBE_1_54TFT_WIFI
    usb_serial_jtag_write_bytes(output.data(), output.size(), pdMS_TO_TICKS(200));
#else
    uart_write_bytes(UART_NUM_0, output.data(), output.size());
#endif
}

void SyncTask(void*) {
    std::string line;
    bool overflow = false;
    char bytes[64];
    for (;;) {
        // Always block for at least one tick, even on transport errors or a
        // continuously full RX queue. Let idle/Wi-Fi/audio tasks run.
        vTaskDelay(1);
#if CONFIG_CUBE_STUDIO_RPC
        CubeStudioPoll();
        uint8_t studio_bytes[256];
        uint32_t studio_epoch;
        const size_t studio_count =
            CubeBleStudioRead(studio_bytes, sizeof(studio_bytes), studio_epoch);
        for (size_t i = 0; i < studio_count; ++i)
            CubeStudioFeedBle(studio_bytes[i], studio_epoch);
#endif
        if (server == nullptr && IpAddress() != "0.0.0.0") {
            httpd_config_t config = HTTPD_DEFAULT_CONFIG();
            config.server_port = 8765;
            config.ctrl_port = 32769;
            config.max_uri_handlers = 2;
            config.stack_size = 4096;
            if (httpd_start(&server, &config) == ESP_OK) {
                httpd_uri_t info = {};
                info.uri = "/v1/info";
                info.method = HTTP_GET;
                info.handler = Info;
                httpd_register_uri_handler(server, &info);
                httpd_uri_t update = {};
                update.uri = "/v1/codex";
                update.method = HTTP_POST;
                update.handler = Update;
                httpd_register_uri_handler(server, &update);
            }
        }
        int count = 0;
        if (uart_ready) {
#if CONFIG_CUBE_USB_DONGLE_OUTPUT
            count = CubeCdcRead(bytes, sizeof(bytes));
#elif CONFIG_BOARD_TYPE_XINGZHI_CUBE_1_54TFT_WIFI
            count = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), pdMS_TO_TICKS(200));
#else
            count = uart_read_bytes(UART_NUM_0, bytes, sizeof(bytes), pdMS_TO_TICKS(200));
#endif
        } else
            vTaskDelay(pdMS_TO_TICKS(200));
        for (int i = 0; i < count; ++i) {
            const char c = bytes[i];
#if CONFIG_CUBE_STUDIO_RPC
            if (CubeStudioFeed(static_cast<uint8_t>(c))) {
                line.clear();
                overflow = false;
                continue;
            }
#endif
            if (c == '\n') {
                if (!overflow) {
                    if (line == "PING")
                        Reply("PROSPECTOR-SCANNER/1");
                    else if (line == "CAPS")
                        Reply("CUBE-CODEX/2");
                    else if (line == "PAIR")
                        Reply("CUBE-PAIR " + IpAddress() + " " + token);
#if CONFIG_CUBE_STUDIO_RPC
                    else if (line == "DONGLE DIAG")
                        Reply("CUBE-DIAG/1 reset=" + std::to_string(unsigned(esp_reset_reason())) +
                              " uptime_ms=" + std::to_string(esp_timer_get_time() / 1000) +
                              " heap_free=" + std::to_string(esp_get_free_heap_size()) +
                              " heap_min=" + std::to_string(esp_get_minimum_free_heap_size()) +
                              " sync_stack_free=" +
                              std::to_string(uxTaskGetStackHighWaterMark(nullptr)));
#endif
#if CONFIG_CUBE_SPLIT_BENCH
#if CONFIG_CUBE_BLE_DONGLE_OUTPUT
                    else if (line == "DONGLE")
                        Reply("CUBE-DUAL-CANDIDATE/1 " + std::to_string(CubeSplitBenchStatus()) +
                              (CubeDualEnabled() ? " HID-ON" : " HID-OFF"));
                    else if (line == "DONGLE HOST" || line.rfind("DONGLE BT ", 0) == 0 ||
                             line.rfind("DONGLE OUTPUT ", 0) == 0 || line == "DONGLE ENABLE" ||
                             line == "DONGLE DISABLE")
                        Reply(CubeDualCommand(line));
#endif
#if CONFIG_CUBE_USB_DONGLE_OUTPUT
                    else if (line == "DONGLE")
                        Reply("CUBE-USB-CANDIDATE/1 " + std::to_string(CubeSplitBenchStatus()) +
                              (CubeUsbEnabled() ? " HID-ON" : " HID-OFF"));
#else
                    else if (line == "DONGLE")
                        Reply("CUBE-SPLIT-BENCH/1 " + std::to_string(CubeSplitBenchStatus()) +
                              " HID-OFF");
#endif
                    else if (line.rfind("DONGLE BIND ", 0) == 0)
                        Reply(CubeSplitBenchBind(line));
#if CONFIG_CUBE_USB_DONGLE_OUTPUT && !CONFIG_CUBE_BLE_DONGLE_OUTPUT
                    else if (line == "DONGLE ENABLE") {
                        CubeUsbEnable(true);
                        Reply("CUBE-USB-CANDIDATE ENABLED");
                    } else if (line == "DONGLE DISABLE") {
                        CubeUsbEnable(false);
                        Reply("CUBE-USB-CANDIDATE DISABLED");
                    }
#endif
#endif
                    else if (line.rfind("CODEX", 0) == 0)
                        Reply(Apply(line.c_str(), true) ? "OK" : "ERR");
                }
                line.clear();
                overflow = false;
            } else if (c != '\r') {
                if (line.size() < 159 && !overflow)
                    line += c;
                else
                    overflow = true;
            }
        }
    }
}
}  // namespace

CodexSnapshot GetCodexSnapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    const int64_t now = esp_timer_get_time();
    const Sample* sample = nullptr;
    CodexSnapshot snapshot;
    // UART bridges cannot reliably report cable removal: USB priority is a
    // 75-second lease refreshed by successful data packets, not by PING.
    if (Fresh(usb, now) && now - usb.at < 75000000) {
        sample = &usb;
        snapshot.transport = "USB";
    } else if (Fresh(wifi, now)) {
        sample = &wifi;
        snapshot.transport = "WI-FI";
    } else if (Fresh(usb, now)) {
        sample = &usb;
        snapshot.transport = "USB";
    }
    if (sample != nullptr) {
        snapshot.metrics = sample->metrics;
        snapshot.online = true;
        if (sample->metrics.age_seconds + (now - sample->at) / 1000000 > 900) {
            snapshot.metrics.left = snapshot.metrics.week_left = -1;
        }
    }
    return snapshot;
}

void StartCodexSync() {
    // Netif lookup performs a TCP/IP IPC call. Initialize it before starting
    // our task; native Wi-Fi initialization is idempotent and can reuse it.
    if (esp_netif_init() != ESP_OK) {
        ESP_LOGE("CodexSync", "Network stack initialization failed");
        return;
    }
    Settings settings("codex", true);
    token = settings.GetString("token");
    if (token.size() != 32) {
        uint8_t random[16];
        esp_fill_random(random, sizeof(random));
        char hex[33];
        for (int i = 0; i < 16; ++i)
            std::snprintf(hex + i * 2, 3, "%02x", random[i]);
        token = hex;
        settings.SetString("token", token);
    }
#if CONFIG_CUBE_USB_DONGLE_OUTPUT
#if CONFIG_CUBE_STUDIO_RPC
    CubeStudioInitialize();
#endif
    uart_ready = StartCubeUsbDongle();
    if (!uart_ready)
        ESP_LOGE("CodexSync", "USB HID/CDC initialization failed");
#elif CONFIG_BOARD_TYPE_XINGZHI_CUBE_1_54TFT_WIFI
    // Xingzhi's USB connector is wired to the S3 native USB Serial/JTAG.
    // Keep diagnostic logging on UART0 so logs cannot consume sync requests.
    usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_config.rx_buffer_size = 1024;
    usb_config.tx_buffer_size = 1024;
    if (usb_serial_jtag_is_driver_installed() ||
        usb_serial_jtag_driver_install(&usb_config) == ESP_OK) {
        uart_ready = true;
    } else {
        ESP_LOGE("CodexSync", "USB Serial/JTAG initialization failed");
    }
#else
    // Zhengchen uses GPIO20 for backlight: its UART transport stays unchanged.
    uart_config_t config = {};
    config.baud_rate = 115200;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    if (uart_param_config(UART_NUM_0, &config) != ESP_OK ||
        uart_set_pin(UART_NUM_0, 43, 44, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
        (!uart_is_driver_installed(UART_NUM_0) &&
         uart_driver_install(UART_NUM_0, 1024, 0, 0, nullptr, 0) != ESP_OK)) {
        ESP_LOGE("CodexSync", "UART0 initialization failed");
    } else
        uart_ready = true;
#endif
#if CONFIG_CUBE_STUDIO_RPC
    constexpr uint32_t sync_stack = 12288;  // Bounded nested DYA metadata/relay protobuf decoding.
#else
    constexpr uint32_t sync_stack = 4096;
#endif
    if (xTaskCreate(SyncTask, "codex_sync", sync_stack, nullptr, 2, nullptr) != pdPASS) {
        ESP_LOGE("CodexSync", "Unable to start sync task");
    }
}
