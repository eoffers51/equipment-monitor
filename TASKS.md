# Equipment Health Monitor — Task List

Companion to `TASK.md` (the spec). Section numbers like **§2** refer to that file. Design decisions are in `docs/adr/`, the pin map in `docs/pins.md`, parts in `docs/bom.md`.

**Rules**
- Do tasks in order. A task is done only when its **Verified result** is met and recorded in `docs/log.md` (date, measured numbers, photo/video if useful). Measured values come from the meter (`meter-tech`), the bench camera (`bench-eye`), the serial log (`build-ops`) or a host-test run; never from memory.
- Answer the **control questions** in chat in your own words before moving on; the coach writes the approved answers into `docs/log.md`. The answers are never written anywhere else in the repo.
- Logic components (`bmp280_calc`, `analysis`, `button_decoder`, `monitor_fsm`, `indicator`, `blackbox`) get host tests in `host_test/` (created in **T05**). A task that touches one of them is not done until `.\host_test\run_tests.ps1` passes.
- When T01–T18 are all done, the project is done. S1–S5 are optional.

**Hard constraints (quote these in `CLAUDE.md`)**
1. Pins only from `docs/pins.md`, defined once in `main/board_pins.h`. Everything is on header J1; GPIO0/3/45/46, 19/20, 43/44, 35–37 and 48 are never used.
2. 3.3 V logic everywhere. Both sensor modules are powered from the 3V3 pin; nothing is ever connected to 5 V except in S2 (and then USB is unplugged).
3. §1: accelerometer at 1 kHz from the MPU-6050 data-ready interrupt (ADR-001), never polled; every lost sample and every I2C error is counted. BMP280 ≥ 50 Hz (≈ 167 Hz, read by the same task). One I2C bus at 400 kHz, new `i2c_master` driver only.
4. §2: 50 ms analysis windows; a level change needs ≥ 200 ms of continuous exceedance; hysteresis on every threshold; spikes ≤ 20 ms can never trigger.
5. §4 / ADR-005: buttons sampled at 1 kHz from the tick ISR, decoded by hardware-free code; one debounced edge = one event; chord actions on release.
6. §6 / ADR-002: black box in `RTC_NOINIT_ATTR` RAM with magic + CRC-16, 64 records of 8 bytes, first entry after every start = reset reason; never NVS/flash.
7. §7 / ADR-003: `esp_task_wdt`, 1 s, panic on trigger, idle tasks not subscribed, feed only after successful work; the culprit name is captured in the TWDT ISR hook; red LED flashes `esp_reset_reason()` times after a fault reset.
8. §8 / ADR-004: standby = 100 ms light-sleep cycle, MPU cycle mode 20 Hz, BMP280 forced mode; deep sleep after 60 s idle in standby, EXT1 `ANY_LOW` on GPIO4 | GPIO5, wait for button release first; never sleep in Alarm.
9. §9: press-edge → LED ≤ 20 ms active / ≤ 50 ms standby (logged `lat_us`); `|dt − 1000| ≤ 50 µs` between samples; free heap at 600 s equals free heap at 60 s; no `malloc` after init.
10. Layout per ADR-006: `driver/`, `esp_sleep`, `esp_timer`, `esp_task_wdt`, `esp_system`, `esp_attr`, `gptimer`, `ledc`, `i2c_master`, `rtc_io` headers only under `components/board_hal/`; logic components include nothing from ESP-IDF or FreeRTOS and compile with `gcc -Wall -Wextra -Werror -std=c11`.
11. Build: `. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1 *> $null; idf.py build` must finish with zero warnings; `idf.py -B build_clang ... clang-check` is run before closing any task that changed C code. Never `idf.py monitor` from Claude.

---

## Overview

| # | Task | Phase | Spec | Host test | Done |
|---|---|---|---|---|---|
| T01 | Bench check without the MCU | Bench | HW | | ☑ 2026-11-04 |
| T02 | Project skeleton, HAL shell, boot log | Bring-up | §6, §9 | | ☑ 2026-11-06 |
| T03 | Four LEDs: GPIO heartbeat + LEDC blue | Bring-up | §5 | | ☐ |
| T04 | I2C bus at 400 kHz + address scan | Bring-up | HW, §1 | | ☐ |
| T05 | BMP280: ID, calibration, max-rate temperature | Sensors | §1 | `bmp280_calc` (creates `host_test/`) | ☐ |
| T06 | MPU-6050: wake, 1 kHz config, gravity check | Sensors | §1 | | ☐ |
| T07 | Data-ready interrupt → sampler task, jitter, lost samples | Timing | §1, §9 | | ☐ |
| T08 | Analysis: dynamic RMS, temperature mean, blue load LED | Analysis | §2, §5 | `analysis` | ☐ |
| T09 | Thresholds: hysteresis + 200 ms qualification | Analysis | §2 | `analysis` (threshold) | ☐ |
| T10 | Tick ISR + button decoder + latency instrumentation | Buttons | §4, §9 | `button_decoder` | ☐ |
| T11 | Device FSM and LED patterns on paper | FSM | §3, §4, §5 | | ☐ |
| T12 | FSM + indicator implementation, integration, latency | FSM | §3, §4, §5, §9 | `monitor_fsm`, `indicator` | ☐ |
| T13 | Black box in RTC RAM, dump on double press | Black box | §6 | `blackbox` | ☐ |
| T14 | Task watchdog, fault simulation, reset-cause flash code | Self-monitoring | §5, §7 | | ☐ |
| T15 | Standby: 10 Hz, sensor low-power modes, light sleep | Power | §8, §9 | | ☐ |
| T16 | Deep sleep after 60 s, button wake, sleep-cycle count | Power | §4, §6, §8 | | ☐ |
| T17 | 10-minute soak: jitter, heap, latency table, alarm never sleeps | Integration | §8, §9 | | ☐ |
| T18 | Final review + documentation | Wrap-up | all | | ☐ |

Estimated size: one session for T01–T06, T11, T13, T17, T18; two sessions for T07, T08, T09, T10, T12, T14, T15, T16.

---

## Phase 1 — Bench (no MCU)

### T01. Bench check without the MCU
**Goal:** know the real parts before a single line of code: pull-ups, LED currents, button pins, rail voltages.
**Covers:** Hardware section of the spec. **Prerequisites:** none. **Creates:** `docs/log.md` (your learning log) with the first entries.

