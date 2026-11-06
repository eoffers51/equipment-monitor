#include "hal_sys.h"

#include <stddef.h>

#include "esp_idf_version.h"
#include "esp_system.h"

/* Short names indexed by the esp_reset_reason_t value (esp_system.h, v6.0.2).
 * Designated initializers tie each name to its enum constant, so the table stays
 * correct even if Espressif reorders or inserts values. A value without a row
 * reads as NULL and is reported as "UNKNOWN" below. */
static const char *const RESET_NAMES[] = {
    [ESP_RST_UNKNOWN] = "UNKNOWN",
    [ESP_RST_POWERON] = "POWERON",
    [ESP_RST_EXT] = "EXT",
    [ESP_RST_SW] = "SW",
    [ESP_RST_PANIC] = "PANIC",
    [ESP_RST_INT_WDT] = "INT_WDT",
    [ESP_RST_TASK_WDT] = "TASK_WDT",
    [ESP_RST_WDT] = "WDT",
    [ESP_RST_DEEPSLEEP] = "DEEPSLEEP",
    [ESP_RST_BROWNOUT] = "BROWNOUT",
    [ESP_RST_SDIO] = "SDIO",
    [ESP_RST_USB] = "USB",
    [ESP_RST_JTAG] = "JTAG",
    [ESP_RST_EFUSE] = "EFUSE",
    [ESP_RST_PWR_GLITCH] = "PWR_GLITCH",
    [ESP_RST_CPU_LOCKUP] = "CPU_LOCKUP",
};

/* Number of rows; the array size comes from the highest designated index + 1. */
#define RESET_NAME_COUNT (sizeof(RESET_NAMES) / sizeof(RESET_NAMES[0]))

hal_reset_reason_t hal_sys_reset_reason(void)
{
    const esp_reset_reason_t reason = esp_reset_reason();
    /* Compare as unsigned: a negative or too-large value must never index the table. */
    const size_t index = (size_t)reason;
    const char *name = (index < RESET_NAME_COUNT) ? RESET_NAMES[index] : NULL;

    const hal_reset_reason_t result = {
        .code = (int)reason,
        .name = (name != NULL) ? name : "UNKNOWN",
    };
    return result;
}

uint32_t hal_sys_free_heap(void)
{
    return esp_get_free_heap_size();
}

uint32_t hal_sys_min_free_heap(void)
{
    return esp_get_minimum_free_heap_size();
}

const char *hal_sys_idf_version(void)
{
    return esp_get_idf_version();
}

void hal_sys_restart(void)
{
    esp_restart();
}
