#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_INSTANCES       8
#define INSTANCE_ID_LEN     37
#define INSTANCE_NAME_LEN   48
#define INSTANCE_URL_LEN    128
#define INSTANCE_APIKEY_LEN 64

#define PSK_MAX_LEN      64
#define HOSTNAME_MAX_LEN 32
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MAX_LEN 64

typedef struct {
    char id[INSTANCE_ID_LEN];           /* UUID string */
    char name[INSTANCE_NAME_LEN];
    char url[INSTANCE_URL_LEN];         /* e.g. "https://kuma.example.com" */
    char apikey[INSTANCE_APIKEY_LEN];
} uptime_instance_t;

/** Initialize NVS and storage namespaces */
esp_err_t storage_init(void);

/** WiFi credentials */
esp_err_t storage_set_wifi_creds(const char *ssid, const char *password);
esp_err_t storage_get_wifi_ssid(char *ssid, size_t len);
esp_err_t storage_get_wifi_pass(char *pass, size_t len);
bool storage_has_wifi_creds(void);

/** Uptime Kuma instances */
esp_err_t storage_get_instances(uptime_instance_t *instances, int *count);
esp_err_t storage_set_instances(const uptime_instance_t *instances, int count);
esp_err_t storage_add_instance(const uptime_instance_t *inst);
esp_err_t storage_update_instance(int id, const uptime_instance_t *inst);
esp_err_t storage_delete_instance(int id);

/** Poll interval (seconds) */
esp_err_t storage_get_poll_interval(uint16_t *interval_s);
esp_err_t storage_set_poll_interval(uint16_t interval_s);

/** Config: PSK and hostname */
esp_err_t storage_set_psk(const char *psk);
esp_err_t storage_get_psk(char *psk, size_t len);
bool storage_has_psk(void);

esp_err_t storage_set_hostname(const char *hostname);
esp_err_t storage_get_hostname(char *hostname, size_t len);

/** Erase all NVS data (factory reset) */
esp_err_t storage_factory_reset(void);
