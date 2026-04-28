#include "wifi_portal.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dns_server.h"
#include "wifi.h"

static const char *TAG = "wifi_portal";

#define NVS_NS "bullerby"
#define NVS_KEY_SSID "wifi_ssid"
#define NVS_KEY_PASS "wifi_pass"

static const char s_html[] =
    "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Bullerby WiFi</title>"
    "<style>"
    "body{font-family:sans-serif;padding:1rem;max-width:30rem;margin:auto}"
    "input{width:100%;padding:.4rem;font-size:1rem;box-sizing:border-box}"
    "button{padding:.5rem 1rem;font-size:1rem}"
    "#nets{list-style:none;padding:0;margin:.5rem 0;border:1px solid #ccc;border-radius:.3rem;max-height:14rem;overflow:auto}"
    "#nets li{padding:.55rem .7rem;cursor:pointer;border-bottom:1px solid #eee}"
    "#nets li:last-child{border-bottom:0}"
    "#nets li:hover{background:#f0f0f0}"
    "#nets li.sel{background:#dde7ff}"
    ".muted{color:#666;font-size:.9rem}"
    "</style></head><body>"
    "<h2>Bullerby WiFi</h2>"
    "<p class=\"muted\">Tap a network or type its name.</p>"
    "<ul id=\"nets\"><li class=\"muted\">Scanning…</li></ul>"
    "<p><button type=\"button\" id=\"rescan\">Rescan</button></p>"
    "<form method=\"POST\" action=\"/save\">"
    "<p><label>Network name (SSID)<br><input id=\"ssid\" name=\"ssid\" required maxlength=\"32\"></label></p>"
    "<p><label>Password (empty if open)<br><input name=\"pass\" type=\"password\" maxlength=\"64\"></label></p>"
    "<p><button type=\"submit\">Save &amp; reboot</button></p>"
    "</form>"
    "<script>"
    "const ul=document.getElementById('nets'),ssid=document.getElementById('ssid');"
    "function scan(){ul.innerHTML='<li class=\"muted\">Scanning…</li>';"
    "fetch('/scan').then(r=>r.json()).then(j=>{ul.innerHTML='';"
    "if(!j.networks||!j.networks.length){ul.innerHTML='<li class=\"muted\">No networks found.</li>';return;}"
    "j.networks.forEach(n=>{const li=document.createElement('li');li.textContent=n;"
    "li.onclick=()=>{ssid.value=n;[...ul.children].forEach(c=>c.classList.remove('sel'));li.classList.add('sel');};"
    "ul.appendChild(li);});}).catch(()=>{ul.innerHTML='<li class=\"muted\">Scan failed.</li>';});}"
    "document.getElementById('rescan').onclick=scan;scan();"
    "</script></body></html>";

static void url_decode(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '+') {
            *w++ = ' ';
            r++;
        } else if (*r == '%' && r[1] && r[2]) {
            char hex[3] = {r[1], r[2], 0};
            *w++ = (char)strtol(hex, NULL, 16);
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, s_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t scan_get_handler(httpd_req_t *req)
{
    char ssids[16][33];
    int n = wifi_scan_ssids(ssids, 16);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"networks\":[");
    for (int i = 0; i < n; i++) {
        if (i) {
            httpd_resp_sendstr_chunk(req, ",");
        }
        httpd_resp_sendstr_chunk(req, "\"");
        for (const char *p = ssids[i]; *p; p++) {
            char c = *p;
            if (c == '"' || c == '\\') {
                char esc[3] = {'\\', c, 0};
                httpd_resp_sendstr_chunk(req, esc);
            } else if ((unsigned char)c < 0x20) {
                continue;
            } else {
                char buf[2] = {c, 0};
                httpd_resp_sendstr_chunk(req, buf);
            }
        }
        httpd_resp_sendstr_chunk(req, "\"");
    }
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t http_404_handler(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirect", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char body[384];
    int r = httpd_req_recv(req, body, sizeof(body) - 1);
    if (r <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_OK;
    }
    body[r] = '\0';

    char ssid[33] = {0};
    char pass[65] = {0};
    if (httpd_query_key_value(body, "ssid", ssid, sizeof(ssid)) != ESP_OK || ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid required");
        return ESP_OK;
    }
    url_decode(ssid);
    if (httpd_query_key_value(body, "pass", pass, sizeof(pass)) != ESP_OK) {
        pass[0] = '\0';
    }
    url_decode(pass);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "nvs");
        return ESP_OK;
    }
    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY_PASS, pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs save: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "saved WiFi SSID \"%s\" — rebooting", ssid);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK. Rebooting…", HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

void wifi_portal_run(const char *ap_ssid)
{
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    wifi_enter_softap(ap_ssid);

    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets = 7;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;

    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }

    httpd_uri_t u_root = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
    httpd_uri_t u_scan = {.uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler};
    httpd_uri_t u_save = {.uri = "/save", .method = HTTP_POST, .handler = save_post_handler};
    httpd_register_uri_handler(server, &u_root);
    httpd_register_uri_handler(server, &u_scan);
    httpd_register_uri_handler(server, &u_save);
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_handler);

    dns_server_config_t dcfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    dns_server_handle_t dns = start_dns_server(&dcfg);
    if (!dns) {
        ESP_LOGE(TAG, "DNS server start failed");
    }

    ESP_LOGI(TAG, "captive portal running — join \"%s\" and open http://192.168.4.1/", ap_ssid);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
