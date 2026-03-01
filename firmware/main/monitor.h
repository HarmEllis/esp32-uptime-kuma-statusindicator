#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "storage.h"

typedef enum {
    MONITOR_STATUS_UNKNOWN,
    MONITOR_STATUS_ALL_UP,
    MONITOR_STATUS_SOME_DOWN,
    MONITOR_STATUS_UNREACHABLE,
    MONITOR_STATUS_API_KEY_INVALID,
} monitor_status_t;

typedef struct {
    int id;
    char name[INSTANCE_NAME_LEN];
    bool reachable;
    bool key_valid;
    int up;
    int down;
} monitor_instance_result_t;

/** Start the monitor polling task */
esp_err_t monitor_start(void);

/** Stop the monitor polling task */
void monitor_stop(void);

/** Get the current aggregate monitor status */
monitor_status_t monitor_get_status(void);

/** Trigger an immediate poll (aborts current delay) */
void monitor_trigger_poll(void);

/** Get per-instance results (copies into caller-provided buffer, returns count) */
int monitor_get_instance_results(monitor_instance_result_t *results, int max_count);
