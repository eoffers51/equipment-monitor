#pragma once

/* Application constants. Spec section numbers refer to TASK.md. */

/* §1: accelerometer sample rate in active mode. */
#define APP_SAMPLE_RATE_HZ 1000U

/* §2 / constraint 4: length of one analysis window. */
#define APP_WINDOW_MS 50U

/* Samples in one window: 1000 * 50 / 1000 = 50. */
#define APP_SAMPLES_PER_WINDOW ((APP_SAMPLE_RATE_HZ * APP_WINDOW_MS) / 1000U)

/* Integer division would silently drop a remainder (like pdMS_TO_TICKS(5) at 100 Hz),
 * so refuse to compile if the window is not a whole number of samples. */
_Static_assert((APP_SAMPLE_RATE_HZ * APP_WINDOW_MS) % 1000U == 0U, "window must hold a whole number of samples");

/* §8: standby sample rate. */
#define APP_STANDBY_RATE_HZ 10U

/* T02: boot-log repeat period. */
#define APP_STATUS_LOG_PERIOD_MS 10000U

/* §5 / T03: blue load LED PWM. 5 kHz is far above what the eye (or the camera) sees as flicker. */
#define APP_LEDC_FREQ_HZ 5000U
/* 8 bits: duty 0..256, where 256 = always on. */
#define APP_LEDC_RES_BITS 8U

/* Period of the app_main loop. Timing inside the loop comes from the µs clock, not from
 * counting loops, so this only sets how quickly a change is noticed (at most this late). */
#define APP_MAIN_LOOP_MS 10U
