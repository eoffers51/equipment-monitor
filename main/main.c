/*
 * Equipment Health Monitor.
 * T02: logs the IDF version, why the chip started (§6) and the free heap (§9),
 *      then repeats uptime and heap every APP_STATUS_LOG_PERIOD_MS.
 * T03: sets up the four LEDs, all dark (blue on LEDC PWM at duty 0). Patterns come with the indicator (T12).
 * T04: starts the shared I2C bus (§1) and scans it for devices at boot.
 *      Both sensors are registered with the HAL, so a missing one counts as an I2C error.
 *      If the scan stops on a bus fault, the bus is reset once. After the scan the MPU-6050's WHO_AM_I is
 *      read (bring-up only: the ID check moves into the mpu6050 driver in T06).
 *      T04 also rescanned every 5 s; that bring-up rescan ended in T05: 112 probes keep the bus busy for
 *      milliseconds, and from T05 on a task reads the bus every few ms.
 * T05: at boot the bmp280 driver identifies and resets the temperature sensor and reads its
 *      calibration, which is logged. A missing sensor is logged and the firmware runs on without temperature.
 *      It then configures the sensor for its highest temperature rate (normal mode) and logs the two
 *      configuration registers as read back from the chip.
 *      If all of that worked, a TEMPORARY task (temp_rate_task) reads the raw temperature every
 *      APP_TEMP_TASK_PERIOD_MS and once a second logs the temperature and how many reads worked.
 *      The T07 sampler task replaces it.
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
#include "bmp280.h"
#include "bmp280_calc.h"
#include "hal_gpio.h"
#include "hal_i2c.h"
#include "hal_ledc.h"
#include "hal_sys.h"
#include "hal_time.h"

/* Microseconds per second, to turn the µs clock into whole seconds of uptime. */
#define US_PER_S 1000000
/* Microseconds per millisecond, to turn the µs clock into ms for the LED timing. */
#define US_PER_MS 1000
/* The temperature line reports counts "per second", so its period is fixed at one second, not a setting. */
#define TEMP_LOG_PERIOD_MS 1000U
/* Hundredths per degree: the calc result is in 0.01 degC. */
#define CENTI_PER_DEGREE 100

/* pdMS_TO_TICKS() divides with integers. At a tick rate where 6 ms is not a whole number of ticks
 * (100 Hz: 0.6 ticks) it would round down, and 0 ticks would make vTaskDelayUntil() wait not at
 * all. Refuse to compile instead. */
_Static_assert((APP_TEMP_TASK_PERIOD_MS * configTICK_RATE_HZ) % 1000U == 0U,
               "APP_TEMP_TASK_PERIOD_MS must be a whole number of ticks");

static const char *TAG_BOOT = "boot";
static const char *TAG_STATUS = "status";
static const char *TAG_I2C = "i2c";
static const char *TAG_BMP280 = "bmp280";

/* The two sensors' HAL indices, filled in at boot. File-scope so the ID reads (and later the
 * sensor drivers) can use them. Start as HAL_I2C_DEV_INVALID, not the zero a static would get:
 * index 0 is the MPU-6050, so an index used before registration would silently talk to it.
 * HAL_I2C_DEV_INVALID makes that mistake return ESP_ERR_INVALID_ARG instead. */
static hal_i2c_dev_t s_dev_mpu6050 = HAL_I2C_DEV_INVALID;
static hal_i2c_dev_t s_dev_bmp280 = HAL_I2C_DEV_INVALID;

/* The BMP280's calibration, needed for every temperature conversion. Written once by
 * bmp280_bringup() before temp_rate_task exists; the task only reads it. xTaskCreate() comes after
 * the write, so the task can never see a half-written struct and no lock is needed. */
static bmp280_calib_t s_bmp280_calib;

/* Return true once `*next_us` has passed, and move it one period on. The new deadline is the
 * old one plus the period (not now_us plus the period), so a repeating job stays on its grid. */
static bool deadline_due(int64_t *next_us, int64_t now_us, uint32_t period_ms)
{
    if (now_us < *next_us) {
        return false;
    }
    const int64_t period_us = (int64_t)period_ms * US_PER_MS;
    *next_us += period_us;
    if (*next_us <= now_us) {
        /* The loop stalled for more than a period: skip the missed runs, don't burst them. */
        *next_us = now_us + period_us;
    }
    return true;
}

/* Probe every ordinary 7-bit address once and log who answered. Nothing here aborts: a missing
 * or unplugged sensor is a normal answer for a scan.
 * Returns ESP_OK if the scan ran to the end, or the bus error that stopped it. */
