#include "bmp280.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hal_i2c.h"
#include "hal_time.h"

/* Register addresses (BMP280 datasheet DS001-26 and BME280 rev 1.24, "Memory map"; the two chips
 * agree on every register used here). */
#define BMP280_REG_CALIB 0x88U      /* first calibration byte (dig_T1 LSB) */
#define BMP280_REG_CALIB_LAST 0x9FU /* last calibration byte (dig_P9 MSB) */
#define BMP280_REG_ID 0xD0U         /* moved here from main/app_config.h (T04 bring-up) */
#define BMP280_REG_RESET 0xE0U      /* write BMP280_RESET_CMD for a full power-on reset */
#define BMP280_REG_STATUS 0xF3U     /* bit 0 = im_update */
#define BMP280_REG_CTRL_MEAS 0xF4U  /* osrs_t, osrs_p, mode */
#define BMP280_REG_CONFIG 0xF5U     /* t_sb, filter, spi3w_en; directly after ctrl_meas */
#define BMP280_REG_TEMP_MSB 0xFAU   /* temp_msb, then temp_lsb (0xFB) and temp_xlsb (0xFC) */

/* The raw temperature is 3 registers: msb = bits 19:12, lsb = bits 11:4, xlsb bits 7:4 = bits 3:0
 * (the low nibble of xlsb is not part of the value). Shifts that put each byte in its place. */
#define BMP280_TEMP_LEN 3U
#define BMP280_TEMP_MSB_SHIFT 12U
#define BMP280_TEMP_LSB_SHIFT 4U
#define BMP280_TEMP_XLSB_SHIFT 4U

/* Reset value of the result registers (0x80, 0x00, 0x00 assembled): no measurement yet, or the
 * measurement is skipped. */
#define BMP280_ADC_NO_DATA 0x80000U

/* Bit positions inside ctrl_meas: osrs_t[7:5] | osrs_p[4:2] | mode[1:0]. */
#define BMP280_CTRL_MEAS_OSRS_T_SHIFT 5U
#define BMP280_CTRL_MEAS_OSRS_P_SHIFT 2U
#define BMP280_CTRL_MEAS_MODE_SHIFT 0U
/* Bit positions inside config: t_sb[7:5] | filter[4:2] | bit 1 unused | spi3w_en[0].
 * spi3w_en stays 0: it only switches the SPI interface to 3 wires, and this chip talks I2C. */
#define BMP280_CONFIG_T_SB_SHIFT 5U
#define BMP280_CONFIG_FILTER_SHIFT 2U

/* Field widths: osrs_t, osrs_p, t_sb and filter are 3 bits wide, mode is 2 bits. */
#define BMP280_FIELD3_MASK 0x07U

/* config bits the datasheet defines: t_sb[7:5], filter[4:2], spi3w_en[0]. Bit 1 is unused, so the
 * read-back does not compare it. */
#define BMP280_CONFIG_DEFINED_BITS 0xFDU
#define BMP280_MODE_MASK 0x03U

/* The only IDs accepted. Early BMP280 samples reported 0x56/0x57; they are not accepted, so any
 * ID other than these two means a chip this driver was not written for. */
#define BMP280_CHIP_ID_BMP280 0x58U
#define BMP280_CHIP_ID_BME280 0x60U

/* The reset register ignores every value except this one, so a stray write cannot reset the chip. */
#define BMP280_RESET_CMD 0xB6U

/* status bit 0 "im_update": 1 while the chip copies its calibration from NVM into the registers
 * 0x88..0x9F. Reading the calibration before it is 0 could return half-copied values. */
#define BMP280_STATUS_IM_UPDATE 0x01U

/* Datasheet start-up time after a reset: 2 ms. The status is read only after this wait: during
 * start-up the chip may not answer, and a NACKed read would be counted as an I2C error (§1). */
#define BMP280_STARTUP_MS 2U

/* How many times im_update is checked (one start-up time apart) before giving up: 5 passes of
 * hal_time_delay_ms(2), which waits 2-3 ms each (it adds one tick), so 10-15 ms in total, at least
 * five times the datasheet value. A bound, so a chip that never finishes cannot hang the boot. */
#define BMP280_RESET_POLLS 5U

/* 0x88..0x9F: 12 words of 2 bytes (T1..T3, P1..P9). */
#define BMP280_CALIB_LEN 24U
_Static_assert(BMP280_CALIB_LEN == (BMP280_REG_CALIB_LAST - BMP280_REG_CALIB + 1U),
               "calibration length must match the register range");

/* Two bytes, least significant first (the order of the calibration block), as an unsigned
 * 16-bit number. Done in unsigned arithmetic: the high byte shifted left by 8 is at most 0xFF00,
 * so nothing overflows and no sign bit gets in the way. */
