#pragma once

/*
 * hal_i2c: the one I2C bus both sensors share (§1, one bus at 400 kHz).
 * T04: bus setup, device registration, address probe, register reads/writes for the sensor
 * drivers, a bus reset and an error counter. Pins arrive as plain int from main/board_pins.h.
 */

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/** A registered device, as returned by hal_i2c_add_device(). It is only a small index into the
 *  HAL's own table, so callers never see the driver's handle type (or need its header). */
typedef uint8_t hal_i2c_dev_t;

/** "No device": never handed out by hal_i2c_add_device(), and rejected by every function that
 *  takes a hal_i2c_dev_t. Initialise a stored index with it, because a zero-initialised index
 *  would be 0, which is a real device (the first one registered). */
#define HAL_I2C_DEV_INVALID ((hal_i2c_dev_t)0xFFU)

/** Create the I2C master bus on `sda_gpio` / `scl_gpio` with the internal pull-ups off.
 *  `scl_hz` is the clock every device added with hal_i2c_add_device() runs at (it must not be 0).
 *  Returns ESP_ERR_INVALID_STATE if the bus already exists, ESP_ERR_INVALID_ARG for a bad pin
 *  or scl_hz, otherwise the driver's error. main/ wraps it in ESP_ERROR_CHECK (a boot-time
 *  configuration error). */
esp_err_t hal_i2c_init(int sda_gpio, int scl_gpio, uint32_t scl_hz);

/** Register the device at 7-bit address `addr7` and write its index to `*out_dev`.
 *  This sends nothing on the bus, so it succeeds even if the sensor is unplugged.
 *  Returns ESP_ERR_INVALID_STATE before hal_i2c_init(), ESP_ERR_INVALID_ARG for a NULL
 *  `out_dev`, an address above 0x7F or an address that is already registered, ESP_ERR_NO_MEM
 *  when the table is full, otherwise the driver's error. Called at boot only (ESP_ERROR_CHECK). */
esp_err_t hal_i2c_add_device(uint8_t addr7, hal_i2c_dev_t *out_dev);

/** Ask whether a device answers at the 7-bit address `addr7` (0x00..0x7F).
 *  Returns ESP_OK if it answered, ESP_ERR_NOT_FOUND if nobody did, ESP_ERR_TIMEOUT if the bus
 *  itself is stuck (for example a missing pull-up), ESP_ERR_INVALID_STATE before hal_i2c_init().
 *  Counted as one error (see hal_i2c_error_count()): ESP_ERR_NOT_FOUND at a registered address,
 *  or any bus result other than ESP_OK / ESP_ERR_NOT_FOUND at any address.
 *  The HAL never aborts or logs: the caller decides what an error means. (The driver itself
 *  stays quiet on a NACK but prints one error line on a timeout.) */
esp_err_t hal_i2c_probe(uint8_t addr7);

/** Read `len` registers of device `dev` into `buf`, starting at register `reg`, in one transfer.
 *  Returns ESP_OK, ESP_ERR_INVALID_STATE before hal_i2c_init(), ESP_ERR_INVALID_ARG for an
 *  index that hal_i2c_add_device() never handed out (e.g. HAL_I2C_DEV_INVALID), a NULL `buf` or
 *  `len` 0, otherwise the driver's error (a missing device gives ESP_ERR_INVALID_RESPONSE).
 *  Every driver error is counted (see hal_i2c_error_count()); the argument checks are not,
 *  because then nothing went out on the bus. `buf` is not valid after an error. */
esp_err_t hal_i2c_read_regs(hal_i2c_dev_t dev, uint8_t reg, uint8_t *buf, size_t len);

/** Write one byte `value` to register `reg` of device `dev`.
 *  Returns and counts errors exactly like hal_i2c_read_regs(). */
esp_err_t hal_i2c_write_reg(hal_i2c_dev_t dev, uint8_t reg, uint8_t value);

/** Reset the I2C controller and clock a stuck sensor free (see hal_i2c.c for the details).
 *  For after a bus fault, e.g. a probe that returned ESP_ERR_TIMEOUT. Returns ESP_OK,
 *  ESP_ERR_INVALID_STATE before hal_i2c_init(), otherwise the driver's error (the driver
 *  then also prints an error line). Not counted as an I2C error: it repairs, it does not
 *  transfer, and the fault that led to it was already counted. */
esp_err_t hal_i2c_bus_reset(void);

/** Number of I2C errors since boot (§1: every I2C error is counted). Never reset; it wraps
 *  after 2^32 errors. */
uint32_t hal_i2c_error_count(void);
