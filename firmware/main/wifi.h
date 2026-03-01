#pragma once

#include "esp_err.h"
#include <stdbool.h>

/** Initialize WiFi STA mode and connect using NVS credentials */
esp_err_t wifi_init_sta(void);

/** Initialize WiFi AP mode (UptimeKumaMonitor AP at 192.168.4.1) */
esp_err_t wifi_init_ap(void);

/** Check if WiFi STA is connected */
bool wifi_is_connected(void);

/** Check if AP mode is active */
bool wifi_is_ap_active(void);

/** Get current IP address as string. Returns empty string if not connected. */
void wifi_get_ip_str(char *buf, size_t len);

/** Get current RSSI. Returns 0 if not connected. */
int8_t wifi_get_rssi(void);

/** Disconnect and reconnect with new credentials (already stored in NVS) */
esp_err_t wifi_reconnect(void);
