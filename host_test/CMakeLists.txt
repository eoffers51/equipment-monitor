# Host tests: run the hardware-free logic components on the PC (no board needed).
# Uses Unity, the same test framework ESP-IDF uses on the chip. Pattern from Lesson_1_3_Blink.
# Run with .\host_test\run_tests.ps1 (or /host-test).
cmake_minimum_required(VERSION 3.16)
project(host_test C)

# Where ESP-IDF is (only Unity is taken from it): pass -DIDF_PATH=... or set the IDF_PATH variable
set(IDF_PATH "$ENV{IDF_PATH}" CACHE PATH "ESP-IDF directory")
if(NOT EXISTS "${IDF_PATH}/components/unity")
    message(FATAL_ERROR "ESP-IDF not found: pass -DIDF_PATH=C:/esp/v6.0.2/esp-idf")
endif()

set(UNITY_DIR "${IDF_PATH}/components/unity/unity/src")
set(COMPONENTS_DIR "${CMAKE_CURRENT_LIST_DIR}/../components")

# One test_<component>.c per logic component; later tasks add analysis, button_decoder,
# monitor_fsm, indicator and blackbox here (ADR-006).
add_executable(host_test
    test_main.c
    test_bmp280_calc.c
    "${UNITY_DIR}/unity.c"
    "${COMPONENTS_DIR}/bmp280_calc/bmp280_calc.c")

# No fakes/ folder: the logic components include only standard C headers, so there is nothing
# from ESP-IDF to fake. If one ever needs a fake, that is a sign it broke the ADR-006 rule.
target_include_directories(host_test PRIVATE
    "${UNITY_DIR}"
    "${COMPONENTS_DIR}/bmp280_calc/include")

# -std=c11 (not gnu11): the logic must compile with any C11 compiler, so GNU extensions are off.
# -Werror turns every warning into a failed build, the same bar as the zero-warning ESP build.
target_compile_options(host_test PRIVATE -std=c11 -Wall -Wextra -Werror)

enable_testing()
add_test(NAME host_test COMMAND host_test)
