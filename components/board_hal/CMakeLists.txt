# board_hal: the only component that talks to ESP-IDF system and peripheral APIs (ADR-006).
# The public headers in include/ use only <stdint.h>/<stdbool.h> types, so nothing goes in REQUIRES:
# callers of board_hal never see an IDF header through it.
#
# PRIV_REQUIRES:
# - esp_timer: not a common requirement, so hal_time.c cannot include esp_timer.h without it.
# - esp_system: already a common requirement of every component (tools/cmake/build.cmake),
#   listed anyway so this file names every IDF piece hal_sys.c really uses.
# - esp_driver_gpio: driver/gpio.h for hal_gpio.c (each peripheral driver is its own IDF component).
# - esp_driver_ledc: driver/ledc.h for hal_ledc.c.
# - esp_driver_i2c: driver/i2c_master.h for hal_i2c.c (the new i2c_master driver, constraint 3).
# freertos (vTaskDelay in hal_time.c) is a common requirement of every component, so it is not listed.
idf_component_register(SRCS "hal_sys.c" "hal_time.c" "hal_gpio.c" "hal_ledc.c" "hal_i2c.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_system esp_timer esp_driver_gpio esp_driver_ledc esp_driver_i2c)
