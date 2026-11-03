# Pin budget

Board: YD-ESP32-S3 (ESP32-S3-DevKitC-1 v1.0 clone, module ESP32-S3-WROOM-1-N16R8, 3.3 V logic, **not 5 V tolerant**). Header positions: hold the board with the antenna up, USB-C ports down; **J1 is the left header** (starts with 3V3), **J3 the right one** (starts with GND).

Every signal is on **J1**, so the whole project wires to one side of the board. The only place in the firmware that may contain these numbers is `main/board_pins.h` (one `#define` per signal).

## Signals

| Signal | GPIO | Header | Direction | Pull | Why this pin is safe | Notes |
|---|---|---|---|---|---|---|
| `BTN_ACK` | 4 | J1-4 | input | internal pull-up (≈ 45 kΩ), pressed = LOW | no strapping, no USB/UART0/JTAG/PSRAM; **RTC_GPIO4** so EXT1 deep-sleep wake works | wire: GPIO4 → button → GND |
| `BTN_MODE` | 5 | J1-5 | input | internal pull-up, pressed = LOW | same; **RTC_GPIO5** (deep-sleep wake needs GPIO ≤ 21) | wire: GPIO5 → button → GND |
| `DBG` (optional) | 6 | J1-6 | output | none | plain free pin | toggled in the data-ready ISR for an optional scope/analyzer check of the 1 kHz period (T07) |
| `I2C_SDA` | 8 | J1-12 | open-drain I/O | pull-ups on the modules (≈ 2.2 kΩ ∥ 10 kΩ) | plain free pin; the ESP-IDF example default for SDA | both sensors on this line |
| `I2C_SCL` | 9 | J1-15 | open-drain output | pull-ups on the modules | plain free pin; example default for SCL | 400 kHz |
| `MPU_INT` | 10 | J1-16 | input, rising-edge interrupt | none (the MPU-6050 drives it push-pull, active high) | plain free pin, right next to SCL | GY-521 `INT` pin |
| `LED_GREEN` (heartbeat) | 11 | J1-17 | output | none | plain digital pin (ADC2 irrelevant), input at reset so the LED stays dark until init | GPIO → resistor → LED → GND |
| `LED_YELLOW` (warning) | 12 | J1-18 | output | none | same | |
| `LED_RED` (alarm / reset code) | 13 | J1-19 | output | none | same | |
| `LED_BLUE` (load, PWM) | 14 | J1-20 | output, **LEDC** channel | none | same; LEDC can be routed to any GPIO through the GPIO matrix | 5 kHz, 8-bit duty |
| 3V3 | — | J1-1 / J1-2 | power | | | feeds **both** sensor modules |
| GND | — | J1-22 (also J3-1, J3-21, J3-22) | power | | | J1-22 is right below the LED block |

Skipped on purpose while wiring down J1: **J1-13 (GPIO3)** and **J1-14 (GPIO46)** are strapping pins; leave them empty.

Reserved, not used: GPIO0 (BOOT button, strapping), GPIO48 (onboard WS2812), GPIO19/20 (USB), GPIO43/44 (console UART0 to the CH343 bridge), GPIO35–37 (octal PSRAM), GPIO3/45/46 (strapping).

**Used: 9 signals (10 with `DBG`). Spare safe pins: GPIO1, 2, 7, 15, 16, 17, 18, 21, 38, 47 (and 39–42 if JTAG is never needed).**

Why the buttons must be on GPIO ≤ 21: deep-sleep wake (§8) uses the EXT1 wake source, which only accepts RTC-capable GPIOs (0–21 on the ESP32-S3). GPIO4 and GPIO5 are RTC_GPIO4/5. The internal pull-ups are kept alive in deep sleep by leaving the `RTC_PERIPH` power domain on (ADR-004); if the pin does not read 3.3 V with the meter during deep sleep (T16), add the optional external 10 kΩ pull-ups from the BOM.

## Sensor modules

