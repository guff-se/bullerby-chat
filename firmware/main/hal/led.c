#include "hal.h"

#include "board_config.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "led";

esp_err_t hal_led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    hal_led_set(false);

    ESP_LOGI(TAG, "LED ready");
    return ESP_OK;
}

void hal_led_set(bool on)
{
    gpio_set_level(LED_PIN, on ? 1 : 0);
}
