#include <driver/i2c_master.h>
#include <driver/rtc_io.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <wifi_station.h>

#include "application.h"
#include "codecs/no_audio_codec.h"
#include "button.h"
#include "config.h"
#include "display/lcd_display.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "otto_emoji_display.h"
#include "power_manager.h"
#include "system_reset.h"
#include "wifi_board.h"

#define TAG "OttoRobot"

extern void InitializeOttoController();

class OttoRobot : public WifiBoard {
private:
    LcdDisplay* display_;
    PowerManager* power_manager_;
    Button boot_button_;
    Button wake_up_button_;
    void InitializePowerManager() {
        power_manager_ =
            new PowerManager(POWER_CHARGE_DETECT_PIN, POWER_ADC_UNIT, POWER_ADC_CHANNEL);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;

        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new OttoEmojiDisplay(
            panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting &&
                !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });

        wake_up_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            ESP_LOGI(TAG, "Wake up button pressed - triggering wake word");
            std::string wake_word = "你好小智";
            app.WakeWordInvoke(wake_word);
        });
    }

    void InitializeOttoController() {
        ESP_LOGI(TAG, "初始化Otto机器人MCP控制器");
        ::InitializeOttoController();
    }

    void InitializeWakeUpSource() {
        // Cấu hình EXT0 wake source cho GPIO4
        // Khi GPIO4 từ LOW lên HIGH (nhấn nút) sẽ đánh thức từ deep sleep
        esp_sleep_enable_ext0_wakeup(WAKE_UP_BUTTON_GPIO, 1);
        
        // Cấu hình pull-down để đảm bảo khi không nhấn nút sẽ là LOW
        rtc_gpio_pulldown_en(WAKE_UP_BUTTON_GPIO);
        rtc_gpio_pullup_dis(WAKE_UP_BUTTON_GPIO);
        
        ESP_LOGI(TAG, "Wake-up source configured for GPIO %d", WAKE_UP_BUTTON_GPIO);
        
        // Kiểm tra nguyên nhân wake-up
        esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
        switch(wakeup_reason) {
            case ESP_SLEEP_WAKEUP_EXT0: {
                ESP_LOGI(TAG, "Wakeup caused by external signal using RTC_IO (GPIO %d)", WAKE_UP_BUTTON_GPIO);
                // Tự động bắt đầu trò chuyện khi được đánh thức bởi nút
                vTaskDelay(pdMS_TO_TICKS(1000)); // Đợi hệ thống khởi động hoàn tất
                auto& app = Application::GetInstance();
                std::string wake_word = "你好小智";
                ESP_LOGI(TAG, "Auto-triggering wake word after button wake-up");
                app.WakeWordInvoke(wake_word);
                break;
            }
            case ESP_SLEEP_WAKEUP_TIMER:
                ESP_LOGI(TAG, "Wakeup caused by timer");
                break;
            case ESP_SLEEP_WAKEUP_TOUCHPAD:
                ESP_LOGI(TAG, "Wakeup caused by touchpad");
                break;
            case ESP_SLEEP_WAKEUP_ULP:
                ESP_LOGI(TAG, "Wakeup caused by ULP program");
                break;
            case ESP_SLEEP_WAKEUP_EXT1:
                ESP_LOGI(TAG, "Wakeup caused by EXT1");
                break;
            case ESP_SLEEP_WAKEUP_GPIO:
                ESP_LOGI(TAG, "Wakeup caused by GPIO");
                break;
            case ESP_SLEEP_WAKEUP_UART:
                ESP_LOGI(TAG, "Wakeup caused by UART");
                break;
            case ESP_SLEEP_WAKEUP_WIFI:
                ESP_LOGI(TAG, "Wakeup caused by WiFi");
                break;
            case ESP_SLEEP_WAKEUP_COCPU:
                ESP_LOGI(TAG, "Wakeup caused by COCPU");
                break;
            case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
                ESP_LOGI(TAG, "Wakeup caused by COCPU trap trigger");
                break;
            case ESP_SLEEP_WAKEUP_BT:
                ESP_LOGI(TAG, "Wakeup caused by BT");
                break;
            case ESP_SLEEP_WAKEUP_VAD:
                ESP_LOGI(TAG, "Wakeup caused by VAD");
                break;
            case ESP_SLEEP_WAKEUP_UNDEFINED:
            case ESP_SLEEP_WAKEUP_ALL:
            default:
                ESP_LOGI(TAG, "Wakeup was not caused by deep sleep: %d", wakeup_reason);
                break;
        }
    }

public:
    OttoRobot() : boot_button_(BOOT_BUTTON_GPIO), wake_up_button_(WAKE_UP_BUTTON_GPIO) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializePowerManager();
        InitializeOttoController();
        InitializeWakeUpSource();
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
        charging = power_manager_->IsCharging();
        discharging = !charging;
        level = power_manager_->GetBatteryLevel();
        return true;
    }
};

DECLARE_BOARD(OttoRobot);
