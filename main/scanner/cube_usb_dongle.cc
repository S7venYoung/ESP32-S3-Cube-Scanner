#include "cube_usb_dongle.h"
#include <esp_log.h>
#include <esp_mac.h>
#include <class/hid/hid_device.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <tinyusb.h>
#include <tinyusb_cdc_acm.h>
#include <tinyusb_default_config.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {
constexpr char kTag[] = "CubeUsbDongle";
// One shared HID map: ID1 boot-shaped keyboard, ID2 three independent media buttons.
const uint8_t report_map[] = {TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
                              0x05,
                              0x0c,
                              0x09,
                              0x01,
                              0xa1,
                              0x01,
                              0x85,
                              0x02,
                              0x15,
                              0x00,
                              0x25,
                              0x01,
                              0x09,
                              0xe9,
                              0x09,
                              0xea,
                              0x09,
                              0xe2,
                              0x75,
                              0x01,
                              0x95,
                              0x03,
                              0x81,
                              0x02,
                              0x75,
                              0x05,
                              0x95,
                              0x01,
                              0x81,
                              0x03,
                              0xc0};
const tusb_desc_device_t device = {.bLength = sizeof(tusb_desc_device_t),
                                   .bDescriptorType = TUSB_DESC_DEVICE,
                                   .bcdUSB = 0x0200,
                                   .bDeviceClass = TUSB_CLASS_MISC,
                                   .bDeviceSubClass = MISC_SUBCLASS_COMMON,
                                   .bDeviceProtocol = MISC_PROTOCOL_IAD,
                                   .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
                                   .idVendor = 0x303a,
                                   .idProduct = 0x4004,
                                   .bcdDevice = 0x0100,
                                   .iManufacturer = 1,
                                   .iProduct = 2,
                                   .iSerialNumber = 3,
                                   .bNumConfigurations = 1};
const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN, 0,
                          100),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64),
    TUD_HID_DESCRIPTOR(2, 5, HID_ITF_PROTOCOL_KEYBOARD, sizeof(report_map), 0x83, 16, 1)};
char serial_number[13];
const char language[] = {0x09, 0x04, 0};
const char* strings[] = {language,      "Xingzhi Cube",    "Cube ZMK Dongle Candidate",
                         serial_number, "Cube Codex Sync", "Cube Keyboard + Media"};
std::mutex mutex;
cube_dongle::ReportQueue<64> frames;
portMUX_TYPE cache_mux = portMUX_INITIALIZER_UNLOCKED;
cube_dongle::ReportFrame report_cache;
uint32_t revision = 0;
bool enabled = true;
std::atomic<bool> completed{false};
std::atomic<bool> failed{false};
std::atomic<uint32_t> link_epoch{0};
std::atomic<bool> neutral_delivered{false};
void OnUsbEvent(tinyusb_event_t*, void*) { ++link_epoch; }

