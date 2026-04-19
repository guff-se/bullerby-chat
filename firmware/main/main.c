/*
 * Bullerby Chat — Firmware
 *
 *  - Display + LVGL: home UI (ring of family circles + center message bubble)
 *  - Touch: CST816D via LVGL pointer indev
 *  - Audio: BOOT button hold → record → local loopback + (optional) upload
 *  - Network (CONFIG_BULLERBY_ENABLE_NET): WiFi → register → WS → relay playback
 */

#include <stdatomic.h>
#include <string.h>

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
#include "app/app_audio.h"
#include "model/model_families.h"

#if CONFIG_BULLERBY_ENABLE_NET
#include "net/identity.h"
#include "net/net.h"
#endif

static const char *TAG = "main";

extern i2s_chan_handle_t hal_codec_get_tx(void);
extern i2s_chan_handle_t hal_codec_get_rx(void);

/**
 * One I2S STD stereo DMA frame from the codec driver (`hal/codec.c`: dma_frame_num = 240).
 * Duplex ESP32-S3 I2S often needs TX running (fed with silence) for RX to see bit clock/data.
 */
#define CODEC_I2S_DMA_FRAMES   240
#define CODEC_I2S_CHUNK_BYTES  (CODEC_I2S_DMA_FRAMES * sizeof(int16_t) * 2)

/** Zero silence fed to TX during mic capture (BSS). */
static int16_t s_i2s_tx_silence[CODEC_I2S_DMA_FRAMES * 2];

static void i2s_duplex_tx_silence_tick(i2s_chan_handle_t tx)
{
    if (!tx) {
        return;
    }
    size_t written = 0;
    (void)i2s_channel_write(tx, s_i2s_tx_silence, sizeof(s_i2s_tx_silence), &written, pdMS_TO_TICKS(50));
}

static esp_err_t i2s_duplex_mic_start(i2s_chan_handle_t tx, i2s_chan_handle_t rx)
{
    hal_pa_enable(false);
    esp_err_t err = hal_codec_tx_enable(true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2S TX enable failed: %s", esp_err_to_name(err));
    }
    err = hal_codec_rx_enable(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S RX enable failed: %s", esp_err_to_name(err));
        hal_codec_tx_enable(false);
        return err;
    }
    /* Prime TX FIFO so the link runs before first RX read. */
    i2s_duplex_tx_silence_tick(tx);
    return ESP_OK;
}

static void i2s_duplex_mic_stop(i2s_chan_handle_t tx, i2s_chan_handle_t rx)
{
    (void)tx;
    (void)rx;
    hal_codec_rx_enable(false);
    hal_codec_tx_enable(false);
}

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

/** Set from LVGL when the record screen starts/stops capture (`audio_task` polls this). */
static atomic_bool s_ui_mic_recording = ATOMIC_VAR_INIT(false);

void app_audio_set_ui_recording(bool active)
{
    atomic_store_explicit(&s_ui_mic_recording, active, memory_order_release);
}

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

    hal_codec_tx_enable(false);

    /* Held in BSS — play_mono_pcm runs serialised behind s_audio_lock, so sharing is safe. */
    static int16_t stereo_chunk[480 * 2];

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
    hal_codec_tx_enable(true);

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

    hal_codec_tx_enable(false);
    hal_pa_enable(false);

    if (retuned) {
        i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE);
        clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        i2s_channel_reconfig_std_clock(tx, &clk);
    }
}

