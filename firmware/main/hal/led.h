#ifndef HAL_LED_H
#define HAL_LED_H

#include <esp_err.h>
#include <stdbool.h>

esp_err_t led_init(void);
void led_set(bool on);

#endif // HAL_LED_H
