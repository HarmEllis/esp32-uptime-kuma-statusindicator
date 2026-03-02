#include "improv.h"
#include "storage.h"
#include "wifi.h"
#include "led.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static const char *TAG = "improv";

#define IMPROV_BUF_SIZE   256

/* Improv Serial protocol constants */
#define IMPROV_HEADER     "IMPROV"
#define IMPROV_VERSION    1

/* Packet types */
#define TYPE_CURRENT_STATE  0x01
#define TYPE_ERROR_STATE    0x02
#define TYPE_RPC_COMMAND    0x03
#define TYPE_RPC_RESULT     0x04

/* RPC commands */
#define CMD_WIFI_SETTINGS   0x01
#define CMD_IDENTIFY        0x02
#define CMD_GET_DEVICE_INFO 0x03
#define CMD_GET_WIFI_NETWORKS 0x04

/* States */
#define STATE_READY         0x02
#define STATE_PROVISIONING  0x03
#define STATE_PROVISIONED   0x04

/* Errors */
#define ERROR_NONE          0x00
#define ERROR_INVALID_RPC   0x01
#define ERROR_UNABLE_TO_CONNECT 0x03

static uint8_t s_state = STATE_READY;
static int s_uart_fd = -1;

/* Low-level UART read/write using file descriptor (VFS).
 * This avoids uart_driver_install() which would take over UART0
 * and break the bootloader auto-reset sequence used by esptool. */

static int uart_read_one(uint8_t *byte, int timeout_ms)
{
    int ret = read(s_uart_fd, byte, 1);
    if (ret == 1) return 1;

    vTaskDelay(pdMS_TO_TICKS(timeout_ms));
    ret = read(s_uart_fd, byte, 1);
    return (ret == 1) ? 1 : 0;
}