static esp_err_t i2c_scan(void)
{
    esp_err_t result = ESP_OK;
    int found = 0;

    /* `unsigned int`, not uint8_t: with a uint8_t counter, a last address of 0xFF would make
     * `addr <= last` always true and the loop would never end. */
    for (unsigned int addr = APP_I2C_SCAN_FIRST_ADDR; addr <= APP_I2C_SCAN_LAST_ADDR; addr++) {
        const esp_err_t err = hal_i2c_probe((uint8_t)addr);
        if (err == ESP_OK) {
            ESP_LOGI(TAG_I2C, "found 0x%02x", addr);
            found++;
        } else if (err != ESP_ERR_NOT_FOUND) {
            /* NOT_FOUND is the expected answer at an empty address. Anything else (TIMEOUT) means
             * the bus itself is broken, so every further address would fail the same way, each
             * costing tens of ms: stop the scan here. */
            ESP_LOGW(TAG_I2C, "probe 0x%02x: %s, scan stopped", addr, esp_err_to_name(err));
            result = err;
            break;
        }
    }

    if (result == ESP_OK) {
        ESP_LOGI(TAG_I2C, "%d device(s)", found);
    } else {
        /* A stopped scan only saw part of the range, so its count must not look like a full answer. */
        ESP_LOGW(TAG_I2C, "%d device(s) before the scan stopped", found);
    }
    return result;
}

/* Bring-up only: read one ID register and log it. A failed read is logged and the boot goes on:
 * an unplugged sensor must not stop the firmware. The value is only printed here, not
 * compared; the mpu6050 driver (T06) will check it, as the bmp280 driver already does. */
static void i2c_log_id(const char *name, hal_i2c_dev_t dev, uint8_t reg)
{
    uint8_t id = 0U;
    const esp_err_t err = hal_i2c_read_regs(dev, reg, &id, sizeof(id));
    if (err == ESP_OK) {
        ESP_LOGI(TAG_I2C, "%s=0x%02x", name, id);
    } else {
        ESP_LOGW(TAG_I2C, "%s: %s", name, esp_err_to_name(err));
    }
}

/* Boot-only I2C check: scan, repair the bus if the scan hit a bus fault, read the MPU-6050's ID,
 * then the error total. The total comes last so it already includes a failed ID read.
 * The BMP280's ID is not read here: bmp280_init() checks it (T05). */
static void i2c_check(void)
{
    if (i2c_scan() != ESP_OK) {
        /* Once, not in a loop: a bus that one reset does not fix needs a look at the wiring, and
         * the sensor bring-up after this will show the failure in the log. */
        const esp_err_t err = hal_i2c_bus_reset();
        ESP_LOGW(TAG_I2C, "bus reset: %s", esp_err_to_name(err));
    }

    i2c_log_id("mpu6050 who_am_i", s_dev_mpu6050, (uint8_t)APP_I2C_REG_MPU6050_WHO_AM_I);

    /* Running total since boot (hal_i2c_error_count()). */
    ESP_LOGI(TAG_I2C, "errors=%" PRIu32, hal_i2c_error_count());
}

/* T05, boot only: identify and reset the temperature sensor, read and log its calibration, then
 * configure it and log the configuration read back from the chip.
 * Nothing here stops the firmware (no ESP_ERROR_CHECK): without this sensor there is no
 * temperature, but vibration, buttons and LEDs must still work.
 * Returns true only if every step worked; then s_bmp280_calib holds the calibration and the chip
 * is measuring, so reading it makes sense. */
