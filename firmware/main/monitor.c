#include "monitor.h"
#include "storage.h"
#include "wifi.h"
#include "led.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "monitor";

#define RESPONSE_BUF_SIZE  8192
#define FIRST_POLL_DELAY_S 5
#define HTTP_TIMEOUT_MS    15000

static TaskHandle_t s_task_handle = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static monitor_status_t s_status = MONITOR_STATUS_UNKNOWN;
static monitor_instance_result_t s_results[MAX_INSTANCES];
static int s_result_count = 0;

typedef struct {
    bool api_reachable;
    bool api_key_valid;
    int monitors_up;
    int monitors_down;
} fetch_result_t;

static void parse_metrics(const char *data, size_t len, fetch_result_t *result)
{
    /* Parse Prometheus text format line by line.
     * Lines of interest: "monitor_status{...} <value>"
     * Value: 0=DOWN, 1=UP, 2=PENDING, 3=MAINTENANCE (ignore 2,3) */
    const char *p = data;
    const char *end = data + len;

    while (p < end) {
        /* Find start of line */
        const char *line_start = p;

        /* Find end of line */
        const char *line_end = memchr(p, '\n', end - p);
        if (!line_end) {
            line_end = end;
        }
        p = line_end + 1;

        /* Check if line starts with "monitor_status{" */
        const char *prefix = "monitor_status{";
        size_t prefix_len = strlen(prefix);
        size_t line_len = line_end - line_start;

        if (line_len < prefix_len) continue;
        if (memcmp(line_start, prefix, prefix_len) != 0) continue;

        /* Find closing brace */
        const char *brace = memchr(line_start + prefix_len, '}', line_len - prefix_len);
        if (!brace) continue;

        /* Get value after "} " */
        const char *val_start = brace + 1;
        while (val_start < line_end && (*val_start == ' ' || *val_start == '\t')) {
            val_start++;
        }
        if (val_start >= line_end) continue;

        int status = 0;
        while (val_start < line_end && *val_start >= '0' && *val_start <= '9') {
            status = status * 10 + (*val_start - '0');
            val_start++;
        }

        switch (status) {
        case 0:
            result->monitors_down++;
            break;
        case 1:
            result->monitors_up++;
            break;
        /* 2=PENDING, 3=MAINTENANCE: ignore */
        default:
            break;
        }
    }
}

typedef struct {
    char *buf;
    int len;
    int capacity;
} http_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buf_t *buf = (http_buf_t *)evt->user_data;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (buf && evt->data_len > 0) {
            int remaining = buf->capacity - buf->len - 1;
            int to_copy = evt->data_len < remaining ? evt->data_len : remaining;
            if (to_copy > 0) {
                memcpy(buf->buf + buf->len, evt->data, to_copy);
                buf->len += to_copy;
                buf->buf[buf->len] = '\0';
            }
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

static fetch_result_t fetch_metrics(const uptime_instance_t *inst)
{
    fetch_result_t result = {
        .api_reachable = false,
        .api_key_valid = false,
        .monitors_up = 0,
        .monitors_down = 0,
    };

    char url[INSTANCE_URL_LEN + 16];
    snprintf(url, sizeof(url), "%s/metrics", inst->url);

    char *buf = malloc(RESPONSE_BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        return result;
    }
    buf[0] = '\0';

    http_buf_t http_buf = {
        .buf = buf,
        .len = 0,
        .capacity = RESPONSE_BUF_SIZE,
    };

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = http_event_handler,
        .user_data = &http_buf,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        free(buf);
        return result;
    }

    /* Uptime Kuma uses password-only Basic Auth (empty username, apikey as password) */
    esp_http_client_set_username(client, "");
    esp_http_client_set_password(client, inst->apikey);
    esp_http_client_set_method(client, HTTP_METHOD_GET);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            result.api_reachable = true;
            result.api_key_valid = true;
            parse_metrics(buf, http_buf.len, &result);
        } else if (status_code == 401) {
            result.api_reachable = true;
            result.api_key_valid = false;
            ESP_LOGW(TAG, "Instance '%s': HTTP 401 (invalid API key)", inst->name);
        } else {
            ESP_LOGW(TAG, "Instance '%s': HTTP %d", inst->name, status_code);
        }
    } else {
        ESP_LOGW(TAG, "Instance '%s': HTTP error %s", inst->name, esp_err_to_name(err));
        result.api_reachable = false;
    }

    esp_http_client_cleanup(client);
    free(buf);
    return result;
}

