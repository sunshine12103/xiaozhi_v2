#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
 
// Audio sample rates
#define AUDIO_INPUT_SAMPLE_RATE  16000  // INMP441 optimal sample rate
#define AUDIO_OUTPUT_SAMPLE_RATE 24000  // MAX98357 output sample rate

// Enable simplex I2S mode (separate I2S for mic and speaker)
#define AUDIO_I2S_METHOD_SIMPLEX

// INMP441 Digital Microphone I2S pins
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_15  // Word Select (LR)
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_2   // Serial Clock (SCK)  
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_14  // Serial Data (SD)

// MAX98357 I2S Audio Amplifier pins
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_42  // Data Out
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_41  // Bit Clock
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_40  // Left/Right Clock

// Button configuration  
#define BUILTIN_LED_GPIO        GPIO_NUM_2   // Dùng GPIO 2 cho LED
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define TOUCH_BUTTON_GPIO       GPIO_NUM_NC
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

// TFT Display 240x280 SPI pins
#define DISPLAY_CS_PIN          GPIO_NUM_39  // Chip Select
#define DISPLAY_RST_PIN         GPIO_NUM_6   // Reset
#define DISPLAY_DC_PIN          GPIO_NUM_16  // Data/Command
#define DISPLAY_MOSI_PIN        GPIO_NUM_11  // MOSI (SDA)
#define DISPLAY_CLK_PIN         GPIO_NUM_12  // SCLK
#define DISPLAY_BACKLIGHT_PIN   GPIO_NUM_48  // Giữ nguyên GPIO 48

// Display configuration for ST7789 280x240 - Landscape với SWAP_XY = true
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   280      // Landscape width với SWAP_XY
#define DISPLAY_HEIGHT  240      // Landscape height với SWAP_XY
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y true    // Quay lại true như Korvo2-V3
#define DISPLAY_SWAP_XY true     // BẮT BUỘC true để có chế độ ngang
#define DISPLAY_INVERT_COLOR true   
#define DISPLAY_RGB_ORDER LCD_RGB_ELEMENT_ORDER_RGB  // Thử RGB để emoji có màu đúng
#define DISPLAY_OFFSET_X  20     // Dịch nhiều hơn để căn giữa
#define DISPLAY_OFFSET_Y  0     // Dịch nhiều hơn để căn giữa
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT true   // Thử true để tăng độ sáng
#define DISPLAY_SPI_MODE 0

// SPI configuration - thêm các định nghĩa này
#define DISPLAY_SPI_SCK_PIN     DISPLAY_CLK_PIN
#define DISPLAY_SPI_MOSI_PIN    DISPLAY_MOSI_PIN  
#define DISPLAY_SPI_CS_PIN      DISPLAY_CS_PIN
#define DISPLAY_SPI_HOST        SPI3_HOST

// Audio input reference (set to false for INMP441)
#define AUDIO_INPUT_REFERENCE false

#endif // _BOARD_CONFIG_H_