Steps:
1. Modules unpowered, nothing else connected: measure the resistance SDA–VCC and SCL–VCC on the GY-521 and on the BMP280 breakout. Write both down; compute the parallel value.
2. Board on USB (UART port), nothing wired: measure the 3V3 pin and the 5V pin against GND.
3. Wire the GY-521 and the BMP280 to 3V3 and GND only (no data lines yet). Measure the SDA idle voltage. Note the GY-521 power LED.
4. Each LED in series with its resistor from 3V3 to GND: measure V_f across the LED and V_R across the resistor; compute I = V_R / R. Measure each resistor in Ω mode first.
5. Buttons: continuity across each pin pair, pressed and released. Mark the pair to use.

**Verified result**
- Pull-ups recorded: GY-521 ≈ 2.2 kΩ, BMP280 ≈ 10 kΩ (or 4.7 kΩ), both within ±15 % of a standard value; parallel ≈ 1.8 kΩ (or as computed).
- 3V3 pin = 3.30 ± 0.10 V; 5V pin = 4.6–5.2 V; SDA idle with both modules powered = 3.1–3.3 V.
- Table in `docs/log.md`: colour, R measured (100 Ω ± 5 %), V_f, V_R, I; red/yellow between 9 and 16 mA, green and blue between 2.5 and 7 mA (both InGaN-type, V_f ≈ 2.7–2.8 V), blue the lowest.
- Button pin pair identified: < 2 Ω pressed, OL released, and the "always-connected" pair named.

**Control questions**
1. Why does the GY-521's SDA idle voltage read slightly below 3.3 V although the pull-ups "go to VCC"?
2. Two pull-ups in parallel: what happens to the rise time on SDA, and when would that become a problem for 400 kHz?
3. The spec wants ~5 mA per LED: why control the current and not the voltage, and what would 3.3 V directly on the blue LED do?

---

## Phase 2 — Bring-up

### T02. Project skeleton, HAL shell, boot log
**Goal:** a project that builds with the ADR-006 layout, logs the reset reason and the free heap, and proves the dependency rule.
**Covers:** §6 (reset reason at every start), §9 (heap figure). **Prerequisites:** T01. **Creates:** `components/board_hal` (`hal_sys`, `hal_time`), empty folders + `CMakeLists.txt` for `mpu6050`, `bmp280`, `bmp280_calc`, `analysis`, `button_decoder`, `monitor_fsm`, `indicator`, `blackbox`; `main/board_pins.h`, `main/app_config.h`; `sdkconfig.defaults`.

Steps:
1. `sdkconfig.defaults`: `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_FREERTOS_HZ=1000`. Delete `sdkconfig`, run `idf.py set-target esp32s3`, build. Add `idf_build_set_property(MINIMAL_BUILD ON)` to the top-level `CMakeLists.txt`.
2. `board_pins.h` from `docs/pins.md` (one `#define` per signal, nothing else). `app_config.h` with the first constants (sample rate, window length).
3. `hal_sys.h`: `hal_sys_reset_reason()` (returns the enum value and a name), `hal_sys_free_heap()`, `hal_sys_min_free_heap()`; `hal_time.h`: `hal_time_now_us()`. `main.c` logs IDF version, reset reason, free heap at boot and then every 10 s.
4. Dependency proof: add `#include "driver/gpio.h"` to `components/analysis/analysis.c`, build, expect the error, remove it, rebuild clean.
5. Commit on the `eoffers51` account; check `git config user.email` first.

**Verified result**
- `idf.py build` ends with `Project build complete` and **zero** `warning:` lines; no 2 MB flash warning in the boot log.
- Serial log after `idf.py flash`: `boot: reset_reason=1 (POWERON)`, `boot: free_heap=<n>` where `<n>` > 300 000; after pressing RST the same reason; after a planned `esp_restart()` test line: `reset_reason=3 (SW)` (then remove the line).
- Step 4 produced `fatal error: driver/gpio.h: No such file or directory` and the build is clean again.
- `grep -rln "driver/\|esp_sleep\|esp_timer\|esp_task_wdt\|esp_system\|esp_attr\|gptimer\|ledc\|i2c_master\|rtc_io" components main` lists only files under `components/board_hal/`.

**Control questions**
1. Why does `MINIMAL_BUILD` plus `PRIV_REQUIRES` make the dependency rule something the compiler enforces, not just a convention?
2. What is the difference between `CONFIG_FREERTOS_HZ=1000` and the 1 kHz sampling rate of the sensor — what does each clock drive?
3. Where does `esp_reset_reason()` get its information from, and why can a value differ between the chip's own reset register and what IDF reports?

### T03. Four LEDs: GPIO heartbeat + LEDC blue
**Goal:** the LED block on J1-17..J1-20 driven from the HAL; the green heartbeat timed on camera; the blue LED on PWM with a meter-checkable duty.
**Covers:** §5. **Prerequisites:** T02. **Creates/extends:** `hal_gpio` (LED outputs), `hal_ledc` (one channel, 5 kHz, 8-bit).

Steps:
1. Wire the four LEDs with their resistors to GPIO11–14, cathodes to J1-22 GND (see `docs/pins.md`). Camera on the LED block.
2. `hal_gpio_led_init(gpio)` sets the level LOW before configuring the output (no flash at init); `hal_gpio_led_set(gpio, bool)`.
3. Test loop in `main.c` (temporary): green toggles every 500 ms; red and yellow alternate every 1 s.
4. `hal_ledc_init(gpio, 5000 Hz, 8 bit)` and `hal_ledc_set_duty(0..255)`. Temporary test: hold duty 64, 128, 255 for 10 s each, then sweep 0→255 over 2 s repeatedly.
5. Reset the board three times on camera: no LED may light between reset and the first log line.

**Verified result**
- Camera: green period 1.00 ± 0.05 s, on-time 500 ± 30 ms, averaged over ≥ 10 cycles.
- Meter on **GPIO14 (before the resistor)** against GND: 0.83 ± 0.2 V at duty 64, 1.65 ± 0.2 V at 128, 3.30 ± 0.1 V at 255.
- Camera: blue brightness ramps visibly during the sweep; no LED lights during three resets.
- `grep -n "GPIO_NUM_\|[^a-z_]1[1-4][^0-9]" components/board_hal/*.c` finds no pin numbers inside the HAL (they arrive as arguments).

**Control questions**
1. Why must the output level be written before the pin is switched to output mode, and what did the camera check in step 5 prove?
2. What does the meter actually measure on a 5 kHz PWM pin, and why would the reading be wrong if you measured across the LED instead?
3. What limits the LEDC duty resolution at a given frequency on this chip?

### T04. I2C bus at 400 kHz + address scan
**Goal:** both sensors answer on one bus; the firmware survives a missing device.
**Covers:** Hardware, §1 (one bus, 400 kHz). **Prerequisites:** T03. **Creates/extends:** `hal_i2c` (`hal_i2c_init(sda, scl, 400000)`, `hal_i2c_probe(addr)`, `hal_i2c_write_reg`, `hal_i2c_read_regs` with a 20 ms timeout, `hal_i2c_bus_reset`, error counter).

