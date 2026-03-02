#include "http_server.h"
#include "auth.h"
#include "storage.h"
#include "wifi.h"
#include "led.h"
#include "monitor.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

static const char *TAG = "httpd";
static httpd_handle_t s_server = NULL;
static int64_t s_start_time = 0;

/* ── Helpers ────────────────────────────────────────────────── */

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "ESP32 Uptime Monitor API is running. Use /api/v1/health");
    return ESP_OK;
}

static esp_err_t send_json(httpd_req_t *req, int status, cJSON *json)
{
    char *str = cJSON_PrintUnformatted(json);
    httpd_resp_set_status(req, status == 200 ? "200 OK" :
                                status == 201 ? "201 Created" :
                                status == 400 ? "400 Bad Request" :
                                status == 401 ? "401 Unauthorized" :
                                status == 404 ? "404 Not Found" :
                                status == 429 ? "429 Too Many Requests" :
                                "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, str);
    free(str);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t send_error(httpd_req_t *req, int status, const char *msg)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "error", msg);
    return send_json(req, status, j);
}

static char *read_body(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len > 2048) return NULL;

    char *buf = malloc(len + 1);
    if (!buf) return NULL;

    int received = 0;
    while (received < len) {
        int ret = httpd_req_recv(req, buf + received, len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            free(buf);
            return NULL;
        }
        received += ret;
    }

    buf[len] = '\0';
    return buf;
}

static bool parse_id_from_uri(httpd_req_t *req, const char *prefix, int *id_out)
{
    const char *uri = req->uri;
    size_t prefix_len = strlen(prefix);
    if (strncmp(uri, prefix, prefix_len) != 0) return false;

    const char *id_str = uri + prefix_len;
    if (*id_str == '\0' || *id_str == '?') return false;

    int id = 0;
    while (*id_str && *id_str != '?') {
        if (*id_str < '0' || *id_str > '9') return false;

        int digit = *id_str - '0';
        if (id > (INT_MAX - digit) / 10) return false;
        id = id * 10 + digit;
        id_str++;
    }

    *id_out = id;
    return true;
}

static esp_err_t cors_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ── Monitor status string ────────────────────────────────── */

static const char *monitor_status_str(monitor_status_t s)
{
    switch (s) {
    case MONITOR_STATUS_ALL_UP:          return "all_up";
    case MONITOR_STATUS_SOME_DOWN:       return "some_down";
    case MONITOR_STATUS_UNREACHABLE:     return "unreachable";
    case MONITOR_STATUS_API_KEY_INVALID: return "api_key_invalid";
    default:                             return "unknown";
    }
}

/* ── Health ──────────────────────────────────────────────────── */

static esp_err_t health_handler(httpd_req_t *req)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", "ok");
    cJSON_AddNumberToObject(j, "uptime_s", (double)(esp_timer_get_time() - s_start_time) / 1000000.0);
    cJSON_AddNumberToObject(j, "rssi", wifi_get_rssi());
    cJSON_AddNumberToObject(j, "free_heap", (double)esp_get_free_heap_size());

    char ip[16];
    wifi_get_ip_str(ip, sizeof(ip));
    cJSON_AddStringToObject(j, "ip", ip);
    cJSON_AddBoolToObject(j, "psk_configured", storage_has_psk());
    cJSON_AddStringToObject(j, "monitor_status", monitor_status_str(monitor_get_status()));

    return send_json(req, 200, j);
}

/* ── Auth ────────────────────────────────────────────────────── */

static esp_err_t challenge_handler(httpd_req_t *req)
{
    if (auth_is_locked_out()) {
        return send_error(req, 429, "Too many attempts, try again later");
    }

    char challenge[AUTH_CHALLENGE_LEN * 2 + 1];
    auth_create_challenge(challenge, sizeof(challenge));

    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "challenge", challenge);
    return send_json(req, 200, j);
}

static esp_err_t login_handler(httpd_req_t *req)
{
    if (auth_is_locked_out()) {
        return send_error(req, 429, "Too many attempts, try again later");
    }

    char *body = read_body(req);
    if (!body) return send_error(req, 400, "Invalid body");

    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) return send_error(req, 400, "Invalid JSON");

    const char *challenge = cJSON_GetStringValue(cJSON_GetObjectItem(j, "challenge"));
    const char *response = cJSON_GetStringValue(cJSON_GetObjectItem(j, "response"));

    if (!challenge || !response) {
        cJSON_Delete(j);
        return send_error(req, 400, "Missing challenge or response");
    }

    char token[AUTH_TOKEN_LEN * 2 + 1];
    esp_err_t err = auth_login(challenge, response, token, sizeof(token));
    cJSON_Delete(j);

    if (err == ESP_ERR_INVALID_STATE) {
        return send_error(req, 429, "Too many attempts, try again later");
    }
    if (err != ESP_OK) {
        return send_error(req, 401, "Authentication failed");
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "token", token);
    return send_json(req, 200, resp);
}