static bool bmp280_bringup(void)
{
    uint8_t chip_id = 0U;
    esp_err_t err = bmp280_init(s_dev_bmp280, &chip_id);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        /* The ID was read but is not 0x58/0x60: print it, it tells what is really at that address. */
        ESP_LOGW(TAG_BMP280, "id=0x%02x is not a BMP280/BME280, no temperature", chip_id);
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG_BMP280, "init: %s, no temperature", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG_BMP280, "id=0x%02x", chip_id);

    bmp280_calib_t calib = {0};
    err = bmp280_read_calib(s_dev_bmp280, &calib);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_BMP280, "calib: %s, no temperature", esp_err_to_name(err));
        return false;
    }
    /* PRIu16 / PRId16 match the fields' declared types, so the format and the value can never
     * disagree about the sign (dig_T1/dig_P1 are unsigned, the rest signed). */
    ESP_LOGI(TAG_BMP280, "dig_T1=%" PRIu16 " dig_T2=%" PRId16 " dig_T3=%" PRId16, calib.dig_T1, calib.dig_T2,
             calib.dig_T3);
    ESP_LOGI(TAG_BMP280,
             "dig_P1=%" PRIu16 " dig_P2=%" PRId16 " dig_P3=%" PRId16 " dig_P4=%" PRId16 " dig_P5=%" PRId16
             " dig_P6=%" PRId16 " dig_P7=%" PRId16 " dig_P8=%" PRId16 " dig_P9=%" PRId16,
             calib.dig_P1, calib.dig_P2, calib.dig_P3, calib.dig_P4, calib.dig_P5, calib.dig_P6, calib.dig_P7,
             calib.dig_P8, calib.dig_P9);

    /* The settings T05 step 3 asks for: the least oversampling (x1 for both measurements), the
     * shortest rest between measurements, no filter, and normal mode so the chip keeps measuring
     * on its own. */
    uint8_t ctrl_meas = 0U;
    uint8_t config = 0U;
    err = bmp280_configure(s_dev_bmp280, BMP280_OSRS_X1, BMP280_OSRS_X1, BMP280_TSB_0_5_MS, BMP280_FILTER_OFF,
                           BMP280_MODE_NORMAL, &ctrl_meas, &config);
    if (err == BMP280_ERR_READBACK) {
        /* The chip answered, but holds something else than was written: print what it holds. */
        ESP_LOGW(TAG_BMP280, "read back ctrl_meas=0x%02x config=0x%02x, not what was written, no temperature",
                 ctrl_meas, config);
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG_BMP280, "configure: %s, no temperature", esp_err_to_name(err));
        return false;
    }
    /* Both values come from the read-back, not from what was sent: this line proves the chip
     * stored them. */
    ESP_LOGI(TAG_BMP280, "ctrl_meas=0x%02x config=0x%02x", ctrl_meas, config);

    /* Stored only now, so the calibration of a half-brought-up sensor is never used. */
    s_bmp280_calib = calib;
    return true;
}

/* TEMPORARY (T05 step 5; the T07 sampler task replaces it): read the raw temperature every
 * APP_TEMP_TASK_PERIOD_MS, convert it, and once a second log the latest temperature with how many
 * reads worked, found no data, or failed in that second.
 * Bus sharing: since the 5 s rescan was removed, app_main does not touch the bus after boot, so
 * this task is the only one using it, and the T07 notes in hal_i2c.c (error-counter race, bus
 * reset without the bus lock) do not apply yet. They do once the sampler task shares the bus.
 * Everything here is on the stack or static: no malloc in the loop (§9). */
