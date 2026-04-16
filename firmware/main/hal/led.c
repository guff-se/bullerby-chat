#include "hal.h"

#include "board_config.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "led";

static adc_oneshot_unit_handle_t adc_handle = NULL;

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

    gpio_config_t chg_cfg = {
        .pin_bit_mask = (1ULL << BATTERY_CHARGE_PIN),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&chg_cfg);

    adc_oneshot_unit_init_cfg_t adc_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&adc_cfg, &adc_handle);
    if (err == ESP_OK) {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &chan_cfg);
    }

    ESP_LOGI(TAG, "LED & battery monitoring ready");
    return ESP_OK;
}

void hal_led_set(bool on)
{
    gpio_set_level(LED_PIN, on ? 1 : 0);
}

static const struct {
    int adc;
    int pct;
} batt_curve[] = {
    {1980, 0}, {2100, 10}, {2180, 30},
    {2250, 50}, {2370, 80}, {2480, 100},
};
#define CURVE_LEN (sizeof(batt_curve) / sizeof(batt_curve[0]))

int hal_battery_percent(void)
{
    if (adc_handle == NULL) {
        return -1;
    }

    int raw = 0;
    if (adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &raw) != ESP_OK) {
        return -1;
    }

    if (raw <= batt_curve[0].adc) {
        return 0;
    }
    if (raw >= batt_curve[CURVE_LEN - 1].adc) {
        return 100;
    }

    for (int i = 1; i < CURVE_LEN; i++) {
        if (raw <= batt_curve[i].adc) {
            int d_adc = batt_curve[i].adc - batt_curve[i - 1].adc;
            int d_pct = batt_curve[i].pct - batt_curve[i - 1].pct;
            return batt_curve[i - 1].pct + (raw - batt_curve[i - 1].adc) * d_pct / d_adc;
        }
    }
    return 100;
}

bool hal_battery_charging(void)
{
    return gpio_get_level(BATTERY_CHARGE_PIN) == 0;
}
