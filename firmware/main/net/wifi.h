#ifndef NET_WIFI_H
#define NET_WIFI_H

#include <stddef.h>

#include "esp_err.h"
#include <stdbool.h>

/** Once: netif + default STA/AP + `esp_wifi_init` + event handlers. */
esp_err_t wifi_init_driver(void);

/**
 * Start STA with credentials and block until IPv4 or timeout.
 * Call only after `wifi_init_driver()`.
 */
bool wifi_sta_connect(const char *ssid, const char *password, int timeout_ms);

/** SoftAP SSID for setup, e.g. Bullerby-A1B2 (from STA MAC). */
void wifi_build_setup_ap_ssid(char *out, size_t out_sz);

/**
 * Open SoftAP `ap_ssid` (no password), DHCP option 114 for captive detection.
 * Stops STA if running. Call only after `wifi_init_driver()`.
 */
void wifi_enter_softap(const char *ap_ssid);

bool wifi_is_connected(void);
bool wifi_wait_connected(int timeout_ms);

/**
 * Active scan for nearby APs. Fills up to `max` unique SSIDs (33-byte slots,
 * NUL-terminated) sorted by signal strength. Returns count, or -1 on failure.
 * Requires SoftAP/APSTA started (e.g. after `wifi_enter_softap`).
 */
int wifi_scan_ssids(char (*ssids)[33], int max);

#endif // NET_WIFI_H
