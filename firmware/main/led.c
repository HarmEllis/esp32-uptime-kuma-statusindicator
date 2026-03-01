#include "led.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define LED_GREEN   GPIO_NUM_22
#define LED_RED     GPIO_NUM_23
#define LED_BUILTIN GPIO_NUM_2

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

esp_err_t led_init(void)
{
    BaseType_t ret = xTaskCreate(led_task, "led", 2048, NULL, 2, NULL);
    return ret == pdPASS ? ESP_OK : ESP_FAIL;
}
