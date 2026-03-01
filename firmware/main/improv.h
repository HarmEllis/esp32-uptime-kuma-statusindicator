#pragma once

#include "esp_err.h"

/**
 * Start the Improv WiFi Serial provisioning task.
 * Listens on UART0 at 115200 baud for Improv Serial packets.
 * When WiFi credentials are received, stores them and triggers WiFi connection.
 */
esp_err_t improv_start(void);
