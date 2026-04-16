# Hardware: AI Voice Chat Robot (ESP32-S3 1.28" LCD)

**UI:** Interface design for the round display is specified in [ui-spec.md](ui-spec.md).

## Product Listing

AI Voice Chat Robot 1.28In LCD Screen ESP32-S3 Development Board Smart DeepSeek
Assistant Robot for Home Office School

## Specifications

- **MCU:** ESP32-S3 (dual-core 240MHz, 16MB flash QIO, octal PSRAM 80MHz)
- **Display:** 240x240 GC9A01 round SPI LCD at 40MHz, PWM backlight (GPIO 42)
- **Audio Codec:** ES8311 over I2C (I2C_NUM_0), I2S at 24kHz in/out
- **Speaker Amplifier:** PA on GPIO 46
- **Touch:** CST816D capacitive touch over I2C (I2C_NUM_1), polled at 10ms
- **Button:** BOOT button on GPIO 0
- **LED:** Single LED on GPIO 48
- **Battery:** ADC on GPIO 1, charging detect on GPIO 41
- **Connectivity:** WiFi (ESP32-S3 built-in)
- **Storage:** SD card slot
- **Product Size:** 55 x 48 x 33.5 mm
- **Weight:** 88g
- **Color:** White

## Reference Firmware

Original firmware: https://github.com/78/xiaozhi-esp32/tree/main

Board config in reference repo: `main/boards/sp-esp32-s3-1.28-box/`

### Key Pin Assignments (from reference)

| Function        | GPIO |
|-----------------|------|
| LCD Backlight   | 42   |
| Speaker PA      | 46   |
| Boot Button     | 0    |
| LED             | 48   |
| Battery ADC     | 1    |
| Charge Detect   | 41   |
| I2C_0 (Audio)   | Board-specific SDA/SCL |
| I2C_1 (Touch)   | Board-specific SDA/SCL |

### Build System

- ESP-IDF >= 5.5.2 (CMake-based, `idf.py build`)
- Dependencies via `idf_component.yml` (ESP Component Registry)
- Custom partition table for 16MB flash
