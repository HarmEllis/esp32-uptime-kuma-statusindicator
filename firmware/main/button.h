#pragma once

#include "esp_err.h"

/** Start monitoring the BOOT button (GPIO 0) for factory reset */
esp_err_t button_init(void);
