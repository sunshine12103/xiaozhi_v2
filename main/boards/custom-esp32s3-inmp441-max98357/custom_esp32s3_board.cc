#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "../common/backlight.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "led/single_led.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "CustomEsp32S3Board"

class CustomEsp32S3Board : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_;

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        
        ESP_LOGI(TAG, "Initializing SPI bus on host %d", DISPLAY_SPI_HOST);
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        
        // Khởi tạo backlight TRƯỚC (quan trọng!)
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            ESP_LOGI(TAG, "Initializing backlight on GPIO %d", DISPLAY_BACKLIGHT_PIN);
            
            // Delay nhỏ để đảm bảo system ổn định
            vTaskDelay(pdMS_TO_TICKS(50));
            
            // Khởi tạo GPIO 48 như output pin trước khi dùng làm backlight
            gpio_config_t io_conf = {};
            io_conf.intr_type = GPIO_INTR_DISABLE;
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pin_bit_mask = (1ULL << DISPLAY_BACKLIGHT_PIN);
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
            esp_err_t ret = gpio_config(&io_conf);
            
            if (ret == ESP_OK) {
                // Bật backlight ngay bằng cách set high
                gpio_set_level(DISPLAY_BACKLIGHT_PIN, 1);
                ESP_LOGI(TAG, "GPIO %d set to HIGH for backlight", DISPLAY_BACKLIGHT_PIN);
                
                // Delay trước khi khởi tạo PWM
                vTaskDelay(pdMS_TO_TICKS(50));
                
                auto backlight = GetBacklight();
                if (backlight) {
                    backlight->SetBrightness(1.0f, false); // Bật backlight PWM
                    ESP_LOGI(TAG, "Backlight PWM set to maximum brightness");
                }
            } else {
                ESP_LOGE(TAG, "Failed to configure GPIO %d for backlight: %s", DISPLAY_BACKLIGHT_PIN, esp_err_to_name(ret));
            }
        }
        
        // LCD panel IO initialization (theo cách ESP32S3-Korvo2-V3)
        ESP_LOGI(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 60 * 1000 * 1000;  // Tăng lên 60MHz như Korvo2-V3 
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &panel_io));

        // Initialize LCD driver chip (ST7789) - theo cách Korvo2-V3
        ESP_LOGI(TAG, "Install LCD driver ST7789");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;  // Dùng reset pin
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        
        ESP_LOGI(TAG, "Resetting and initializing panel");
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        vTaskDelay(pdMS_TO_TICKS(100)); // Thêm delay sau reset
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        vTaskDelay(pdMS_TO_TICKS(100)); // Thêm delay sau init
        
        ESP_LOGI(TAG, "Configuring panel settings");
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR));
        
        // Bật display (quan trọng!)
        ESP_LOGI(TAG, "Turning on display");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
        
        // Thêm delay để display ổn định
        vTaskDelay(pdMS_TO_TICKS(100));

        ESP_LOGI(TAG, "Creating SpiLcdDisplay with size %dx%d", DISPLAY_WIDTH, DISPLAY_HEIGHT);
        display_ = new SpiLcdDisplay(panel_io, panel,
                                   DISPLAY_WIDTH, DISPLAY_HEIGHT, 
                                   DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, 
                                   DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, 
                                   DISPLAY_SWAP_XY);
        
        // Force sử dụng light theme để có màu sắc
        ESP_LOGI(TAG, "Setting light theme for colorful display");
        auto& theme_manager = LvglThemeManager::GetInstance();
        auto light_theme = theme_manager.GetTheme("light");
        if (light_theme) {
            display_->SetTheme(light_theme);
            ESP_LOGI(TAG, "Light theme applied successfully");
            
            // Test màu sắc của light theme
            ESP_LOGI(TAG, "Light theme colors:");
            ESP_LOGI(TAG, "  Background: white");
            ESP_LOGI(TAG, "  Text: black");
            ESP_LOGI(TAG, "  User bubble: #95EC69 (green)");
            ESP_LOGI(TAG, "  Assistant bubble: white");
            ESP_LOGI(TAG, "  System bubble: #E0E0E0 (light gray)");
        } else {
            ESP_LOGW(TAG, "Light theme not found, using default");
        }
        
        ESP_LOGI(TAG, "Display initialization completed successfully");
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

public:
    CustomEsp32S3Board() : boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Starting CustomEsp32S3Board initialization");
        
        ESP_LOGI(TAG, "Step 1: Initialize SPI");
        InitializeSpi();
        
        ESP_LOGI(TAG, "Step 2: Initialize LCD Display"); 
        InitializeLcdDisplay();
        
        ESP_LOGI(TAG, "Step 3: Initialize Buttons");
        InitializeButtons();
        
        ESP_LOGI(TAG, "CustomEsp32S3Board initialization completed successfully");
    }

    virtual ~CustomEsp32S3Board() {
        delete display_;
    }

    virtual Led* GetLed() override {
        // Dùng GPIO 2 cho LED, tránh conflict với GPIO 48 (backlight)
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, 
            AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            // Force max brightness multiple times để đảm bảo
            backlight.SetBrightness(1.0f, false);
            vTaskDelay(pdMS_TO_TICKS(10));
            backlight.SetBrightness(1.0f, true);  // Force update
            ESP_LOGI("BACKLIGHT", "Forced max brightness 100%%");
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(CustomEsp32S3Board);