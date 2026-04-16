#include "led.h"
#include "board_config.h"
#include <driver/gpio.h>

esp_err_t led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(LED_PIN, 0);
    return ESP_OK;
}

void led_set(bool on)
{
    gpio_set_level(LED_PIN, on ? 1 : 0);
}
