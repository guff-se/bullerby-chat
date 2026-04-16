#ifndef NET_WIFI_H
#define NET_WIFI_H

#include <esp_err.h>
#include <stdbool.h>

// Initialize WiFi in station mode and connect to the given network.
// Blocks until connected or timeout (10 seconds).
// For the skeleton, SSID and password are passed directly.
// Later: read from NVS or provisioning.
esp_err_t wifi_init_sta(const char *ssid, const char *password);

// Check if WiFi is currently connected.
bool wifi_is_connected(void);

#endif // NET_WIFI_H
