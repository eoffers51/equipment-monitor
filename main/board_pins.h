#pragma once

/*
 * Pin budget for the YD-ESP32-S3 (ESP32-S3-DevKitC-1 clone). Source of truth: docs/pins.md.
 * Every pin is defined here once and nowhere else; the board_hal component receives them as
 * arguments. Plain numbers on purpose: GPIO_NUM_x would need an ESP-IDF header in main/.
 */

/* Buttons: internal pull-up, pressed = LOW. Both RTC-capable (EXT1 deep-sleep wake). */
#define PIN_BTN_ACK 4  /* J1-4,  RTC_GPIO4 */
#define PIN_BTN_MODE 5 /* J1-5,  RTC_GPIO5 */

/* Optional debug output, toggled in the data-ready ISR for a scope check (T07). */
#define PIN_DBG 6 /* J1-6 */

/* I2C bus, 400 kHz, pull-ups on the sensor modules. */
#define PIN_I2C_SDA 8 /* J1-12 */
#define PIN_I2C_SCL 9 /* J1-15 */

/* MPU-6050 data-ready interrupt, push-pull, active high, rising edge. */
#define PIN_MPU_INT 10 /* J1-16 */

/* LEDs: GPIO -> 100 ohm -> LED -> GND, active high. */
#define PIN_LED_GREEN 11  /* J1-17, heartbeat */
#define PIN_LED_YELLOW 12 /* J1-18, warning */
#define PIN_LED_RED 13    /* J1-19, alarm / reset-cause code */
#define PIN_LED_BLUE 14   /* J1-20, load level, LEDC PWM */
