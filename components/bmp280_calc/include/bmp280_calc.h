#pragma once

/*
 * bmp280_calc: hardware-free maths for the BMP280 / BME280 (ADR-006). Plain C11, no ESP-IDF
 * header, so the host tests compile it with any gcc.
 * T05: the calibration type (checkpoint 1) and the temperature compensation (checkpoint 3).
 */

#include <stdint.h>

/** The chip's factory calibration ("trimming parameters"), registers 0x88..0x9F, one 16-bit
 *  word each, least significant byte first. Names and signedness follow the datasheet table:
 *  dig_T1 and dig_P1 are unsigned, all others are signed. The temperature uses T1..T3; the
 *  pressure coefficients P1..P9 are read and logged now but only used in a stretch task. */
typedef struct {
    uint16_t dig_T1; /**< 0x88/0x89 */
    int16_t dig_T2;  /**< 0x8A/0x8B */
    int16_t dig_T3;  /**< 0x8C/0x8D */
    uint16_t dig_P1; /**< 0x8E/0x8F */
    int16_t dig_P2;  /**< 0x90/0x91 */
    int16_t dig_P3;  /**< 0x92/0x93 */
    int16_t dig_P4;  /**< 0x94/0x95 */
    int16_t dig_P5;  /**< 0x96/0x97 */
    int16_t dig_P6;  /**< 0x98/0x99 */
    int16_t dig_P7;  /**< 0x9A/0x9B */
    int16_t dig_P8;  /**< 0x9C/0x9D */
    int16_t dig_P9;  /**< 0x9E/0x9F */
} bmp280_calib_t;

/** Turn a raw 20-bit temperature reading into degrees Celsius x 100 (2508 = 25.08 degC), using
 *  the datasheet's 32-bit integer compensation (BMP280 datasheet 3.11.3, BME280 4.2.3).
 *  @param calib  the chip's calibration from bmp280_read_calib(); must not be NULL.
 *  @param adc_t  the raw reading: registers 0xFA..0xFC assembled as (msb << 12) | (lsb << 4) | (xlsb >> 4),
 *                so 0 .. 1048575. (The name follows the datasheet's adc_T; it is a value, not a type.)
 *  @return temperature in 0.01 degC. */
int32_t bmp280_calc_temp_centi(const bmp280_calib_t *calib, int32_t adc_t);