static uint16_t u16_le(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[1] << 8) | bytes[0]);
}

/* The same two bytes as a signed 16-bit (two's-complement) number. The bits are identical; only
 * their meaning changes: 0xFFFF is 65535 as uint16_t but -1 as int16_t. Without this cast a
 * negative coefficient would reach the maths as a large positive one.
 * Converting a uint16_t above 32767 to int16_t is "implementation-defined" in C11 (6.3.1.3);
 * GCC and Clang define it as wrap-around modulo 2^16, which gives exactly the two's-complement
 * value. */
static int16_t s16_le(const uint8_t *bytes)
{
    return (int16_t)u16_le(bytes);
}

esp_err_t bmp280_init(hal_i2c_dev_t dev, uint8_t *out_chip_id)
{
    if (out_chip_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Identify before writing anything: the reset write below must never reach an unknown chip
     * that happens to sit at this address. */
    uint8_t id = 0U;
    esp_err_t err = hal_i2c_read_regs(dev, (uint8_t)BMP280_REG_ID, &id, sizeof(id));
    if (err != ESP_OK) {
        return err;
    }
    *out_chip_id = id;
    if (id != BMP280_CHIP_ID_BMP280 && id != BMP280_CHIP_ID_BME280) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* The ESP32 can restart (crash, watchdog, flashing) while the sensor stays powered and keeps
     * running in whatever mode it was left in. A reset puts it back into one known state. */
    err = hal_i2c_write_reg(dev, (uint8_t)BMP280_REG_RESET, (uint8_t)BMP280_RESET_CMD);
    if (err != ESP_OK) {
        return err;
    }

    /* Bounded wait for im_update to clear. The delay comes first in every pass: see
     * BMP280_STARTUP_MS for why the chip is not read straight after the reset. */
    for (unsigned int poll = 0U; poll < BMP280_RESET_POLLS; poll++) {
        hal_time_delay_ms(BMP280_STARTUP_MS);
        uint8_t status = 0U;
        err = hal_i2c_read_regs(dev, (uint8_t)BMP280_REG_STATUS, &status, sizeof(status));
        if (err != ESP_OK) {
            return err;
        }
        if ((status & BMP280_STATUS_IM_UPDATE) == 0U) {
            return ESP_OK;
        }
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t bmp280_read_calib(hal_i2c_dev_t dev, bmp280_calib_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* One burst read of all 24 bytes: one transaction instead of 12, and the chip's register
     * pointer moves on by itself after each byte (see hal_i2c_read_regs). */
    uint8_t raw[BMP280_CALIB_LEN] = {0};
    const esp_err_t err = hal_i2c_read_regs(dev, (uint8_t)BMP280_REG_CALIB, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }

    /* raw[i] holds register 0x88 + i, so each word starts at (its register - 0x88). */
    out->dig_T1 = u16_le(&raw[0]);  /* 0x88 */
    out->dig_T2 = s16_le(&raw[2]);  /* 0x8A */
    out->dig_T3 = s16_le(&raw[4]);  /* 0x8C */
    out->dig_P1 = u16_le(&raw[6]);  /* 0x8E */
    out->dig_P2 = s16_le(&raw[8]);  /* 0x90 */
    out->dig_P3 = s16_le(&raw[10]); /* 0x92 */
    out->dig_P4 = s16_le(&raw[12]); /* 0x94 */
    out->dig_P5 = s16_le(&raw[14]); /* 0x96 */
    out->dig_P6 = s16_le(&raw[16]); /* 0x98 */
    out->dig_P7 = s16_le(&raw[18]); /* 0x9A */
    out->dig_P8 = s16_le(&raw[20]); /* 0x9C */
    out->dig_P9 = s16_le(&raw[22]); /* 0x9E */
    return ESP_OK;
}

esp_err_t bmp280_configure(hal_i2c_dev_t dev, uint8_t osrs_t, uint8_t osrs_p, uint8_t t_sb, uint8_t filter,
                           uint8_t mode, uint8_t *out_ctrl_meas, uint8_t *out_config)
{
    /* Check every argument before the first write, so a bad argument never changes the chip.
     * (A bus error between the two writes can still leave config new and ctrl_meas old; the
     * caller then has no temperature and the error is counted by hal_i2c.) */
    if (out_ctrl_meas == NULL || out_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (osrs_t > BMP280_OSRS_X16 || osrs_p > BMP280_OSRS_X16 || t_sb > BMP280_TSB_1000_MS ||
        filter > BMP280_FILTER_16) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mode != BMP280_MODE_SLEEP && mode != BMP280_MODE_FORCED && mode != BMP280_MODE_NORMAL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Each code is masked to its field width before the shift, so a field can never spill into
     * its neighbour's bits. The checks above already guarantee that; the mask keeps it true
     * even if someone later widens a check. */
    const uint8_t ctrl_meas = (uint8_t)(((osrs_t & BMP280_FIELD3_MASK) << BMP280_CTRL_MEAS_OSRS_T_SHIFT) |
                                        ((osrs_p & BMP280_FIELD3_MASK) << BMP280_CTRL_MEAS_OSRS_P_SHIFT) |
                                        ((mode & BMP280_MODE_MASK) << BMP280_CTRL_MEAS_MODE_SHIFT));
    const uint8_t config = (uint8_t)(((t_sb & BMP280_FIELD3_MASK) << BMP280_CONFIG_T_SB_SHIFT) |
                                     ((filter & BMP280_FIELD3_MASK) << BMP280_CONFIG_FILTER_SHIFT));

    /* config first, ctrl_meas second. The chip is still asleep here (bmp280_init() left it so),
     * and in sleep mode config writes are never ignored. Writing ctrl_meas starts the chosen mode,
     * and in normal mode the datasheet allows the chip to ignore a config write.
     * Humidity (BME280 only) is left off: ctrl_hum (0xF2) resets to "skipped" and is not written.
     * If humidity is ever wanted, ctrl_hum must be written BEFORE ctrl_meas: the chip only takes a
     * new ctrl_hum value over when ctrl_meas is written afterwards. */
    esp_err_t err = hal_i2c_write_reg(dev, (uint8_t)BMP280_REG_CONFIG, config);
    if (err != ESP_OK) {
        return err;
    }
    err = hal_i2c_write_reg(dev, (uint8_t)BMP280_REG_CTRL_MEAS, ctrl_meas);
    if (err != ESP_OK) {
        return err;
    }

    /* Read both back in one 2-byte transfer: config (0xF5) sits right after ctrl_meas (0xF4), and
     * the register pointer moves on by itself. A write that the chip ACKed is not proof it was
     * stored; only reading it back is. */
    uint8_t readback[2] = {0};
    err = hal_i2c_read_regs(dev, (uint8_t)BMP280_REG_CTRL_MEAS, readback, sizeof(readback));
    if (err != ESP_OK) {
        return err;
    }
    *out_ctrl_meas = readback[0];
    *out_config = readback[1];

    /* In forced mode the chip clears the mode bits back to sleep (00) once its one measurement is
     * done, which may already have happened by the time of the read-back. So for forced mode the
     * mode bits are left out of the comparison; every other bit must match. */
    uint8_t ctrl_meas_compare = 0xFFU;
    if (mode == BMP280_MODE_FORCED) {
        ctrl_meas_compare = (uint8_t)~(BMP280_MODE_MASK << BMP280_CTRL_MEAS_MODE_SHIFT);
    }
    if ((readback[0] & ctrl_meas_compare) != (ctrl_meas & ctrl_meas_compare) ||
        (readback[1] & BMP280_CONFIG_DEFINED_BITS) != config) {
        return BMP280_ERR_READBACK;
    }
    return ESP_OK;
}

esp_err_t bmp280_read_raw_temp(hal_i2c_dev_t dev, int32_t *out_adc_t)
{
    if (out_adc_t == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* All three bytes in one burst read, never three single reads: during a burst the chip
     * freezes ("shadows") the result registers, so the three bytes come from the same measurement.
     * With separate reads a new measurement could land in between and mix the msb of one
     * measurement with the lsb of the next. */
    uint8_t raw[BMP280_TEMP_LEN] = {0};
    const esp_err_t err = hal_i2c_read_regs(dev, (uint8_t)BMP280_REG_TEMP_MSB, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }

    /* Each byte is cast to uint32_t before its shift. Without the cast it would be promoted to
     * int, which happens to be 32 bits here, but on a 16-bit int 0xFF << 12 would overflow; the
     * cast also keeps the whole expression unsigned, so no sign bit is involved.
     * xlsb is shifted right: its 4 useful bits are the top half of the byte. */
    const uint32_t adc_t = ((uint32_t)raw[0] << BMP280_TEMP_MSB_SHIFT) | ((uint32_t)raw[1] << BMP280_TEMP_LSB_SHIFT) |
                           ((uint32_t)raw[2] >> BMP280_TEMP_XLSB_SHIFT);
    /* At most 0xFFFFF (20 bits), so it always fits in int32_t, the type the datasheet formula uses.
     * Written for 0x80000 too: the caller may know it is a real reading (see bmp280.h). */
    *out_adc_t = (int32_t)adc_t;
    if (adc_t == BMP280_ADC_NO_DATA) {
        return BMP280_ERR_NO_DATA;
    }
    return ESP_OK;
}
