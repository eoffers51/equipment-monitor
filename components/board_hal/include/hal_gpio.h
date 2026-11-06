#pragma once

/*
 * hal_gpio: plain GPIO pins. For now only LED outputs (T03); button inputs and the
 * MPU-6050 interrupt pin follow in later tasks.
 * Pins arrive as plain int from main/board_pins.h, so no pin number lives in board_hal.
 */

#include <stdbool.h>

#include "esp_err.h"

/** Make `gpio` a push-pull output that starts LOW (LED off), with no pull-up/down and no interrupt.
 *  Returns ESP_ERR_INVALID_ARG for an invalid pin; the caller decides (main/ uses ESP_ERROR_CHECK). */
esp_err_t hal_gpio_led_init(int gpio);

/** Switch the LED on `gpio` on (HIGH) or off (LOW). Call hal_gpio_led_init() first.
 *  Returns the driver's error; the caller counts it (ADR-006), the HAL never aborts or logs. */
esp_err_t hal_gpio_led_set(int gpio, bool on);
