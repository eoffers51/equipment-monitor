#include "unity.h"

// Each test file has a function that runs its tests
void run_bmp280_calc_tests(void);

// Unity calls these before and after every test (nothing to do here)
void setUp(void)
{
}
void tearDown(void)
{
}

int main(void)
{
    UNITY_BEGIN();
    run_bmp280_calc_tests();
    return UNITY_END(); // number of failed tests: 0 = all passed
}
