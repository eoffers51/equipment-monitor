# board_hal: the only component that talks to ESP-IDF system and peripheral APIs (ADR-006).
# The public headers in include/ use only <stdint.h> types, so nothing goes in REQUIRES:
# callers of board_hal never see an IDF header through it.
#
# PRIV_REQUIRES:
# - esp_timer: not a common requirement, so hal_time.c cannot include esp_timer.h without it.
# - esp_system: already a common requirement of every component (tools/cmake/build.cmake),
#   listed anyway so this file names every IDF piece hal_sys.c really uses.
idf_component_register(SRCS "hal_sys.c" "hal_time.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_system esp_timer)
