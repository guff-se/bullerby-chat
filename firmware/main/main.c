// Bullerby Chat — Firmware skeleton
// Hardware test: display, touch, audio record/playback, LED, WiFi
//
// Boot button (GPIO 0) cycles through states:
//   IDLE → RECORDING → IDLE (with recorded audio) → PLAYING → IDLE
//
// Touch events are shown on screen as coordinates.
// WiFi connects in the background (edit SSID/password below).

#include "board_config.h"
#include "display.h"
#include "touch.h"
#include "led.h"
#include "es8311.h"
#include "audio.h"
#include "wifi.h"
#include "ui.h"

#include <esp_log.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "main";

// ---- WiFi credentials for testing (change these!) ----
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// Simple debounced boot button check
static bool boot_button_pressed(void)
{
    static bool was_pressed = false;
    bool pressed = (gpio_get_level(BOOT_BUTTON_PIN) == 0);

    if (pressed && !was_pressed) {
        was_pressed = true;
        vTaskDelay(pdMS_TO_TICKS(50));  // Debounce
        return true;
    }
    if (!pressed) {
        was_pressed = false;
    }
    return false;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Bullerby Chat Skeleton ===");

    // ---- NVS (required for WiFi) ----
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ---- Hold power on (RTC GPIO 3) ----
    rtc_gpio_init(POWER_HOLD_PIN);
    rtc_gpio_set_direction(POWER_HOLD_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(POWER_HOLD_PIN, 1);

    // ---- LED ----
    led_init();
    led_set(true);
    ESP_LOGI(TAG, "LED: ON");

    // ---- Display ----
    lv_display_t *disp = NULL;
    ESP_ERROR_CHECK(display_init(&disp));

    // ---- UI ----
    ui_init(disp);
    ui_set_status("Starting up...");

    // ---- Touch ----
    esp_err_t touch_ret = touch_init(disp);
    if (touch_ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch init failed (non-fatal): %s", esp_err_to_name(touch_ret));
    }

    // ---- Audio ----
    ESP_ERROR_CHECK(audio_init());

    // ---- Boot button (GPIO 0 as input) ----
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn_cfg);

    // ---- WiFi (non-blocking — runs in background) ----
    ui_set_status("Connecting WiFi...");
    ret = wifi_init_sta(WIFI_SSID, WIFI_PASSWORD);
    if (ret == ESP_OK) {
        ui_set_status("WiFi connected!");
    } else {
        ui_set_status("WiFi timeout (retrying)");
    }

    // ---- Power-on LED blink ----
    led_set(false);
    vTaskDelay(pdMS_TO_TICKS(200));
    led_set(true);
    vTaskDelay(pdMS_TO_TICKS(200));
    led_set(false);

    // ---- Main loop ----
    ui_set_status("Ready — press button");
    ESP_LOGI(TAG, "Entering main loop. Press BOOT button to record/play.");

    bool has_recording = false;

    while (1) {
        if (boot_button_pressed()) {
            audio_state_t state = audio_get_state();

            switch (state) {
                case AUDIO_STATE_IDLE:
                    if (has_recording) {
                        // Play the recording
                        ESP_LOGI(TAG, "Starting playback");
                        ui_show_playback(true);
                        led_set(true);
                        audio_start_playback();
                        has_recording = false;
                    } else {
                        // Start recording
                        ESP_LOGI(TAG, "Starting recording");
                        ui_show_recording(true);
                        led_set(true);
                        audio_start_recording();
                    }
                    break;

                case AUDIO_STATE_RECORDING: {
                    // Stop recording
                    size_t len = audio_stop_recording();
                    ui_show_recording(false);
                    led_set(false);
                    has_recording = (len > 0);
                    if (has_recording) {
                        float secs = (float)len / (AUDIO_SAMPLE_RATE * 2);
                        ESP_LOGI(TAG, "Recorded %.1f seconds", secs);
                        ui_set_status("Tap to play");
                    } else {
                        ui_set_status("Empty recording");
                    }
                    break;
                }

                case AUDIO_STATE_PLAYING:
                    // Stop playback
                    audio_stop_playback();
                    ui_show_playback(false);
                    led_set(false);
                    ui_set_status("Ready — press button");
                    break;
            }
        }

        // Update UI based on audio state (auto-return from playback)
        if (audio_get_state() == AUDIO_STATE_IDLE) {
            // Playback might have finished on its own
            static audio_state_t last_state = AUDIO_STATE_IDLE;
            if (last_state == AUDIO_STATE_PLAYING) {
                ui_show_playback(false);
                led_set(false);
                ui_set_status("Ready — press button");
            }
            last_state = AUDIO_STATE_IDLE;
        } else {
            static audio_state_t last_state2 = AUDIO_STATE_IDLE;
            last_state2 = audio_get_state();
            (void)last_state2;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
