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