Steps:
1. Wire SDA → GPIO8 (J1-12), SCL → GPIO9 (J1-15) to both modules; keep the modules on 3V3.
2. `hal_i2c_init` with the new `i2c_master` driver (`i2c_new_master_bus`, internal pull-ups off because the modules have theirs), one device handle per sensor address.
3. Scan 0x08–0x77 with `i2c_master_probe`, log every hit.
4. Unplug the BMP280's SDA, rescan; plug it back, rescan. No crash, no `ESP_ERROR_CHECK` abort in the scan path.
5. Meter: SDA and SCL idle levels with the ESP32 connected.

**Verified result**
- Log: `i2c: 400000 Hz sda=8 scl=9`, `i2c: found 0x68`, `i2c: found 0x76` (or `0x77`), `i2c: 2 device(s)`; exactly two hits.
- Step 4: `i2c: 1 device(s)` while unplugged, `2 device(s)` again after replugging, error counter incremented by the number of failed probes and nothing else.
- Meter: SDA and SCL idle = 3.1–3.3 V.
- Optional (scope mode of the 2C53T, read by you): SCL burst frequency 400 kHz ± 10 % during a scan.

**Control questions**
1. What does a device do electrically to "ACK" its address, and why can a probe of an empty address never damage anything?
2. Why do we not enable the ESP32's internal pull-ups on SDA/SCL here, and when would we have to?
3. What is the worst-case time one `hal_i2c_read_regs` of 6 bytes can block, and where does that number come from?

---

## Phase 3 — Sensors

### T05. BMP280: ID, calibration, max-rate temperature
**Goal:** a working temperature stream at the sensor's maximum rate, a hardware-free compensation routine, and the host-test project.
**Covers:** §1 (≥ 50 Hz). **Prerequisites:** T04. **Creates:** `bmp280` driver (ID, reset, calibration read, `bmp280_configure(osrs_t, osrs_p, t_sb, filter, mode)`, `bmp280_read_raw_temp`), `bmp280_calc` logic (`bmp280_calc_temp_centi(calib, raw)` → int32 °C × 100), **`host_test/`** copied from `Lesson_1_3_Blink` with `test_bmp280_calc.c`.

Steps:
1. Read `ID` (0xD0). If it says 0x60 you own a BME280: note it, the temperature path is identical.
2. Read the calibration block 0x88–0x9F into `dig_T1..T3` (and P1..P9 for S3). Log them.
3. Configure for the highest temperature rate: compute from the datasheet formula the conversion time for `osrs_t = ×1`, `osrs_p = ×1`, `t_sb = 0.5 ms`, filter off, normal mode. Write the two config bytes, read them back.
4. `bmp280_calc`: implement the datasheet's 32-bit integer compensation. Host test: with **your module's** calibration values, compare the integer result with a double-precision implementation of the same formula for raw values 500000, 520000 and 540000 (expect agreement within 0.01 °C) and check monotonicity.
5. Temporary task: read raw temperature every 6 ms, log the compensated value and the reads-per-second once a second. Touch the sensor with a finger for 20 s.

**Verified result**
- Log: `bmp280: id=0x58` (or `0x60`), `dig_T1=… dig_T2=… dig_T3=…` all non-zero and not 0xFFFF, `ctrl_meas=0x27 config=0x00` read back.
- Datasheet-computed ODR written in `docs/log.md` and ≥ 150 Hz; measured `bmp280: reads/s=166 ± 5`.
- Temperature plausible (15–30 °C) and within ±1.0 °C of the meter in temperature mode (the meter has it; if `meter-tech` cannot read that mode yet, the user reads the display and quotes it); finger touch raises it by ≥ 2 °C within 20 s and it falls back afterwards.
- `.\host_test\run_tests.ps1` builds with `-Wall -Wextra -Werror -std=c11` and reports ≥ 4 passing tests, 0 failing.

**Control questions**
1. In the integer compensation, which intermediate product would overflow a 32-bit signed integer if the raw value and `dig_T2` were at their limits, and how does the reference code avoid it?
2. Why did the datasheet ODR come out higher than "reads per second" could ever need, and what does reading faster than the ODR give you?
3. Why does the BMP280 have no data-ready pin, and what does that mean for how the sampler task will read it later?

### T06. MPU-6050: wake, 1 kHz config, gravity check
**Goal:** the accelerometer configured for 1 kHz output and proven with gravity.
**Covers:** §1. **Prerequisites:** T05. **Creates:** `mpu6050` driver (`mpu6050_init` = reset, wake with PLL clock, DLPF, `SMPLRT_DIV = 0`, ±2 g; `mpu6050_read_accel(int16 x,y,z)`; register dump helper).

Steps:
1. Read `WHO_AM_I` (0x75). Write `PWR_MGMT_1`: device reset, wait 100 ms, then clear `SLEEP` and select `CLKSEL = 1` (PLL with X gyro). Read it back.
2. `CONFIG` (0x1A) `DLPF_CFG = 1`, `SMPLRT_DIV` (0x19) = 0, `ACCEL_CONFIG` (0x1C) = ±2 g. Read all three back and log them.
3. Temporary polling loop at 10 Hz (polling is allowed only in this bring-up task): read 6 bytes from 0x3B, log x, y, z and the magnitude in g.
4. Board flat on the bench, then on each of its edges: record the readings.
5. Tap the breadboard while logging; note the peak.

**Verified result**
- Log: `mpu6050: who_am_i=0x68`, `pwr_mgmt_1=0x01 config=0x01 smplrt_div=0x00 accel_config=0x00` after read-back.
- Flat: z = 16384 ± 1600 LSB, |x| and |y| < 1600; magnitude = 1.00 ± 0.10 g in all four orientations (table in `docs/log.md`).
- A tap shows a magnitude peak > 1.3 g in at least one 10 Hz sample.

**Control questions**
1. With `DLPF_CFG = 1` the gyro output rate is 1 kHz; what would the sample rate have been with `DLPF_CFG = 0` and `SMPLRT_DIV = 0`, and why does the accelerometer not care?
2. What is the LSB-per-g figure at ±2 g, and how does it change the overflow risk in the RMS calculation later?
3. Why choose the gyro PLL as the clock source when the project only uses the accelerometer in active mode?

