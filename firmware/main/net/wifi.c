#include "wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/inet.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_events;
static volatile bool s_connected;
static bool s_driver_inited;
static bool s_wifi_started;
/** When true, STA disconnect does not trigger reconnect (SoftAP / captive portal). */
static bool s_portal_mode;

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_wifi_events) {
            xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        }
        if (!s_portal_mode) {
            ESP_LOGW(TAG, "disconnected — reconnecting");
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        if (s_wifi_events) {
            xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        }
    }
}

esp_err_t wifi_init_driver(void)
{
    if (s_driver_inited) {
        return ESP_OK;
    }
    s_wifi_events = xEventGroupCreate();
    if (!s_wifi_events) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL),
        TAG, "wifi ev");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL, NULL),
        TAG, "ip ev");

    s_driver_inited = true;
    ESP_LOGI(TAG, "driver init (STA + AP netifs)");
    return ESP_OK;
}

bool wifi_is_connected(void)
{
    return s_connected;
}

bool wifi_wait_connected(int timeout_ms)
{
    if (!s_wifi_events) {
        return false;
    }
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, ticks);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

bool wifi_sta_connect(const char *ssid, const char *password, int timeout_ms)
{
    if (!ssid || ssid[0] == '\0' || !s_driver_inited) {
        return false;
    }

    s_portal_mode = false;
    s_connected = false;
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);

    if (s_wifi_started) {
        esp_wifi_stop();
        s_wifi_started = false;
    }

    wifi_config_t wifi_cfg = {0};
    strlcpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    if (password) {
        strlcpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password));
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_mode STA: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config STA: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_start: %s", esp_err_to_name(err));
        return false;
    }
    s_wifi_started = true;
    ESP_LOGI(TAG, "STA connecting to \"%s\"…", ssid);

    if (wifi_wait_connected(timeout_ms)) {
        return true;
    }
    ESP_LOGW(TAG, "no IP within %d ms", timeout_ms);
    return false;
}

void wifi_build_setup_ap_ssid(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) {
        return;
    }
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, out_sz, "Bullerby-%02X%02X", mac[4], mac[5]);
}

void wifi_enter_softap(const char *ap_ssid)
{
    if (!ap_ssid || ap_ssid[0] == '\0' || !s_driver_inited) {
        return;
    }

    s_portal_mode = true;
    s_connected = false;
    if (s_wifi_events) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }

    if (s_wifi_started) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        s_wifi_started = false;
    }

    wifi_config_t apcfg = {0};
    size_t len = strlen(ap_ssid);
    if (len >= sizeof(apcfg.ap.ssid)) {
        len = sizeof(apcfg.ap.ssid) - 1;
    }
    memcpy((char *)apcfg.ap.ssid, ap_ssid, len);
    apcfg.ap.ssid_len = (uint8_t)len;
    apcfg.ap.max_connection = 4;
    apcfg.ap.authmode = WIFI_AUTH_OPEN;
    apcfg.ap.channel = 1;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_mode AP: %s", esp_err_to_name(err));
        return;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &apcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config AP: %s", esp_err_to_name(err));
        return;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_start AP: %s", esp_err_to_name(err));
        return;
    }
    s_wifi_started = true;

    esp_netif_ip_info_t ip_info;
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        esp_netif_get_ip_info(ap_netif, &ip_info);
        char ip_addr[16];
        inet_ntoa_r(ip_info.ip.addr, ip_addr, sizeof(ip_addr));

        char uri[48];
        snprintf(uri, sizeof(uri), "http://%s", ip_addr);
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, uri, strlen(uri));
        esp_netif_dhcps_start(ap_netif);

        ESP_LOGI(TAG, "SoftAP \"%s\" at %s (DHCP captive URI set)", ap_ssid, ip_addr);
    } else {
        ESP_LOGW(TAG, "SoftAP \"%s\" started (no WIFI_AP_DEF netif)", ap_ssid);
    }
}
