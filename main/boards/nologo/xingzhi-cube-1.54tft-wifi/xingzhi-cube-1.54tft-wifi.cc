#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "led/single_led.h"
#include "power_manager.h"
#include "power_save_timer.h"
#include "system_reset.h"
#include "wifi_board.h"
#if CONFIG_ZMK_SCANNER_MODE
#include <esp_system.h>
#include <esp_timer.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>
#include "scanner/codex_scanner_display.h"
#include "scanner/zmk_scanner.h"
void StartCodexSync();
using BoardDisplay = CodexScannerDisplay;
#else
using BoardDisplay = SpiLcdDisplay;
#endif

#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>

#include <driver/rtc_io.h>
#include <esp_sleep.h>

#define TAG "XINGZHI_CUBE_1_54TFT_WIFI"

class XINGZHI_CUBE_1_54TFT_WIFI : public WifiBoard {
private:
#if CONFIG_ZMK_SCANNER_MODE
    static constexpr uint16_t kBootLongPressMs = 3000;
    esp_timer_handle_t download_timer_ = nullptr;
    static void DownloadShutdown() {
        // Scanner already uses hardware Serial/JTAG, not TinyUSB OTG.
        SET_PERI_REG_MASK(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    }
    void EnterDownloadMode() {
        if (download_timer_)
            return;
        esp_timer_create_args_t args = {};
        args.callback = [](void*) { Application::GetInstance().Schedule([]() { esp_restart(); }); };
        args.name = "scanner_download";
        esp_err_t err = esp_timer_create(&args, &download_timer_);
        if (err == ESP_OK) {
            err = esp_register_shutdown_handler(DownloadShutdown);
            if (err == ESP_OK) {
                // Allow notice rendering and BOOT release without blocking.
                err = esp_timer_start_once(download_timer_, 600000);
                if (err != ESP_OK)
                    esp_unregister_shutdown_handler(DownloadShutdown);
            }
        }
        if (err != ESP_OK) {
            if (download_timer_)
                esp_timer_delete(download_timer_);
            download_timer_ = nullptr;
            ESP_LOGE(TAG, "Cannot request ROM download: %s", esp_err_to_name(err));
            GetDisplay()->ShowNotification("下载模式启动失败，请手动进入 BOOT");
            return;
        }
        GetDisplay()->ShowNotification("进入下载模式，请松开 BOOT 并刷新串口", 10000);
    }
#else
    static constexpr uint16_t kBootLongPressMs = 0;
#endif
    Button boot_button_;
    Button volume_up_button_;
    Button volume_down_button_;
    BoardDisplay* display_ = nullptr;
    PowerSaveTimer* power_save_timer_ = nullptr;
    PowerManager* power_manager_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    void InitializePowerManager() {
        power_manager_ = new PowerManager(GPIO_NUM_38);
        power_manager_->OnChargingStatusChanged([this](bool is_charging) {
            if (is_charging) {
                power_save_timer_->SetEnabled(false);
            } else {
                power_save_timer_->SetEnabled(true);
            }
        });
    }

    void InitializePowerSaveTimer() {
        rtc_gpio_init(GPIO_NUM_21);
        rtc_gpio_set_direction(GPIO_NUM_21, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level(GPIO_NUM_21, 1);

#if CONFIG_ZMK_SCANNER_MODE
        power_save_timer_ = new PowerSaveTimer(-1, -1, -1);
#else
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
#endif
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(1);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutting down");
            rtc_gpio_set_level(GPIO_NUM_21, 0);
            // 启用保持功能，确保睡眠期间电平不变
            rtc_gpio_hold_en(GPIO_NUM_21);
            esp_lcd_panel_disp_on_off(panel_, false);  // 关闭显示
            esp_deep_sleep_start();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SDA;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SCL;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
#if CONFIG_ZMK_SCANNER_MODE
        boot_button_.OnDoubleClick([this]() {
            power_save_timer_->WakeUp();
            Application::GetInstance().Schedule([this]() {
                if (download_timer_)
                    return;
                Application::GetInstance().SetDeviceState(kDeviceStateWifiConfiguring);
                EnterWifiConfigMode();
            });
        });
#endif
        boot_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.Schedule([]() { Application::GetInstance().ToggleChatState(); });
        });

        boot_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            Application::GetInstance().Schedule([this]() {
#if CONFIG_ZMK_SCANNER_MODE
                EnterDownloadMode();
#else
                Application::GetInstance().SetDeviceState(kDeviceStateWifiConfiguring);
                EnterWifiConfigMode();
#endif
            });
        });

        volume_up_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_up_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
#if !CONFIG_ZMK_SCANNER_MODE
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
#endif
        });

        volume_down_button_.OnClick([this]() {
            power_save_timer_->WakeUp();
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });

        volume_down_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
#if CONFIG_ZMK_SCANNER_MODE
            Application::GetInstance().Schedule([this]() { display_->NextDashboardTheme(); });
#else
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
#endif
        });
    }

    void InitializeSt7789Display() {
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS;
        io_config.dc_gpio_num = DISPLAY_DC;
        io_config.spi_mode = 3;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io_));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RES;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));

        display_ =
            new BoardDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X,
                             DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

public:
#if CONFIG_ZMK_SCANNER_MODE
    void StartNetwork() override {
        StartCodexSync();
        WifiBoard::StartNetwork();
    }
    void SetNetworkEventCallback(NetworkEventCallback callback) override {
        WifiBoard::SetNetworkEventCallback([callback](NetworkEvent event, const std::string& data) {
            if (callback)
                callback(event, data);
            if (event == NetworkEvent::Connected)
                Application::GetInstance().Schedule([]() {
                    static bool started = false;
                    if (!started) {
                        started = true;
                        StartZmkScanner();
                    }
                });
        });
    }
#endif
    XINGZHI_CUBE_1_54TFT_WIFI()
        : boot_button_(BOOT_BUTTON_GPIO, false, kBootLongPressMs),
          volume_up_button_(VOLUME_UP_BUTTON_GPIO),
          volume_down_button_(VOLUME_DOWN_BUTTON_GPIO) {
        InitializePowerSaveTimer();
        InitializeSpi();
        InitializeButtons();
        InitializeSt7789Display();
        InitializePowerManager();
#if CONFIG_ZMK_SCANNER_MODE
        display_->RegisterDashboardTools();
#endif
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                               AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
                                               AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK,
                                               AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (!power_manager_->HasBatteryLevel())
            return false;
        static bool last_discharging = false;
        charging = power_manager_->IsCharging();
        discharging = power_manager_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }
        level = power_manager_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(XINGZHI_CUBE_1_54TFT_WIFI);
