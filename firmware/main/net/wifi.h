#ifndef NET_WIFI_H
#define NET_WIFI_H

#include <esp_err.h>
#include <stdbool.h>

/**
 * Bring up the WiFi station and keep it connected in the background.
 * Reads credentials from `ssid` / `password` (usually Kconfig defaults).
 * Returns ESP_OK once the station has started — the event loop will drive
 * connect / reconnect. `wifi_is_connected()` reflects live state.
 *
 * Safe to call once only.
 */
esp_err_t wifi_init_sta(const char *ssid, const char *password);

/** True while the station holds an IPv4 lease. */
bool wifi_is_connected(void);

/** Block up to `timeout_ms` waiting for a GOT_IP event; returns true if obtained. */
bool wifi_wait_connected(int timeout_ms);

#endif // NET_WIFI_H
