# Learning log

Measured numbers and control-question answers per task. Numbers come from the bench meter (`docs/measurements.jsonl` has every raw reading); answers are the learner's own words, written down by the coach after approval in chat.

## T01 Bench check without the MCU (2026-11-03, questions 2026-11-04)

### Measurements

Pull-ups, modules unpowered:

| Point | Measured | Note |
|---|---|---|
| GY-521 SDA–VCC, SCL–VCC | 3 MΩ, drifting | wrong method: the module's regulator sits in the VCC path |
| GY-521 SDA–SCL | 4.45 kΩ | two pull-ups in series → 2.22 kΩ each |
| BMP280 SDA–VCC, SCL–VCC | 10.0 kΩ, 10.0 kΩ | |
| Parallel on the shared bus | 1 / (1/2.22k + 1/10k) = **1.82 kΩ** | computed |

Rails, board on USB, nothing wired: 3V3 = 3.283 V, 5V = 4.683 V (the 5V pin loss is a parked question for T02).

SDA idle with both modules on 3V3 and GND only: GY-521 3.277 V, BMP280 3.279 V.

LEDs, each from 3V3 through its 100 Ω resistor, I = V_R / R:

| LED | R (Ω mode) | V_f | V_R | V_f + V_R | I |
|---|---|---|---|---|---|
| red | 98.99 Ω | 2.091 V | 1.177 V | 3.268 V | 11.9 mA |
| yellow | 98.96 Ω | 2.013 V | 1.256 V | 3.269 V | 12.7 mA |
| green | 98.98 Ω | 2.655 V | 0.614 V | 3.269 V | 6.2 mA |
| blue | 98.96 Ω | 2.790 V | 0.471 V | 3.261 V | 4.8 mA, lowest |

The green LED is an InGaN type (V_f 2.655 V instead of the classic 2.1 V), so it gets only 6.2 mA with 100 Ω. (3.28 − 2.655) / 0.012 A = 52 Ω → 56 Ω would give ≈ 11 mA. Decision: keep 100 Ω, a heartbeat may be dim.

Button (tactile, 4 pins, numbered 1 top-left, 2 top-right, 3 bottom-left, 4 bottom-right): 1-3 and 2-4 always connected; 1-2 open released, connected pressed. So four pairs close on press, not two as predicted. For wiring, GPIO and GND go to different sides, for example pins 1 and 2.

### Control questions

**1. Why does the GY-521's SDA idle voltage read slightly below 3.3 V although the pull-ups "go to VCC"?**
At idle no I2C device pulls SDA down (open-drain drivers only pull low, never high), so the only current through the pull-up is leakage into the sensor inputs and the meter. Measured 3.277 V against a 3.283 V rail: 5 mV / 2.22 kΩ ≈ 2.3 µA. The 5 mV is leakage, not a load. A visible drop below 3.3 V appears only when something pulls, for example SDA low during a transfer (≈ 0.4 V).

**2. Two pull-ups in parallel: what happens to the rise time on SDA, and when would that become a problem for 400 kHz?**
A larger pull-up charges the bus capacitance more slowly, so the rise time grows; a smaller (parallel) pull-up makes it faster. At 400 kHz a bit lasts 2.5 µs and I2C fast mode allows a rise time of at most 300 ns. With t_r ≈ 2.2 · R · C and C ≈ 100 pF: 1.82 kΩ → 400 ns, 10 kΩ → 2.2 µs, almost a full bit. Neither is strictly under 300 ns, but 1.82 kΩ works with short wires; 10 kΩ alone would not. The GY-521's 2.2 kΩ carries the bus.
*Correction 2026-11-10 (T04): the I2C spec defines t_r from 30 % to 70 %, so t_r = 0.847·R·C = 0.847 × 1.82 kΩ × 100 pF ≈ 154 ns, inside 300 ns (10 kΩ alone: ≈ 847 ns, still outside). 2.2·R·C (≈ 400 ns) is the 10→90 % rise time.*

**3. The spec wants a few mA per LED: why control the current and not the voltage, and what would 3.3 V directly on the blue LED do?**
An LED's voltage is almost constant (blue 2.79 V at 4.8 mA, red 2.09 V at 11.9 mA) while its current changes a lot with tiny voltage changes. So setting a voltage cannot set the brightness; setting the current can. The series resistor turns the fixed 3.3 V into a chosen current: I = (3.3 − V_f) / R. With 3.3 V directly on the blue LED the 0.5 V excess has nothing to drop across, the current is limited only by wiring and the chip, and the LED burns out.

