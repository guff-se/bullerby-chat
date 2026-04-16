#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Bullerby Chat — Board configuration for Spotpear ESP32-S3 1.28" Round LCD Box
// Pin assignments match the reference firmware (xiaozhi-esp32 sp-esp32-s3-1.28-box)

#include <driver/gpio.h>

// --- Audio (ES8311 codec) ---
#define AUDIO_SAMPLE_RATE       24000
#define AUDIO_I2S_PORT          I2S_NUM_0
#define AUDIO_I2S_MCLK_PIN      GPIO_NUM_16
#define AUDIO_I2S_BCLK_PIN      GPIO_NUM_9
#define AUDIO_I2S_WS_PIN        GPIO_NUM_45
#define AUDIO_I2S_DIN_PIN       GPIO_NUM_10  // Codec DOUT → ESP32 DIN (mic data)
#define AUDIO_I2S_DOUT_PIN      GPIO_NUM_8   // ESP32 DOUT → Codec DIN (speaker data)

#define AUDIO_CODEC_I2C_PORT    I2C_NUM_0
#define AUDIO_CODEC_I2C_SDA     GPIO_NUM_15
#define AUDIO_CODEC_I2C_SCL     GPIO_NUM_14
#define AUDIO_CODEC_I2C_FREQ    100000
#define ES8311_I2C_ADDR         0x18  // ES8311 default address (AD0=LOW)

#define AUDIO_PA_PIN            GPIO_NUM_46   // Speaker amplifier enable

// --- Display (GC9A01 240x240 round SPI LCD) ---
#define DISPLAY_WIDTH           240
#define DISPLAY_HEIGHT          240

#define DISPLAY_SPI_HOST        SPI3_HOST
#define DISPLAY_SPI_SCLK_PIN    GPIO_NUM_4
#define DISPLAY_SPI_MOSI_PIN    GPIO_NUM_2
#define DISPLAY_SPI_CS_PIN      GPIO_NUM_5
#define DISPLAY_SPI_DC_PIN      GPIO_NUM_47
#define DISPLAY_SPI_RESET_PIN   GPIO_NUM_38
#define DISPLAY_SPI_FREQ_HZ     (40 * 1000 * 1000)

#define DISPLAY_BL_PIN          GPIO_NUM_42
#define DISPLAY_BL_INVERT       true  // 0% duty = full brightness

#define DISPLAY_MIRROR_X        true
#define DISPLAY_MIRROR_Y        false

// --- Touch (CST816D capacitive touch) ---
#define TOUCH_I2C_PORT          I2C_NUM_1
#define TOUCH_I2C_SDA           GPIO_NUM_11
#define TOUCH_I2C_SCL           GPIO_NUM_7
#define TOUCH_I2C_FREQ          400000
#define TOUCH_RST_PIN           GPIO_NUM_6
#define TOUCH_INT_PIN           GPIO_NUM_12
#define CST816D_I2C_ADDR        0x15

#define TOUCH_POLL_MS           10  // Poll interval in milliseconds

// --- Button ---
#define BOOT_BUTTON_PIN         GPIO_NUM_0

// --- LED ---
#define LED_PIN                 GPIO_NUM_48

// --- Battery ---
#define BATTERY_ADC_PIN         GPIO_NUM_1
#define BATTERY_CHARGE_PIN      GPIO_NUM_41

// --- Power ---
#define POWER_HOLD_PIN          GPIO_NUM_3  // RTC GPIO, hold HIGH to stay powered

// --- Audio buffer ---
#define AUDIO_MAX_RECORD_SEC    30
#define AUDIO_BUFFER_SIZE       (AUDIO_SAMPLE_RATE * 2 * AUDIO_MAX_RECORD_SEC)  // 16-bit mono

#endif // BOARD_CONFIG_H