### T07. Data-ready interrupt → sampler task, jitter, lost samples
**Goal:** the real 1 kHz data path: INT → ISR → task notification → read, with timing and loss counters that prove §1 and §9.
**Covers:** §1 (interrupt, no sample lost, counters), §9 (≤ 50 µs). **Prerequisites:** T06. **Creates/extends:** `hal_gpio` (INT input, ISR install on the calling core, IRAM handler calling a registered callback), `mpu6050_int_config` (push-pull, active high, latched, cleared by any read, `DATA_RDY_EN`), `main/sampler_task.c`.

Steps:
1. Wire INT → GPIO10 (J1-16). Configure the MPU's INT pin and enable `DATA_RDY_EN`. Meter on GPIO10: average voltage before any firmware reaction.
2. ISR (IRAM): take `hal_time_now_us()`; if a read is still pending, `lost++`; set pending; notify the sampler task (`vTaskNotifyGiveFromISR`). Install the ISR service **from the sampler task**, which is pinned to core 1 at priority 10.
3. Sampler loop: wait for the notification (100 ms timeout → `no_int++`); read 6 bytes; clear pending; on error `i2c_err++` and after 10 consecutive errors call `hal_i2c_bus_reset`. Keep `dt_min/dt_max` of the ISR timestamps and `read_max_us`.
4. Every 6th sample read the BMP280 raw temperature in the same task (replaces the T05 loop). Every second log one stats line and reset min/max.
5. Loss experiments: (a) add `vTaskDelay(2)` inside the read path for 10 s, observe, remove; (b) short SDA to GND for 2 s with a jumper, observe, remove; (c) pull the INT wire for 2 s, observe, reinsert.

**Verified result**
- Stats line for 60 consecutive seconds: `samples=1000±1 lost=0 i2c_err=0 no_int=0 dt_min≥950 dt_max≤1050 read_max<500` (µs), so `|dt − 1000| ≤ 50 µs`.
- Experiment (a): `lost` ≈ 600–700 per second while the delay is in; (b): `i2c_err` ≈ 1500–2000 over the 2 s, `samples` back to 1000 within 2 s after the short is removed; (c): `no_int` increments ≈ 10 per second, `samples=0`, recovery after reinsertion.
- Meter on GPIO10 in normal operation: 0.1–1.0 V average; converted with `t_react = V / 3.3 V × 1000 µs` it agrees with the logged ISR-to-read latency within ±30 %.
- Optional (scope on `DBG` GPIO6 toggled in the ISR): 500 Hz square wave.

**Control questions**
1. Why is the pending flag enough to detect a lost sample, and what kind of loss would it miss?
2. The ISR runs on core 1 only because of where `gpio_install_isr_service` was called — explain what that implies for interrupt latency and for the control task on core 0.
3. The INT pin's average voltage is a measurement of a time: write the formula and state its two assumptions.
4. Why is `vTaskDelay(2)` in the read path a 2–3 ms delay with `CONFIG_FREERTOS_HZ=1000`, and what would it be at the default 100 Hz?

---

## Phase 4 — Analysis

### T08. Analysis: dynamic RMS, temperature mean, blue load LED
**Goal:** the vibration number of §2 computed in hardware-free code, tested on the host with synthetic signals, and shown on the blue LED.
**Covers:** §2 (RMS with gravity removed, mean), §5 (blue brightness). **Prerequisites:** T07. **Creates:** `analysis` (`analysis_init(rate_hz, window_ms)`, `analysis_feed_accel(x, y, z)`, `analysis_feed_temp(centi)`, `analysis_window_ready()` → `{rms_mg, temp_mean_centi, n_accel, n_temp}`), `host_test/test_analysis.c`.

Steps:
1. Gravity removal: per-axis exponential average with α = 1/256 at 1 kHz (τ ≈ 0.26 s); dynamic = sample − estimate. Window = 50 ms = 50 samples. RMS = sqrt(mean(dx² + dy² + dz²)), converted to milli-g with the LSB/g figure from T06. Use a 64-bit or float accumulator; justify in a comment.
2. Host tests: constant input (any orientation) → RMS 0 ± 1 mg after settling; 50 Hz sine of amplitude 1000 LSB on one axis → RMS = 707 LSB ± 1 % (43 mg); the same sine plus 16384 LSB DC → identical RMS; temperature mean of a known list.
3. Target: the sampler feeds `analysis`; a window result every 50 ms is sent to the control task's queue as `EVT_WINDOW`. Log once per second: `vib_rms_mg=<n> temp=<x.xx>`.
4. Blue LED: duty = min(255, rms_mg × 255 / 400), updated every window from the control task.
5. Bench: at rest 10 s; tapping; continuous shaking of the breadboard for 5 s. Camera on the blue LED.

**Verified result**
- Host tests: ≥ 6 tests pass, including the two sine cases within 1 %.
- Target at rest on the bench: `vib_rms_mg` ≤ 10 for 10 consecutive seconds; shaking: ≥ 200; temperature mean stable within ±0.1 °C at rest.
- Camera: blue LED dark at rest and clearly brighter while shaking; the logged duty value tracks `vib_rms_mg` with the formula above (spot-check 3 values).
- Heap logged at 60 s and 120 s identical.

**Control questions**
1. Why remove gravity per axis before taking the magnitude, rather than subtracting 1 g from the magnitude?
2. What does α = 1/256 mean in seconds, and how would the RMS of a 2 Hz hand motion differ from a 100 Hz machine vibration of equal amplitude through this filter?
3. Where could an `int32_t` overflow in your RMS code, and how did the host test (not) catch it?

### T09. Thresholds: hysteresis + 200 ms qualification
**Goal:** NORMAL / WARNING / ALARM per signal with no flicker and no reaction to short spikes, proven with millisecond-exact host tests.
**Covers:** §2 (thresholds, 20 ms / 200 ms, hysteresis). **Prerequisites:** T08. **Creates:** `threshold.h/.c` inside `analysis` (`threshold_init(warn_on, warn_off, alarm_on, alarm_off, qualify_ms)`, `threshold_feed(value, dt_ms)` → level or "no change"), `host_test/test_threshold.c`; `app_config.h` gets the numbers.

Steps:
1. Design on paper: a level only rises after ≥ 200 ms continuously above the *on* threshold and only falls after ≥ 200 ms continuously below the *off* threshold. Write the four thresholds for vibration (start: warn 150/120 mg, alarm 300/250 mg) and temperature (ambient + 3 / + 2 °C warn, + 6 / + 5 °C alarm, using the T05 ambient).
2. Host tests in ms: spike of 20 ms at 10 × alarm_on → no change; 150 ms → no change (document as the undefined zone); 250 ms → ALARM; value oscillating between alarm_off and alarm_on for 5 s → stays ALARM; below alarm_off for 200 ms → WARNING; below warn_off → NORMAL; feed with dt = 500 ms (standby) → ALARM after one window.
3. Target: the control task feeds both signals per window; log every level change as `level vib NORMAL->ALARM t=<ms>` with the window value.
4. Bench: 10 single taps; one 1 s shake; finger on the BMP280 for 60 s (temporarily lower the temperature thresholds if your room is warm).