## T02 Project skeleton, HAL shell, boot log (2026-11-06)

### Results

Build: `Project build complete`, 0 `warning:` lines, 51 components with `MINIMAL_BUILD` (138 before). Boot log: `SPI Flash Size : 16MB`, no 2 MB warning.

| Test | ROM line | IDF `reset_reason` |
|---|---|---|
| flash (auto-reset through EN) | `rst:0x1 (POWERON)` | `1 (POWERON)` |
| RST button | | `1 (POWERON)` |
| `esp_restart()` test line (removed after) | `rst:0xc (RTC_SW_CPU_RST)` | `3 (SW)` |

Prediction was `2 (EXT)` for the RST button; on the ESP32-S3 the EN pin resets the whole chip, so it reads as power-on (`ESP_RST_EXT` is "not applicable", `esp_system.h:27`).

Free heap: 401 260 bytes at boot and unchanged at 10 … 60 s (min_free_heap 401 244).

Dependency proof (`#include "driver/gpio.h"` in `analysis.c`):

| Run | Setup | Result |
|---|---|---|
| 1 | `main` does not require `analysis` | builds "clean": `analysis.c` not compiled |
| 2 | `main` has `PRIV_REQUIRES board_hal analysis` | `analysis.c:2:10: fatal error: driver/gpio.h: No such file or directory` |
| 3 | include removed | clean, 0 warnings |

### Control questions

**1. Why does `MINIMAL_BUILD` plus `PRIV_REQUIRES` make the dependency rule something the compiler enforces, not just a convention?**
Every dependency has to be declared on purpose; if we include something we didn't declare, the build fails at compile time. Mechanism: each component's include path (`-I` list) is only the core IDF set plus the components named in its own `REQUIRES` / `PRIV_REQUIRES`. A header from any other component is "not found", even if that component is built anyway. Run 2: `esp_driver_gpio` was in the build, but `analysis` had no `PRIV_REQUIRES esp_driver_gpio`, so `analysis.c:2:10: fatal error: driver/gpio.h: No such file or directory`. `MINIMAL_BUILD` adds the second half: without it, all components are built (138) and `main`, which lists no requirements, sees all of them; with it only `main` and what it asks for are built (51–52). A component nobody requires isn't compiled at all (run 1 built "clean" with the bad include), so `main` must list `analysis`. Limit: the core set (`esp_system`, `freertos`, `log`…) is visible everywhere, so a direct `esp_restart()` in `main` would compile; the grep check covers that.

**2. What is the difference between `CONFIG_FREERTOS_HZ=1000` and the 1 kHz sampling rate of the sensor — what does each clock drive?**
The FreeRTOS tick (`CONFIG_FREERTOS_HZ=1000`, an ESP32 timer interrupt) wakes tasks: it decides when a `vTaskDelay()` ends and when tasks take turns. The 1 kHz samples are driven by the MPU-6050's own clock (rate set in its registers); each new sample raises INT → GPIO10 interrupt → the interrupt wakes the sampler directly, without waiting for a tick. So with HZ=100 the sample rate would not change. HZ matters for delays: at 100 Hz one tick is 10 ms, and `pdMS_TO_TICKS(5)` = 5 · 100 / 1000 = 0 (integer division), so `vTaskDelay(0)` doesn't wait at all; at 1000 Hz it is 5 ticks (4–5 ms, the first tick is partial).

**3. Where does `esp_reset_reason()` get its information from, and why can a value differ between the chip's own reset register and what IDF reports?**
The ROM reads the chip's hardware reset register and prints it before any IDF code runs (`rst:0xc (RTC_SW_CPU_RST)`). `esp_reset_reason()` takes that hardware cause and adds a hint that IDF saved before the reset in an RTC register that survives a CPU reset (`RTC_RESET_CAUSE_REG`). A software restart, a panic and the task watchdog all end in the same hardware "software CPU reset" (0xc), so the register alone can't tell them apart; the hint can (`PANIC`, `TASK_WDT`, `INT_WDT`, `BROWNOUT`), and without a hint it is `SW` (`reset_reason.c:22-30`). The numbers also differ because IDF uses its own chip-independent enum: hardware 0xc = 12 → IDF 3 (SW); hardware 0x1 → IDF 1 (POWERON), which is why the EN pin / RST button show POWERON, not EXT.

## T03 Four LEDs: GPIO heartbeat + LEDC blue (2026-11-06)

### Results

Wiring checked on camera (LEDs in GPIO rows J1-17..20, 100 Ω on the GND side: GPIO → LED → R → GND, same current in either order) and by continuity: all four resistor rail ends beep to J1-22 GND.

