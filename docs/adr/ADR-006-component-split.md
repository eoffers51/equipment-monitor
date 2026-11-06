# ADR-006: Component split — one HAL, two sensor drivers on top of it, six hardware-free logic components

**Status:** Accepted
**Date:** 2026-11-03
**Deciders:** project-architect (proposed), learner (confirms in T02)

## Context

The project CLAUDE.md fixes the layout: `components/` with explicit `REQUIRES` / `PRIV_REQUIRES`, one HAL component that is the only place with `driver/`, `esp_*` and FreeRTOS includes, hardware-free logic components with host tests in `host_test/` (Unity + MinGW, pattern from `Lesson_1_3_Blink`), and a thin `main/`. The learner has done this split twice (Lesson 1.3: `led`/`lights` hardware vs `siren` logic; dental: `fsm` logic with host tests). New here: sensor register maps that need I2C but are not "drivers" in the ESP-IDF sense, and DSP-ish maths that must be testable.

## Decision

Three layers below `main/`:

| Layer | Component | Responsibility | Includes allowed | Host test |
|---|---|---|---|---|
| HAL | `board_hal` | `hal_i2c` (bus, probe, `write_reg`, `read_regs` with timeout, bus reset), `hal_gpio` (LED outputs, button inputs with pull-ups, INT edge ISR hook), `hal_ledc` (blue PWM), `hal_tick` (gptimer 1 kHz callback), `hal_time` (µs clock), `hal_wdt` (task-watchdog subscribe/feed + ISR culprit capture), `hal_rtcmem` (RTC noinit buffers), `hal_sleep` (light/deep sleep, wake decoding), `hal_sys` (reset reason, free heap, restart, UART flush) | `driver/*`, `esp_*`, FreeRTOS, `esp_attr.h` | no |
| Sensor drivers | `mpu6050`, `bmp280` | register maps and mode sequences (wake, 1 kHz config, INT config, cycle mode, sleep; ID, calibration, normal/forced mode, raw reads) | `hal_i2c.h`, `esp_err.h`, `esp_log.h` only | no (thin, exercised on target in T05–T07) |
| Logic (pure C11) | `bmp280_calc`, `analysis`, `button_decoder`, `monitor_fsm`, `indicator`, `blackbox` | temperature compensation; window RMS/mean + thresholds with hysteresis and time qualification; button events; device state machine; LED patterns and flash codes; black-box ring + CRC | `<stdint.h>`, `<stdbool.h>`, `<string.h>` and each other where needed | **yes**, all six |
| Application | `main/` | `main.c` (init, wiring), `board_pins.h`, `app_config.h` (thresholds, windows, timings), `sampler_task.c`, `control_task.c`, `events.h` | component headers, FreeRTOS (tasks, queues, notifications), `esp_log.h`, `esp_err.h` | no |

Dependency rule: logic never includes HAL or driver headers; drivers include only `hal_i2c.h`; `main/` is the only place where a logic result is turned into a HAL call ("apply" pattern from dental T13).

## Options Considered

### Option A: this split (chosen)
| Dimension | Assessment |
|---|---|
| Testability | Every decision-making line (thresholds, FSM, debounce, CRC, compensation, patterns) runs on the PC |
| Isolation | The build system enforces it: a logic component has no `REQUIRES`, so `#include "driver/gpio.h"` fails to compile |
| Size | 10 components; each ≤ 200 lines, one header each |
| Beginner load | Medium: more folders, but each has one job and the pattern is already known |

### Option B: a fat `sensors` component that includes the I2C driver and the maths
Fewer folders, but the compensation and RMS would be untestable without the board, and the driver would leak `driver/i2c_master.h` to everyone.

### Option C: `main/` only, files not components
Nothing enforces the dependency rule; the learner's previous projects already moved past this.

## Trade-off Analysis

Separating `bmp280_calc` (≈ 40 lines) from `bmp280` looks pedantic, but it is the only place in the project with integer-overflow-prone fixed-point maths, and a host test against a double-precision reference is the cheapest way to catch it. `indicator` is separate from `monitor_fsm` because LED timing (blink phases, flash codes) is time-driven while the FSM is event-driven; mixing them is the classic way to break "the state alone determines the behaviour". The threshold logic lives inside `analysis` (two headers) because CLAUDE.md names it so and because they are tested with the same synthetic signals.

## Consequences

- Tasks live in `main/` because they are glue (they call both HAL and logic); FreeRTOS is therefore allowed in `main/`. CLAUDE.md should say so explicitly (reported to the coach).
- `host_test/CMakeLists.txt` lists the six logic `.c` files plus Unity; `fakes/` is only needed if a logic file includes something non-standard (it should not).
- Every `esp_err_t` from the HAL is checked in the drivers and returned; `main/` wraps init calls in `ESP_ERROR_CHECK` and counts run-time errors instead of aborting.
- Pin numbers appear only in `main/board_pins.h`; the HAL receives them as arguments. `grep` for `GPIO_NUM_` outside `main/board_pins.h` and `components/board_hal/` must find nothing.
- The enforcement test from the clean-arch skill (add `#include "driver/gpio.h"` to a logic file, expect a build error, remove) is part of T02's Verified result.

## Action Items
1. [ ] T02: create the skeleton (all component folders with CMakeLists and empty headers), prove the dependency rule.
2. [ ] T05: `host_test/` created with `bmp280_calc`; later tasks add `analysis`, `button_decoder`, `monitor_fsm`, `indicator`, `blackbox`.
3. [ ] T18: `README.md` "how the code works" diagram of these layers.

## Amendment 2026-11-06: component name `board_hal`
The HAL component is named `board_hal`, not `hal`. ESP-IDF ships its own `hal` component (a common requirement of every component); a project component with the same name would replace it and break the build (ESP-IDF build-system docs, "Multiple Components with the Same Name"). File and function prefixes stay `hal_*`.