/* ── Instances ─────────────────────────────────────────────── */

static esp_err_t instances_get_handler(httpd_req_t *req)
{
    if (!auth_check_request(req)) return send_error(req, 401, "Unauthorized");

    uptime_instance_t instances[MAX_INSTANCES];
    int count = 0;
    storage_get_instances(instances, &count);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddNumberToObject(t, "id", i);
        cJSON_AddStringToObject(t, "uuid", instances[i].id);
        cJSON_AddStringToObject(t, "name", instances[i].name);
        cJSON_AddStringToObject(t, "url", instances[i].url);
        cJSON_AddStringToObject(t, "apikey", instances[i].apikey);
        cJSON_AddItemToArray(arr, t);
    }

    cJSON *j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, "instances", arr);
    return send_json(req, 200, j);
}

static esp_err_t instances_post_handler(httpd_req_t *req)
{
    if (!auth_check_request(req)) return send_error(req, 401, "Unauthorized");

    char *body = read_body(req);
    if (!body) return send_error(req, 400, "Invalid body");

    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) return send_error(req, 400, "Invalid JSON");

    const char *uuid = cJSON_GetStringValue(cJSON_GetObjectItem(j, "uuid"));
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(j, "name"));
    const char *url  = cJSON_GetStringValue(cJSON_GetObjectItem(j, "url"));
    const char *apikey = cJSON_GetStringValue(cJSON_GetObjectItem(j, "apikey"));

    if (!name || !url) {
        cJSON_Delete(j);
        return send_error(req, 400, "Missing name or url");
    }

    uptime_instance_t inst = {0};
    if (uuid) {
        strncpy(inst.id, uuid, INSTANCE_ID_LEN - 1);
    }
    strncpy(inst.name, name, INSTANCE_NAME_LEN - 1);
    strncpy(inst.url, url, INSTANCE_URL_LEN - 1);
    if (apikey) {
        strncpy(inst.apikey, apikey, INSTANCE_APIKEY_LEN - 1);
    }
    cJSON_Delete(j);

    esp_err_t err = storage_add_instance(&inst);
    if (err == ESP_ERR_NO_MEM) return send_error(req, 400, "Maximum instances reached");
    if (err != ESP_OK) return send_error(req, 500, "Failed to save instance");

    monitor_trigger_poll();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "created");
    return send_json(req, 201, resp);
}

static esp_err_t instances_put_handler(httpd_req_t *req)
{
    if (!auth_check_request(req)) return send_error(req, 401, "Unauthorized");

    int id = 0;
    if (!parse_id_from_uri(req, "/api/v1/instances/", &id)) {
        return send_error(req, 400, "Invalid instance id");
    }

    char *body = read_body(req);
    if (!body) return send_error(req, 400, "Invalid body");

    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) return send_error(req, 400, "Invalid JSON");

    const char *uuid = cJSON_GetStringValue(cJSON_GetObjectItem(j, "uuid"));
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(j, "name"));
    const char *url  = cJSON_GetStringValue(cJSON_GetObjectItem(j, "url"));
    const char *apikey = cJSON_GetStringValue(cJSON_GetObjectItem(j, "apikey"));

    if (!name || !url) {
        cJSON_Delete(j);
        return send_error(req, 400, "Missing name or url");
    }

    uptime_instance_t inst = {0};
    if (uuid) {
        strncpy(inst.id, uuid, INSTANCE_ID_LEN - 1);
    }
    strncpy(inst.name, name, INSTANCE_NAME_LEN - 1);
    strncpy(inst.url, url, INSTANCE_URL_LEN - 1);
    if (apikey) {
        strncpy(inst.apikey, apikey, INSTANCE_APIKEY_LEN - 1);
    }
    cJSON_Delete(j);

    esp_err_t err = storage_update_instance(id, &inst);
    if (err == ESP_ERR_INVALID_ARG) return send_error(req, 404, "Instance not found");
    if (err != ESP_OK) return send_error(req, 500, "Failed to update instance");

    monitor_trigger_poll();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "updated");
    return send_json(req, 200, resp);
}

