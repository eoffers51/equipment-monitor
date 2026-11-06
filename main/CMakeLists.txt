# main: the application only (init + wiring). It reaches the chip through board_hal.
# freertos and log are common requirements of every component, so they are not listed.
# No INCLUDE_DIRS: quoted includes already find board_pins.h / app_config.h in this folder,
# and nothing may ever require main, so main exports no headers.
idf_component_register(SRCS "main.c"
                       PRIV_REQUIRES board_hal analysis)
