# bmp280: register-level driver for the BMP280 / BME280 temperature sensor (T05, ADR-006).
# It reaches the chip only through board_hal (hal_i2c.h for the bus, hal_time.h for the
# datasheet's start-up wait after a reset).
#
# REQUIRES (public: these types appear in include/bmp280.h, so every caller needs the headers too):
# - board_hal: hal_i2c_dev_t is a parameter of every function.
# - bmp280_calc: bmp280_calib_t is what bmp280_read_calib() fills in.
# No PRIV_REQUIRES: hal_time.h, used only inside bmp280.c, also comes from board_hal.
idf_component_register(SRCS "bmp280.c"
                       INCLUDE_DIRS "include"
                       REQUIRES board_hal bmp280_calc)