**Verified result**
- Host tests: ≥ 8 pass; the 20 ms spike case is tested with an amplitude 10 × the alarm threshold.
- Serial log: 10 single taps → **0** level-change lines; the 1 s shake → exactly `vib NORMAL->ALARM` then `vib ALARM->WARNING`/`->NORMAL` as it decays. A temporary debug line `vib window above alarm_on t=<ms>` on the first window over the threshold shows the level change stamped 200–250 ms later.
- Finger test: `temp NORMAL->WARNING` after the mean passes warn_on for ≥ 200 ms, and `WARNING->NORMAL` only after it drops below warn_off.

**Control questions**
1. Draw the four thresholds and show a signal that would flicker without hysteresis but is stable with it.
2. Why is "≥ 200 ms above" tested in milliseconds rather than in windows, and what breaks if the window length changes in standby?
3. Can a 20 ms spike straddling two 50 ms windows ever reach the 200 ms qualification? Show the arithmetic.

---

## Phase 5 — Buttons and the state machine

### T10. Tick ISR + button decoder + latency instrumentation
**Goal:** every physical press becomes exactly one event of the right kind, with the press-to-event latency measured.
**Covers:** §4 (all button semantics, exactly once), §7 (chord timing), §9 (latency instrumentation). **Prerequisites:** T09. **Creates:** `hal_tick` (gptimer, 1 MHz resolution, 1000-tick alarm, callback), `hal_gpio` button inputs (pull-up, `hal_gpio_button_read_both()`), `button_decoder` (ADR-005 events, `chord_held_ms`), `main/events.h`, `host_test/test_button_decoder.c`.

Steps:
1. Wire BTN_ACK GPIO4 → button → GND and BTN_MODE GPIO5 → button → GND (J1-4, J1-5), internal pull-ups.
2. `button_decoder_feed(dec, ack, mode, now_ms)` → at most one event per call. Debounce = 8 identical samples; MODE_SHORT after release + 300 ms with no second press; MODE_DOUBLE on the second press edge within 300 ms; MODE_LONG at 2000 ms held with ACK up; CHORD_HANG / CHORD_CRASH on release after ≥ 3000 / ≥ 6000 ms both held. Raw edge timestamp kept per button.
3. Host tests (≥ 10): a 7 ms bounce burst → one press; 20 clean presses → 20 events; short, double, long, long-cancelled-by-ACK, chord 4 s, chord 7 s, chord released at 2 s → nothing, two presses 400 ms apart → two SHORTs.
4. Tick ISR: read both pins, feed the decoder, `xQueueSendFromISR` the event with the ISR timestamp; count queue-full drops; measure the ISR duration (max) like dental T06.
5. Control task (first version): receives events, logs `btn <event> t=<ms> lat_us=<edge→event>`; on any press edge toggles the green LED immediately (temporary feedback) and logs `lat_led_us`.

**Verified result**
- Host tests: ≥ 10 pass, including the bounce burst and the long-cancelled-by-ACK case.
- Serial log: 20 ACK presses → exactly 20 `ACK_PRESS`; 20 single MODE presses at least 0.5 s apart → 20 `MODE_PRESS` and 20 `MODE_SHORT`; 10 double presses → 10 `MODE_DOUBLE`, **0** `MODE_SHORT`; MODE held 2.5 s → 1 `MODE_LONG`, 0 `MODE_SHORT`; both held ≈ 4 s → `CHORD_HANG`; ≈ 7 s → `CHORD_CRASH`; both held ≈ 2 s → nothing.
- `lat_led_us` ≤ 12 000 for every one of 20 presses; ISR max duration < 5 µs; queue drops = 0.
- Camera: the green LED changes on every press (20 of 20 visible).

**Control questions**
1. Why does a double-press-capable button make the *short* action late by design, and which §9 reaction is still inside 20 ms?
2. Why are chord actions decided on release and not at the 3 s mark?
3. What is the worst-case latency from contact closure to `ACK_PRESS` in your implementation, in ms, and which three terms add up to it?
4. `chord_held_ms` is read by a task while the ISR writes it: why is that safe for this variable and not for a two-field struct?

### T11. Device FSM and LED patterns on paper
**Goal:** the complete behaviour of §3–§5 written as a table before any code.
**Covers:** §3 (states), §4 (button actions), §5 (indication), §8 (which states sleep). **Prerequisites:** T10. **Creates:** `docs/fsm.md`.

Steps:
1. States from §3: `NORMAL`, `WARNING`, `ALARM`, `ALARM_ACK`, `STANDBY`, `SLEEP`. Events: `LEVEL_CHANGED(worst of vib/temp)`, `ACK_PRESS`, `MODE_SHORT`, `MODE_DOUBLE`, `MODE_LONG`, `CHORD_HANG`, `CHORD_CRASH`, `IDLE_TIMEOUT`, `BOOT(reset_reason)`, `WAKE(ext1 mask)`.
2. Fill every state × event cell with next state + actions from a fixed action list (`SET_RATE_ACTIVE`, `SET_RATE_STANDBY`, `LOG_EVENT`, `DUMP_BLACKBOX`, `ENTER_DEEP_SLEEP`, `SIMULATE_HANG`, `SIMULATE_CRASH`, `RESTART_IDLE_TIMER`). "Ignored" is an explicit answer. Fixed decisions from the ADRs: power-on starts in `NORMAL`; a deep-sleep wake starts in `STANDBY`; `ALARM` from `STANDBY` switches to the active rate; `ALARM_ACK` → `NORMAL` when the level is back to NORMAL; a new ALARM level in `ALARM_ACK` → `ALARM`; `IDLE_TIMEOUT` only counts in `STANDBY`.
3. LED table per state: green 1 Hz (500/500) active, 0.5 Hz (100/1900) in `STANDBY`; yellow on in `WARNING` (and while a warning level exists in `ALARM_ACK`?) — decide; red 5 Hz (100/100) in `ALARM`, solid in `ALARM_ACK`; blue = load in every awake state; everything off in `SLEEP`. Chord feedback (yellow ≥ 3 s, red ≥ 6 s) and the reset-code flashes are overlays owned by `indicator`, not states.
4. Draw the diagram. Define `monitor_fsm.h` (inputs = events, output = `fsm_actions_t`, no IDF types) and `indicator.h` (`indicator_update(state, now_ms, load, overlay)` → four LED levels).

