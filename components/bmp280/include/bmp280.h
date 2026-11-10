#pragma once

/*
 * bmp280: register-level driver for the Bosch BMP280 temperature/pressure sensor. It accepts the
 * BME280 too (same temperature and pressure registers, plus humidity), which is what the bench
 * module turned out to be (docs/bom.md).
 * T05 checkpoint 1: identify, soft reset, read the calibration. Checkpoint 2: oversampling,
 * standby time, filter and mode (bmp280_configure). Checkpoint 4: the raw temperature read
 * (bmp280_read_raw_temp); the maths is in bmp280_calc, which has no hardware dependency.
 * The chip is reached only through hal_i2c (ADR-006). The driver never logs: it returns an
 * esp_err_t and main/ decides what to print or count.
 */

#include <stdint.h>

#include "esp_err.h"
#include "hal_i2c.h"
#include "bmp280_calc.h"

/** Check that `dev` is a BMP280 (ID 0x58) or BME280 (ID 0x60), then soft-reset it and wait until
 *  it has reloaded its calibration. Afterwards the chip is in sleep mode with default settings.
 *  `*out_chip_id` gets the ID whenever the ID read worked, also when the ID is not accepted, so
 *  the caller can log what answered.
 *  Returns ESP_OK; ESP_ERR_INVALID_ARG for a NULL `out_chip_id`; ESP_ERR_NOT_SUPPORTED for any
 *  other ID (nothing is written to that chip); ESP_ERR_TIMEOUT if the chip is still copying its
 *  calibration after the bounded wait (10-15 ms); otherwise the hal_i2c error (a missing
 *  sensor gives ESP_ERR_INVALID_RESPONSE).
 *  Blocks for a few ms while other tasks run. Boot only, from a task, never from an ISR. */
esp_err_t bmp280_init(hal_i2c_dev_t dev, uint8_t *out_chip_id);

/** Read the calibration block (registers 0x88..0x9F) in one transfer into `*out`.
 *  Call after bmp280_init(). Returns ESP_OK, ESP_ERR_INVALID_ARG for a NULL `out`, otherwise the
 *  hal_i2c error; `*out` is left unchanged after an error. */
esp_err_t bmp280_read_calib(hal_i2c_dev_t dev, bmp280_calib_t *out);

/* Setting codes for bmp280_configure(), the bit patterns from the datasheet tables (BMP280
 * DS001-26 / BME280 rev 1.24, ctrl_meas and config registers). They are field values, not
 * register bytes: bmp280_configure() shifts each one into its place. Where the datasheet gives a
 * setting two codes (oversampling 101/110/111 all mean x16, mode 01/10 both mean forced), only one
 * is defined and accepted, so a read-back can be compared with exactly one expected byte. */

/* Oversampling of one measurement (osrs_t for temperature, osrs_p for pressure).
 * SKIP turns that measurement off; its result register then holds 0x80000. */
#define BMP280_OSRS_SKIP 0x00U
#define BMP280_OSRS_X1 0x01U
#define BMP280_OSRS_X2 0x02U
#define BMP280_OSRS_X4 0x03U
#define BMP280_OSRS_X8 0x04U
#define BMP280_OSRS_X16 0x05U

/* Power mode. SLEEP: no measurements (the state after bmp280_init()). FORCED: one measurement,
 * then the chip goes back to sleep by itself. NORMAL: measures over and over, resting for t_sb
 * between measurements. */
#define BMP280_MODE_SLEEP 0x00U
#define BMP280_MODE_FORCED 0x01U
#define BMP280_MODE_NORMAL 0x03U

/* t_sb: rest time between two measurements in normal mode. Codes 6 and 7 are not defined and
 * not accepted: they mean 2000/4000 ms on a BMP280 but 10/20 ms on a BME280, so the same code
 * would run the two chips at very different rates. Codes 0..5 are the same on both. */
