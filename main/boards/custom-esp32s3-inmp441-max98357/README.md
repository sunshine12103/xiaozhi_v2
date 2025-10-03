# Custom ESP32-S3 Board với INMP441 và MAX98357

Board tùy chỉnh sử dụng ESP32-S3 với microphone INMP441 và speaker MAX98357.

## Thông số phần cứng

### Microcontroller
- **Chip**: ESP32-S3
- **Flash**: 16MB
- **PSRAM**: Có hỗ trợ

### Audio
- **Microphone**: INMP441 (I2S Digital Mic)
  - WS (Word Select): GPIO 15
  - SD (Serial Data): GPIO 14  
  - SCK (Serial Clock): GPIO 2

- **Speaker**: MAX98357 (I2S Audio Amplifier)
  - DOUT (Data Out): GPIO 42
  - BCLK (Bit Clock): GPIO 41
  - LRC (Left/Right Clock): GPIO 40

### Display
- **Type**: TFT LCD 240x280 (ST7789)
- **Interface**: SPI
- **Pins**:
  - CS (Chip Select): GPIO 39
  - RST (Reset): GPIO 6
  - DC (Data/Command): GPIO 16
  - MOSI (Data): GPIO 11
  - SCLK (Clock): GPIO 12
  - BL (Backlight): GPIO 48

### Controls
- **Boot Button**: GPIO 0

## Cấu hình Audio

Board này sử dụng **Simplex I2S mode** với 2 interface I2S riêng biệt:
- I2S0: Cho INMP441 microphone (input)
- I2S1: Cho MAX98357 speaker (output)

### Sample Rates
- **Input**: 16kHz (optimal cho INMP441)
- **Output**: 24kHz (cho MAX98357)

## Build Instructions

1. Đặt board config trong thư mục:
   ```
   main/boards/custom-esp32s3-inmp441-max98357/
   ```

2. Build firmware:
   ```bash
   python scripts/release.py custom-esp32s3-inmp441-max98357
   ```

3. Flash firmware vào ESP32-S3

## Kết nối phần cứng

### INMP441 Microphone
```
INMP441    ESP32-S3
VDD    ->  3.3V
GND    ->  GND
WS     ->  GPIO 15
SD     ->  GPIO 14
SCK    ->  GPIO 2
```

### MAX98357 Speaker
```
MAX98357   ESP32-S3
VIN    ->  5V
GND    ->  GND
DOUT   ->  GPIO 42
BCLK   ->  GPIO 41
LRC    ->  GPIO 40
```

### TFT Display (ST7789)
```
Display    ESP32-S3
VCC    ->  3.3V
GND    ->  GND
CS     ->  GPIO 39
RST    ->  GPIO 6
DC     ->  GPIO 16
MOSI   ->  GPIO 11
SCLK   ->  GPIO 12
BL     ->  GPIO 48
```

## Notes

- INMP441 là digital mic không cần analog processing
- MAX98357 có built-in DAC và amplifier
- ST7789 display driver hỗ trợ 240x280 resolution
- Sử dụng separate I2S channels để tránh conflict giữa mic và speaker