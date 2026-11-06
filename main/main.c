/*
 * Equipment Health Monitor.
 * T02: logs the IDF version, why the chip started (§6) and the free heap (§9),
 *      then repeats uptime and heap every APP_STATUS_LOG_PERIOD_MS.
 * T03: sets up the four LEDs, all dark (blue on LEDC PWM at duty 0). Patterns come with the indicator (T12).
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "app_config.h"
#include "board_pins.h"
#include "hal_gpio.h"
#include "hal_ledc.h"
#include "hal_sys.h"
#include "hal_time.h"

/* Microseconds per second, to turn the µs clock into whole seconds of uptime. */
#define US_PER_S 1000000
/* Microseconds per millisecond, to turn the µs clock into ms for the LED timing. */
#define US_PER_MS 1000

static const char *TAG_BOOT = "boot";
static const char *TAG_STATUS = "status";

void app_main(void)
{
    const hal_reset_reason_t reset = hal_sys_reset_reason();

    /* One fact per line, so each Verified-result line can be matched on its own. */
    ESP_LOGI(TAG_BOOT, "idf=%s", hal_sys_idf_version());
    ESP_LOGI(TAG_BOOT, "reset_reason=%d (%s)", reset.code, reset.name);
    ESP_LOGI(TAG_BOOT, "free_heap=%" PRIu32, hal_sys_free_heap());
    ESP_LOGI(TAG_BOOT, "min_free_heap=%" PRIu32, hal_sys_min_free_heap());

    /* All four LEDs start as LOW outputs. Blue too: LEDC only takes over the pin's output
     * signal, so the GPIO setup still decides the pulls (both off) and the level before PWM. */
    /* Init errors mean a wrong pin or setting in board_pins.h / app_config.h: stop at boot. */
    ESP_ERROR_CHECK(hal_gpio_led_init(PIN_LED_GREEN));
    ESP_ERROR_CHECK(hal_gpio_led_init(PIN_LED_YELLOW));
    ESP_ERROR_CHECK(hal_gpio_led_init(PIN_LED_RED));
    ESP_ERROR_CHECK(hal_gpio_led_init(PIN_LED_BLUE));
    ESP_ERROR_CHECK(hal_ledc_init(PIN_LED_BLUE, APP_LEDC_FREQ_HZ, (uint8_t)APP_LEDC_RES_BITS));

    /* Every deadline below is an absolute time on the µs clock, so nothing drifts even though
     * each loop pass takes a little longer than APP_MAIN_LOOP_MS. */
    const int64_t start_us = hal_time_now_us();
    int64_t next_status_us = start_us + ((int64_t)APP_STATUS_LOG_PERIOD_MS * US_PER_MS);

    while (1) {
        const int64_t now_us = hal_time_now_us();

        if (now_us >= next_status_us) {
            /* Add the period to the old deadline (not to now_us), so the log stays on a 10 s grid. */
            next_status_us += (int64_t)APP_STATUS_LOG_PERIOD_MS * US_PER_MS;
            if (next_status_us <= now_us) {
                /* The loop stalled for more than a period: skip the missed lines, don't burst them. */
                next_status_us = now_us + ((int64_t)APP_STATUS_LOG_PERIOD_MS * US_PER_MS);
            }

            const int64_t uptime_s = now_us / US_PER_S;
            /* free_heap that keeps falling means a leak (§9); min_free_heap is the lowest it ever was. */
            ESP_LOGI(TAG_STATUS, "uptime_s=%" PRId64 " free_heap=%" PRIu32 " min_free_heap=%" PRIu32, uptime_s,
                     hal_sys_free_heap(), hal_sys_min_free_heap());
        }

        /* pdMS_TO_TICKS is exact here because CONFIG_FREERTOS_HZ=1000 (1 tick = 1 ms). */
        vTaskDelay(pdMS_TO_TICKS(APP_MAIN_LOOP_MS));
    }
}
