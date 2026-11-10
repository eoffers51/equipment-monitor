#include "hal_time.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

/* Milliseconds per second, to turn a time in ms into ticks with configTICK_RATE_HZ (ticks/s). */
#define HAL_MS_PER_S 1000U

int64_t hal_time_now_us(void)
{
    /* One 64-bit µs clock for every timestamp, so events from different tasks compare directly. */
    return esp_timer_get_time();
}

void hal_time_delay_ms(uint32_t ms)
{
    /* ms -> ticks rounded UP, not down like pdMS_TO_TICKS: at a slower tick rate rounding down
     * could turn a short wait into 0 ticks. 64-bit so ms * rate cannot overflow. */
    /* Longer than any datasheet wait in this project and far below what TickType_t can hold, so the
     * cast below can never wrap (a wrapped value could become vTaskDelay(0), no wait at all). */
    if (ms > HAL_TIME_DELAY_MAX_MS) {
        ms = HAL_TIME_DELAY_MAX_MS;
    }
    const uint64_t ticks = (((uint64_t)ms * configTICK_RATE_HZ) + (HAL_MS_PER_S - 1U)) / HAL_MS_PER_S;

    /* +1 tick: vTaskDelay(n) wakes the task at the n-th tick interrupt after the call, and the
     * call lands somewhere inside a tick period, so n ticks can be almost a whole tick shorter
     * than n periods. One extra tick makes "at least ms" true. */
    vTaskDelay((TickType_t)(ticks + 1U));
}
