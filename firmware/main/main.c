#include "storage.h"
#include "led.h"
#include "wifi.h"
#include "improv.h"
#include "auth.h"
#include "http_server.h"
#include "monitor.h"
#include "button.h"
#include "esp_log.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

static void start_mdns(void)
{
    char hostname[HOSTNAME_MAX_LEN];
    storage_get_hostname(hostname, sizeof(hostname));

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return;
    }
    mdns_hostname_set(hostname);
    mdns_instance_name_set("ESP32 Uptime Monitor");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS started: %s.local", hostname);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 Uptime Monitor starting...");

    /* 1. Storage (NVS) */
    ESP_ERROR_CHECK(storage_init());

    /* 2. LED status indicator */
    ESP_ERROR_CHECK(led_init());
    led_set_state(LED_STARTUP);

    /* 3. Auth module */
    ESP_ERROR_CHECK(auth_init());

    /* 4. Boot button (hold 10s = factory reset) */
    ESP_ERROR_CHECK(button_init());

    /* 5. Start Improv WiFi Serial provisioning (always runs) */
    ESP_ERROR_CHECK(improv_start());

    /* 6. WiFi — connect if credentials exist, otherwise start AP */
    if (storage_has_wifi_creds()) {
        ESP_LOGI(TAG, "WiFi credentials found, connecting...");
        esp_err_t err = wifi_init_sta();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WiFi connection failed, starting AP mode");
            wifi_init_ap();
        }
    } else {
        ESP_LOGI(TAG, "No WiFi credentials, starting AP mode");
        wifi_init_ap();
    }

    /* 7. HTTP server (runs in both STA and AP mode) */
    ESP_ERROR_CHECK(http_server_start());

    /* 8. mDNS (useful in STA mode) */
    if (wifi_is_connected()) {
        start_mdns();
    }

    /* 9. Monitor task (checks wifi_is_connected() internally) */
    ESP_ERROR_CHECK(monitor_start());

    ESP_LOGI(TAG, "Boot complete");

    /* Main loop: nothing to do — all work is done by tasks */
    vTaskDelay(portMAX_DELAY);
}
