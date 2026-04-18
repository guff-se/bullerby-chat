#include "wifi.h"

#include <string.h>

#include <esp_check.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT  BIT0

static EventGroupHandle_t s_wifi_events;
static volatile bool s_connected;
static bool s_started;

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "disconnected — reconnecting");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init_sta(const char *ssid, const char *password)
{
    if (s_started) {
        return ESP_OK;
    }
    if (ssid == NULL || ssid[0] == '\0') {
        ESP_LOGE(TAG, "empty SSID — refusing to init WiFi");
        return ESP_ERR_INVALID_ARG;
    }

    s_wifi_events = xEventGroupCreate();

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &on_wifi_event, NULL, NULL),
        TAG, "wifi event register");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &on_wifi_event, NULL, NULL),
        TAG, "ip event register");

    wifi_config_t wifi_cfg = {0};
    strlcpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    if (password) {
        strlcpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password));
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set_mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG, "set_config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");

    s_started = true;
    ESP_LOGI(TAG, "station started, connecting to \"%s\"…", ssid);
    return ESP_OK;
}

bool wifi_is_connected(void)
{
    return s_connected;
}

bool wifi_wait_connected(int timeout_ms)
{
    if (!s_wifi_events) return false;
    TickType_t ticks = (timeout_ms < 0)
                           ? portMAX_DELAY
                           : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE, ticks);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}