Camera, 30 fps, 15 cycles: green period 1.000 s (mean 1.0017, min 0.999, max 1.032), on-time 506 ms mean (500–533, one frame = 33 ms). Red/yellow alternate, period 1.999–2.032 s. Blue sweep restarts every 1.999 s; about 60 % of the visible change in the first 0.5 s (prediction: most change at low duty; camera tone curve exaggerates it).

Meter on GPIO14 vs J1-22 GND (LEDC 5 kHz, 8 bit), ideal = 3.283 V × duty / 256:

| Duty | Ideal | With blue LED + 100 Ω | LED disconnected |
|---|---|---|---|
| 64 | 0.821 V | 0.7856 V | 0.8109 V |
| 128 | 1.642 V | 1.5781 V | |
| 255 | 3.270 V | 3.148 V (below 3.20–3.40) | 3.254 V |

The loaded readings are all about 4 % low. Removing the load fixed it, so the pin itself has resistance: (3.254 − 3.148) V / ((3.148 − 2.79) V / 100 Ω) = 0.106 V / 3.6 mA ≈ 29 Ω. T03 target for 255 is now "with the LED disconnected".

Resets: three RST presses, no LED lit (by eye; camera retake skipped).

### Control questions

**1. Why must the output level be written before the pin is switched to output mode, and what did the camera check in step 5 prove?**
A GPIO has an output latch (the value) and an output enable. If output is enabled while the latch still holds 1, the pin drives HIGH until the next call writes 0: a short LED flash. Writing the level first (latch = 0) and only then enabling output means the pin goes straight to LOW whatever the latch held before. Order A (output first) with latch = 1: LED on → off (flash); order B (level first): off → off. After a chip reset (POWERON — the RST button / EN pin, T02) the latch is already 0, so step 5 (three RST presses, no LED lit, checked by eye) proved the power-on path is dark, but not the case the order protects: a software, panic or watchdog reset, where the CPU restarts but the GPIO registers may keep an LED's old 1. That case comes back in T14 (red LED flash code after a fault reset).

**2. What does the meter actually measure on a 5 kHz PWM pin, and why would the reading be wrong if you measured across the LED instead?**
On a 5 kHz PWM pin the meter shows the average voltage: the pin is only ever 0 V or HIGH, but one period is 200 µs, far shorter than the meter's measuring time, so it averages to V_high × duty / 256. Measured: duty 64 → 0.7856 V, 128 → 1.5781 V, 255 → 3.148 V (with the LED; V_high ≈ 3.15 V under load, 3.254 V at 255 unloaded). Across the LED the HIGH level is not the pin voltage but the LED's V_f (≈ 2.79 V for the blue one), so the average becomes V_f × duty / 256: at duty 128 that is 2.79 × 128/256 ≈ 1.40 V instead of 1.58 V on the pin. That reading is about the LED, not the PWM output, and since V_f changes with current it would not even scale cleanly with duty.

**3. What limits the LEDC duty resolution at a given frequency on this chip?**
The LEDC counts ticks of its source clock (APB, 80 MHz), and one PWM period must hold 2^bits ticks. So the resolution is limited by the clock: max bits = floor(log2(f_clk / f_pwm)). At 5 kHz: 80 000 000 / 5000 = 16 000 ticks per period, 2^13 = 8192 fits and 2^14 = 16 384 doesn't → 13 bits max (we use 8). The second limit is the hardware counter width, 14 bits on the ESP32-S3 (`SOC_LEDC_TIMER_BIT_WIDTH`): at 100 Hz the clock would allow log2(800 000) ≈ 19.6 bits, but you still get only 14. So: higher frequency → fewer bits (clock limit); low frequency → capped at 14 bits (counter limit).

## T04 I2C bus at 400 kHz + address scan (2026-11-09/10)

### Results

Scan 0x08–0x77 (112 probes, prediction 112 / 2 ACK: correct): `found 0x68`, `found 0x76`, `2 device(s)` on every scan. One scan ≈ 19 ms: 0x68 is probe 96 and was logged 16 ms after the start → ≈ 0.17 ms per probe (probes run at a fixed 100 kHz in IDF v6.0.2).

Unplug test (BMP280 SDA pulled for 3 scans, prediction "1 device, +1 per scan, stays after replug": correct): `1 device(s)` and errors 0 → 1 → 2 → 3, back to `2 device(s)` with errors staying at 3, no reboot. Heap flat at 393344 (136 bytes less than before, used once at boot by the two device registrations).

