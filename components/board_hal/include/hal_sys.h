#pragma once

/*
 * hal_sys: chip-level system facts (reset reason, heap, IDF version).
 * Plain C types only, so callers never need an ESP-IDF header.
 */

#include <stdint.h>

/** Reset reason: the esp_reset_reason_t number and a short name such as "POWERON". */
typedef struct {
    int code;         /**< Numeric esp_reset_reason_t value (1 = power-on, 3 = software, ...). */
    const char *name; /**< Short static name, never NULL ("UNKNOWN" if not recognised). */
} hal_reset_reason_t;

/** Return why the chip last started, as a code and a name. */
hal_reset_reason_t hal_sys_reset_reason(void);

/** Return the bytes of heap free right now. */
uint32_t hal_sys_free_heap(void);

/** Return the lowest free-heap value seen since boot (the "low-water mark"). */
uint32_t hal_sys_min_free_heap(void);

/** Return the ESP-IDF version string the firmware was built with, e.g. "v6.0.2". */
const char *hal_sys_idf_version(void);

/** Restart the chip now (software reset); the next boot reports reset reason SW. Does not return. */
void hal_sys_restart(void);
