/*
 * bmp280_calc: temperature compensation for the BMP280 / BME280.
 * Hardware-free (ADR-006): only <stdint.h> through the header, so it runs unchanged on the chip
 * and in host_test/ on the PC.
 */

#include "bmp280_calc.h"

int32_t bmp280_calc_temp_centi(const bmp280_calib_t *calib, int32_t adc_t)
{
    // Widen the 16-bit coefficients to int32_t first, exactly like the datasheet's casts. On the
    // compilers used here (xtensa-gcc, esp-clang, MinGW gcc) int is 32 bits, so 16-bit values would
    // promote to it anyway; the casts change nothing here and keep the code portable to a 16-bit int.
    const int32_t dig_t1 = (int32_t)calib->dig_T1;
    const int32_t dig_t2 = (int32_t)calib->dig_T2;
    const int32_t dig_t3 = (int32_t)calib->dig_T3;

    // The two lines below are the datasheet reference code with only the names changed, so they
    // can be checked against it symbol by symbol. Keep them exactly as written; the host tests
    // check the datasheet example (2508), the double formula and the +128 rounding below.
    //
    // Right-shifting a negative signed value is implementation-defined in C11 (6.5.7 p5). The
    // terms in brackets go negative for cold readings, and the datasheet code expects an
    // arithmetic shift (the sign bit is copied in, so -8 >> 1 == -4). GCC and Clang, the only
    // compilers this project uses (xtensa-gcc, esp-clang, MinGW gcc), define it that way.
    const int32_t var1 = (((adc_t >> 3) - (dig_t1 << 1)) * dig_t2) >> 11;
    const int32_t var2 = (((((adc_t >> 4) - dig_t1) * ((adc_t >> 4) - dig_t1)) >> 12) * dig_t3) >> 14;

    // t_fine is the datasheet's shared "fine temperature"; the pressure formula (stretch task S3)
    // needs it too, but nothing does yet, so it stays a local instead of the reference's global.
    const int32_t t_fine = var1 + var2;

    // t_fine / 5120 is degC, so * 100 gives centi-degrees: t_fine * 100 / 5120 = t_fine * 5 / 256.
    // +128 is half of 256, so the shift rounds to the nearest 0.01 degC instead of always down.
    return (t_fine * 5 + 128) >> 8;
}
