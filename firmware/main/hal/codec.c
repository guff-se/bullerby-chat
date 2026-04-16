#include "hal.h"

#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "es8311.h"

/* I2S sample rate is AUDIO_SAMPLE_RATE (see hal.h). For Opus, 16 kHz is common —
 * add resampling or change PLL when integrating encode. */

static const char *TAG = "codec";

static es8311_handle_t es8311 = NULL;
static i2s_chan_handle_t i2s_tx = NULL;
static i2s_chan_handle_t i2s_rx = NULL;

/* ── Public handles for audio module ─────────────────────────────── */

i2s_chan_handle_t hal_codec_get_tx(void) { return i2s_tx; }
i2s_chan_handle_t hal_codec_get_rx(void) { return i2s_rx; }

/* ── PA control ──────────────────────────────────────────────────── */

void hal_pa_enable(bool on)
{
    gpio_set_level(PIN_PA_EN, on ? 1 : 0);
}

/* ── Init ────────────────────────────────────────────────────────── */

esp_err_t hal_codec_init(void)
{
    ESP_LOGI(TAG, "Initialising ES8311 codec");

    /* PA GPIO */
    gpio_config_t pa_cfg = {
        .pin_bit_mask = 1ULL << PIN_PA_EN,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&pa_cfg);
    hal_pa_enable(false);

    /* I2C0 — legacy driver (es8311 component uses i2c_port_t) */
    i2c_config_t i2c_cfg = {
        .mode          = I2C_MODE_MASTER,
        .sda_io_num    = PIN_I2C0_SDA,
        .scl_io_num    = PIN_I2C0_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master        = { .clk_speed = 400000 },
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM_0, &i2c_cfg), TAG, "I2C0 param failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_NUM_0, i2c_cfg.mode, 0, 0, 0), TAG, "I2C0 driver install failed");

    es8311 = es8311_create(I2C_NUM_0, ES8311_I2C_ADDR);
    if (es8311 == NULL) {
        ESP_LOGE(TAG, "Failed to create ES8311 handle");
        return ESP_FAIL;
    }

    es8311_clock_config_t clk_cfg = {
        .mclk_inverted      = false,
        .sclk_inverted      = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency     = 12288000,
        .sample_frequency   = AUDIO_SAMPLE_RATE,
    };
    ESP_RETURN_ON_ERROR(es8311_init(es8311, &clk_cfg, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16),
                        TAG, "ES8311 init failed");
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(es8311, 80, NULL), TAG, "ES8311 volume failed");

    /* I2S standard mode — full duplex */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &i2s_tx, &i2s_rx), TAG, "I2S channel create failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_WS,
            .dout = PIN_I2S_DIN,   /* ESP DOUT → codec DIN */
            .din  = PIN_I2S_DOUT,  /* codec DOUT → ESP DIN */
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(i2s_tx, &std_cfg), TAG, "I2S TX init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(i2s_rx, &std_cfg), TAG, "I2S RX init failed");

    ESP_LOGI(TAG, "Codec ready (sample rate %d Hz)", AUDIO_SAMPLE_RATE);
    return ESP_OK;
}
