#include "hal_gpio.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

esp_err_t hal_gpio_led_init(int gpio)
{
    /* T03 step 2: level first, then output mode. gpio_set_level() also rejects an invalid pin
     * number before the shift below could use it. */
    const esp_err_t err = gpio_set_level((gpio_num_t)gpio, 0U);
    if (err != ESP_OK) {
        return err;
    }

    /* gpio_config() instead of gpio_reset_pin(): reset_pin turns the pull-up ON (gpio.c:471 in
     * v6.0.2), and an LED output needs neither pull. Every field is named, so nothing is left to
     * a default by accident. */
    const gpio_config_t cfg = {
        .pin_bit_mask = (uint64_t)1U << (unsigned)gpio, /* one bit per GPIO, 64-bit because the S3 has 49 */
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

esp_err_t hal_gpio_led_set(int gpio, bool on)
{
    return gpio_set_level((gpio_num_t)gpio, on ? 1U : 0U);
}
