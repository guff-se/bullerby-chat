#include "audio.h"
#include "board_config.h"
#include "es8311.h"
#include "esp_check.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <driver/i2s_std.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char *TAG = "audio";

static i2c_master_bus_handle_t s_codec_i2c = NULL;
static i2s_chan_handle_t s_i2s_tx = NULL;
static i2s_chan_handle_t s_i2s_rx = NULL;

static uint8_t *s_rec_buffer = NULL;    // PSRAM recording buffer (mono 16-bit PCM)
static size_t s_rec_len = 0;            // Bytes recorded so far
static volatile audio_state_t s_state = AUDIO_STATE_IDLE;
static TaskHandle_t s_task = NULL;

// I2S reads stereo frames (L+R). We only keep the left channel (mic data).
// Each stereo frame = 4 bytes (16-bit L + 16-bit R). Mono = 2 bytes.
#define I2S_READ_CHUNK      480        // Stereo samples per read (= 10ms at 24kHz)
#define I2S_READ_BYTES      (I2S_READ_CHUNK * 4)  // Bytes per stereo chunk

static void recording_task(void *arg)
{
    uint8_t *chunk = heap_caps_malloc(I2S_READ_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!chunk) {
        ESP_LOGE(TAG, "Failed to alloc I2S read buffer");
        s_state = AUDIO_STATE_IDLE;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Recording started");
    es8311_pa_enable(false);  // Mute speaker during recording to avoid feedback
    i2s_channel_enable(s_i2s_rx);

    while (s_state == AUDIO_STATE_RECORDING && s_rec_len < AUDIO_BUFFER_SIZE) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(s_i2s_rx, chunk, I2S_READ_BYTES, &bytes_read, pdMS_TO_TICKS(100));
        if (ret != ESP_OK || bytes_read == 0) continue;

        // Extract left channel (mono) from interleaved stereo
        int16_t *stereo = (int16_t *)chunk;
        int16_t *mono = (int16_t *)(s_rec_buffer + s_rec_len);
        size_t stereo_samples = bytes_read / 4;  // 4 bytes per stereo frame
        size_t mono_bytes = stereo_samples * 2;

        if (s_rec_len + mono_bytes > AUDIO_BUFFER_SIZE) {
            mono_bytes = AUDIO_BUFFER_SIZE - s_rec_len;
            stereo_samples = mono_bytes / 2;
        }

        for (size_t i = 0; i < stereo_samples; i++) {
            mono[i] = stereo[i * 2];  // Left channel
        }
        s_rec_len += mono_bytes;
    }

    i2s_channel_disable(s_i2s_rx);
    free(chunk);

    if (s_state == AUDIO_STATE_RECORDING) {
        s_state = AUDIO_STATE_IDLE;  // Max duration reached
    }

    float secs = (float)s_rec_len / (AUDIO_SAMPLE_RATE * 2);
    ESP_LOGI(TAG, "Recording stopped: %u bytes (%.1f sec)", (unsigned)s_rec_len, secs);

    s_task = NULL;
    vTaskDelete(NULL);
}

