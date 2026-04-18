/*
 * Bullerby Chat — Firmware
 *
 *  - Display + LVGL: home UI (ring of family circles + center message bubble)
 *  - Touch: CST816D via LVGL pointer indev
 *  - Audio: BOOT button hold → record → local loopback + (optional) upload
 *  - Network (CONFIG_BULLERBY_ENABLE_NET): WiFi → register → WS → relay playback
 */

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "hal/hal.h"
#include "app/ui_app.h"
#include "model/model_families.h"

#if CONFIG_BULLERBY_ENABLE_NET
#include "net/identity.h"
#include "net/net.h"
#endif

static const char *TAG = "main";

extern i2s_chan_handle_t hal_codec_get_tx(void);
extern i2s_chan_handle_t hal_codec_get_rx(void);

/* ── Audio shared state ─────────────────────────────────────────────── */

#define AUDIO_BUF_SAMPLES  (AUDIO_SAMPLE_RATE * 10)
#define AUDIO_BUF_BYTES    (AUDIO_BUF_SAMPLES * sizeof(int16_t))
/** Server cap is 128 KiB. At 24 kHz mono 16-bit that's ~2.7 s of audio. */
#define UPLOAD_CAP_BYTES   (128 * 1024)

/** Stereo capture buffer (interleaved L/R 16-bit). */
static int16_t *s_cap_buf = NULL;
/** Mono upload / playback buffer (left channel only). */
static int16_t *s_mono_buf = NULL;

static SemaphoreHandle_t s_audio_lock;

static void audio_lock(void)
{
    if (s_audio_lock) xSemaphoreTake(s_audio_lock, portMAX_DELAY);
}
static void audio_unlock(void)
{
    if (s_audio_lock) xSemaphoreGive(s_audio_lock);
}

/** Copy left channel from interleaved stereo PCM into `dst`. Returns mono byte count. */
static size_t deinterleave_left(const int16_t *stereo, size_t stereo_bytes, int16_t *dst)
{
    size_t frames = stereo_bytes / 4;
    for (size_t i = 0; i < frames; i++) {
        dst[i] = stereo[i * 2];
    }
    return frames * sizeof(int16_t);
}

/** Play mono 16-bit PCM through the speaker at `sample_rate` Hz (stereo-expanded). */
static void play_mono_pcm(const int16_t *mono, size_t mono_bytes, int sample_rate)
{
    if (mono_bytes == 0) return;

    i2s_chan_handle_t tx = hal_codec_get_tx();
    if (!tx) return;

    i2s_channel_disable(tx);

    /* Retune TX clock if needed, then restore to mic rate on the way out. */
    bool retuned = (sample_rate > 0 && sample_rate != AUDIO_SAMPLE_RATE);
    if (retuned) {
        i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
        clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        if (i2s_channel_reconfig_std_clock(tx, &clk) != ESP_OK) {
            ESP_LOGW(TAG, "could not retune I2S TX to %d Hz — playing at %d",
                     sample_rate, AUDIO_SAMPLE_RATE);
            retuned = false;
        }
    }

    hal_pa_enable(true);
    i2s_channel_enable(tx);

    int16_t stereo_chunk[480 * 2];
    size_t frames = mono_bytes / sizeof(int16_t);
    size_t done = 0;
    while (done < frames) {
        size_t chunk = (frames - done > 480) ? 480 : (frames - done);
        for (size_t i = 0; i < chunk; i++) {
            stereo_chunk[i * 2]     = mono[done + i];
            stereo_chunk[i * 2 + 1] = mono[done + i];
        }
        size_t written = 0;
        i2s_channel_write(tx, stereo_chunk, chunk * 2 * sizeof(int16_t),
                          &written, portMAX_DELAY);
        done += chunk;
    }

    i2s_channel_disable(tx);
    hal_pa_enable(false);

    if (retuned) {
        i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE);
        clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        i2s_channel_reconfig_std_clock(tx, &clk);
    }
}

/* ── BOOT-button capture loop ───────────────────────────────────────── */

