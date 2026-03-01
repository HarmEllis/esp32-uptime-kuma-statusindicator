#pragma once

#include "esp_err.h"

typedef enum {
    LED_STARTUP,          /* all off */
    LED_CONNECTING,       /* builtin slow blink: 500ms on, 500ms off */
    LED_AP_MODE,          /* builtin fast blink: 250ms on, 250ms off */
    LED_ALL_UP,           /* green solid */
    LED_MONITORS_DOWN,    /* red solid */
    LED_ERROR_BLINK,      /* red fast blink: 100ms on, 100ms off */
    LED_IDENTIFY,         /* all 3 LEDs blink 3x rapidly (Improv identify) */
    LED_RESET_WARN,       /* green+red fast blink (button reset warning) */
    LED_RESET_CONFIRM,    /* all solid (reset confirmed, about to reboot) */
} led_state_t;

/** Start the LED task (GREEN=22, RED=23, BUILTIN=2) */
esp_err_t led_init(void);

/** Set the LED state */
void led_set_state(led_state_t state);

/** Get the current LED state */
led_state_t led_get_state(void);