/** Log duration, warn on very short clips, report peak of first ~200 ms for mic sanity. */
static void log_capture_stats(const char *ctx, size_t mono_bytes, const int16_t *mono)
{
    float sec = (float)mono_bytes / (float)(AUDIO_SAMPLE_RATE * sizeof(int16_t));
    ESP_LOGI(TAG, "[%s] PCM captured: %u mono bytes (%.2f s @ %d Hz)",
             ctx, (unsigned)mono_bytes, sec, AUDIO_SAMPLE_RATE);
    if (mono_bytes < (size_t)(AUDIO_SAMPLE_RATE * sizeof(int16_t) / 10)) {
        ESP_LOGW(TAG, "[%s] clip shorter than ~100 ms — mic may be silent or stop was very fast",
                 ctx);
    }
    if (mono_bytes < sizeof(int16_t)) {
        return;
    }
    int peak = 0;
    size_t samples = mono_bytes / sizeof(int16_t);
    size_t scan = samples > 4800 ? 4800 : samples;
    for (size_t i = 0; i < scan; i++) {
        int s = mono[i];
        if (s < 0) {
            s = -s;
        }
        if (s > peak) {
            peak = s;
        }
    }
    ESP_LOGI(TAG, "[%s] level check (~first 200 ms): peak=%d / 32767 (%.1f%% FS)",
             ctx, peak, (double)(100.0f * (float)peak / 32767.0f));
}

static void finish_capture_and_play(const char *ctx, size_t cap_stereo_bytes)
{
    size_t mono_bytes = deinterleave_left(s_cap_buf, cap_stereo_bytes, s_mono_buf);
    log_capture_stats(ctx, mono_bytes, s_mono_buf);

    if (mono_bytes > 0) {
        ESP_LOGI(TAG, "[%s] speaker playback starting…", ctx);
        play_mono_pcm(s_mono_buf, mono_bytes, AUDIO_SAMPLE_RATE);
        ESP_LOGI(TAG, "[%s] speaker playback done", ctx);
    } else {
        ESP_LOGW(TAG, "[%s] no audio samples — skipping playback", ctx);
    }

#if CONFIG_BULLERBY_ENABLE_NET
    if (mono_bytes > 0 && net_is_online()) {
        size_t up_bytes = mono_bytes > UPLOAD_CAP_BYTES ? UPLOAD_CAP_BYTES : mono_bytes;
        if (up_bytes < mono_bytes) {
            ESP_LOGW(TAG, "[%s] clipping upload to %u bytes (server 128 KiB cap)",
                     ctx, (unsigned)up_bytes);
        }
        float duration = (float)up_bytes / (float)(AUDIO_SAMPLE_RATE * 2);
        esp_err_t uerr = net_send_pcm(NULL /* broadcast */,
                                      (const uint8_t *)s_mono_buf, up_bytes,
                                      AUDIO_SAMPLE_RATE, duration);
        if (uerr == ESP_OK) {
            ESP_LOGI(TAG, "[%s] upload finished OK", ctx);
        } else {
            ESP_LOGW(TAG, "[%s] upload failed: %s", ctx, esp_err_to_name(uerr));
        }
    } else if (mono_bytes > 0) {
        ESP_LOGI(TAG, "[%s] offline — skipping upload", ctx);
    }
#endif
}

/* ── Mic capture: UI record screen + BOOT-button hold ─────────────── */