static void update_led_state(void)
{
    bool any_api_key_invalid = false;
    bool any_unreachable = false;
    bool any_down = false;
    bool all_up = true;
    bool has_instances = (s_result_count > 0);

    for (int i = 0; i < s_result_count; i++) {
        if (!s_results[i].key_valid) {
            any_api_key_invalid = true;
            all_up = false;
        } else if (!s_results[i].reachable) {
            any_unreachable = true;
            all_up = false;
        } else if (s_results[i].down > 0) {
            any_down = true;
            all_up = false;
        }
    }

    if (!has_instances) {
        /* No instances configured: stay at connecting/startup LED */
        return;
    }

    if (any_api_key_invalid) {
        s_status = MONITOR_STATUS_API_KEY_INVALID;
        led_set_state(LED_ERROR_BLINK);
    } else if (any_unreachable) {
        s_status = MONITOR_STATUS_UNREACHABLE;
        led_set_state(LED_ERROR_BLINK);
    } else if (any_down) {
        s_status = MONITOR_STATUS_SOME_DOWN;
        led_set_state(LED_MONITORS_DOWN);
    } else if (all_up) {
        s_status = MONITOR_STATUS_ALL_UP;
        led_set_state(LED_ALL_UP);
    }
}

static void monitor_task(void *arg)
{
    /* Initial delay */
    vTaskDelay(pdMS_TO_TICKS(FIRST_POLL_DELAY_S * 1000));

    while (1) {
        if (!wifi_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        uptime_instance_t instances[MAX_INSTANCES];
        int count = 0;
        storage_get_instances(instances, &count);

        if (count == 0) {
            ESP_LOGI(TAG, "No instances configured, skipping poll");
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_result_count = 0;
            memset(s_results, 0, sizeof(s_results));
            s_status = MONITOR_STATUS_UNKNOWN;
            xSemaphoreGive(s_mutex);
        } else {
            ESP_LOGI(TAG, "Polling %d instance(s)...", count);

            monitor_instance_result_t new_results[MAX_INSTANCES];

            for (int i = 0; i < count; i++) {
                fetch_result_t fr = fetch_metrics(&instances[i]);

                new_results[i].id = i;
                strncpy(new_results[i].name, instances[i].name, INSTANCE_NAME_LEN - 1);
                new_results[i].name[INSTANCE_NAME_LEN - 1] = '\0';
                new_results[i].reachable = fr.api_reachable;
                new_results[i].key_valid = fr.api_key_valid;
                new_results[i].up = fr.monitors_up;
                new_results[i].down = fr.monitors_down;

                ESP_LOGI(TAG, "  [%s] reachable=%d key_valid=%d up=%d down=%d",
                         instances[i].name, fr.api_reachable, fr.api_key_valid,
                         fr.monitors_up, fr.monitors_down);
            }

            xSemaphoreTake(s_mutex, portMAX_DELAY);
            memcpy(s_results, new_results, count * sizeof(monitor_instance_result_t));
            s_result_count = count;
            xSemaphoreGive(s_mutex);

            update_led_state();
        }

        /* Wait for poll interval, allow abort via xTaskAbortDelay */
        uint16_t interval_s = 60;
        storage_get_poll_interval(&interval_s);
        if (interval_s < 5) interval_s = 5;

        vTaskDelay(pdMS_TO_TICKS((uint32_t)interval_s * 1000));
    }
}

esp_err_t monitor_start(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    BaseType_t ret = xTaskCreate(monitor_task, "monitor", 12288, NULL, 4, &s_task_handle);
    return ret == pdPASS ? ESP_OK : ESP_FAIL;
}

void monitor_stop(void)
{
    if (s_task_handle) {
        vTaskDelete(s_task_handle);
        s_task_handle = NULL;
    }
}

monitor_status_t monitor_get_status(void)
{
    return s_status;
}

void monitor_trigger_poll(void)
{
    if (s_task_handle) {
        xTaskAbortDelay(s_task_handle);
    }
}

int monitor_get_instance_results(monitor_instance_result_t *results, int max_count)
{
    if (!s_mutex || !results) return 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int count = s_result_count < max_count ? s_result_count : max_count;
    memcpy(results, s_results, count * sizeof(monitor_instance_result_t));
    xSemaphoreGive(s_mutex);

    return count;
}
