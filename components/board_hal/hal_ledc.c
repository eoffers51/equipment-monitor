#include "hal_ledc.h"

#include "driver/ledc.h"
#include "esp_err.h"

/* The ESP32-S3 has only low-speed mode (high-speed exists only on the original ESP32). */
#define HAL_LEDC_MODE LEDC_LOW_SPEED_MODE
#define HAL_LEDC_TIMER LEDC_TIMER_0
#define HAL_LEDC_CHANNEL LEDC_CHANNEL_0

/* Duty value for 100 % on: 2^resolution_bits (driver/ledc.h, ledc_channel_config_t.duty).
 * Stays 0 until hal_ledc_init(), so a call before init clamps to "off". */
static uint32_t s_max_duty;

esp_err_t hal_ledc_init(int gpio, uint32_t freq_hz, uint8_t resolution_bits)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode = HAL_LEDC_MODE,
        .duty_resolution = (ledc_timer_bit_t)resolution_bits,
        .timer_num = HAL_LEDC_TIMER,
        .freq_hz = freq_hz,
        /* Named explicitly; LEDC_AUTO_CLK picks the same source first on the S3
         * (esp_hal_ledc/esp32s3/include/hal/ledc_ll.h:32). Light-sleep behaviour: T15. */
        .clk_cfg = LEDC_USE_APB_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_channel_config_t channel_cfg = {
        .gpio_num = gpio,
        .speed_mode = HAL_LEDC_MODE,
        .channel = HAL_LEDC_CHANNEL,
        .timer_sel = HAL_LEDC_TIMER,
        .duty = 0U,  /* LED dark (T03 step 5) */
        .hpoint = 0, /* the pulse starts at the beginning of each period */
    };
    err = ledc_channel_config(&channel_cfg);
    if (err != ESP_OK) {
        return err;
    }

    /* Only now, after the driver accepted resolution_bits (it must be < LEDC_TIMER_BIT_MAX),
     * is the shift known to be in range. */
    s_max_duty = (uint32_t)1U << resolution_bits;
    return ESP_OK;
}

esp_err_t hal_ledc_set_duty(uint32_t duty)
{
    /* The driver does not range-check duty (ledc.c:1101), and a value above 2^bits gives
     * nonsense on the pin, so clamp here. */
    if (duty > s_max_duty) {
        duty = s_max_duty;
    }

    /* Two steps: ledc_set_duty() only stages the value; ledc_update_duty() makes it take
     * effect at the next period. Not thread-safe (driver/ledc.h), so call from one task only. */
    const esp_err_t err = ledc_set_duty(HAL_LEDC_MODE, HAL_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(HAL_LEDC_MODE, HAL_LEDC_CHANNEL);
}