**Verified result**
- `docs/fsm.md` has the full table: 6 states × 10 events = 60 cells, none empty, each action from the fixed list; the LED table has all 6 states × 4 LEDs.
- The edge cases are written down: ACK in `NORMAL`, MODE_SHORT in `ALARM` (does it leave an alarm?), LEVEL_CHANGED in `SLEEP`, CHORD in every state, the deep-sleep wake press being consumed.
- Both header files exist, compile on the host (empty `.c`), and contain no `esp_`/FreeRTOS types.

**Control questions**
1. Why is `ALARM_ACK` a separate state and not a flag inside `ALARM`?
2. Which of your cells would change if "never sleep during an alarm" were dropped from the spec?
3. Moore or Mealy: which is your table, and what does that mean for where the LED pattern is decided?

### T12. FSM + indicator implementation, integration, button latency
**Goal:** the device behaves per `docs/fsm.md` on the bench with host-tested logic; the §9 active-mode latency is measured.
**Covers:** §3, §4 (except sleep), §5 (patterns), §9 (20 ms). **Prerequisites:** T11. **Creates:** `monitor_fsm`, `indicator`, `host_test/test_monitor_fsm.c`, `host_test/test_indicator.c`; `main/control_task.c` becomes the real control task (queue → thresholds → FSM → actions → indicator → HAL, 10 ms loop timeout).

Steps:
1. `monitor_fsm.c`: table-driven (`const` table indexed by state and event, designated initializers, `*_COUNT` sentinels, `assert` bounds). Entry/exit actions, not per-tick work.
2. Host tests: one test per non-ignored cell, one test proving ignored events keep the state, a full scenario NORMAL → WARNING → ALARM → ALARM_ACK → NORMAL → STANDBY → NORMAL.
3. `indicator.c`: blink phases from `now_ms`, overlays (chord feedback, flash code sequence); host test the 5 Hz pattern edges and a 6-flash sequence by stepping time.
4. Control task: `apply()` is the only function that turns `fsm_actions_t` into HAL calls. Latency: when `ACK_PRESS` turns the red LED solid (in `ALARM`) log `lat_us = now − t_edge`. `MODE_SHORT` toggles `STANDBY` (LED pattern only; the real power behaviour comes in T15).
5. Bench run on camera: shake → ALARM (red 5 Hz) → ACK (red solid) → still → NORMAL; MODE short → STANDBY (green slow) → MODE short → NORMAL; finger → WARNING (yellow).

**Verified result**
- Host tests: ≥ 20 FSM tests + ≥ 4 indicator tests pass; `grep -n "esp_\|freertos\|driver/" components/monitor_fsm components/indicator` finds nothing.
- Camera: red in ALARM 100 ± 20 ms on / 100 ± 20 ms off averaged over ≥ 10 cycles; red solid within one video frame after the ACK press; green 1.00 ± 0.05 s active, 2.00 ± 0.1 s period in STANDBY.
- Serial log: 20 ACK presses during alarms → `lat_us` max ≤ 20 000; state-change lines match `docs/fsm.md` for the whole step-5 scenario.
- Queue drops = 0, `lost = 0` throughout.

**Control questions**
1. Which bug appears if the red LED were switched in the transition code instead of on state entry/exit?
2. What did a host test catch (or would have caught) that the bench cannot show reliably?
3. How does the control task's 10 ms loop timeout interact with the 20 ms budget, and why is the press edge not delayed by it?

---

## Phase 6 — Black box and self-monitoring

### T13. Black box in RTC RAM, dump on double press
**Goal:** the last 64 events survive resets and crashes, are gone after a power loss, and a corrupted log is refused.
**Covers:** §6 (all bullets). **Prerequisites:** T12. **Creates:** `blackbox` logic (ADR-002 layout, CRC-16, `blackbox_open/add/count/get/dump`), `hal_rtcmem` (`RTC_NOINIT_ATTR` buffer + a separate fault-culprit struct), `host_test/test_blackbox.c`.

Steps:
1. Record and header layout from ADR-002; CRC-16 (CCITT, loop implementation) over the header and records. `blackbox_open(buf, len)` returns `BB_RESTORED` or `BB_FRESH` and the reason (`magic`, `crc`, `index out of range`).
2. Host tests: 70 adds keep the newest 64 in order; open on zeroed memory → FRESH; flip one byte of a valid buffer → FRESH with reason `crc`; set `head = 200` → FRESH with reason `index`; the sleep-cycle counter survives open/add cycles.
3. Boot: `blackbox_open` on the RTC buffer, then add `RESET(reason, culprit)` as the first entry of this boot. Every FSM state change, button event, level change, I2C fault (counter crossing) and (later) sleep/wake is added via `apply()`.
4. `MODE_DOUBLE` → `blackbox_dump` prints `#<i> boot=<b> t=<ms> <TYPE> <arg> <aux>` for all entries, oldest first.
5. Tests on target: (a) run a scenario, double press, press RST, double press again; (b) unplug USB for 10 s, replug, double press; (c) temporary line before `blackbox_open` that flips one byte, flash, observe, remove, flash again.

**Verified result**
- Host tests: ≥ 6 pass.
- (a) After RST the dump shows the pre-reset entries **plus** a new `RESET 1 (POWERON)` entry at the start of the new boot; the count never exceeds 64.
- (b) After the 10 s power loss the dump has exactly **one** entry: `RESET 1`; boot log says `blackbox: FRESH (magic)` or `FRESH (crc)`.
- (c) Boot log `blackbox: FRESH (crc)` with the corruption line, `RESTORED` without it.
- `sizeof` the RTC struct logged and ≤ 600 bytes.

**Control questions**
1. What exactly is "garbage" in RTC RAM after power-up, and why is a magic word alone not enough to detect it?
2. Why does pressing RST keep the log although the reset reason says POWERON?
3. If the chip crashed between writing a record and writing the CRC, what happens at the next boot, and is that acceptable under §6?

### T14. Task watchdog, fault simulation, reset-cause flash code
**Goal:** a hang anywhere is caught within 1 s, named, and visible after the reboot.
**Covers:** §7 (all), §5 (flash code), §6 (first entry = reset reason + culprit). **Prerequisites:** T13. **Creates/extends:** `hal_wdt` (`hal_wdt_init`, `hal_wdt_subscribe_self(name)`, `hal_wdt_feed()`, ISR hook → culprit struct), `sdkconfig.defaults` (ADR-003 values), `indicator` flash-code overlay, `apply()` handlers for `SIMULATE_HANG` / `SIMULATE_CRASH`.

