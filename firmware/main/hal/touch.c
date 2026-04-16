#include "hal.h"

#include "board_config.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "touch";

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static esp_timer_handle_t s_timer = NULL;
static lv_indev_t *s_indev = NULL;

static volatile bool s_touched = false;
static volatile int s_touch_x = 0;
static volatile int s_touch_y = 0;

static volatile uint8_t s_gesture_fifo = HAL_TOUCH_GEST_NONE;
static volatile uint8_t s_prev_gesture_byte = 0;

static void cst816d_read(void)
{
    if (!s_dev) {
        return;
    }

    uint8_t reg = 0x02;
    uint8_t data[6] = {0};
    esp_err_t ret = i2c_master_transmit_receive(s_dev, &reg, 1, data, sizeof(data), 10);
    if (ret != ESP_OK) {
        return;
    }

    uint8_t num_points = data[0] & 0x01;
    if (num_points > 0) {
        s_touch_x = ((data[1] & 0x0F) << 8) | data[2];
        s_touch_y = ((data[3] & 0x0F) << 8) | data[4];
        s_touched = true;
    } else {
        s_touched = false;
    }

    uint8_t greg = 0x01;
    uint8_t gbyte = 0;
    if (i2c_master_transmit_receive(s_dev, &greg, 1, &gbyte, 1, 10) == ESP_OK) {
        if (gbyte != s_prev_gesture_byte) {
            s_prev_gesture_byte = gbyte;
            if (gbyte != HAL_TOUCH_GEST_NONE) {
                s_gesture_fifo = gbyte;
            }
        }
    }
}

static void touch_timer_cb(void *arg)
{
    (void)arg;
    cst816d_read();
}

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (s_touched) {
        data->point.x = s_touch_x;
        data->point.y = s_touch_y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

uint8_t hal_touch_consume_gesture(void)
{
    uint8_t g = s_gesture_fifo;
    s_gesture_fifo = HAL_TOUCH_GEST_NONE;
    return g;
}

esp_err_t hal_touch_init(lv_display_t *disp)
{
    ESP_LOGI(TAG, "Initializing CST816D touch");

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TOUCH_I2C_PORT,
        .sda_io_num = TOUCH_I2C_SDA,
        .scl_io_num = TOUCH_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_bus),
                        TAG, "Touch I2C bus init failed");

    gpio_config_t rst_cfg = {
        .pin_bit_mask = (1ULL << TOUCH_RST_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_cfg);

    gpio_config_t int_cfg = {
        .pin_bit_mask = (1ULL << TOUCH_INT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&int_cfg);

    gpio_set_level(TOUCH_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(TOUCH_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CST816D_I2C_ADDR,
        .scl_speed_hz = TOUCH_I2C_FREQ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_dev),
                        TAG, "Failed to add CST816D to I2C bus");

    uint8_t chip_reg = 0xA3;
    uint8_t chip_id = 0;
    esp_err_t cid_ret = i2c_master_transmit_receive(s_dev, &chip_reg, 1, &chip_id, 1, 100);
    if (cid_ret == ESP_OK) {
        ESP_LOGI(TAG, "CST816D chip ID: 0x%02X", chip_id);
    } else {
        ESP_LOGW(TAG, "CST816D not responding (may still work with polling)");
    }

    esp_timer_create_args_t timer_args = {
        .callback = touch_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "touch_poll",
        .skip_unhandled_events = true,
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_timer), TAG, "Timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_timer, TOUCH_POLL_MS * 1000),
                        TAG, "Timer start failed");

    lvgl_port_lock(0);
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, lvgl_touch_read_cb);
    lv_indev_set_display(s_indev, disp);
    lv_indev_set_scroll_throw(s_indev, 0);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Touch initialized (polling every %d ms)", TOUCH_POLL_MS);
    return ESP_OK;
}
