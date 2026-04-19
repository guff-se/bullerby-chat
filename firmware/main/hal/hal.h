#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* ── Pin Definitions ─────────────────────────────────────────────── */

/* Display — GC9A01 on SPI3 */
#define PIN_LCD_SCLK        4
#define PIN_LCD_MOSI        2
#define PIN_LCD_CS           5
#define PIN_LCD_DC          47
#define PIN_LCD_RST         38
#define PIN_LCD_BL          42
#define LCD_SPI_HOST        SPI3_HOST
#define LCD_SPI_FREQ_HZ     (40 * 1000 * 1000)
#define LCD_H_RES           240
#define LCD_V_RES           240

/* Audio codec — ES8311 on I2C bus 0, I2S */
#define PIN_I2C0_SDA        15
#define PIN_I2C0_SCL        14
#define PIN_I2S_MCLK        16
#define PIN_I2S_BCLK         9
#define PIN_I2S_WS          45
#define PIN_I2S_DIN         10  /* ESP → codec */
#define PIN_I2S_DOUT         8  /* codec → ESP */
#define PIN_PA_EN           46
#define AUDIO_SAMPLE_RATE   24000
#define ES8311_I2C_ADDR     0x18

/* Touch — CST816D on I2C bus 1 */
#define PIN_I2C1_SDA        11
#define PIN_I2C1_SCL         7
#define PIN_TOUCH_RST        6
#define PIN_TOUCH_INT       12
#define CST816D_I2C_ADDR    0x15

/* Misc */
#define PIN_LED             48
#define PIN_BOOT_BTN         0
#define PIN_BATT_ADC         1
#define PIN_CHARGE_DET      41

/** Below this charge %, UI should show a low-battery warning (firmware plan). */
#define BATTERY_PCT_LOW_WARN 15

/* Codec: I2S runs at AUDIO_SAMPLE_RATE (24 kHz today). Product target for Opus
 * is often 16 kHz — resample or reclock before encode when adding Opus. */

/* ── HAL Init Functions ──────────────────────────────────────────── */

/** Initialise the GC9A01 display + LVGL. Returns the LVGL display pointer. */
esp_err_t hal_display_init(lv_display_t **disp_out);

/** Initialise CST816D touch and register with LVGL. */
esp_err_t hal_touch_init(lv_display_t *disp);

/** CST816D gesture register values (common mapping — verify on hardware). */
#define HAL_TOUCH_GEST_NONE       0x00
#define HAL_TOUCH_GEST_SWIPE_UP   0x01
#define HAL_TOUCH_GEST_SWIPE_DOWN 0x02
#define HAL_TOUCH_GEST_SWIPE_LEFT 0x03
#define HAL_TOUCH_GEST_SWIPE_RIGHT 0x04

/**
 * Pop one hardware-reported gesture from CST816D (register 0x01), or 0.
 */
uint8_t hal_touch_consume_gesture(void);

/** Initialise ES8311 codec (I2C + I2S) via Espressif's esp_codec_dev. */
esp_err_t hal_codec_init(void);

/** Enable / disable the speaker amplifier. */
void hal_pa_enable(bool on);

/**
 * Latch POWER_HOLD_PIN high via RTC GPIO so boards with a soft-power push-button
 * stay on after release. No-op on boards without the latch (pin is unused elsewhere).
 */
esp_err_t hal_power_init(void);

/** Initialise status LED GPIO. */
esp_err_t hal_led_init(void);

/** Set LED on/off. */
void hal_led_set(bool on);

/** Read battery voltage (0–100 percent). Returns -1 on error. */
int hal_battery_percent(void);

/** Read whether battery is charging. */
bool hal_battery_charging(void);