Steps:
1. `sdkconfig.defaults`: `CONFIG_ESP_TASK_WDT_TIMEOUT_S=1`, `CONFIG_ESP_TASK_WDT_PANIC=y`, `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=n`, `..._CPU1=n`. Rebuild from a deleted `sdkconfig`.
2. Sampler and control task subscribe themselves. Feed rules: sampler only after a successful sample; control task once per loop. Verify in the log that normal operation never triggers.
3. `esp_task_wdt_isr_user_handler()` in `hal_wdt.c` calls `esp_task_wdt_print_triggered_tasks()` with a handler that copies the text into the RTC culprit struct (no logging). At boot the culprit is appended to the RESET entry and then cleared.
4. `CHORD_HANG` → the control task enters `while (1) {}` (interrupts enabled). `CHORD_CRASH` → `abort()` or a null write. Flash-code overlay: after a reset with reason ∈ {PANIC, INT_WDT, TASK_WDT, WDT, BROWNOUT, CPU_LOCKUP} the red LED flashes `reason` times (200 ms on / 300 ms off, then 1 s pause, once).
5. Three fault tests with the camera and the serial log: chord ≈ 4 s; chord ≈ 7 s; INT wire pulled for 3 s during active mode.

**Verified result**
- Chord ≈ 4 s then release: within ≤ 1.0 s the log shows `Task watchdog got triggered` naming `control`, the chip reboots, the first black-box entry of the new boot is `RESET 6 (TASK_WDT) culprit=control`, and the camera counts **6** red flashes.
- Chord ≈ 7 s: `Guru Meditation Error` / abort backtrace, then `RESET 4 (PANIC)` and **4** red flashes.
- INT wire pulled ≥ 1 s: `RESET 6 (TASK_WDT) culprit=sampler` after the reboot; with the wire back the device runs normally.
- 10 minutes of normal operation (active, with button presses and alarms): zero watchdog triggers; the stats line keeps `lost=0`.
- Power-on, RST and `esp_restart()` resets produce **no** flashes.

**Control questions**
1. Why does the hang loop have to keep interrupts enabled for this test, and which watchdog (and which reset reason) would fire otherwise?
2. "Feed only after successful work": what class of failure does this catch that "feed every loop" would not, and what does it risk?
3. Why can the culprit name be captured in the ISR hook but the RESET entry only be written at the next boot?
4. Why were the idle tasks unsubscribed, and what check did we give up by doing so?

---

## Phase 7 — Power

### T15. Standby: 10 Hz, sensor low-power modes, light sleep
**Goal:** `STANDBY` really saves power: sensors in their low-power modes, the CPU asleep between 100 ms cycles, buttons still answered within 50 ms.
**Covers:** §8 (standby bullets, no sleep in alarm), §9 (50 ms). **Prerequisites:** T14. **Creates/extends:** `mpu6050_set_cycle_mode(20 Hz, accel only)` / `mpu6050_set_active()`, `bmp280_forced_read()` / `bmp280_set_sleep()`, `hal_sleep_light_until(deadline_us, gpio_mask)` (timer + GPIO low-level wake, UART flush first), `hal_gpio_int_enable(bool)`, control task standby loop (ADR-004).

Steps:
1. Entering `STANDBY`: disable the data-ready interrupt and `INT_ENABLE`; MPU → `CYCLE=1`, `TEMP_DIS=1`, gyro standby, `LP_WAKE_CTRL = 20 Hz`; BMP280 → sleep mode. Read back `PWR_MGMT_1/2`.
2. Standby cycle every 100 ms: BMP280 forced conversion, MPU latest accel read, BMP280 read, `analysis` (re-initialised for 10 Hz, window = 500 ms), thresholds, FSM, LEDs, log, then light sleep until the next 100 ms boundary with GPIO wake on both buttons.
3. Leaving `STANDBY` (MODE short or an ALARM level): sensors back to active modes, `INT_ENABLE`, sampler re-armed, analysis back to 1 kHz.
4. Latency in standby: same `lat_us` log on `ACK_PRESS` / `MODE_PRESS`.
5. Bench: 60 s in standby at rest; 20 presses in standby; a 2 s shake in standby; toggle active/standby 10 times.

**Verified result**
- Log in standby: `mpu6050: pwr_mgmt_1=0x28 pwr_mgmt_2=0x87` (cycle, temp off, 20 Hz, gyro standby), `bmp280: mode=forced`, `standby: cycles/s=10 ± 1`, `sleep: wake=TIMER` ≈ 10 per second and `wake=GPIO` on every press.
- `lat_us` max over 20 presses in standby ≤ 50 000; the green LED keeps its 2.00 ± 0.1 s pattern on camera.
- Shake in standby: `vib NORMAL->ALARM` within 1 s, the state goes to `ALARM`, the stats line shows `samples=1000` again and `sleep:` lines stop until the alarm is acknowledged and cleared.
- 10 toggles: `lost` grows by ≤ 2 per transition (explain the number) and `i2c_err=0`.

**Control questions**
1. Why does the ESP32's timer, not the MPU's cycle rate, define the 10 Hz, and why is the MPU set to 20 Hz and not 5 Hz?
2. What stops during light sleep and what keeps running? Name one thing in this firmware that would misbehave if it were not handled before `esp_light_sleep_start()`.
3. How much of the 50 ms budget is spent before the tick ISR even sees the button, and where did that number come from in your log?

### T16. Deep sleep after 60 s, button wake, sleep-cycle count
**Goal:** the device goes to deep sleep from standby and on MODE hold, wakes on any button, and remembers how often it slept.
**Covers:** §8 (deep sleep bullets), §4 (MODE hold > 2 s), §6 (sleep/wake entries survive). **Prerequisites:** T15. **Creates/extends:** `hal_sleep_deep(gpio_mask)` (EXT1 `ANY_LOW`, `RTC_PERIPH` on, `rtc_gpio_pullup_en`, UART flush), `hal_sleep_wake_info()` (cause bitmask + EXT1 pin mask), `monitor_fsm` `SLEEP` state actions, `blackbox` header sleep-cycle counter, `IDLE_TIMEOUT` timer in the control task (60 s, restarted by any event, only in `STANDBY`).

Steps:
1. `SLEEP` entry sequence (ADR-004): `LOG SLEEP(cycle)`, sensors to sleep (MPU `SLEEP=1`, BMP280 sleep), LEDs off, **wait until both buttons read released**, `sleep_cycles++`, arm EXT1 on GPIO4 | GPIO5, flush UART, `esp_deep_sleep_start()`.
2. Boot: if `reset_reason == DEEPSLEEP`, decode `esp_sleep_get_wakeup_causes()` and `esp_sleep_get_ext1_wakeup_status()`, add `WAKE(pin mask)` after the RESET entry, start the FSM in `STANDBY`, consume the waking press.
3. Idle timer: restarted by every event in `STANDBY`; fires `IDLE_TIMEOUT` after 60 s.
4. Bench: standby, hands off, stopwatch from the last event; press ACK to wake; repeat three times; then MODE hold ≈ 2.5 s; double press after each wake.
5. Meter during deep sleep: GPIO4 and GPIO5 against GND; GPIO11–14 against GND.

