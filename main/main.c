/*
 * Equipment Health Monitor.
 * T02: logs the IDF version, why the chip started (§6) and the free heap (§9),
 *      then repeats uptime and heap every APP_STATUS_LOG_PERIOD_MS.
 * T03: sets up the four LEDs, all dark (blue on LEDC PWM at duty 0). Patterns come with the indicator (T12).
 * T04: starts the shared I2C bus (§1) and scans it for devices at boot and then every
 *      APP_I2C_SCAN_PERIOD_MS, so a sensor can be unplugged and replugged without a reset.
 *      Both sensors are registered with the HAL, so a missing one counts as an I2C error.
 *      The repeating scan is bring-up only: 112 probes keep the bus busy for milliseconds, so it goes before the
 *      1 kHz sampler shares the bus (T07, §9 jitter).
 *      If a scan stops on a bus fault, the bus is reset once. After every scan the two sensors' ID registers are
 *      read (bring-up only: the ID checks move into the mpu6050 / bmp280 drivers in T05/T06).
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
#include "hal_gpio.h"
#include "hal_i2c.h"
#include "hal_ledc.h"
#include "hal_sys.h"
#include "hal_time.h"

/* Microseconds per second, to turn the µs clock into whole seconds of uptime. */
#define US_PER_S 1000000
/* Microseconds per millisecond, to turn the µs clock into ms for the LED timing. */
#define US_PER_MS 1000

static const char *TAG_BOOT = "boot";
static const char *TAG_STATUS = "status";
static const char *TAG_I2C = "i2c";

/* The two sensors' HAL indices, filled in at boot. File-scope so the ID reads (and later the
 * sensor drivers) can use them. Start as HAL_I2C_DEV_INVALID, not the zero a static would get:
 * index 0 is the MPU-6050, so an index used before registration would silently talk to it.
 * HAL_I2C_DEV_INVALID makes that mistake return ESP_ERR_INVALID_ARG instead. */
static hal_i2c_dev_t s_dev_mpu6050 = HAL_I2C_DEV_INVALID;
static hal_i2c_dev_t s_dev_bmp280 = HAL_I2C_DEV_INVALID;

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
 * or unplugged sensor is a normal answer for a scan, and the next scan simply tries again.
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
             * costing tens of ms: stop this scan and try again at the next one. */
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

/* Bring-up only: read one ID register and log it. A failed read is logged and the loop goes on:
 * an unplugged sensor must not stop the firmware. The value is only printed here, not
 * compared; the drivers (T05/T06) will check it. */
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

/* One I2C check: scan, repair the bus if the scan hit a bus fault, read both IDs, then the error
 * total. The total comes last so it already includes any failed ID read of this round. */
static void i2c_check(void)
{
    if (i2c_scan() != ESP_OK) {
        /* Once per check, not in a loop: if the reset does not help, the next check (one scan
         * period later) tries again, and the log shows every attempt. */
        const esp_err_t err = hal_i2c_bus_reset();
        ESP_LOGW(TAG_I2C, "bus reset: %s", esp_err_to_name(err));
    }

    i2c_log_id("mpu6050 who_am_i", s_dev_mpu6050, (uint8_t)APP_I2C_REG_MPU6050_WHO_AM_I);
    i2c_log_id("bmp280 id", s_dev_bmp280, (uint8_t)APP_I2C_REG_BMP280_ID);

    /* Running total since boot, so the change between two checks is the number of new errors. */
    ESP_LOGI(TAG_I2C, "errors=%" PRIu32, hal_i2c_error_count());
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

    /* Every deadline below is an absolute time on the µs clock, so nothing drifts even though
     * each loop pass takes a little longer than APP_MAIN_LOOP_MS. */
    const int64_t start_us = hal_time_now_us();
    int64_t next_status_us = start_us + ((int64_t)APP_STATUS_LOG_PERIOD_MS * US_PER_MS);
    /* The boot scan just ran, so the first repeat is one period from now. */
    int64_t next_scan_us = start_us + ((int64_t)APP_I2C_SCAN_PERIOD_MS * US_PER_MS);

    while (1) {
        const int64_t now_us = hal_time_now_us();

        if (deadline_due(&next_status_us, now_us, APP_STATUS_LOG_PERIOD_MS)) {
            const int64_t uptime_s = now_us / US_PER_S;
            /* free_heap that keeps falling means a leak (§9); min_free_heap is the lowest it ever was. */
            ESP_LOGI(TAG_STATUS, "uptime_s=%" PRId64 " free_heap=%" PRIu32 " min_free_heap=%" PRIu32, uptime_s,
                     hal_sys_free_heap(), hal_sys_min_free_heap());
        }

        if (deadline_due(&next_scan_us, now_us, APP_I2C_SCAN_PERIOD_MS)) {
            i2c_check();
        }

        /* pdMS_TO_TICKS is exact here because CONFIG_FREERTOS_HZ=1000 (1 tick = 1 ms). */
        vTaskDelay(pdMS_TO_TICKS(APP_MAIN_LOOP_MS));
    }
}
