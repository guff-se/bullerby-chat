#include "hal.h"

#include "board_config.h"
#include "es8311.h"

#include "esp_check.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

static const char *TAG = "codec";

static i2c_master_bus_handle_t s_codec_i2c = NULL;
static i2s_chan_handle_t s_i2s_tx = NULL;
static i2s_chan_handle_t s_i2s_rx = NULL;
static bool s_i2s_tx_enabled = false;
static bool s_i2s_rx_enabled = false;

i2s_chan_handle_t hal_codec_get_tx(void)
{
    return s_i2s_tx;
}

i2s_chan_handle_t hal_codec_get_rx(void)
{
    return s_i2s_rx;
}

esp_err_t hal_codec_tx_enable(bool on)
{
    if (!s_i2s_tx) return ESP_ERR_INVALID_STATE;
    if (on == s_i2s_tx_enabled) return ESP_OK;
    esp_err_t err = on ? i2s_channel_enable(s_i2s_tx) : i2s_channel_disable(s_i2s_tx);
    if (err == ESP_OK) s_i2s_tx_enabled = on;
    return err;
}

esp_err_t hal_codec_rx_enable(bool on)
{
    if (!s_i2s_rx) return ESP_ERR_INVALID_STATE;
    if (on == s_i2s_rx_enabled) return ESP_OK;
    esp_err_t err = on ? i2s_channel_enable(s_i2s_rx) : i2s_channel_disable(s_i2s_rx);
    if (err == ESP_OK) s_i2s_rx_enabled = on;
    return err;
}

void hal_pa_enable(bool on)
{
    (void)es8311_pa_enable(on);
}

esp_err_t hal_codec_init(void)
{
    ESP_LOGI(TAG, "Initializing audio codec");

    i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = AUDIO_CODEC_I2C_PORT,
        .sda_io_num = AUDIO_CODEC_I2C_SDA,
        .scl_io_num = AUDIO_CODEC_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_codec_i2c),
                        TAG, "Codec I2C bus init failed");

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

    ESP_RETURN_ON_ERROR(es8311_codec_init(s_codec_i2c), TAG, "ES8311 init failed");
    es8311_set_volume(100);
    es8311_set_mic_gain(6);

    ESP_LOGI(TAG, "Codec ready (%d Hz)", AUDIO_SAMPLE_RATE);
    return ESP_OK;
}
