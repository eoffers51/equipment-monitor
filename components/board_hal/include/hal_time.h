#pragma once

/*
 * hal_time: the microsecond clock used for timestamps and uptime, and (T05) a short blocking
 * delay for the waits a sensor datasheet asks for, so the sensor drivers need no FreeRTOS header.
 */

#include <stdint.h>

/** Return microseconds since boot (64-bit, so it does not wrap in practice). */
int64_t hal_time_now_us(void);

/* Largest wait hal_time_delay_ms() accepts (60 s); larger values are capped. */
#define HAL_TIME_DELAY_MAX_MS 60000U

/** Block the calling task for at least `ms` milliseconds; other tasks run meanwhile.
 *  For short datasheet waits (a sensor's start-up time), not for timing a loop. The wait can be
 *  up to two ticks longer than asked at a slow tick rate (at most 1 ms more at CONFIG_FREERTOS_HZ=1000). Values above
 *  HAL_TIME_DELAY_MAX_MS are capped to it. Task context only: never from an ISR. */
void hal_time_delay_ms(uint32_t ms);
