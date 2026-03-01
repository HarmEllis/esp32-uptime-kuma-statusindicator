#pragma once

#include "esp_err.h"
#include <stdbool.h>

/** Start the HTTP server on port 80 */
esp_err_t http_server_start(void);

/** Stop the HTTP server */
esp_err_t http_server_stop(void);

/** Check if HTTP server is running */
bool http_server_is_running(void);