| Module | VCC | I2C address | Identity register | Expected value |
|---|---|---|---|---|
| GY-521 (MPU-6050) | **3V3** (the module has its own 3.3 V LDO; `AD0` is pulled low on the board) | `0x68` (`0x69` only if AD0 is tied high) | `WHO_AM_I` (0x75) | `0x68` |
| BMP280 breakout | **4-pin module (VCC, GND, SCL, SDA), powered from 3V3** (user confirmed 2026-11-03; whether it carries its own regulator is then irrelevant, and 5 V is never applied) | `0x76` when SDO is low, `0x77` when SDO is high; the T04 scan decides | `ID` (0xD0) | `0x58` (a reading of `0x60` means the module is actually a BME280) |

Both modules carry their own I2C pull-up resistors (GY-521 ≈ 2.2 kΩ, BMP280 breakout ≈ 10 kΩ, some 4.7 kΩ). In parallel that is ≈ 1.8 kΩ to 3.3 V, fine for 400 kHz on breadboard wires; the ESP32's internal 45 kΩ pull-ups are negligible next to it. T01 measures the real values. Because the GY-521 regulator is fed with 3.3 V it works in dropout, so its SDA/SCL idle level reads ≈ 3.1–3.3 V, which is still a valid HIGH for the ESP32 (V_IH ≥ 0.75 × 3.3 V = 2.5 V).

Wiring of the GY-521: VCC → 3V3, GND → GND, SCL → GPIO9, SDA → GPIO8, INT → GPIO10. XDA, XCL, AD0 stay unconnected.
Wiring of the BMP280: VCC/VIN → 3V3, GND → GND, SCL/SCK → GPIO9, SDA/SDI → GPIO8. CSB (if present) → 3V3 to force I2C mode; SDO → GND for 0x76 (leave as the board has it and let the scan tell you).

## LED series resistors

Supply 3.3 V from the GPIO. The user fits **100 Ω on every LED** (decided 2026-11-03). I = (3.3 V − V_f) / 100 Ω; the "≈ 5 mA" column shows what the textbook value would have been, for the control question.

| LED | Typical V_f | I with 100 Ω (plan) | Measured T01 (R = 99.0 Ω) | R for ≈ 5 mA (not used) |
|---|---|---|---|---|
| Red | 1.9 V | (3.3 − 1.9) / 100 = 14 mA | V_f 2.091 V, V_R 1.177 V, 11.9 mA | 270 Ω |
| Yellow | 2.0 V | 13 mA | V_f 2.013 V, V_R 1.256 V, 12.7 mA | 270 Ω |
| Green (turned out InGaN-type) | 2.1 V | 12 mA | V_f 2.655 V, V_R 0.614 V, 6.2 mA (kept at 100 Ω by choice) | 220 Ω |
| Blue | 2.9 V | 4 mA | V_f 2.790 V, V_R 0.471 V, 4.8 mA | 82–100 Ω |

Every value is below the GPIO's 20 mA default drive strength and the usual 20 mA LED limit, so 100 Ω is safe; red/yellow/green will simply be bright. Total with all four on: ≈ 36 mA measured (43 mA planned) spread over four pins, no transistor needed. If a colour is uncomfortably bright on camera, swap in 220 Ω for that one LED only. T01 replaces the typical V_f values with measured ones (V_f across the LED, V_R across the resistor, I = V_R / R) and records them in `docs/log.md`.

## sdkconfig that follows from the hardware

- `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` (N16R8 module; the 2 MB default prints a boot warning)
- PSRAM stays **off** (`CONFIG_SPIRAM` not set): nothing in this project needs it, and GPIO35–37 stay reserved anyway.
- `CONFIG_FREERTOS_HZ=1000` so `vTaskDelay(1)` is 1 ms (needed for the 6 ms BMP280 loop and the ≤ 20 ms button budget).
- Watchdog and panic settings are added in T14 (see ADR-003).