static void playback_task(void *arg)
{
    ESP_LOGI(TAG, "Playback started (%u bytes)", (unsigned)s_rec_len);
    es8311_pa_enable(true);
    i2s_channel_enable(s_i2s_tx);

    // We need to expand mono back to stereo for I2S output
    size_t chunk_mono_bytes = I2S_READ_CHUNK * 2;  // Mono chunk
    uint8_t *stereo_chunk = heap_caps_malloc(I2S_READ_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!stereo_chunk) {
        ESP_LOGE(TAG, "Failed to alloc I2S write buffer");
        goto done;
    }

    size_t offset = 0;
    while (s_state == AUDIO_STATE_PLAYING && offset < s_rec_len) {
        size_t remain = s_rec_len - offset;
        size_t mono_bytes = (remain < chunk_mono_bytes) ? remain : chunk_mono_bytes;
        size_t samples = mono_bytes / 2;

        // Expand mono to stereo (duplicate L→R)
        int16_t *mono = (int16_t *)(s_rec_buffer + offset);
        int16_t *stereo = (int16_t *)stereo_chunk;
        for (size_t i = 0; i < samples; i++) {
            stereo[i * 2] = mono[i];      // Left
            stereo[i * 2 + 1] = mono[i];  // Right (duplicate)
        }

        size_t bytes_written = 0;
        i2s_channel_write(s_i2s_tx, stereo_chunk, samples * 4, &bytes_written, pdMS_TO_TICKS(100));
        offset += mono_bytes;
    }

    free(stereo_chunk);

done:
    i2s_channel_disable(s_i2s_tx);
    es8311_pa_enable(false);
    s_state = AUDIO_STATE_IDLE;
    ESP_LOGI(TAG, "Playback finished");

    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t audio_init(void)
{
    ESP_LOGI(TAG, "Initializing audio subsystem");

    // ---- Codec I2C bus (I2C_NUM_0) ----
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = AUDIO_CODEC_I2C_PORT,
        .sda_io_num = AUDIO_CODEC_I2C_SDA,
        .scl_io_num = AUDIO_CODEC_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_codec_i2c),
                        TAG, "Codec I2C bus init failed");

    // ---- I2S channels ----
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx),
                        TAG, "I2S channel creation failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_I2S_MCLK_PIN,
            .bclk = AUDIO_I2S_BCLK_PIN,
            .ws = AUDIO_I2S_WS_PIN,
            .dout = AUDIO_I2S_DOUT_PIN,
            .din = AUDIO_I2S_DIN_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx, &std_cfg),
                        TAG, "I2S TX init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_rx, &std_cfg),
                        TAG, "I2S RX init failed");

    // ---- ES8311 codec ----
    ESP_RETURN_ON_ERROR(es8311_codec_init(s_codec_i2c), TAG, "ES8311 init failed");
    es8311_set_volume(70);
    es8311_set_mic_gain(4);  // ~24dB

    // ---- Recording buffer in PSRAM ----
    s_rec_buffer = heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_rec_buffer) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes in PSRAM for recording buffer",
                 AUDIO_BUFFER_SIZE);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Audio initialized (buffer=%dKB in PSRAM)", AUDIO_BUFFER_SIZE / 1024);
    return ESP_OK;
}

esp_err_t audio_start_recording(void)
{
    if (s_state != AUDIO_STATE_IDLE) return ESP_ERR_INVALID_STATE;

    s_rec_len = 0;
    s_state = AUDIO_STATE_RECORDING;
    xTaskCreate(recording_task, "record", 4096, NULL, 5, &s_task);
    return ESP_OK;
}

size_t audio_stop_recording(void)
{
    if (s_state != AUDIO_STATE_RECORDING) return 0;
    s_state = AUDIO_STATE_IDLE;

    // Wait for task to finish
    while (s_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return s_rec_len;
}

esp_err_t audio_start_playback(void)
{
    if (s_state != AUDIO_STATE_IDLE) return ESP_ERR_INVALID_STATE;
    if (s_rec_len == 0) {
        ESP_LOGW(TAG, "Nothing to play");
        return ESP_ERR_INVALID_STATE;
    }

    s_state = AUDIO_STATE_PLAYING;
    xTaskCreate(playback_task, "playback", 4096, NULL, 5, &s_task);
    return ESP_OK;
}

void audio_stop_playback(void)
{
    if (s_state != AUDIO_STATE_PLAYING) return;
    s_state = AUDIO_STATE_IDLE;

    while (s_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

audio_state_t audio_get_state(void)
{
    return s_state;
}

const uint8_t *audio_get_buffer(size_t *out_len)
{
    if (out_len) *out_len = s_rec_len;
    return s_rec_buffer;
}
