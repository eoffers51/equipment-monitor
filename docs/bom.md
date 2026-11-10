# Bill of materials

"Confirming measurement" is the one bench check that proves the part is what we think it is. Measured values go into `docs/log.md` (from `meter-tech` or the user only; never invented).

## Already owned

| # | Part | Value / type | Qty | Why | Confirming measurement (meter unless stated) |
|---|---|---|---|---|---|
| 1 | ESP32-S3 board | YD-ESP32-S3 (DevKitC-1 v1.0 clone, N16R8), CH343 on COM5 | 1 | the controller | 3V3 pin = 3.30 ± 0.10 V and 5V pin = 4.6–5.2 V with USB plugged in (T01); boot log shows `ESP32-S3`, 16 MB flash (T02) |
| 2 | GY-521 module | MPU-6050, I2C, with INT pin, onboard 3.3 V LDO, AD0 pulled low | 1 | vibration sensor, 1 kHz accelerometer, data-ready interrupt, cycle mode for standby | unpowered: SDA–VCC ≈ 2.2 kΩ (module pull-up); powered from 3V3: SDA idle 3.1–3.3 V; scan finds `0x68`, `WHO_AM_I` = 0x68 (T01, T04, T06) |
| 3 | BMP280 breakout | I2C, **4-pin** (VCC, GND, SCL, SDA), run from 3V3 | 1 | temperature sensor, ≥ 50 Hz, forced mode for standby | unpowered: SDA–VCC ≈ 10 kΩ (4.7 kΩ on some); scan finds `0x76` or `0x77`; `ID` = 0x58 (T01, T04, T05). **On the bench (T04, 2026-11-10): sold as BMP280 but the chip is a BME280** (`ID` 0xD0 = 0x60, adds humidity), at `0x76`. Temperature/pressure registers match the BMP280; the T05 driver must accept 0x60 and check the BME280 datasheet for the standby table and the `ctrl_hum` write order. |
| 4 | Tactile push buttons | 6 × 6 mm 4-pin (or 2-pin) | 2 | `BTN_ACK`, `BTN_MODE` | continuity: < 2 Ω pressed, OL released, measured across the **diagonal** pins (the two pins on one side of a 4-pin switch are always connected) (T01) |
| 5 | LED, green | 5 mm or 3 mm, InGaN-type green (V_f measured 2.66 V, not the 2.1 V of a classic green) | 1 | heartbeat | V_f and V_R measured in series with its resistor from 3V3, I = V_R / R; with 100 Ω only ≈ 6 mA, kept by choice (a heartbeat may be dim); 56 Ω would give ≈ 11 mA (T01) |
| 6 | LED, yellow | V_f ≈ 2.0 V | 1 | warning | same |
| 7 | LED, red | V_f ≈ 1.9 V | 1 | alarm, reset-cause flash code | same |
| 8 | LED, blue | V_f ≈ 2.9 V | 1 | load indicator (LEDC PWM) | same; expect the lowest current of the four with equal resistors |
| 9 | Resistors for the LEDs | **100 Ω** on all four (user's choice; measured in T01: red 11.9 mA, yellow 12.7 mA, green 6.2 mA, blue 4.8 mA, see `docs/pins.md`) | 4 | current limit, well under 20 mA each | resistance in Ω mode within 5 % of the colour code before fitting (T01) |
| 10 | Breadboard + jumper wires | half-size breadboard, M-M jumpers | 1 + ~20 | everything on J1 of the board | continuity of each jumper (OL = broken wire, a classic) |
| 11 | USB-C cable | data cable, to the **UART** USB-C port (CH343) | 1 | flash and serial log | the PC lists `COM5` (`USB-Enhanced-SERIAL CH343`) |

## Bench tools

| Tool | Used for |
|---|---|
| FNIRSI 2C53T (DMM mode, read by `meter-tech`) | rail voltages, pull-up resistances, LED V_f / current, average voltage of PWM and interrupt pins, 3.3 V on the button pins during deep sleep, temperature mode (**the meter has it**; the meter MCP server does not decode that mode yet, only V/Ω/F/diode/continuity; extend it or read the display by eye in T05) as a reference for the BMP280 |
| FNIRSI 2C53T scope mode (optional, read by the user) | SCL frequency (400 kHz), INT period (1 kHz), LEDC duty; never required by a Verified result |
| Bench camera (`bench-eye`) | LED timing from video: heartbeat period, 5 Hz alarm blink, number of reset-code flashes, "all LEDs dark during boot" |
| Serial log (`build-ops`, COM5) | every counter, timestamp and latency printed by the firmware |

## Maybe needed (buy or find only if the named test fails)

| # | Part | Value | Qty | When it is needed | Confirming measurement |
|---|---|---|---|---|---|
| 12 | Pull-up resistors for the buttons | 10 kΩ | 2 | only if a button pin does **not** read ≈ 3.3 V with the meter while the board is in deep sleep (T16), i.e. the internal RTC pull-up did not stay enabled | button pin 3.3 V in deep sleep, device wakes on every press |
| 13 | External I2C pull-ups | 4.7 kΩ | 2 | only if T01 finds **no** pull-ups on either module (SDA–VCC reads OL) or T04 shows I2C errors with the modules' pull-ups alone | SDA/SCL idle 3.3 V, scan finds both addresses, `i2c_err=0` over 60 s |
| 14 | Series resistors 220 Ω | 220 Ω | up to 3 | only if a red/yellow/green LED at 100 Ω is too bright to judge on camera (T03/T08) | I ≈ 5–6 mA on that LED |
| 15 | 100 nF ceramic capacitor | 0.1 µF | 2 | only if the sensor modules reset or the scan is flaky on long jumpers; across VCC–GND at each module | I2C errors drop to 0 over 60 s |
| 16 | USB breakout or bench supply with current readout | 5 V | 1 | stretch task S2 only: current in active / standby / deep sleep (the meter goes in series with the 5V pin; never USB and the 5V pin at the same time) | active ≈ 40–70 mA, standby lower, deep sleep ≈ 10–20 mA (the CH343 bridge and the board LDO dominate on this dev board) |

Nothing on this list touches mains. The board is powered from USB only; the 5V pin is used only for S2 with USB unplugged.
