#pragma once

/**
 * Bring up SoftAP, HTTP server + DNS redirect. Blocks until POST /save writes
 * NVS and the device reboots (or forever if the user never submits).
 */
void wifi_portal_run(const char *ap_ssid);