ID reads at 400 kHz: MPU-6050 `who_am_i=0x68`; the "BMP280" module answered `id=0x60`, so it is a **BME280** (BMP280 = 0x58).

Meter, DC V vs GND, firmware running: SDA idle 3.277 V, SCL idle 3.276 V (target 3.1–3.3 V; prediction "about 3.28").

### Control questions

**1. What does a device do electrically to ACK its address, and why can a probe of an empty address never damage anything?**
On the 9th clock the master lets go of SDA, and the addressed device pulls SDA LOW. LOW = ACK. Nobody ever drives SDA HIGH: only the pull-up resistors pull the line high, and each device can only pull it LOW or let go. So two outputs can never fight. At an empty address nobody pulls, SDA stays HIGH = NACK, and nothing happens. Even when a device pulls LOW, the current is limited by the pull-up:

| Quantity | Formula | Calculation | Result |
|---|---|---|---|
| Current when pulled LOW | V_rail / R_pull-up | 3.283 V / 1.82 kΩ (2.22 k ∥ 10.0 k, T01) | 1.8 mA (< 3 mA fast-mode sink limit) |

**2. Why do we not enable the ESP32's internal pull-ups on SDA/SCL here, and when would we have to?**
The modules already carry pull-ups (2.22 kΩ ∥ 10.0 kΩ = 1.82 kΩ, T01). The internal pull-up (≈ 45 kΩ) would sit in parallel with them, and a parallel resistor lowers the total R (faster rise, not slower). But it barely changes anything, so it is not needed:

| Quantity | Formula | Calculation | Result |
|---|---|---|---|
| Modules + internal | 1 / (1/1.82 k + 1/45 k) | | 1.75 kΩ (−4 %) |

We would have to enable it only if the bus had no pull-ups at all, because then SDA/SCL float and never go HIGH. Even then it is too weak for 400 kHz:

| Quantity | Formula | Calculation | Result |
|---|---|---|---|
| Rise time, internal only | 0.847 · R · C (30→70 %, I2C spec) | 0.847 × 45 kΩ × 100 pF | ≈ 3.8 µs ≫ 300 ns (fast mode), > 1000 ns (standard mode) |

So without module pull-ups the right fix is external resistors (≈ 2.2–4.7 kΩ). The internal one is only a stop-gap for a short, slow bus.

**3. What is the worst-case time one `hal_i2c_read_regs` of 6 bytes can block, and where does that number come from?**
A healthy read: 9 bytes on the wire (addr+W, reg, addr+R, 6 data) × 9 clocks = 81 clocks × 2.5 µs (400 kHz) ≈ 0.2 ms. The worst case is not the 20 ms timeout, because the driver uses `xfer_timeout_ms` for each wait, not once per call: bus lock (`i2c_master.c:1008`), command wait (`:535`), done event (`:582`), about 3 × 20 = 60 ms. After an earlier timeout the driver also clears the bus first, which can busy-wait up to 50 ms (`:679-681`, `:86-95`), so up to ≈ 110 ms.

| Quantity | Formula | Calculation | Result |
|---|---|---|---|
| Healthy read | bytes × 9 × t_clk | 9 × 9 × 2.5 µs | ≈ 0.2 ms |
| Worst case | waits × timeout + bus clear | 3 × 20 ms + 50 ms | ≈ 110 ms |
| Samples missed at 1 kHz | t_block × 1 kHz | 60–110 ms × 1/ms | 60–110 (must be counted, §1) |

It stays well inside the 1 s hang budget of §7, but at 1 kHz it means lost samples.

## T05 BME280: ID, calibration, max-rate temperature (2026-11-10)

### Results

Chip: `id=0x60`, so the module sold as BMP280 is a **BME280** (same temperature path). Calibration of this chip: dig_T1=27984, dig_T2=26460, dig_T3=50 (datasheet example: 27504 / 26435 / −1000). Read back: `ctrl_meas=0x27 config=0x00` (osrs_t ×1, osrs_p ×1, normal mode; t_sb 0.5 ms, filter off).

Datasheet ODR (BME280, humidity skipped):

| Quantity | Formula | Calculation | Result |
|---|---|---|---|
| t_measure, typical | 1 + 2·T_os + (2·P_os + 0.5) | 1 + 2 + 2.5 | 5.5 ms |
| ODR, typical | 1000 / (t_measure + t_sb) | 1000 / (5.5 + 0.5) | 166.7 Hz (≥ 150 Hz ✅) |
| t_measure, max | 1.25 + 2.3·T_os + (2.3·P_os + 0.575) | 1.25 + 2.3 + 2.875 | 6.425 ms |
| ODR, worst case | 1000 / (t_max + t_sb) | 1000 / 6.925 | 144.4 Hz (still ≈ 3× the 50 Hz of §1) |