static esp_err_t instances_delete_handler(httpd_req_t *req)
{
    if (!auth_check_request(req)) return send_error(req, 401, "Unauthorized");

    int id = 0;
    if (!parse_id_from_uri(req, "/api/v1/instances/", &id)) {
        return send_error(req, 400, "Invalid instance id");
    }

    esp_err_t err = storage_delete_instance(id);
    if (err == ESP_ERR_INVALID_ARG) return send_error(req, 404, "Instance not found");
    if (err != ESP_OK) return send_error(req, 500, "Failed to delete instance");

    monitor_trigger_poll();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "deleted");
    return send_json(req, 200, resp);
}

/* ── Monitor status ──────────────────────────────────────────── */

static esp_err_t monitor_status_handler(httpd_req_t *req)
{
    if (!auth_check_request(req)) return send_error(req, 401, "Unauthorized");

    monitor_instance_result_t results[MAX_INSTANCES];
    int count = monitor_get_instance_results(results, MAX_INSTANCES);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", results[i].id);
        cJSON_AddStringToObject(item, "name", results[i].name);
        cJSON_AddBoolToObject(item, "reachable", results[i].reachable);
        cJSON_AddBoolToObject(item, "key_valid", results[i].key_valid);
        cJSON_AddNumberToObject(item, "up", results[i].up);
        cJSON_AddNumberToObject(item, "down", results[i].down);
        cJSON_AddItemToArray(arr, item);
    }

    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "status", monitor_status_str(monitor_get_status()));
    cJSON_AddItemToObject(j, "instances", arr);
    return send_json(req, 200, j);
}

/* ── Settings ────────────────────────────────────────────────── */

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    if (!auth_check_request(req)) return send_error(req, 401, "Unauthorized");

    char hostname[HOSTNAME_MAX_LEN];
    storage_get_hostname(hostname, sizeof(hostname));

    char ssid[WIFI_SSID_MAX_LEN] = {0};
    storage_get_wifi_ssid(ssid, sizeof(ssid));

    char ip[16];
    wifi_get_ip_str(ip, sizeof(ip));

    uint16_t poll_iv = 60;
    storage_get_poll_interval(&poll_iv);

    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "hostname", hostname);
    cJSON_AddStringToObject(j, "wifi_ssid", ssid);
    cJSON_AddStringToObject(j, "ip", ip);
    cJSON_AddNumberToObject(j, "poll_interval", poll_iv);
    return send_json(req, 200, j);
}

static esp_err_t settings_wifi_handler(httpd_req_t *req)
{
    if (!auth_check_request(req)) return send_error(req, 401, "Unauthorized");

    char *body = read_body(req);
    if (!body) return send_error(req, 400, "Invalid body");

    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) return send_error(req, 400, "Invalid JSON");

    const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItem(j, "ssid"));
    const char *pass = cJSON_GetStringValue(cJSON_GetObjectItem(j, "password"));
    if (!ssid) {
        cJSON_Delete(j);
        return send_error(req, 400, "Missing ssid");
    }

    storage_set_wifi_creds(ssid, pass ? pass : "");
    cJSON_Delete(j);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    send_json(req, 200, resp);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t settings_psk_handler(httpd_req_t *req)
{
    if (!auth_check_request(req)) return send_error(req, 401, "Unauthorized");

    char *body = read_body(req);
    if (!body) return send_error(req, 400, "Invalid body");

    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) return send_error(req, 400, "Invalid JSON");

    const char *psk = cJSON_GetStringValue(cJSON_GetObjectItem(j, "psk"));
    if (!psk || strlen(psk) < 8) {
        cJSON_Delete(j);
        return send_error(req, 400, "PSK must be at least 8 characters");
    }

    storage_set_psk(psk);
    cJSON_Delete(j);
    auth_invalidate_all();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "updated");
    return send_json(req, 200, resp);
}

