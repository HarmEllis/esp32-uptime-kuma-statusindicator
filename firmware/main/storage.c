#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "storage";

#define NVS_WIFI_NS      "wifi"
#define NVS_INSTANCES_NS "instances"
#define NVS_CONFIG_NS    "config"

#define KEY_SSID         "ssid"
#define KEY_PASS         "pass"
#define KEY_INSTANCES    "instances"
#define KEY_INST_CNT     "inst_cnt"
#define KEY_POLL_IV      "poll_iv"
#define KEY_PSK          "psk"
#define KEY_HOSTNAME     "hostname"
#define KEY_LED_BRIGHTNESS "led_bright"

#define DEFAULT_POLL_INTERVAL_S 60

esp_err_t storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

/* WiFi credentials */

esp_err_t storage_set_wifi_creds(const char *ssid, const char *password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_WIFI_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_PASS, password);
    if (err == ESP_OK) err = nvs_commit(h);

    nvs_close(h);
    return err;
}

esp_err_t storage_get_wifi_ssid(char *ssid, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_WIFI_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    err = nvs_get_str(h, KEY_SSID, ssid, &len);
    nvs_close(h);
    return err;
}

esp_err_t storage_get_wifi_pass(char *pass, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_WIFI_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    err = nvs_get_str(h, KEY_PASS, pass, &len);
    nvs_close(h);
    return err;
}

bool storage_has_wifi_creds(void)
{
    char ssid[WIFI_SSID_MAX_LEN];
    return storage_get_wifi_ssid(ssid, sizeof(ssid)) == ESP_OK && ssid[0] != '\0';
}

/* Uptime Kuma instances */

esp_err_t storage_get_instances(uptime_instance_t *instances, int *count)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_INSTANCES_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        *count = 0;
        return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : err;
    }

    uint8_t cnt = 0;
    err = nvs_get_u8(h, KEY_INST_CNT, &cnt);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *count = 0;
        nvs_close(h);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }

    if (cnt == 0) {
        *count = 0;
        nvs_close(h);
        return ESP_OK;
    }

    size_t blob_len = cnt * sizeof(uptime_instance_t);
    err = nvs_get_blob(h, KEY_INSTANCES, instances, &blob_len);
    if (err == ESP_OK) {
        *count = cnt;
    } else {
        *count = 0;
    }

    nvs_close(h);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

esp_err_t storage_set_instances(const uptime_instance_t *instances, int count)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_INSTANCES_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(h, KEY_INST_CNT, (uint8_t)count);
    if (err == ESP_OK && count > 0) {
        err = nvs_set_blob(h, KEY_INSTANCES, instances, count * sizeof(uptime_instance_t));
    } else if (err == ESP_OK) {
        err = nvs_erase_key(h, KEY_INSTANCES);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) err = nvs_commit(h);

    nvs_close(h);
    return err;
}

esp_err_t storage_add_instance(const uptime_instance_t *inst)
{
    uptime_instance_t instances[MAX_INSTANCES];
    int count = 0;
    esp_err_t err = storage_get_instances(instances, &count);
    if (err != ESP_OK) return err;

    if (count >= MAX_INSTANCES) {
        ESP_LOGE(TAG, "Max instances (%d) reached", MAX_INSTANCES);
        return ESP_ERR_NO_MEM;
    }

    instances[count] = *inst;
    return storage_set_instances(instances, count + 1);
}

esp_err_t storage_update_instance(int id, const uptime_instance_t *inst)
{
    uptime_instance_t instances[MAX_INSTANCES];
    int count = 0;
    esp_err_t err = storage_get_instances(instances, &count);
    if (err != ESP_OK) return err;

    if (id < 0 || id >= count) return ESP_ERR_INVALID_ARG;

    instances[id] = *inst;
    return storage_set_instances(instances, count);
}

esp_err_t storage_delete_instance(int id)
{
    uptime_instance_t instances[MAX_INSTANCES];
    int count = 0;
    esp_err_t err = storage_get_instances(instances, &count);
    if (err != ESP_OK) return err;

    if (id < 0 || id >= count) return ESP_ERR_INVALID_ARG;

    /* Shift remaining instances down */
    for (int i = id; i < count - 1; i++) {
        instances[i] = instances[i + 1];
    }

    return storage_set_instances(instances, count - 1);
}

/* Poll interval */

esp_err_t storage_get_poll_interval(uint16_t *interval_s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_INSTANCES_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        *interval_s = DEFAULT_POLL_INTERVAL_S;
        return ESP_OK;
    }

    err = nvs_get_u16(h, KEY_POLL_IV, interval_s);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *interval_s = DEFAULT_POLL_INTERVAL_S;
        return ESP_OK;
    }
    return err;
}

esp_err_t storage_set_poll_interval(uint16_t interval_s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_INSTANCES_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_u16(h, KEY_POLL_IV, interval_s);
    if (err == ESP_OK) err = nvs_commit(h);

    nvs_close(h);
    return err;
}

/* Config */

esp_err_t storage_set_psk(const char *psk)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_CONFIG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, KEY_PSK, psk);
    if (err == ESP_OK) err = nvs_commit(h);

    nvs_close(h);
    return err;
}

esp_err_t storage_get_psk(char *psk, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_CONFIG_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    err = nvs_get_str(h, KEY_PSK, psk, &len);
    nvs_close(h);
    return err;
}

bool storage_has_psk(void)
{
    char psk[PSK_MAX_LEN];
    return storage_get_psk(psk, sizeof(psk)) == ESP_OK && psk[0] != '\0';
}

esp_err_t storage_set_hostname(const char *hostname)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_CONFIG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, KEY_HOSTNAME, hostname);
    if (err == ESP_OK) err = nvs_commit(h);

    nvs_close(h);
    return err;
}

esp_err_t storage_get_hostname(char *hostname, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_CONFIG_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        strncpy(hostname, "esp-uptimemonitor", len);
        return ESP_OK;
    }

    err = nvs_get_str(h, KEY_HOSTNAME, hostname, &len);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(hostname, "esp-uptimemonitor", len);
        return ESP_OK;
    }
    return err;
}

esp_err_t storage_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset: erasing all NVS data");
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) return err;
    return nvs_flash_init();
}

#ifdef CONFIG_IDF_TARGET_ESP32S3

esp_err_t storage_get_led_brightness(uint8_t *brightness)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_CONFIG_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        *brightness = 30;
        return ESP_OK;
    }

    err = nvs_get_u8(h, KEY_LED_BRIGHTNESS, brightness);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *brightness = 30;
        return ESP_OK;
    }
    if (err == ESP_OK) {
        if (*brightness < 1)   *brightness = 1;
        if (*brightness > 100) *brightness = 100;
    }
    return err;
}

esp_err_t storage_set_led_brightness(uint8_t brightness)
{
    if (brightness < 1 || brightness > 100) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_CONFIG_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(h, KEY_LED_BRIGHTNESS, brightness);
    if (err == ESP_OK) err = nvs_commit(h);

    nvs_close(h);
    return err;
}

#endif /* CONFIG_IDF_TARGET_ESP32S3 */