void ReleaseLocked() {
    neutral_delivered = false;
    frames.ReleaseAndInhibit();
    ++revision;
    portENTER_CRITICAL(&cache_mux);
    report_cache = {};
    portEXIT_CRITICAL(&cache_mux);
}
void Worker(void*) {
    bool inflight = false;
    unsigned phase = 0;
    uint32_t ticket = 0, epoch = link_epoch.load();
    for (;;) {
        const uint32_t now_epoch = link_epoch.load();
        if (epoch != now_epoch) {
            std::lock_guard<std::mutex> lock(mutex);
            ReleaseLocked();
            inflight = false;
            phase = 0;
            epoch = now_epoch;
            completed = false;
        }
        if (failed.exchange(false)) {
            std::lock_guard<std::mutex> lock(mutex);
            ReleaseLocked();
            inflight = false;
            phase = 0;
        }
        if (inflight && completed.exchange(false)) {
            std::lock_guard<std::mutex> lock(mutex);
            inflight = false;
            if (ticket != revision)
                phase = 0;
            else if (++phase == 2) {
                if (frames.Front() && frames.Front()->Neutral())
                    neutral_delivered = true;
                frames.Pop();
                phase = 0;
            }
        }
        if (!inflight && tud_mounted() && !tud_suspended() && tud_hid_ready()) {
            std::lock_guard<std::mutex> lock(mutex);
            const auto* front = frames.Front();
            if (front) {
                ticket = revision;
                const bool boot = tud_hid_get_protocol() == HID_PROTOCOL_BOOT;
                if (phase == 1 && boot) {
                    if (front->Neutral())
                        neutral_delivered = true;
                    frames.Pop();
                    phase = 0;
                } else {
                    completed = false;
                    const uint8_t id = phase == 0 ? (boot ? 0 : 1) : 2;
                    const void* body = phase == 0 ? static_cast<const void*>(front->keyboard.data())
                                                  : &front->media;
                    inflight = tud_hid_report(id, body, phase == 0 ? 8 : 1);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
}  // namespace

extern "C" const uint8_t* tud_hid_descriptor_report_cb(uint8_t) { return report_map; }
extern "C" uint16_t tud_hid_get_report_cb(uint8_t, uint8_t id, hid_report_type_t type,
                                          uint8_t* buffer, uint16_t max) {
    if (type != HID_REPORT_TYPE_INPUT)
        return 0;
    cube_dongle::ReportFrame frame;
    portENTER_CRITICAL(&cache_mux);
    frame = report_cache;
    portEXIT_CRITICAL(&cache_mux);
    const uint16_t size = (id == 0 || id == 1) ? 8 : id == 2 ? 1 : 0;
    if (!size || max < size)
        return 0;
    std::memcpy(buffer, size == 8 ? static_cast<const void*>(frame.keyboard.data()) : &frame.media,
                size);
    return size;
}
extern "C" void tud_hid_set_report_cb(uint8_t, uint8_t, hid_report_type_t, const uint8_t*,
                                      uint16_t) {}
extern "C" void tud_hid_report_complete_cb(uint8_t, const uint8_t*, uint16_t) { completed = true; }
extern "C" void tud_hid_report_failed_cb(uint8_t, hid_report_type_t, const uint8_t*, uint16_t) {
    failed = true;
}
extern "C" void tud_hid_set_protocol_cb(uint8_t, uint8_t) { ++link_epoch; }

bool StartCubeUsbDongle() {
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
        return false;
    for (size_t i = 0; i < 6; ++i)
        std::snprintf(serial_number + i * 2, 3, "%02X", mac[i]);
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG(OnUsbEvent);
    cfg.descriptor.device = &device;
    cfg.descriptor.full_speed_config = configuration;
    cfg.descriptor.string = strings;
    cfg.descriptor.string_count = sizeof(strings) / sizeof(strings[0]);
    if (tinyusb_driver_install(&cfg) != ESP_OK)
        return false;
    tinyusb_config_cdcacm_t cdc{};
    cdc.cdc_port = TINYUSB_CDC_ACM_0;
    if (tinyusb_cdcacm_init(&cdc) != ESP_OK) {
        tinyusb_driver_uninstall();
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        ReleaseLocked();  // Initial enumeration must also wait for neutral input.
    }
    if (xTaskCreate(Worker, "cube-hid", 4096, nullptr, 5, nullptr) != pdPASS) {
        tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
        tinyusb_driver_uninstall();
        return false;
    }
    ESP_LOGW(kTag, "USB candidate: HID keyboard/media + CDC; hardware validation required");
    return true;
}
int CubeCdcRead(char* bytes, size_t size) {
    size_t count = 0;
    if (tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, reinterpret_cast<uint8_t*>(bytes), size, &count) !=
        ESP_OK)
        return 0;
    if (!count)
        vTaskDelay(pdMS_TO_TICKS(20));
    return count;
}
void CubeCdcWrite(const char* bytes, size_t size) {
    if (tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, reinterpret_cast<const uint8_t*>(bytes),
                                   size) != size)
        ESP_LOGW(kTag, "CDC reply buffer full");
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(200));
}
void CubeUsbSubmit(const cube_dongle::ReportFrame& frame) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!enabled)
        return;
    const bool previously_inhibited = frames.Inhibited();
    if (!frames.Push(frame) && !previously_inhibited) {
        ++revision;
        ESP_LOGW(kTag, "Report queue overflow: release and wait for neutral input");
    }
    portENTER_CRITICAL(&cache_mux);
    report_cache = frames.Latest();
    portEXIT_CRITICAL(&cache_mux);
}
void CubeUsbEnable(bool value) {
    std::lock_guard<std::mutex> lock(mutex);
    enabled = value;
    ReleaseLocked();
}
bool CubeUsbEnabled() {
    std::lock_guard<std::mutex> lock(mutex);
    return enabled;
}
void CubeUsbRelease() {
    std::lock_guard<std::mutex> lock(mutex);
    ReleaseLocked();
}
bool CubeUsbConnected() { return tud_mounted() && !tud_suspended(); }
bool CubeUsbAttached() { return tud_mounted(); }
bool CubeUsbNeutralDelivered() { return neutral_delivered.load(); }
const uint8_t* CubeUsbReportMap(size_t& size) {
    size = sizeof(report_map);
    return report_map;
}
