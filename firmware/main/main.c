/*
 * Bullerby Chat — Firmware
 *
 *  - Display + LVGL: home UI (dummy families), recording stub, carousel (finger drag)
 *  - Touch: CST816D; LVGL pointer events for strip pan (hardware gestures optional)
 *  - Audio: BOOT button hold → record; release → playback (PCM loopback)
 *  - WiFi: optional (sdkconfig / menuconfig: CONFIG_BULLERBY_ENABLE_WIFI)
 */

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#if CONFIG_BULLERBY_ENABLE_WIFI
#include "esp_netif.h"
#include "esp_wifi.h"
#endif

#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "hal/hal.h"
#include "app/ui_app.h"

static const char *TAG = "main";

extern i2s_chan_handle_t hal_codec_get_tx(void);
extern i2s_chan_handle_t hal_codec_get_rx(void);

#if CONFIG_BULLERBY_ENABLE_WIFI

#define WIFI_SSID     "YOUR_SSID"
#define WIFI_PASS     "YOUR_PASS"

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init(void)
{
    ESP_LOGI(TAG, "Initialising WiFi (SSID: %s)", WIFI_SSID);

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

#endif /* CONFIG_BULLERBY_ENABLE_WIFI */

#define AUDIO_BUF_SAMPLES  (24000 * 10)
#define AUDIO_BUF_BYTES    (AUDIO_BUF_SAMPLES * sizeof(int16_t))

static int16_t *audio_buf = NULL;
static size_t   audio_len = 0;

static void audio_task(void *arg)
{
    (void)arg;

    audio_buf = heap_caps_malloc(AUDIO_BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (audio_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer in PSRAM");
        vTaskDelete(NULL);
        return;
    }

    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << PIN_BOOT_BTN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn_cfg);

    i2s_chan_handle_t tx = hal_codec_get_tx();
    i2s_chan_handle_t rx = hal_codec_get_rx();

    while (1) {
        if (gpio_get_level(PIN_BOOT_BTN) == 0) {
            ESP_LOGI(TAG, "Recording...");
            hal_led_set(true);

            i2s_channel_enable(rx);
            audio_len = 0;
            size_t bytes_read = 0;

            while (gpio_get_level(PIN_BOOT_BTN) == 0 && audio_len < AUDIO_BUF_BYTES) {
                i2s_channel_read(rx, audio_buf + (audio_len / sizeof(int16_t)),
                                 1024, &bytes_read, portMAX_DELAY);
                audio_len += bytes_read;
            }

            i2s_channel_disable(rx);
            hal_led_set(false);
            ESP_LOGI(TAG, "Recorded %u bytes (%.1f s)", (unsigned)audio_len,
                     (float)audio_len / (AUDIO_SAMPLE_RATE * 2));

            if (audio_len > 0) {
                ESP_LOGI(TAG, "Playing back...");
                hal_pa_enable(true);
                i2s_channel_enable(tx);

                size_t bytes_written = 0;
                size_t offset = 0;
                while (offset < audio_len) {
                    size_t chunk = (audio_len - offset > 1024) ? 1024 : (audio_len - offset);
                    i2s_channel_write(tx, (uint8_t *)audio_buf + offset,
                                      chunk, &bytes_written, portMAX_DELAY);
                    offset += bytes_written;
                }

                i2s_channel_disable(tx);
                hal_pa_enable(false);
                ESP_LOGI(TAG, "Playback done");
            }

            vTaskDelay(pdMS_TO_TICKS(300));
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Bullerby Chat ===");

    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run) {
        ESP_LOGI(TAG, "Running app partition: %s @ 0x%08lx", run->label, (unsigned long)run->address);
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    hal_led_init();
    hal_led_set(true);

    lv_display_t *disp = NULL;
    ESP_ERROR_CHECK(hal_display_init(&disp));
    ESP_ERROR_CHECK(hal_touch_init(disp));
    ESP_ERROR_CHECK(hal_codec_init());

    ESP_ERROR_CHECK(ui_app_init(disp));

    hal_led_set(false);

#if CONFIG_BULLERBY_ENABLE_WIFI
    wifi_init();
#else
    ESP_LOGI(TAG, "WiFi disabled (CONFIG_BULLERBY_ENABLE_WIFI=n)");
#endif

    xTaskCreate(audio_task, "audio", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Ready. Drag strip; tap list for inbox; tap center bubble → record (BOOT = audio test).");
}