static void audio_task(void *arg)
{
    (void)arg;

    s_cap_buf = heap_caps_malloc(AUDIO_BUF_BYTES, MALLOC_CAP_SPIRAM);
    s_mono_buf = heap_caps_malloc(AUDIO_BUF_BYTES / 2, MALLOC_CAP_SPIRAM);
    uint8_t *rx_dma_staging =
        heap_caps_malloc(CODEC_I2S_CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_cap_buf || !s_mono_buf || !rx_dma_staging) {
        ESP_LOGE(TAG, "failed to allocate audio buffers (PSRAM and/or %u B internal DMA staging)",
                 CODEC_I2S_CHUNK_BYTES);
        if (rx_dma_staging) {
            heap_caps_free(rx_dma_staging);
        }
        if (s_mono_buf) {
            heap_caps_free(s_mono_buf);
        }
        if (s_cap_buf) {
            heap_caps_free(s_cap_buf);
        }
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
    i2s_chan_handle_t tx = hal_codec_get_tx();

    for (;;) {
        /* UI-driven capture (touch Record on family screen) */
        if (atomic_load_explicit(&s_ui_mic_recording, memory_order_acquire)) {
            audio_lock();
            ESP_LOGI(TAG, "[UI] microphone capture started (stop button or timeout ends session)");
            hal_led_set(true);
            if (i2s_duplex_mic_start(tx, rx) != ESP_OK) {
                hal_led_set(false);
                audio_unlock();
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            size_t cap_bytes = 0;
            bool logged_first_rx = false;
            while (atomic_load_explicit(&s_ui_mic_recording, memory_order_relaxed) &&
                   cap_bytes < AUDIO_BUF_BYTES) {
                i2s_duplex_tx_silence_tick(tx);
                size_t want = CODEC_I2S_CHUNK_BYTES;
                if (cap_bytes + want > AUDIO_BUF_BYTES) {
                    want = AUDIO_BUF_BYTES - cap_bytes;
                }
                size_t got = 0;
                esp_err_t r = i2s_channel_read(rx, rx_dma_staging, want, &got, pdMS_TO_TICKS(150));
                /* Short timeout so we notice stop quickly; first RX DMA may not be ready yet —
                 * do not abort on timeout. */
                if (r != ESP_OK && r != ESP_ERR_TIMEOUT) {
                    ESP_LOGW(TAG, "[UI] i2s_channel_read: %s", esp_err_to_name(r));
                    break;
                }
                if (got > 0) {
                    memcpy((uint8_t *)s_cap_buf + cap_bytes, rx_dma_staging, got);
                }
                cap_bytes += got;
                if (got > 0 && !logged_first_rx) {
                    ESP_LOGI(TAG, "[UI] RX DMA active (first chunk %u bytes)", (unsigned)got);
                    logged_first_rx = true;
                }
                if (r == ESP_ERR_TIMEOUT && got == 0) {
                    continue;
                }
            }
            i2s_duplex_mic_stop(tx, rx);
            hal_led_set(false);
            ESP_LOGI(TAG, "[UI] microphone capture ended, raw stereo ring buffer: %u bytes",
                     (unsigned)cap_bytes);
            finish_capture_and_play("UI", cap_bytes);
            audio_unlock();
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }

        if (gpio_get_level(PIN_BOOT_BTN) != 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        audio_lock();
        ESP_LOGI(TAG, "[BOOT] hold-to-capture started (release button or buffer full to stop)");
        hal_led_set(true);

        if (i2s_duplex_mic_start(tx, rx) != ESP_OK) {
            hal_led_set(false);
            audio_unlock();
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        size_t cap_bytes = 0;
        while (gpio_get_level(PIN_BOOT_BTN) == 0 && cap_bytes < AUDIO_BUF_BYTES) {
            i2s_duplex_tx_silence_tick(tx);
            size_t want = CODEC_I2S_CHUNK_BYTES;
            if (cap_bytes + want > AUDIO_BUF_BYTES) {
                want = AUDIO_BUF_BYTES - cap_bytes;
            }
            size_t got = 0;
            esp_err_t r = i2s_channel_read(rx, rx_dma_staging, want, &got, portMAX_DELAY);
            if (r != ESP_OK) {
                ESP_LOGW(TAG, "[BOOT] i2s_channel_read: %s", esp_err_to_name(r));
                break;
            }
            if (got > 0) {
                memcpy((uint8_t *)s_cap_buf + cap_bytes, rx_dma_staging, got);
            }
            cap_bytes += got;
        }
        i2s_duplex_mic_stop(tx, rx);
        hal_led_set(false);
        ESP_LOGI(TAG, "[BOOT] capture ended, raw stereo ring buffer: %u bytes", (unsigned)cap_bytes);
        finish_capture_and_play("BOOT", cap_bytes);
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

    xTaskCreate(audio_task, "audio", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "Ready. Record: open a family → red button (serial tag [UI]); or BOOT hold ([BOOT]).");
}