static int uart_read_n(uint8_t *buf, int n, int timeout_ms)
{
    int total = 0;
    TickType_t start = xTaskGetTickCount();
    TickType_t deadline = start + pdMS_TO_TICKS(timeout_ms);

    while (total < n) {
        int ret = read(s_uart_fd, buf + total, n - total);
        if (ret > 0) {
            total += ret;
        } else {
            if (xTaskGetTickCount() >= deadline) break;
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    return total;
}

static void uart_write_all(const uint8_t *data, int len)
{
    int written = 0;
    while (written < len) {
        int ret = write(s_uart_fd, data + written, len - written);
        if (ret > 0) {
            written += ret;
            continue;
        }

        if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        ESP_LOGW(TAG, "UART write failed at %d/%d (errno=%d)", written, len, errno);
        break;
    }
}

static void send_packet(uint8_t type, const uint8_t *data, uint8_t len)
{
    uint8_t pkt[IMPROV_BUF_SIZE];
    int pos = 0;

    memcpy(pkt, IMPROV_HEADER, 6);
    pos = 6;
    pkt[pos++] = IMPROV_VERSION;
    pkt[pos++] = type;
    pkt[pos++] = len;

    if (len > 0 && data) {
        memcpy(pkt + pos, data, len);
        pos += len;
    }

    uint8_t checksum = 0;
    for (int i = 6; i < pos; i++) {
        checksum += pkt[i];
    }
    pkt[pos++] = checksum;

    uart_write_all(pkt, pos);
}

static void send_state(uint8_t state)
{
    s_state = state;
    send_packet(TYPE_CURRENT_STATE, &state, 1);
}

static void send_error(uint8_t error)
{
    send_packet(TYPE_ERROR_STATE, &error, 1);
}

static void send_rpc_result(uint8_t command, const char *url)
{
    uint8_t data[128];
    int pos = 0;
    data[pos++] = command;

    if (url && url[0]) {
        uint8_t url_len = (uint8_t)strlen(url);
        data[pos++] = 1 + url_len;
        data[pos++] = 1;
        data[pos++] = url_len;
        memcpy(data + pos, url, url_len);
        pos += url_len;
    } else {
        data[pos++] = 0;
    }

    send_packet(TYPE_RPC_RESULT, data, pos);
}

static void handle_wifi_settings(const uint8_t *data, uint8_t len)
{
    if (len < 2) {
        send_error(ERROR_INVALID_RPC);
        return;
    }

    int pos = 0;
    uint8_t ssid_len = data[pos++];
    if (pos + ssid_len > len) { send_error(ERROR_INVALID_RPC); return; }

    char ssid[WIFI_SSID_MAX_LEN] = {0};
    memcpy(ssid, data + pos, ssid_len < WIFI_SSID_MAX_LEN ? ssid_len : WIFI_SSID_MAX_LEN - 1);
    pos += ssid_len;

    if (pos >= len) { send_error(ERROR_INVALID_RPC); return; }
    uint8_t pass_len = data[pos++];
    if (pos + pass_len > len) { send_error(ERROR_INVALID_RPC); return; }

    char pass[WIFI_PASS_MAX_LEN] = {0};
    memcpy(pass, data + pos, pass_len < WIFI_PASS_MAX_LEN ? pass_len : WIFI_PASS_MAX_LEN - 1);

    ESP_LOGI(TAG, "Received WiFi credentials for SSID: %s", ssid);
    send_state(STATE_PROVISIONING);

    esp_err_t err = storage_set_wifi_creds(ssid, pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store WiFi credentials");
        send_error(ERROR_UNABLE_TO_CONNECT);
        send_state(STATE_READY);
        return;
    }

    err = wifi_init_sta();
    if (err == ESP_OK) {
        send_state(STATE_PROVISIONED);

        char ip[16];
        wifi_get_ip_str(ip, sizeof(ip));
        char url[64];
        snprintf(url, sizeof(url), "http://%s", ip);
        send_rpc_result(CMD_WIFI_SETTINGS, url);
        ESP_LOGI(TAG, "Provisioned successfully, device at %s", url);

        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        send_error(ERROR_UNABLE_TO_CONNECT);
        send_state(STATE_READY);
    }
}

static void handle_device_info(void)
{
    uint8_t data[128];
    int pos = 0;
    data[pos++] = CMD_GET_DEVICE_INFO;

#ifdef CONFIG_IDF_TARGET_ESP32S3
#define IMPROV_FW_NAME  "ESP32-S3 Uptime Monitor"
#define IMPROV_HW_CHIP  "ESP32-S3"
#else
#define IMPROV_FW_NAME  "ESP32 Uptime Monitor"
#define IMPROV_HW_CHIP  "ESP32"
#endif

    const char *strings[] = {
        IMPROV_FW_NAME,      /* firmware name */
        "1.0.0",             /* firmware version */
        IMPROV_HW_CHIP,      /* hardware chip */
        "esp-uptimemonitor", /* device name */
    };

    uint8_t total_len = 0;
    for (int i = 0; i < 4; i++) {
        total_len += 1 + strlen(strings[i]);
    }
    data[pos++] = total_len;

    for (int i = 0; i < 4; i++) {
        uint8_t slen = strlen(strings[i]);
        data[pos++] = slen;
        memcpy(data + pos, strings[i], slen);
        pos += slen;
    }

    send_packet(TYPE_RPC_RESULT, data, pos);
}

static void handle_identify(void)
{
    led_state_t prev = led_get_state();
    led_set_state(LED_IDENTIFY);
    /* LED task will restore prev state after 3 blinks */
    (void)prev;
    send_rpc_result(CMD_IDENTIFY, NULL);
}

static void handle_rpc(const uint8_t *data, uint8_t len)
{
    if (len < 1) {
        send_error(ERROR_INVALID_RPC);
        return;
    }

    uint8_t command = data[0];
    uint8_t cmd_data_len = (len > 1) ? data[1] : 0;
    const uint8_t *cmd_data = (len > 2) ? data + 2 : NULL;

    switch (command) {
    case CMD_WIFI_SETTINGS:
        handle_wifi_settings(cmd_data, cmd_data_len);
        break;
    case CMD_IDENTIFY:
        handle_identify();
        break;
    case CMD_GET_DEVICE_INFO:
        handle_device_info();
        break;
    default:
        ESP_LOGW(TAG, "Unknown RPC command: 0x%02X", command);
        send_error(ERROR_INVALID_RPC);
        break;
    }
}

static void improv_task(void *arg)
{
    uint8_t buf[IMPROV_BUF_SIZE];

    if (wifi_is_connected()) {
        send_state(STATE_PROVISIONED);
    } else {
        send_state(STATE_READY);
    }

    while (1) {
        uint8_t b;
        if (uart_read_one(&b, 100) != 1) continue;

        if (b != 'I') continue;

        buf[0] = b;
        int len = uart_read_n(buf + 1, 5, 50);
        if (len != 5 || memcmp(buf, IMPROV_HEADER, 6) != 0) continue;

        len = uart_read_n(buf, 3, 50);
        if (len != 3) continue;

        uint8_t version = buf[0];
        uint8_t type = buf[1];
        uint8_t data_len = buf[2];

        if (version != IMPROV_VERSION) {
            ESP_LOGW(TAG, "Unsupported Improv version: %d", version);
            continue;
        }

        uint8_t data[IMPROV_BUF_SIZE];
        if (data_len > 0) {
            len = uart_read_n(data, data_len, 100);
            if (len != data_len) continue;
        }

        uint8_t checksum_byte;
        len = uart_read_n(&checksum_byte, 1, 50);
        if (len != 1) continue;

        uint8_t checksum = version + type + data_len;
        for (int i = 0; i < data_len; i++) {
            checksum += data[i];
        }
        if (checksum != checksum_byte) {
            ESP_LOGW(TAG, "Improv checksum mismatch");
            continue;
        }

        switch (type) {
        case TYPE_RPC_COMMAND:
            handle_rpc(data, data_len);
            break;
        default:
            ESP_LOGW(TAG, "Unhandled Improv packet type: 0x%02X", type);
            break;
        }
    }
}

esp_err_t improv_start(void)
{
    s_uart_fd = open("/dev/uart/0", O_RDWR | O_NONBLOCK);
    if (s_uart_fd < 0) {
        ESP_LOGE(TAG, "Failed to open /dev/uart/0: %d", errno);
        return ESP_FAIL;
    }

    BaseType_t ret = xTaskCreate(improv_task, "improv", 4096, NULL, 5, NULL);
    return ret == pdPASS ? ESP_OK : ESP_FAIL;
}
