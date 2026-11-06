#include "hal_time.h"

#include "esp_timer.h"

int64_t hal_time_now_us(void)
{
    /* One 64-bit µs clock for every timestamp, so events from different tasks compare directly. */
    return esp_timer_get_time();
}
