# ADR-001: MPU-6050 data path — data-ready interrupt with per-sample reads

**Status:** Accepted
**Date:** 2026-11-03
**Deciders:** project-architect (proposed), learner (confirms in T07)

## Context

§1 requires 1000 accelerometer samples per second "using the data-ready interrupt or the FIFO", no sample lost without being counted, and §9 requires the interval between samples to deviate from nominal by ≤ 50 µs. The two sensors share one 400 kHz I2C bus with the BMP280 (≥ 50 Hz). The ESP-IDF v6.0.2 `i2c_master` driver is synchronous and cannot be called from an ISR. The learner already built a 1 kHz `gptimer` ISR → queue → task pipeline in the dental project, but has never used I2C or a sensor interrupt.

One MPU accelerometer read (register address + repeated start + 6 bytes) takes ≈ 81 bit times ≈ 200–300 µs at 400 kHz, i.e. 20–30 % of the bus at 1 kHz. A 6-byte read every 6th sample for the BMP280 adds ≈ 5 %.

## Decision

Use the **MPU-6050 data-ready interrupt** on `MPU_INT` (GPIO10, rising edge, ISR in IRAM) and read each sample individually from the data registers in a dedicated **sampler task** (core 1, high priority) woken by a task notification. The sensor's own clock defines the 1 kHz timing; the ESP32 only reacts. The ISR records `esp_timer_get_time()` and detects lost samples with a pending flag; the sampler counts I2C errors and resets the bus after repeated failures. The FIFO becomes stretch task S1.

## Options Considered

### Option A: Data-ready INT, one register read per sample (chosen)
| Dimension | Assessment |
|---|---|
| Complexity | Low: ISR of 5 lines, one blocking read per sample |
| Timing | Period = sensor clock (±1 % RC, or gyro-PLL); ISR timestamps show jitter directly, so §9 is measurable |
| Lost-sample detection | Trivial: INT arrives while the previous read is still pending → `lost++`; I2C timeout → `i2c_err++` |
| Learning value | Teaches interrupt → task hand-off, latency, bus budget, and why 1 kHz is "real time" on a 240 MHz chip |

**Pros:** every sample has a timestamp; simplest possible lost-sample rule; the same ISR gives the jitter measurement for free.
**Cons:** 1000 context switches per second (≈ 2–3 % CPU on core 1); a stalled read loses the next sample (counted, as the spec demands).

### Option B: FIFO, batch read every 10 ms
| Dimension | Assessment |
|---|---|
| Complexity | Medium: FIFO enable, count registers, overflow bit, burst reads of 60+ bytes, frame alignment after an overflow |
| Timing | Bus-efficient (one address per batch) but individual sample times are unknown; §9 jitter could only be inferred, not measured |
| Lost-sample detection | Only via the overflow flag (whole FIFO lost, 170 samples), not per sample |
| Learning value | Good second step once Option A works: shows batching and CPU-load trade-offs |

**Pros:** lowest CPU and bus overhead; tolerant to task latency.
**Cons:** hides exactly the real-time behaviour the project is meant to teach; alignment bugs after overflow are a classic beginner trap.

### Option C: `gptimer` 1 kHz polling of the data registers
Rejected outright: forbidden by §1 ("never by polling") and the ESP32 timer and the MPU clock drift against each other, so samples are periodically duplicated or skipped without the firmware noticing.

## Trade-off Analysis

The spec's hard numbers (≤ 50 µs deviation, count every lost sample) are observable with Option A and only estimable with Option B. The cost of A (context switches, bus occupancy ≈ 30 %) is well inside the budget of this chip and bus. The gyro stays powered in active mode so the MPU can use its PLL clock (CLKSEL = 1), which tightens the 1 kHz period from ±1 % to ≈ ±0.1 %; in standby the gyro is off anyway (ADR-004).

## Consequences

- The GPIO ISR service is installed from the sampler task so that the ISR runs on **core 1**, away from logging and the control task on core 0; interrupt latency is then a few µs and the ≤ 50 µs budget holds (measured in T07 and over 10 min in T17).
- A read that takes longer than 1 ms shows up as `lost` — this is wanted, it is how a bus problem becomes visible.
- The BMP280 is read by the same task every 6th sample (≈ 167 Hz) so both sensors are read sequentially and no bus mutex is needed.
- In standby (ADR-004) the data-ready interrupt is disabled and the control task reads the sensors itself at 10 Hz.
- FIFO (S1) can be added later behind the same `sampler` interface to compare CPU load.

## Action Items
1. [ ] T06: MPU in PLL clock mode, DLPF on, `SMPLRT_DIV = 0`, ±2 g, verify 1 g at rest.
2. [ ] T07: INT configured push-pull active-high, latched, cleared by any read; ISR + sampler task; stats line every second (`samples lost i2c_err dt_min dt_max read_max`).
3. [ ] T17: 10-minute soak confirms `lost = 0`, `|dt − 1000| ≤ 50 µs`.