static void temp_rate_task(void *arg)
{
    (void)arg;

    /* Per-second counters, reset after each log line. */
    uint32_t reads = 0U;
    uint32_t no_data = 0U;
    uint32_t errors = 0U;
    /* The stack high-water mark is logged once, after the first log line has used the stack. */
    bool stack_logged = false;
    int32_t temp_centi = 0;
    /* False until the first real result. Before it, 0x80000 is the reset value ("no data yet");
     * after it, the chip is converting every cycle, so 0x80000 is a real reading (bmp280.h).
     * Cleared again by any bus error: a sensor that lost power comes back reset, asleep and
     * holding 0x80000, and must not be counted as 166 good reads/s. (A glitch short enough to
     * cause no bus error is not caught here; this task is temporary, T05 only.) */
    bool have_result = false;

    int64_t next_log_us = hal_time_now_us() + ((int64_t)TEMP_LOG_PERIOD_MS * US_PER_MS);
    /* The wake-up grid starts now. vTaskDelayUntil() moves it on by exactly one period each pass. */
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        /* xTaskDelayUntil, not vTaskDelay: vTaskDelay(6) waits 6 ms counted from NOW, after the
         * read and the log took their time, so the period would be 6 ms plus the work and the
         * rate would come out below 1000 / 6. xTaskDelayUntil waits until last_wake + 6 ms, a
         * fixed grid: a pass that ran a little late (a log line) is followed by a shorter wait.
         * If the task fell more than a whole period behind (a stuck bus: 20 ms per failed read),
         * it returns pdFALSE without waiting; then the grid restarts from now, so missed reads are
         * skipped instead of made up in a burst (the same rule as deadline_due()). */
        if (xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APP_TEMP_TASK_PERIOD_MS)) == pdFALSE) {
            last_wake = xTaskGetTickCount();
        }

        int32_t adc_t = 0;
        const esp_err_t err = bmp280_read_raw_temp(s_dev_bmp280, &adc_t);
        if (err == ESP_OK || (err == BMP280_ERR_NO_DATA && have_result)) {
            temp_centi = bmp280_calc_temp_centi(&s_bmp280_calib, adc_t);
            have_result = true;
            reads++;
        } else if (err == BMP280_ERR_NO_DATA) {
            /* The chip answered, but its result registers still hold the reset value. */
            no_data++;
        } else {
            /* A bus error; hal_i2c has already counted it in hal_i2c_error_count() too. */
            errors++;
            have_result = false;
        }

        if (deadline_due(&next_log_us, hal_time_now_us(), TEMP_LOG_PERIOD_MS)) {
            if (reads == 0U) {
                /* No successful read this second, so there is no temperature to show. */
                ESP_LOGW(TAG_BMP280, "temp=none reads/s=0 no_data=%" PRIu32 " err=%" PRIu32, no_data, errors);
            } else {
                /* Split 0.01 degC into whole degrees and hundredths on the magnitude, and print the
                 * sign on its own. C division rounds toward zero, so for -5 (-0.05 degC) -5 / 100
                 * is 0 and -5 % 100 is -5: printed directly that is "0.-5", and "-0" cannot be
                 * printed as an integer. With the magnitude 5 it is "-" + "0" + "." + "05".
                 * Negating is safe for any temperature this sensor reports (-40..85 degC, so at most
                 * 8500 in magnitude), nowhere near INT32_MIN (whose negative does not fit). */
                const char *sign = (temp_centi < 0) ? "-" : "";
                const int32_t magnitude = (temp_centi < 0) ? -temp_centi : temp_centi;
                ESP_LOGI(TAG_BMP280,
                         "temp=%s%" PRId32 ".%02" PRId32 " C reads/s=%" PRIu32 " no_data=%" PRIu32 " err=%" PRIu32,
                         sign, magnitude / CENTI_PER_DEGREE, magnitude % CENTI_PER_DEGREE, reads, no_data, errors);
            }
            if (!stack_logged) {
                /* Smallest free stack so far, in bytes (IDF counts stack in bytes): checks that
                 * APP_TEMP_TASK_STACK_BYTES is big enough, with the log line already used once. */
                ESP_LOGI(TAG_BMP280, "stack_free_min=%u", (unsigned int)uxTaskGetStackHighWaterMark(NULL));
                stack_logged = true;
            }
            reads = 0U;
            no_data = 0U;
            errors = 0U;
        }
    }
}

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

    /* A bus that cannot be created means a wrong pin or setting: stop at boot like the LEDs.
     * The scan and ID reads below must never stop the firmware, so they have no ESP_ERROR_CHECK. */
    const uint32_t i2c_scl_hz = APP_I2C_SCL_HZ;
    ESP_ERROR_CHECK(hal_i2c_init(PIN_I2C_SDA, PIN_I2C_SCL, i2c_scl_hz));
    ESP_LOGI(TAG_I2C, "%" PRIu32 " Hz sda=%d scl=%d", i2c_scl_hz, PIN_I2C_SDA, PIN_I2C_SCL);
    /* Registering sends nothing on the bus, so it cannot fail because a sensor is unplugged;
     * a failure here is a configuration error (duplicate address, table too small): stop. */
    ESP_ERROR_CHECK(hal_i2c_add_device((uint8_t)APP_I2C_ADDR_MPU6050, &s_dev_mpu6050));
    ESP_ERROR_CHECK(hal_i2c_add_device((uint8_t)APP_I2C_ADDR_BMP280, &s_dev_bmp280));
    i2c_check();

    /* Only a sensor that is identified, calibrated and measuring is worth reading every 6 ms. */
    if (bmp280_bringup()) {
        /* The stack and task control block are allocated here, once at boot, never in a loop (§9).
         * Created after bmp280_bringup() stored the calibration the task reads. */
        const BaseType_t created =
            xTaskCreate(temp_rate_task, "temp_rate", APP_TEMP_TASK_STACK_BYTES, NULL, APP_TEMP_TASK_PRIO, NULL);
        if (created != pdPASS) {
            /* Out of memory at boot. Like a missing sensor: no temperature, but the rest runs on. */
            ESP_LOGE(TAG_BMP280, "temp_rate task not created, no temperature");
        }
    }

    /* Every deadline below is an absolute time on the µs clock, so nothing drifts even though
     * each loop pass takes a little longer than APP_MAIN_LOOP_MS. */
    const int64_t start_us = hal_time_now_us();
    int64_t next_status_us = start_us + ((int64_t)APP_STATUS_LOG_PERIOD_MS * US_PER_MS);

    /* From here on app_main never touches the I2C bus: temp_rate_task is its only user. */
    while (1) {
        const int64_t now_us = hal_time_now_us();

        if (deadline_due(&next_status_us, now_us, APP_STATUS_LOG_PERIOD_MS)) {
            const int64_t uptime_s = now_us / US_PER_S;
            /* free_heap that keeps falling means a leak (§9); min_free_heap is the lowest it ever was. */
            ESP_LOGI(TAG_STATUS, "uptime_s=%" PRId64 " free_heap=%" PRIu32 " min_free_heap=%" PRIu32, uptime_s,
                     hal_sys_free_heap(), hal_sys_min_free_heap());
        }

        /* pdMS_TO_TICKS is exact here because CONFIG_FREERTOS_HZ=1000 (1 tick = 1 ms). */
        vTaskDelay(pdMS_TO_TICKS(APP_MAIN_LOOP_MS));
    }
}
