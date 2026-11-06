/*
 * Equipment Health Monitor, T02: boot log.
 * Logs the IDF version, why the chip started (§6) and the free heap (§9),
 * then repeats uptime and heap every APP_STATUS_LOG_PERIOD_MS.
 */

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_config.h"
#include "hal_sys.h"
#include "hal_time.h"

/* Microseconds per second, to turn the µs clock into whole seconds of uptime. */
#define US_PER_S 1000000

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


    while (1) {
        /* pdMS_TO_TICKS is exact here because CONFIG_FREERTOS_HZ=1000 (1 tick = 1 ms). */
        vTaskDelay(pdMS_TO_TICKS(APP_STATUS_LOG_PERIOD_MS));

        const int64_t uptime_s = hal_time_now_us() / US_PER_S;
        /* free_heap that keeps falling means a leak (§9); min_free_heap is the lowest it ever was. */
        ESP_LOGI(TAG_STATUS, "uptime_s=%" PRId64 " free_heap=%" PRIu32 " min_free_heap=%" PRIu32, uptime_s,
                 hal_sys_free_heap(), hal_sys_min_free_heap());
    }
}
