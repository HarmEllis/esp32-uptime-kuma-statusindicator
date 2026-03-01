#include "wifi.h"
#include "storage.h"
#include "led.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi";

#define AP_SSID     "UptimeKumaMonitor"
#define AP_PASSWORD "FFzppG3oJ76PRs"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          10
#define WIFI_CONNECT_TIMEOUT_MS 120000

static EventGroupHandle_t s_wifi_events;
static int s_retry_count = 0;
static bool s_connected = false;
static bool s_ap_active = false;
static esp_netif_t *s_netif_sta = NULL;
static esp_netif_t *s_netif_ap = NULL;
static char s_ip_str[16] = "";
static bool s_stack_initialized = false;
static bool s_wifi_started = false;
static esp_event_handler_instance_t s_wifi_event_instance = NULL;
static esp_event_handler_instance_t s_ip_event_instance = NULL;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip_str[0] = '\0';
        led_set_state(LED_CONNECTING);

        if (s_retry_count < MAX_RETRY) {
            s_retry_count++;
            ESP_LOGI(TAG, "Reconnecting (attempt %d/%d)", s_retry_count, MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Max retries reached");
            led_set_state(LED_ERROR_BLINK);
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Connected with IP: %s", s_ip_str);
        s_retry_count = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void ensure_stack(void)
{
    if (s_stack_initialized) return;

    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    s_stack_initialized = true;
}

esp_err_t wifi_init_sta(void)
{
    char ssid[WIFI_SSID_MAX_LEN] = {0};
    char pass[WIFI_PASS_MAX_LEN] = {0};

    if (storage_get_wifi_ssid(ssid, sizeof(ssid)) != ESP_OK || ssid[0] == '\0') {
        ESP_LOGW(TAG, "No WiFi credentials stored");
        return ESP_ERR_NOT_FOUND;
    }
    storage_get_wifi_pass(pass, sizeof(pass));

    if (s_wifi_events == NULL) {
        s_wifi_events = xEventGroupCreate();
        if (s_wifi_events == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ensure_stack();

    if (s_netif_sta == NULL) {
        s_netif_sta = esp_netif_create_default_wifi_sta();
        if (s_netif_sta == NULL) {
            return ESP_FAIL;
        }
    }

    if (!s_wifi_started) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    }

    /* Register event handlers if not yet registered (may have been skipped
     * when wifi_init_ap() was called first, which sets s_wifi_started but
     * does not register STA/IP handlers). */
    if (s_wifi_event_instance == NULL) {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &s_wifi_event_instance));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &s_ip_event_instance));
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    led_set_state(LED_CONNECTING);
    s_retry_count = 0;
    s_connected = false;
    s_ip_str[0] = '\0';

    bool started_now = false;
    if (!s_wifi_started) {
        ESP_ERROR_CHECK(esp_wifi_start());
        s_wifi_started = true;
        started_now = true;
    }

    if (!started_now) {
        esp_err_t dis_err = esp_wifi_disconnect();
        if (dis_err != ESP_OK && dis_err != ESP_ERR_WIFI_NOT_CONNECT) {
            ESP_LOGW(TAG, "esp_wifi_disconnect() returned %s", esp_err_to_name(dis_err));
        }

        esp_err_t conn_err = esp_wifi_connect();
        if (conn_err != ESP_OK && conn_err != ESP_ERR_WIFI_CONN) {
            ESP_LOGE(TAG, "esp_wifi_connect() failed: %s", esp_err_to_name(conn_err));
            return conn_err;
        }
    }

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        esp_wifi_set_ps(WIFI_PS_NONE);
        return ESP_OK;
    }
    if (bits == 0) {
        ESP_LOGE(TAG, "WiFi connect timed out");
    }
    return ESP_FAIL;
}

esp_err_t wifi_init_ap(void)
{
    ensure_stack();

    if (s_netif_ap == NULL) {
        s_netif_ap = esp_netif_create_default_wifi_ap();
        if (s_netif_ap == NULL) {
            return ESP_FAIL;
        }
    }

    if (!s_wifi_started) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        s_wifi_started = true;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    wifi_config_t ap_config = {
        .ap = {
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)ap_config.ap.ssid, AP_SSID, sizeof(ap_config.ap.ssid) - 1);
    strncpy((char *)ap_config.ap.password, AP_PASSWORD, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.ssid_len = strlen(AP_SSID);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_ap_active = true;
    led_set_state(LED_AP_MODE);
    ESP_LOGI(TAG, "AP started: SSID=%s, IP=192.168.4.1", AP_SSID);
    return ESP_OK;
}

bool wifi_is_connected(void)
{
    return s_connected;
}

bool wifi_is_ap_active(void)
{
    return s_ap_active;
}

void wifi_get_ip_str(char *buf, size_t len)
{
    strncpy(buf, s_ip_str, len);
    buf[len - 1] = '\0';
}

int8_t wifi_get_rssi(void)
{
    if (!s_connected) return 0;
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

esp_err_t wifi_reconnect(void)
{
    return wifi_init_sta();
}
