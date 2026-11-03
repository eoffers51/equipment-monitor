# ADR-004: Power saving — timer-paced light sleep in standby, EXT1 deep sleep after 60 s

**Status:** Accepted
**Date:** 2026-11-03
**Deciders:** project-architect (proposed), learner (confirms in T15/T16)

## Context

§8: in standby the sampling rate drops to 10 Hz, the CPU sleeps between samples, the sensors go to their low-power modes (MPU-6050 cycle mode, BMP280 forced mode); after 60 s with nothing happening the device deep-sleeps with the sensors asleep, wakes on any button press and keeps the count of sleep cycles. During an alarm it never sleeps. §9: button → LED ≤ 50 ms in standby. Constraints discovered in the datasheets: the MPU-6050 cycle-mode wake rates are 1.25 / 5 / 20 / 40 Hz (no 10 Hz); the BMP280 forced conversion takes ≈ 6 ms; EXT1 deep-sleep wake needs RTC GPIOs (0–21) and the internal pull-ups survive only if the `RTC_PERIPH` domain stays on. ESP-IDF v6.0.2 deprecates `esp_sleep_enable_ext1_wakeup()` and `esp_sleep_get_wakeup_cause()` in favour of `esp_sleep_enable_ext1_wakeup_io()` and `esp_sleep_get_wakeup_causes()`.

## Decision

**Standby = a 100 ms cycle owned by the control task:** wake → trigger a BMP280 forced conversion → read the latest MPU-6050 accelerometer sample (MPU in cycle mode at **20 Hz**, accelerometer only, gyro and temperature off, data-ready interrupt disabled) → read the BMP280 → analysis / thresholds / FSM / LEDs → `esp_light_sleep_start()` with a timer wake for the rest of the 100 ms and GPIO wake (low level) on both buttons. The sampler task is parked (its notify wait times out every 100 ms and only feeds the watchdog while standby is active).

**Deep sleep:** the FSM's `SLEEP` state logs the event, puts the MPU to sleep (`SLEEP` bit) and the BMP280 to sleep mode, turns the LEDs off, **waits until both buttons are released**, increments the sleep-cycle counter in the black-box header (RTC RAM, ADR-002), arms EXT1 `ANY_LOW` on GPIO4 | GPIO5 with `esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON)` + `rtc_gpio_pullup_en()`, flushes the UART, and calls `esp_deep_sleep_start()`. After the wake-reset the boot code logs `ESP_RST_DEEPSLEEP`, the wake cause and the EXT1 pin mask, adds the WAKE entry, and the FSM starts in **Standby** (the waking press is consumed, not interpreted). Power-on or any other reset starts in **Normal (active)**.

## Options Considered

### Option A: timer-paced light sleep, 100 ms cycle, MPU cycle mode 20 Hz (chosen)
| Dimension | Assessment |
|---|---|
| Meets "10 Hz" | Exactly: the ESP32 samples every 100 ms; the MPU's 20 Hz internal rate only guarantees a sample ≤ 50 ms old |
| Button latency | GPIO wake ends the sleep immediately; wake ≈ 1 ms + 8 ms debounce + FSM ≈ 10–15 ms < 50 ms |
| Sensor modes | Both in their datasheet low-power modes as the spec names them |
| Complexity | Low: one loop, two wake sources |

**Pros:** deterministic rate; the sleeping and the sampling are one loop, easy to reason about and to log (`wake=TIMER` / `wake=GPIO`).
**Cons:** the MPU wakes twice as often as needed (µA-level cost); light-sleep wake latency adds to the button budget (measured in T15).

### Option B: MPU data-ready interrupt wakes the ESP32 (cycle mode 20 Hz or 5 Hz)
The rate would be 20 or 5 Hz, not 10; the GPIO ISR would have to be kept armed as a wake source; the BMP280 (no interrupt) still needs a timer. More wake-ups, less clarity.

### Option C: no light sleep, just `vTaskDelay(100)` in standby
Violates "the CPU sleeps between samples" (§8) and teaches nothing about sleep.

### Deep-sleep alternatives
- **EXT0** wakes on one pin only; two buttons need EXT1 (`ANY_LOW` is supported on the S3).
- **External 10 kΩ pull-ups** instead of keeping `RTC_PERIPH` on: equally valid; kept as a fallback in the BOM, chosen only if the meter shows the pin not at 3.3 V during deep sleep (T16).

## Trade-off Analysis

The spec's "10 times per second" and "cycle mode" cannot both be satisfied by the MPU alone, so the ESP32's timer defines the 10 Hz and the MPU's 20 Hz cycle mode only keeps the data fresh. Deep sleep with the waking button still held would wake the chip instantly (level-triggered `ANY_LOW`); waiting for release before sleeping is simpler and more honest than edge-detection tricks. Keeping `RTC_PERIPH` on costs a few µA, negligible next to the dev board's USB bridge and LDO.

## Consequences

- Alarm (any level ALARM) forces the active data path (1 kHz, no sleep); the standby cycle is left and the sampler re-armed. After acknowledge and return to Normal the device stays active until MODE is pressed.
- The 60 s idle timer restarts on every event (button, state change) and runs only in Standby.
- Logging while light-sleeping: the control task calls `uart_wait_tx_done()` through the HAL before each sleep so log lines are not cut.
- The data-ready GPIO interrupt must be disabled in standby (otherwise the MPU's 20 Hz would notify the parked sampler); re-enabled with `INT_ENABLE` when going active.
- Watchdog: paused during light sleep, gone in deep sleep (the wake is a reset); the sampler's standby timeout branch feeds it so the parked task is not reported as hung.
- Timestamps restart at 0 after a deep-sleep wake; the WAKE entry and the boot counter keep the black box readable (ADR-002).
- The sleep-cycle counter lives in the black-box header, so the CRC covers it and a power loss resets it to 0 together with the log (§8 only asks that deep sleep preserves it).

## Action Items
1. [ ] T15: `mpu6050_set_cycle_mode(20 Hz)`, `bmp280_forced_read()`, `hal_sleep_light_until(deadline, wake_gpios)`, standby loop in the control task, latency log.
2. [ ] T16: `SLEEP` state, wait-for-release, EXT1 arming, boot-time wake decoding, sleep-cycle counter.
3. [ ] T16: meter check of the button pins during deep sleep (3.3 V expected); fallback to external pull-ups if not.
