#include "hal.h"

#include "board_config.h"

#include "driver/rtc_io.h"
#include "esp_log.h"

static const char *TAG = "power";

esp_err_t hal_power_init(void)
{
    /* Mirrors xiaozhi-esp32 sp-esp32-s3-1.28-box: if the board has a push-button
     * soft-power circuit, the regulator only stays enabled while this pin is high.
     * rtc_gpio keeps the level across deep sleep. Harmless on boards without the
     * latch — POWER_HOLD_PIN is otherwise unused. */
    esp_err_t err = rtc_gpio_init(POWER_HOLD_PIN);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "rtc_gpio_init(GPIO%d) failed: %s", POWER_HOLD_PIN, esp_err_to_name(err));
        return err;
    }
    rtc_gpio_set_direction(POWER_HOLD_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(POWER_HOLD_PIN, 1);
    ESP_LOGI(TAG, "power-hold GPIO%d latched HIGH", POWER_HOLD_PIN);
    return ESP_OK;
}
