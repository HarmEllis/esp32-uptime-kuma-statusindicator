#include "led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static volatile led_state_t s_state = LED_STARTUP;
static volatile led_state_t s_prev_state = LED_STARTUP;

void led_set_state(led_state_t state)
{
    if (state == LED_IDENTIFY) {
        s_prev_state = s_state;
    }
    s_state = state;
}

led_state_t led_get_state(void)
{
    return s_state;
}

/* ------------------------------------------------------------------ */
#ifdef CONFIG_IDF_TARGET_ESP32S3
/* ESP32-S3: single WS2812 RGB LED on GPIO 48 via RMT led_strip driver */

#include "led_strip.h"
#include "driver/gpio.h"
#include "storage.h"

#define RGB_LED_GPIO GPIO_NUM_48

static led_strip_handle_t s_strip = NULL;
static volatile uint8_t s_brightness = 30;

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return;
    uint8_t br = s_brightness;   /* read once to avoid per-channel tearing */
    led_strip_set_pixel(s_strip, 0, r*br/100, g*br/100, b*br/100);
    led_strip_refresh(s_strip);
}

static void led_task(void *arg)
{
    /* s_strip is initialised in led_init() before this task is created */
    set_rgb(0, 0, 0);

    while (1) {
        switch (s_state) {
        case LED_STARTUP:
            set_rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_CONNECTING:
            set_rgb(0, 0, 255);
            vTaskDelay(pdMS_TO_TICKS(500));
            set_rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case LED_AP_MODE:
            set_rgb(0, 0, 255);
            vTaskDelay(pdMS_TO_TICKS(250));
            set_rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(250));
            break;

        case LED_ALL_UP:
            set_rgb(0, 255, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_MONITORS_DOWN:
            set_rgb(255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_ERROR_BLINK:
            set_rgb(255, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            set_rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_IDENTIFY:
            /* Triple flash white, then restore previous state */
            for (int i = 0; i < 3; i++) {
                set_rgb(255, 255, 255);
                vTaskDelay(pdMS_TO_TICKS(100));
                set_rgb(0, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            s_state = s_prev_state;
            break;

        case LED_RESET_WARN:
            set_rgb(255, 255, 0);
            vTaskDelay(pdMS_TO_TICKS(50));
            set_rgb(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(50));
            break;

        case LED_RESET_CONFIRM:
            set_rgb(255, 255, 255);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }
    }
}

void led_set_brightness(uint8_t percent)
{
    if (percent < 1)   percent = 1;
    if (percent > 100) percent = 100;
    s_brightness = percent;
}

uint8_t led_get_brightness(void)
{
    return s_brightness;
}

/* ------------------------------------------------------------------ */
#else
/* ESP32 (original): three discrete GPIO LEDs — GREEN=22, RED=23, BUILTIN=2 */

#include "driver/gpio.h"

#define LED_GREEN   GPIO_NUM_22
#define LED_RED     GPIO_NUM_23
#define LED_BUILTIN GPIO_NUM_2

static void set_leds(int green, int red, int builtin)
{
    gpio_set_level(LED_GREEN, green);
    gpio_set_level(LED_RED, red);
    gpio_set_level(LED_BUILTIN, builtin);
}

static void led_task(void *arg)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GREEN) | (1ULL << LED_RED) | (1ULL << LED_BUILTIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    set_leds(0, 0, 0);

    while (1) {
        switch (s_state) {
        case LED_STARTUP:
            set_leds(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_CONNECTING:
            set_leds(0, 0, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            set_leds(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case LED_AP_MODE:
            set_leds(0, 0, 1);
            vTaskDelay(pdMS_TO_TICKS(250));
            set_leds(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(250));
            break;

        case LED_ALL_UP:
            set_leds(1, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_MONITORS_DOWN:
            set_leds(0, 1, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_ERROR_BLINK:
            set_leds(0, 1, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            set_leds(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case LED_IDENTIFY:
            /* Triple flash all 3 LEDs, then restore previous state */
            for (int i = 0; i < 3; i++) {
                set_leds(1, 1, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                set_leds(0, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            s_state = s_prev_state;
            break;

        case LED_RESET_WARN:
            set_leds(1, 1, 0);
            vTaskDelay(pdMS_TO_TICKS(50));
            set_leds(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(50));
            break;

        case LED_RESET_CONFIRM:
            set_leds(1, 1, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }
    }
}

#endif /* CONFIG_IDF_TARGET_ESP32S3 */

esp_err_t led_init(void)
{
#ifdef CONFIG_IDF_TARGET_ESP32S3
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, /* 10 MHz */
        .mem_block_symbols = 64,
        .flags = { .with_dma = false },
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE("led", "Failed to init led_strip: %s", esp_err_to_name(err));
        return err;
    }
    uint8_t br = 30;
    storage_get_led_brightness(&br);
    s_brightness = br;
    /* RMT driver uses more stack than plain GPIO — allocate 4096 bytes */
    BaseType_t ret = xTaskCreate(led_task, "led", 4096, NULL, 2, NULL);
#else
    BaseType_t ret = xTaskCreate(led_task, "led", 2048, NULL, 2, NULL);
#endif
    return ret == pdPASS ? ESP_OK : ESP_FAIL;
}