**Verified result**
- Log: `SLEEP cycle=1` 60 ± 1 s after the last event in standby, then silence; ACK press → `boot: reset_reason=8 (DEEPSLEEP) wake=EXT1 pins=0x10 cycles=1`; the three cycles log `cycles=1`, `2`, `3`; a wake by MODE shows `pins=0x20`.
- MODE hold ≈ 2.5 s (ACK up) → `MODE_LONG` → `SLEEP cycle=4` within 1 s **after release** (the wait-for-release is visible in the timestamps).
- Dump after the wake contains the entries from before the sleep, `SLEEP`, `RESET 8`, `WAKE`.
- Meter: GPIO4 = GPIO5 = 3.3 ± 0.1 V during deep sleep (if not: fit the BOM's 10 kΩ pull-ups and repeat); GPIO11–14 = 0.0 ± 0.1 V.
- After the wake the device is in `STANDBY` (green slow blink on camera) and no MODE action was executed by the waking press.

**Control questions**
1. What would happen without the wait-for-release step, and why is the wake source level-triggered here?
2. Why does `t_ms` restart at 0 after a deep-sleep wake while the black box keeps its order?
3. Which parts of the chip stay powered in deep sleep in your configuration, and which of them cost the extra µA you accepted?

---

## Phase 8 — Integration and wrap-up

### T17. 10-minute soak: jitter, heap, latency table, alarm never sleeps
**Goal:** every number in §9 and the sleep rule of §8 proven over a long run, in one table.
**Covers:** §9 (all), §8 (no sleep in alarm), §1 (no loss over time). **Prerequisites:** T16. **Creates:** `docs/verification.md` (the table), optional `docs/log.md` section.

Steps:
1. Active mode, 10 min, `build-ops` serial capture: stats line every second, heap every 30 s (`free_heap`, `min_free_heap`), `dt_min/dt_max` of the whole run (running min/max, not per second).
2. During the run: 20 ACK presses in alarms (shake, ack), 10 MODE toggles, 5 double presses, one WARNING via finger.
3. Standby, 5 min: 20 presses, `lat_us` table; one shake to ALARM; confirm `sleep:` lines stop while in ALARM and resume after ack + clear + MODE.
4. Fill `docs/verification.md`: one row per §9/§8/§1 requirement with the measured value, the limit, pass/fail, and the log line it came from.

**Verified result**
- `free_heap` at 600 s **equals** `free_heap` at 60 s (byte for byte) and `min_free_heap` does not change after 60 s.
- 10-minute `dt_min ≥ 950`, `dt_max ≤ 1050` µs; `lost = 0`, `i2c_err = 0`, `no_int = 0`, queue drops = 0, no watchdog trigger.
- Latency table: active max ≤ 20 000 µs over 20 presses; standby max ≤ 50 000 µs over 20 presses.
- Alarm in standby: zero `sleep:` lines between `-> ALARM` and the return to `STANDBY`.
- `docs/verification.md` complete with a pass in every row.

**Control questions**
1. Which log line proves "no `malloc` in the steady state", and what would a slow 16-byte leak per alarm look like in it?
2. Where did the worst `dt_max` happen (look at what else was logged in that second), and what does that say about interrupt latency on core 1?
3. If a row had failed, which ADR would you reopen first and why?

### T18. Final review + documentation
**Goal:** a finished project someone else can build, test and understand.
**Covers:** all. **Prerequisites:** T17.

Steps:
1. `README.md`: what it is, bench photo, wiring summary (from `docs/pins.md`; `docs/wiring.svg` via the wiring-docs skill if wanted), how to build/flash, how to run the host tests, the layer diagram from ADR-006, the verification table.
2. `docs/` complete: `pins.md` (with measured values from T01), `fsm.md`, `verification.md`, `log.md` with every control question answered, ADRs updated with "Status: Accepted" and any deviation noted.
3. Fresh clone into a new folder: `idf.py set-target esp32s3 && idf.py build`, `.\host_test\run_tests.ps1 -Clean`, clang-tidy run on `build_clang/` with findings fixed.
4. Short retrospective in `docs/log.md`: what went wrong, what you would do differently, which ADR you would change.
5. Public repo check: no personal data, no local absolute paths in any tracked file; repo on `eoffers51`; all T01–T18 ticked in the overview table.

**Verified result**
- Fresh clone builds with zero warnings and all host tests pass (test count written in the README).
- `grep -rn "C:\\\\Users\|/Users/" --include=*.md --include=*.c --include=*.h .` finds nothing.
- `warnings.txt` from clang-tidy is empty or every remaining finding is justified in `docs/log.md`.
- Overview table T01–T18 all ☑; `docs/verification.md` rows all pass.

**Control questions**
1. Explain the whole system in two minutes: interrupt, sampler, analysis, FSM, black box, watchdog, sleep.
2. Which component would change if the MPU-6050 were replaced by a SPI accelerometer, and which would not?
3. Which skill from this project transfers directly to the self-balancing robot, and which assumption here would be wrong there?

---

## Optional stretch tasks

| # | Task | Verified result |
|---|---|---|
| S1 | MPU-6050 FIFO data path (batch of 10 samples every 10 ms) behind the same sampler interface | `lost = 0` with the overflow bit monitored; CPU load of core 1 (idle-hook counter) lower than with ADR-001's path by a measured percentage; a forced overflow (delay the read) is counted as 170 lost samples |
| S2 | Current per mode: meter in series with the 5V pin from a bench supply or USB breakout, USB unplugged | three values recorded (active, standby, deep sleep), each averaged over 30 s; deep sleep the lowest; explanation of why the dev board floor is tens of mA |
| S3 | Pressure compensation + altitude from the BMP280 (`dig_P1..P9`) | host test of the 64-bit pressure formula; logged pressure 950–1050 hPa at sea-level-ish altitude; a 1-floor stair climb shows ≈ −0.4 hPa |
| S4 | Dominant vibration frequency per window (zero-crossing count or a 64-point DFT on one axis) | a 50 Hz test tone from a phone speaker on the sensor shows 50 ± 5 Hz in the log; host test with a synthetic sine |
| S5 | Run-time thresholds via `esp_console` (`thr vib 150 120 300 250`), kept in RAM only | the command changes the level transitions immediately (host-tested parser); a reboot restores `app_config.h` defaults |
