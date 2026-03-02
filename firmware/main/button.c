#include "button.h"
#include "led.h"
#include "storage.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

#define BOOT_GPIO       GPIO_NUM_0
#define POLL_MS         50
#define WARN_MS         2000    /* fast blink starts at 2s */
#define RESET_MS        10000   /* reset triggers at 10s */

static const char *TAG = "button";

static void button_task(void *arg)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    uint32_t held_ms = 0;
    bool warning_active = false;
    led_state_t saved_state = LED_STARTUP;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));

        if (gpio_get_level(BOOT_GPIO) == 0) {
            /* Button pressed (active low) */
            held_ms += POLL_MS;

            if (held_ms >= RESET_MS) {
                ESP_LOGW(TAG, "Factory reset confirmed!");
                led_set_state(LED_RESET_CONFIRM);
                vTaskDelay(pdMS_TO_TICKS(1000));
                storage_factory_reset();
                esp_restart();
            } else if (held_ms >= WARN_MS && !warning_active) {
                ESP_LOGI(TAG, "Reset warning — keep holding to reset");
                saved_state = led_get_state(); /* restore prior indicator after release */
                led_set_state(LED_RESET_WARN);
                warning_active = true;
            }
        } else {
            /* Button released */
            if (warning_active) {
                ESP_LOGI(TAG, "Boot button released, reset cancelled");
                led_set_state(saved_state);
                warning_active = false;
            }
            held_ms = 0;
        }
    }
}

esp_err_t button_init(void)
{
    BaseType_t ret = xTaskCreate(button_task, "button", 3072, NULL, 3, NULL);
    return ret == pdPASS ? ESP_OK : ESP_FAIL;
}
