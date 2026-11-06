#pragma once

/*
 * hal_time: the microsecond clock used for timestamps and uptime.
 */

#include <stdint.h>

/** Return microseconds since boot (64-bit, so it does not wrap in practice). */
int64_t hal_time_now_us(void);
