#include "es8311.h"
#include "board_config.h"
#include <esp_log.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "es8311";

// ES8311 register addresses
#define REG_RESET       0x00
#define REG_CLK1        0x01
#define REG_CLK2        0x02
#define REG_CLK3        0x03
#define REG_CLK4        0x04
#define REG_CLK5        0x05
#define REG_CLK6        0x06
#define REG_CLK7        0x07
#define REG_CLK8        0x08
#define REG_SDP_IN      0x09  // DAC serial data port (input to codec)
#define REG_SDP_OUT     0x0A  // ADC serial data port (output from codec)
#define REG_SYS1        0x0B
#define REG_SYS2        0x0C
#define REG_SYS3        0x0D
#define REG_SYS4        0x0E
#define REG_SYS5        0x0F
#define REG_SYS6        0x10
#define REG_SYS7        0x11
#define REG_SYS8        0x12
#define REG_SYS9        0x13
#define REG_SYS10       0x14
#define REG_ADC1        0x15
#define REG_ADC2        0x16
#define REG_ADC_VOL     0x17
#define REG_DAC1        0x31
#define REG_DAC_VOL     0x32
#define REG_DAC3        0x33
#define REG_DAC4        0x34
#define REG_DAC_RAMP    0x37
#define REG_GPIO        0x44
#define REG_GP          0x45
#define REG_CHIPID1     0xFD
#define REG_CHIPID2     0xFE
#define REG_CHIPVER     0xFF

static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

esp_err_t es8311_codec_init(i2c_master_bus_handle_t i2c_bus)
{
    // Add ES8311 as I2C device
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_I2C_ADDR,
        .scl_speed_hz = AUDIO_CODEC_I2C_FREQ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_dev),
                        TAG, "Failed to add ES8311 to I2C bus");

    // Read chip ID to verify communication
    uint8_t chip_id;
    ESP_RETURN_ON_ERROR(read_reg(REG_CHIPID1, &chip_id), TAG, "Failed to read chip ID");
    ESP_LOGI(TAG, "ES8311 chip ID: 0x%02X", chip_id);

    // Soft reset
    ESP_RETURN_ON_ERROR(write_reg(REG_RESET, 0x1F), TAG, "Reset failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(write_reg(REG_GP, 0x00), TAG, "Clear GP failed");

    // ---- Clock configuration ----
    // MCLK from pin, enable BCLK; codec in slave mode (ESP32 is I2S master)
    // MCLK = 256 * fs = 6.144 MHz for 24kHz
    write_reg(REG_CLK1, 0x3F);   // MCLK source=pin, MCLK/BCLK dividers enabled
    write_reg(REG_CLK2, 0x00);   // MCLK pre-divider = 1 (no division)
    write_reg(REG_CLK3, 0x10);   // ADC oversample rate select
    write_reg(REG_CLK4, 0x10);   // DAC oversample rate select
    write_reg(REG_CLK5, 0x00);   // ADC/DAC FS mode (single speed)
    write_reg(REG_CLK6, 0x03);   // BCLK divider (MCLK/BCLK = 8 for 16-bit stereo)
    write_reg(REG_CLK7, 0x00);   // LRCK divider MSB (slave mode — ignored)
    write_reg(REG_CLK8, 0xFF);   // LRCK divider LSB

    // ---- Serial data port ----
    // I2S standard mode, 16-bit word length
    // Bits [7:6]=00 I2S, [5:4]=11 16-bit, [3:2]=00 reserved, [1:0]=00 normal
    write_reg(REG_SDP_IN, 0x0C);   // DAC data input format
    write_reg(REG_SDP_OUT, 0x0C);  // ADC data output format

    // ---- System / power up ----
    write_reg(REG_SYS1, 0x00);   // System: normal operation
    write_reg(REG_SYS2, 0x00);   // System: normal operation
    write_reg(REG_SYS3, 0x01);   // DAC/ADC mode control
    write_reg(REG_SYS4, 0x02);   // Enable VMIDSEL reference
    write_reg(REG_SYS5, 0x44);   // Power up ADC + DAC analog blocks
    write_reg(REG_SYS6, 0x0C);   // Analog reference buffer enable
    write_reg(REG_SYS7, 0x00);   // System power normal

    // ---- ADC (microphone) path ----
    write_reg(REG_SYS8, 0x28);   // Analog PGA gain (~18dB)
    write_reg(REG_SYS9, 0x00);   // MIC bias
    write_reg(REG_SYS10, 0x18);  // ADC input select: analog mic, DMIC off
    write_reg(REG_ADC1, 0x00);   // ADC power up
    write_reg(REG_ADC2, 0x00);   // ADC control
    write_reg(REG_ADC_VOL, 0xBF); // ADC digital volume (~0dB)

    // ---- DAC (speaker) path ----
    write_reg(REG_DAC1, 0x00);   // DAC control
    write_reg(REG_DAC_VOL, 0xBF); // DAC digital volume (~0dB)
    write_reg(REG_DAC3, 0x10);   // DACx2 enable for mono output
    write_reg(REG_DAC_RAMP, 0x08); // DAC ramp rate

    ESP_LOGI(TAG, "ES8311 initialized (24kHz/16-bit slave mode)");

    // Initialize PA pin but keep it off until needed
    gpio_config_t pa_cfg = {
        .pin_bit_mask = (1ULL << AUDIO_PA_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pa_cfg);
    gpio_set_level(AUDIO_PA_PIN, 0);

    return ESP_OK;
}

esp_err_t es8311_set_volume(uint8_t volume_pct)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    if (volume_pct > 100) volume_pct = 100;

    // Map 0–100% to register value 0x00–0xFF
    // 0x00 = -95.5dB (mute), 0xBF = 0dB, 0xFF = +32dB
    // For safety, map 0–100% → 0x00–0xBF (mute to 0dB)
    uint8_t reg_val = (uint8_t)((volume_pct * 0xBF) / 100);
    return write_reg(REG_DAC_VOL, reg_val);
}

esp_err_t es8311_set_mic_gain(uint8_t gain)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    // gain 0–6 maps to PGA values: 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30
    // Each step ≈ 6dB (0dB to ~36dB)
    if (gain > 6) gain = 6;
    uint8_t reg_val = gain * 0x08;
    return write_reg(REG_SYS8, reg_val);
}

esp_err_t es8311_pa_enable(bool enable)
{
    ESP_LOGI(TAG, "PA %s", enable ? "ON" : "OFF");
    gpio_set_level(AUDIO_PA_PIN, enable ? 1 : 0);
    if (enable) {
        vTaskDelay(pdMS_TO_TICKS(50));  // Allow PA to stabilize
    }
    return ESP_OK;
}
