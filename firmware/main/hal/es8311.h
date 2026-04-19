#ifndef HAL_ES8311_H
#define HAL_ES8311_H

#include <esp_err.h>
#include <driver/i2c_master.h>

// Initialize the ES8311 codec over I2C.
// Call after I2C bus is initialized. Configures for 24kHz 16-bit I2S slave mode.
esp_err_t es8311_codec_init(i2c_master_bus_handle_t i2c_bus);

// Set speaker volume (0–100).
esp_err_t es8311_set_volume(uint8_t volume_pct);

// Set microphone gain (0–6, maps to 0dB–42dB in 6dB steps).
esp_err_t es8311_set_mic_gain(uint8_t gain);

// Enable or disable the speaker power amplifier.
esp_err_t es8311_pa_enable(bool enable);

// Read back key config registers and log them. Diagnostic only.
void es8311_dump_registers(void);

#endif // HAL_ES8311_H