#define BMP280_TSB_0_5_MS 0x00U
#define BMP280_TSB_62_5_MS 0x01U
#define BMP280_TSB_125_MS 0x02U
#define BMP280_TSB_250_MS 0x03U
#define BMP280_TSB_500_MS 0x04U
#define BMP280_TSB_1000_MS 0x05U

/* IIR filter coefficient: OFF passes every measurement through unchanged; a larger coefficient
 * smooths more but follows a real change more slowly. */
#define BMP280_FILTER_OFF 0x00U
#define BMP280_FILTER_2 0x01U
#define BMP280_FILTER_4 0x02U
#define BMP280_FILTER_8 0x03U
#define BMP280_FILTER_16 0x04U

/* Returned by bmp280_configure() when the chip answered but holds other values than were written.
 * A code hal_i2c never returns, so it cannot be confused with a NACK or a stuck bus
 * (those come back as ESP_ERR_INVALID_RESPONSE / ESP_ERR_TIMEOUT). */
#define BMP280_ERR_READBACK ESP_ERR_NOT_FINISHED

/** Set oversampling (`osrs_t`, `osrs_p`: BMP280_OSRS_*), standby time (`t_sb`: BMP280_TSB_*),
 *  filter (`filter`: BMP280_FILTER_*) and power mode (`mode`: BMP280_MODE_*), then read both
 *  registers back into `*out_ctrl_meas` and `*out_config`.
 *  Call after bmp280_init(), while the chip is in sleep mode: the datasheet says a config write
 *  in normal mode may be ignored (that would show up as BMP280_ERR_READBACK). Switching from
 *  normal to forced mode (T15) must first put the chip to sleep.
 *  Returns ESP_OK; ESP_ERR_INVALID_ARG for a code that is not one of the defines above or a NULL
 *  output (nothing is written then); BMP280_ERR_READBACK if a read-back differs from what
 *  was written; otherwise the hal_i2c error. The outputs are filled whenever the read-back
 *  worked, also on BMP280_ERR_READBACK, so the caller can log what the chip really holds.
 *  Task context only, never from an ISR. */
esp_err_t bmp280_configure(hal_i2c_dev_t dev, uint8_t osrs_t, uint8_t osrs_p, uint8_t t_sb, uint8_t filter,
                           uint8_t mode, uint8_t *out_ctrl_meas, uint8_t *out_config);

/* Returned by bmp280_read_raw_temp() when the result registers hold 0x80000, the value they have
 * after a reset (no measurement finished yet) and while temperature is skipped (BMP280_OSRS_SKIP).
 * 0x80000 is ambiguous: at osrs_t >= x1 it is also a real reading (24.15 degC with the T05 bench
 * calibration), so the caller decides: before the first real result it means "no data yet",
 * after one it is a valid value. That is why `*out_adc_t` is written for this code too.
 * ESP_ERR_INVALID_SIZE because neither hal_i2c nor the IDF I2C master driver it calls ever returns
 * it (checked in v6.0.2: esp_driver_i2c/i2c_master.c and i2c_common.c), and it differs from
 * BMP280_ERR_READBACK, so the caller can tell "no reading yet" from a bus fault and from a
 * configuration that did not stick. ESP_ERR_INVALID_STATE was not taken: hal_i2c returns it before
 * hal_i2c_init(). ESP_ERR_NOT_FOUND was not taken: hal_i2c_probe() returns it. */
#define BMP280_ERR_NO_DATA ESP_ERR_INVALID_SIZE

/** Read the raw 20-bit temperature (registers 0xFA..0xFC) in one 3-byte transfer into
 *  `*out_adc_t` (0 .. 1048575), ready for bmp280_calc_temp_centi().
 *  Returns ESP_OK; ESP_ERR_INVALID_ARG for a NULL `out_adc_t`; BMP280_ERR_NO_DATA if the
 *  registers hold 0x80000 (`*out_adc_t` is then 0x80000, see above); otherwise the hal_i2c error,
 *  and `*out_adc_t` is left unchanged. Task context only, never from an ISR. */
esp_err_t bmp280_read_raw_temp(hal_i2c_dev_t dev, int32_t *out_adc_t);
