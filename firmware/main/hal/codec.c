#include "hal.h"

#include "board_config.h"

#include "esp_check.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "codec";

static i2c_master_bus_handle_t s_codec_i2c = NULL;
static i2s_chan_handle_t s_i2s_tx = NULL;
static i2s_chan_handle_t s_i2s_rx = NULL;

static const audio_codec_data_if_t *s_data_if = NULL;
static const audio_codec_ctrl_if_t *s_ctrl_if = NULL;
static const audio_codec_gpio_if_t *s_gpio_if = NULL;
static const audio_codec_if_t      *s_codec_if = NULL;
static esp_codec_dev_handle_t       s_dev = NULL;

i2s_chan_handle_t hal_codec_get_tx(void) { return s_i2s_tx; }
i2s_chan_handle_t hal_codec_get_rx(void) { return s_i2s_rx; }

void hal_pa_enable(bool on)
{
    gpio_set_level(AUDIO_PA_PIN, on ? 1 : 0);
    ESP_LOGI(TAG, "PA %s", on ? "ON" : "OFF");
}

esp_err_t hal_codec_init(void)
{
    ESP_LOGI(TAG, "Initializing audio codec (esp_codec_dev / ES8311)");

    /* ── I2C bus shared with ES8311 ───────────────────────────────────── */
    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = AUDIO_CODEC_I2C_PORT,
        .sda_io_num = AUDIO_CODEC_I2C_SDA,
        .scl_io_num = AUDIO_CODEC_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_codec_i2c),
                        TAG, "Codec I2C bus init failed");

    /* ── I2S duplex channels (master, stereo 16-bit) ──────────────────── */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    chan_cfg.auto_clear_after_cb = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx),
                        TAG, "I2S channel creation failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_I2S_MCLK_PIN,
            .bclk = AUDIO_I2S_BCLK_PIN,
            .ws   = AUDIO_I2S_WS_PIN,
            .dout = AUDIO_I2S_DOUT_PIN,
            .din  = AUDIO_I2S_DIN_PIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx, &std_cfg), TAG, "I2S TX init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_rx, &std_cfg), TAG, "I2S RX init failed");

    /* xiaozhi pattern: enable both channels once, leave running for the lifetime of the app. */
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx), TAG, "I2S TX enable failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx), TAG, "I2S RX enable failed");

    /* ── esp_codec_dev wrappers (ES8311) ──────────────────────────────── */
    audio_codec_i2s_cfg_t i2s_if_cfg = {
        .port       = AUDIO_I2S_PORT,
        .rx_handle  = s_i2s_rx,
        .tx_handle  = s_i2s_tx,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_if_cfg);
    ESP_RETURN_ON_FALSE(s_data_if, ESP_FAIL, TAG, "i2s data_if alloc failed");

    audio_codec_i2c_cfg_t i2c_if_cfg = {
        .port        = AUDIO_CODEC_I2C_PORT,
        .addr        = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle  = s_codec_i2c,
    };
    s_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_if_cfg);
    ESP_RETURN_ON_FALSE(s_ctrl_if, ESP_FAIL, TAG, "i2c ctrl_if alloc failed");

    s_gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(s_gpio_if, ESP_FAIL, TAG, "gpio_if alloc failed");

    /* We toggle the PA ourselves via hal_pa_enable (gated around playback only).
     * Passing GPIO_NUM_NC here stops esp_codec_dev from driving the same pin. */
    es8311_codec_cfg_t es_cfg = {
        .ctrl_if      = s_ctrl_if,
        .gpio_if      = s_gpio_if,
        .codec_mode   = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin       = GPIO_NUM_NC,
        .use_mclk     = true,
        .hw_gain      = { .pa_voltage = 5.0f, .codec_dac_voltage = 3.3f },
        .pa_reverted  = false,
    };
    s_codec_if = es8311_codec_new(&es_cfg);
    ESP_RETURN_ON_FALSE(s_codec_if, ESP_FAIL, TAG, "es8311_codec_new failed");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = s_codec_if,
        .data_if  = s_data_if,
    };
    s_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_dev, ESP_FAIL, TAG, "esp_codec_dev_new failed");

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel         = 1,
        .channel_mask    = 0,
        .sample_rate     = (uint32_t)AUDIO_SAMPLE_RATE,
        .mclk_multiple   = 0,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_dev, &fs), TAG, "esp_codec_dev_open failed");
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(s_dev, 36.0f));  /* dB — SNR/level tradeoff */
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(s_dev, 100));     /* 0–100 */

    /* PA pin: we drive it directly; keep the codec pointed elsewhere. */
    gpio_config_t pa_cfg = {
        .pin_bit_mask = 1ULL << AUDIO_PA_PIN,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&pa_cfg);
    gpio_set_level(AUDIO_PA_PIN, 0);

    ESP_LOGI(TAG, "Codec ready (%d Hz mono, in_gain=36dB, out_vol=100)", AUDIO_SAMPLE_RATE);
    return ESP_OK;
}
