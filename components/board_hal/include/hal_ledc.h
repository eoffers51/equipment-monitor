#pragma once

/*
 * hal_ledc: one PWM output (the blue "load" LED, §5). One LEDC timer drives one channel,
 * so there is a single output and no handle to pass around.
 */

#include <stdint.h>

#include "esp_err.h"

/** Start PWM on `gpio` at `freq_hz` with `resolution_bits` of duty resolution, duty 0 (LED dark).
 *  Returns the driver's error (invalid pin or timer settings); main/ wraps it in ESP_ERROR_CHECK. */
esp_err_t hal_ledc_init(int gpio, uint32_t freq_hz, uint8_t resolution_bits);

/** Set the duty. 0 = off, 2^resolution_bits = always on (256 at 8 bits, so 255 is 255/256).
 *  Larger values are clamped to 2^resolution_bits. Takes effect at the start of the next PWM period.
 *  Returns the driver's error; the caller counts it. */
esp_err_t hal_ledc_set_duty(uint32_t duty);