static esp_err_t settings_poll_handler(httpd_req_t *req)
{
    if (!auth_check_request(req)) return send_error(req, 401, "Unauthorized");

    char *body = read_body(req);
    if (!body) return send_error(req, 400, "Invalid body");

    cJSON *j = cJSON_Parse(body);
    free(body);
    if (!j) return send_error(req, 400, "Invalid JSON");

    cJSON *poll_item = cJSON_GetObjectItem(j, "poll_interval");
    if (!cJSON_IsNumber(poll_item)) {
        cJSON_Delete(j);
        return send_error(req, 400, "Missing poll_interval");
    }

    int interval = (int)cJSON_GetNumberValue(poll_item);
    cJSON_Delete(j);

    if (interval < 5 || interval > 3600) {
        return send_error(req, 400, "poll_interval must be 5-3600 seconds");
    }

    storage_set_poll_interval((uint16_t)interval);
    monitor_trigger_poll();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "updated");
    return send_json(req, 200, resp);
}

/* ── System ───────────────────────────────────────────────── */

static esp_err_t system_reboot_handler(httpd_req_t *req)
{
    if (!auth_check_request(req)) return send_error(req, 401, "Unauthorized");

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    send_json(req, 200, resp);

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* ── Wildcard router for instances ───────────────────────── */

/* ── Server setup ────────────────────────────────────────────── */

esp_err_t http_server_start(void)
{
    s_start_time = esp_timer_get_time();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 24;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    /* CORS preflight */
    httpd_uri_t cors_uri = {
        .uri = "/api/v1/*",
        .method = HTTP_OPTIONS,
        .handler = cors_handler,
    };
    httpd_register_uri_handler(s_server, &cors_uri);

    /* Root */
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
    httpd_register_uri_handler(s_server, &root);

    /* Health (no auth) */
    httpd_uri_t health = { .uri = "/api/v1/health", .method = HTTP_GET, .handler = health_handler };
    httpd_register_uri_handler(s_server, &health);
    httpd_uri_t health_short = { .uri = "/health", .method = HTTP_GET, .handler = health_handler };
    httpd_register_uri_handler(s_server, &health_short);

    /* Auth (no auth) */
    httpd_uri_t challenge = { .uri = "/api/v1/auth/challenge", .method = HTTP_POST, .handler = challenge_handler };
    httpd_uri_t login = { .uri = "/api/v1/auth/login", .method = HTTP_POST, .handler = login_handler };
    httpd_register_uri_handler(s_server, &challenge);
    httpd_register_uri_handler(s_server, &login);

    /* Instances */
    httpd_uri_t inst_get = { .uri = "/api/v1/instances", .method = HTTP_GET, .handler = instances_get_handler };
    httpd_register_uri_handler(s_server, &inst_get);
    httpd_uri_t inst_post = { .uri = "/api/v1/instances", .method = HTTP_POST, .handler = instances_post_handler };
    httpd_register_uri_handler(s_server, &inst_post);
    httpd_uri_t inst_put = { .uri = "/api/v1/instances/*", .method = HTTP_PUT, .handler = instances_put_handler };
    httpd_register_uri_handler(s_server, &inst_put);
    httpd_uri_t inst_del = { .uri = "/api/v1/instances/*", .method = HTTP_DELETE, .handler = instances_delete_handler };
    httpd_register_uri_handler(s_server, &inst_del);

    /* Monitor status */
    httpd_uri_t mon_status = { .uri = "/api/v1/monitor/status", .method = HTTP_GET, .handler = monitor_status_handler };
    httpd_register_uri_handler(s_server, &mon_status);

    /* Settings */
    httpd_uri_t settings_get = { .uri = "/api/v1/settings", .method = HTTP_GET, .handler = settings_get_handler };
    httpd_register_uri_handler(s_server, &settings_get);
    httpd_uri_t settings_wifi = { .uri = "/api/v1/settings/wifi", .method = HTTP_PUT, .handler = settings_wifi_handler };
    httpd_register_uri_handler(s_server, &settings_wifi);
    httpd_uri_t settings_psk = { .uri = "/api/v1/settings/psk", .method = HTTP_PUT, .handler = settings_psk_handler };
    httpd_register_uri_handler(s_server, &settings_psk);
    httpd_uri_t settings_poll = { .uri = "/api/v1/settings/poll", .method = HTTP_PUT, .handler = settings_poll_handler };
    httpd_register_uri_handler(s_server, &settings_poll);

    /* System */
    httpd_uri_t sys_reboot = { .uri = "/api/v1/system/reboot", .method = HTTP_POST, .handler = system_reboot_handler };
    httpd_register_uri_handler(s_server, &sys_reboot);

    ESP_LOGI(TAG, "HTTP server started on port 80");
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    return ESP_OK;
}

bool http_server_is_running(void)
{
    return s_server != NULL;
}
