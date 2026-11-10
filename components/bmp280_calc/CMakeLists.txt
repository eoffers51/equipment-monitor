# bmp280_calc: hardware-free BMP280/BME280 temperature compensation (T05, ADR-006).
# It owns bmp280_calib_t, the calibration the bmp280 driver reads and bmp280_calc_temp_centi()
# consumes, so the type lives on the hardware-free side and the host tests (host_test/) can use
# it without any ESP-IDF header.
# No REQUIRES / PRIV_REQUIRES: like analysis, it must compile with plain C11.
idf_component_register(SRCS "bmp280_calc.c"
                       INCLUDE_DIRS "include")
