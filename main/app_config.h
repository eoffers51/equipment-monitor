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

/* §1 / T04: one I2C bus for both sensors at 400 kHz (fast mode). */
#define APP_I2C_SCL_HZ 400000U

/* T04: 7-bit addresses of the two sensors, registered with the HAL at boot.
 * MPU-6050 (GY-521): 0x68 with its AD0 pin low.
 * BMP280: 0x76 with SDO low, confirmed on the bench 2026-11-09 (0x77 if SDO were high). */
#define APP_I2C_ADDR_MPU6050 0x68U
#define APP_I2C_ADDR_BMP280 0x76U

/* T04: address range of the bus scan. 0x00..0x07 and 0x78..0x7F are reserved by the I2C
 * specification for special purposes, so no ordinary device sits there. */
#define APP_I2C_SCAN_FIRST_ADDR 0x08U
#define APP_I2C_SCAN_LAST_ADDR 0x77U
_Static_assert(APP_I2C_SCAN_FIRST_ADDR <= APP_I2C_SCAN_LAST_ADDR && APP_I2C_SCAN_LAST_ADDR <= 0x7FU,
               "scan range must be a valid 7-bit address range");

/* T04 bring-up: the MPU-6050 identity register, read after the boot scan to prove register reads
 * work. Datasheet value: WHO_AM_I reads 0x68. It moves into the mpu6050 driver in T06; the
 * BMP280 ID register already moved into the bmp280 driver (T05). */
#define APP_I2C_REG_MPU6050_WHO_AM_I 0x75U

/* T05 temporary temperature task (temp_rate_task in main.c); the T07 sampler task replaces it.
 * Period: one raw temperature read every 6 ms. With CONFIG_FREERTOS_HZ=1000 that is exactly 6 ticks.
 * Stack in BYTES: ESP-IDF's xTaskCreate takes bytes, not words like vanilla FreeRTOS
 * (freertos/task.h, usStackDepth). ESP_LOGI's printf-style formatting is the deepest call here,
 * and 3 KB leaves room for it.
 * Priority 2: one above app_main (ESP_TASK_MAIN_PRIO = 1, esp_task.h), so app_main's status log
 * can never make a read late. */
#define APP_TEMP_TASK_PERIOD_MS 6U
#define APP_TEMP_TASK_STACK_BYTES 3072U
#define APP_TEMP_TASK_PRIO 2U

/* Period of the app_main loop. Timing inside the loop comes from the µs clock, not from
 * counting loops, so this only sets how quickly a change is noticed (at most this late). */
#define APP_MAIN_LOOP_MS 10U