Measured: `reads/s` 166 / 167 (task every 6 ms: 1000 / 6 = 166.7), no_data 0, err 0, 23.2 °C at rest. Finger on the chip: 23.28 → 26.72 °C in 13 s (+3.44 °C), falling back afterwards. Heap flat at 389896 (task stack + control block 3448 B, once); stack_free_min 1464 of 3072 B.

Meter in temperature mode (read off the display): 21.5 °C with the probe in the air next to the module (sensor 23.23 °C, +1.73 °C); 23.0 °C with the probe on the BME280 chip (sensor ≈ 23.2 °C, +0.2 °C ✅). The chip sits about 1.5 °C above the room air: heat from the ESP32 board and/or self-heating from measuring 166 times per second.

Host tests (`host_test/`, MinGW gcc, `-std=c11 -Wall -Wextra -Werror`): 7 tests, 0 failures.

### Control questions

**1. In the integer compensation, which intermediate product would overflow a 32-bit signed integer if the raw value and `dig_T2` were at their limits, and how does the reference code avoid it?**
The first product in `var1`: ((adc_T >> 3) − (dig_T1 << 1)) × dig_T2. At the limits (adc_T = 1 048 575, dig_T1 = 0, dig_T2 = 32 767) it is above INT32_MAX:

| Quantity | Formula | Calculation | Result |
|---|---|---|---|
| Bracket at the limits | (adc_T >> 3) − (dig_T1 << 1) | 131 071 − 0 | 131 071 |
| Product at the limits | bracket × dig_T2 | 131 071 × 32 767 | 4 294 836 225 ≈ 2 × INT32_MAX → overflow |
| Bracket, my chip at raw 520 000 | 65 000 − 2 × 27 984 | | 9 032 |
| Product, my chip | 9 032 × 26 460 | | 238 986 720 (fits) |

The reference avoids it by making the number small **before** multiplying: the shift right by 3 drops bits below the sensor's resolution, and subtracting dig_T1 (a factory offset close to the chip's real raw values) leaves only a small difference. Then `>> 11` scales the product down. var2 does the same: it squares the small difference (adc_T >> 4) − dig_T1 and shifts `>> 12` before multiplying by dig_T3. This works because dig_T1 matches the real chip; at arbitrary limit values it would still overflow.

**2. Why did the datasheet ODR come out higher than "reads per second" could ever need, and what does reading faster than the ODR give you?**
We chose the fastest settings on purpose: minimum oversampling (osrs_t ×1, osrs_p ×1) and the minimum standby time (t_sb 0.5 ms), so one cycle is only 5.5 + 0.5 = 6 ms → 166.7 Hz. But temperature changes slowly, because the chip and its board have thermal mass. My finger needed 13 s for +3.44 °C:

| Quantity | Formula | Calculation | Result |
|---|---|---|---|
| ODR (typ) | 1000 / (t_measure + t_sb) | 1000 / (5.5 + 0.5) | 166.7 Hz (§1 needs ≥ 50 Hz) |
| Samples during the finger rise | t × ODR | 13 s × 166 | ≈ 2160 |
| Change per sample | ΔT / samples | 3.44 °C / 2160 | ≈ 0.0016 °C |

So 166 Hz is far more than the signal needs. Reading faster than the ODR gives the old value again, because the next measurement is not finished yet: no new information, only extra bus traffic and power.

**3. Why does the BMP280 have no data-ready pin, and what does that mean for how the sampler task will read it later?**
The BMP280 (and my BME280) was designed for phones and wearables, where pin count and power matter most, and for weather/altitude signals that change slowly (CQ2: about 0.0016 °C per sample). So Bosch lets the host decide the timing instead of signalling with an interrupt. Reading at any moment is safe, because a burst read gets one complete measurement.
For the sampler this means the task **polls on a timer**: it does not know when a new measurement is ready. The two clocks (task 6 ms, sensor ≈ 6 ms) are independent, so **some reads repeat** the old value (or a new one is skipped). That is acceptable for temperature. The MPU-6050 is different: it has an **INT pin** (GPIO10) that says exactly when each new sample is ready, so the 1 kHz vibration path **uses the interrupt** (T07, §1) and never repeats or misses a sample without counting it.