static void audio_task(void *arg)
{
    (void)arg;

    s_cap_buf = heap_caps_malloc(AUDIO_BUF_BYTES, MALLOC_CAP_SPIRAM);
    s_mono_buf = heap_caps_malloc(AUDIO_BUF_BYTES / 2, MALLOC_CAP_SPIRAM);
    if (!s_cap_buf || !s_mono_buf) {
        ESP_LOGE(TAG, "failed to allocate audio buffers in PSRAM");
        vTaskDelete(NULL);
        return;
    }

    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << PIN_BOOT_BTN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn_cfg);

    i2s_chan_handle_t rx = hal_codec_get_rx();

    for (;;) {
        if (gpio_get_level(PIN_BOOT_BTN) != 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        audio_lock();
        ESP_LOGI(TAG, "recording…");
        hal_led_set(true);

        i2s_channel_enable(rx);
        size_t cap_bytes = 0;
        while (gpio_get_level(PIN_BOOT_BTN) == 0 && cap_bytes < AUDIO_BUF_BYTES) {
            size_t got = 0;
            i2s_channel_read(rx, (uint8_t *)s_cap_buf + cap_bytes,
                             1024, &got, portMAX_DELAY);
            cap_bytes += got;
        }
        i2s_channel_disable(rx);
        hal_led_set(false);

        size_t mono_bytes = deinterleave_left(s_cap_buf, cap_bytes, s_mono_buf);
        ESP_LOGI(TAG, "captured %u mono bytes (%.1f s @ %d Hz)",
                 (unsigned)mono_bytes,
                 (float)mono_bytes / (AUDIO_SAMPLE_RATE * 2),
                 AUDIO_SAMPLE_RATE);

        if (mono_bytes > 0) {
            play_mono_pcm(s_mono_buf, mono_bytes, AUDIO_SAMPLE_RATE);
        }

#if CONFIG_BULLERBY_ENABLE_NET
        /* Upload — clip to server cap, send as broadcast. */
        if (mono_bytes > 0 && net_is_online()) {
            size_t up_bytes = mono_bytes > UPLOAD_CAP_BYTES ? UPLOAD_CAP_BYTES : mono_bytes;
            if (up_bytes < mono_bytes) {
                ESP_LOGW(TAG, "clipping upload to %u bytes (server 128 KiB cap)",
                         (unsigned)up_bytes);
            }
            float duration = (float)up_bytes / (AUDIO_SAMPLE_RATE * 2);
            esp_err_t uerr = net_send_pcm(NULL /* broadcast */,
                                          (const uint8_t *)s_mono_buf, up_bytes,
                                          AUDIO_SAMPLE_RATE, duration);
            if (uerr != ESP_OK) {
                ESP_LOGW(TAG, "upload failed: %s", esp_err_to_name(uerr));
            }
        }
#endif
        audio_unlock();

        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

#if CONFIG_BULLERBY_ENABLE_NET

/* Called on the net worker thread after a download completes. */
static void on_remote_audio(const uint8_t *pcm, size_t pcm_len,
                            int sample_rate_hz, const char *from_family_id)
{
    ESP_LOGI(TAG, "incoming audio from %s: %u bytes @ %d Hz",
             from_family_id ? from_family_id : "?",
             (unsigned)pcm_len, sample_rate_hz);
    audio_lock();
    play_mono_pcm((const int16_t *)pcm, pcm_len, sample_rate_hz);
    audio_unlock();
}

#endif /* CONFIG_BULLERBY_ENABLE_NET */

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

    ESP_ERROR_CHECK(model_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    hal_led_init();
    hal_led_set(true);

    lv_display_t *disp = NULL;
    ESP_ERROR_CHECK(hal_display_init(&disp));
    ESP_ERROR_CHECK(hal_touch_init(disp));
    ESP_ERROR_CHECK(hal_codec_init());

    ESP_ERROR_CHECK(ui_app_init(disp));

    hal_led_set(false);

    s_audio_lock = xSemaphoreCreateMutex();

#if CONFIG_BULLERBY_ENABLE_NET
    ESP_ERROR_CHECK(identity_init());
    net_set_playback_cb(on_remote_audio);
    esp_err_t nerr = net_start();
    if (nerr != ESP_OK) {
        ESP_LOGE(TAG, "net_start failed: %s — staying offline", esp_err_to_name(nerr));
    }
#else
    ESP_LOGI(TAG, "Networking disabled (CONFIG_BULLERBY_ENABLE_NET=n)");
#endif

    xTaskCreate(audio_task, "audio", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Ready. Tap a family circle → record; BOOT hold → capture + upload + loopback.");
}
