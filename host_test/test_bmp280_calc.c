#include <stdio.h>
#include "unity.h"
#include "bmp280_calc.h"

/*
 * Tests for bmp280_calc_temp_centi(), the datasheet's 32-bit integer compensation.
 * Unity is built without double support (UNITY_EXCLUDE_DOUBLE is its default), so double
 * results are compared by hand and a failure prints both values through a message.
 */

// The calibration of the learner's own module, read on the bench on 2026-11-10 (SESSION_LOG.md).
// Only T1..T3 matter for temperature; the pressure fields stay 0.
static const bmp280_calib_t OWN_CALIB = {.dig_T1 = 27984, .dig_T2 = 26460, .dig_T3 = 50};

// Agreement required between the integer result and the double reference: 0.01 degC.
#define MAX_DIFF_CENTI 1.0

/* The datasheet's floating-point compensation (BMP280 3.11.3 / BME280 4.2.3, "double" version),
 * written independently of the integer code so the two can check each other. Returns degC. */
static double reference_temp_c(const bmp280_calib_t *calib, int32_t adc_t)
{
    const double t1 = calib->dig_T1;
    const double t2 = calib->dig_T2;
    const double t3 = calib->dig_T3;

    const double var1 = ((double)adc_t / 16384.0 - t1 / 1024.0) * t2;
    const double d = (double)adc_t / 131072.0 - t1 / 8192.0;
    const double var2 = d * d * t3;
    return (var1 + var2) / 5120.0;
}

// Shared body of the three "own calibration" tests: integer result vs double reference.
static void assert_matches_double_reference(int32_t adc_t)
{
    const int32_t actual_centi = bmp280_calc_temp_centi(&OWN_CALIB, adc_t);
    const double expected_centi = reference_temp_c(&OWN_CALIB, adc_t) * 100.0;
    const double diff = (double)actual_centi - expected_centi;

    char msg[96];
    snprintf(msg, sizeof msg, "adc_T=%ld: int=%ld centi-degC, double=%.3f centi-degC", (long)adc_t, (long)actual_centi,
             expected_centi);
    TEST_ASSERT_TRUE_MESSAGE(diff >= -MAX_DIFF_CENTI && diff <= MAX_DIFF_CENTI, msg);
}

// (a) The worked example printed in the Bosch datasheet: the result must match it exactly.
static void test_datasheet_example_gives_2508(void)
{
    const bmp280_calib_t datasheet_calib = {.dig_T1 = 27504, .dig_T2 = 26435, .dig_T3 = -1000};

    TEST_ASSERT_EQUAL_INT32(2508, bmp280_calc_temp_centi(&datasheet_calib, 519888));
}

// (b)-(d) The learner's module at three raw values around room temperature.
static void test_own_calib_matches_double_at_500000(void)
{
    assert_matches_double_reference(500000);
}

static void test_own_calib_matches_double_at_520000(void)
{
    assert_matches_double_reference(520000);
}

static void test_own_calib_matches_double_at_540000(void)
{
    assert_matches_double_reference(540000);
}

// A raw value that is not a multiple of 16, so the >>3 and >>4 below drop low bits. Over
// 400000..600000 this is where the integer result is furthest from the double one (0.753 centi,
// found by a brute-force search with this calibration).
static void test_own_calib_matches_double_at_484319(void)
{
    assert_matches_double_reference(484319);
}

// The +128 before >>8 rounds to the nearest hundredth instead of rounding down. At 520008 the
// double result is 2279.704, so with rounding the answer is 2280; without it, it would be 2279.
// An exact check, because the tolerance tests above cannot tell 2279 from 2280.
static void test_rounding_gives_nearest_hundredth_at_520008(void)
{
    TEST_ASSERT_EQUAL_INT32(2280, bmp280_calc_temp_centi(&OWN_CALIB, 520008));
}

// (e) A warmer sensor gives a larger raw value, so the result must never go down as adc_T goes up.
// Every single raw value is checked (step 1), so no step in the integer rounding can hide.
static void test_own_calib_never_decreases_from_400000_to_600000(void)
{
    int32_t previous = bmp280_calc_temp_centi(&OWN_CALIB, 400000);

    for (int32_t adc_t = 400001; adc_t <= 600000; adc_t++) {
        const int32_t current = bmp280_calc_temp_centi(&OWN_CALIB, adc_t);
        // Assert only on failure: 200 000 passing asserts would be slow and say nothing more.
        if (current < previous) {
            char msg[80];
            snprintf(msg, sizeof msg, "drops from %ld to %ld at adc_T=%ld", (long)previous, (long)current, (long)adc_t);
            TEST_FAIL_MESSAGE(msg);
        }
        previous = current;
    }
}

void run_bmp280_calc_tests(void)
{
    UnitySetTestFile(__FILE__); // so failures point at this file
    RUN_TEST(test_datasheet_example_gives_2508);
    RUN_TEST(test_own_calib_matches_double_at_500000);
    RUN_TEST(test_own_calib_matches_double_at_520000);
    RUN_TEST(test_own_calib_matches_double_at_540000);
    RUN_TEST(test_own_calib_matches_double_at_484319);
    RUN_TEST(test_rounding_gives_nearest_hundredth_at_520008);
    RUN_TEST(test_own_calib_never_decreases_from_400000_to_600000);
}
