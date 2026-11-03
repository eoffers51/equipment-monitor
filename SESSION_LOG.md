# Session log

Claude's handoff notes between sessions. This is separate from `docs/log.md`, which is the user's own learning log (measured numbers, answered control questions).
Keep **Current state** short and up to date. Add new entries at the top of **History**, 3–6 lines each.

## Current state
- **T01 done, `docs/log.md` written. Next: T02.** Rule change 2026-11-04: Claude writes `docs/log.md` (user's numbers and approved CQ answers only), see CLAUDE.md. Evidence gaps the examiner found on 2026-11-04 were closed the same day: user-reported BMP280 10.0 kΩ lines, the GY-521 SDA–SCL 4.45 kΩ line and the computed 1.82 kΩ were added to `docs/measurements.jsonl` (lines 6–9); its first two lines are marked LABEL UNCERTAIN (most likely the BMP280, not the GY-521). The button check is by-ear continuity only (no ohm value), accepted. Measured so far (all in `docs/measurements.jsonl`): GY-521 pull-ups 2.22 kΩ, BMP280 10.0 kΩ, parallel 1.82 kΩ; 3V3 = 3.283 V, 5V = 4.683 V; SDA idle 3.277 / 3.279 V; four resistors 98.96–98.99 Ω; LEDs red V_f 2.091 / V_R 1.177 V (11.9 mA), yellow 2.013 / 1.256 (12.7 mA), green 2.655 / 0.614 (6.2 mA, InGaN-type, user keeps 100 Ω), blue 2.790 / 0.471 (4.8 mA). `docs/pins.md`, `docs/bom.md`, T01 line in `TASKS.md` updated with these.
- Parked recall for later: why the 5V pin reads 4.63–4.68 V and 3V3 does not show the loss. Misconception to revisit: an ohmmeter across a pull-up on a powered-path (LDO) board reads MΩ and drifts; measure SDA–SCL instead.
- Measurement flow (user's request 2026-11-03): the coach reads the meter point by point and asks Save / Repeat / Cancel before logging; `meter-tech` is not used for multi-point runs.
- Plan exists: `TASKS.md` (T01–T18 + S1–S5), `docs/pins.md` (pin budget, everything on header J1), `docs/bom.md`, `docs/adr/ADR-001…006`. No firmware yet; `main/main.c` is an empty `app_main`. `sdkconfig` was generated for esp32s3 but still has the 2 MB flash default and `FREERTOS_HZ=100`; T02 fixes both through `sdkconfig.defaults`.
- Pins (from `docs/pins.md`): BTN_ACK GPIO4, BTN_MODE GPIO5, DBG GPIO6 (optional), SDA GPIO8, SCL GPIO9, MPU_INT GPIO10, LED green/yellow/red/blue GPIO11/12/13/14 (blue on LEDC). Both sensor modules on 3V3 (BMP280 is the 4-pin module). All four LED resistors are 100 Ω.
- Decisions to teach from: ADR-001 data-ready INT + per-sample read (FIFO is S1); ADR-002 black box in `RTC_NOINIT_ATTR` with CRC-16; ADR-003 `esp_task_wdt` 1 s, feed-on-success, culprit captured in the ISR hook, red LED flashes `esp_reset_reason()` times; ADR-004 100 ms light-sleep cycle in standby, MPU cycle 20 Hz, EXT1 deep sleep on GPIO4|5 with wait-for-release; ADR-005 1 kHz sampled two-button decoder, chord classified on release; ADR-006 hal / mpu6050 + bmp280 drivers / six host-tested logic components.
- `host_test/` does not exist yet; it is created in T05 from the `Lesson_1_3_Blink` pattern.
- Learner background to build on: Lesson 1.3 (components, Unity host tests, debounced button), Lesson 2.3 (superloop, `static const` config), dental curing light (gptimer 1 kHz ISR, shift-register debounce, ISR → queue → FSM task, reset-reason logging, power stage). New here: I2C, sensor registers, interrupts from a sensor, RMS/hysteresis, RTC RAM, task watchdog, light/deep sleep, LEDC.

## History

### 2026-11-04 — T01 control questions and wrap-up
- Task-check run on T01. All bench values present except the pull-up and button-ohm evidence gaps listed in Current state. Learner answered CQ1–CQ3 in chat (not in `docs/log.md`, which is still to be written by the learner).
- Recall bank: R006–R010 added from T01 (LDO/ohmmeter trick, rise time vs 300 ns, InGaN current, button pairs, parked 5V question). `TASKS.md` T01 tick is pending the coach.

### 2026-11-03 — T01 checkpoints 1–4
- Pull-ups, rails, idle SDA, resistors and all eight LED voltages measured (values in Current state and `docs/measurements.jsonl`). Green LED turned out InGaN-type (V_f 2.655 V): 6.2 mA with 100 Ω; user computed 56 Ω for 12 mA and chose to keep 100 Ω. T01 verified range for green widened to 2.5–7 mA in `TASKS.md`.
- Meter workflow reworked after the user lost track of which point was captured: console hook now says "METER read … Not stored yet", and every reading ends in a Save / Repeat / Cancel dialog; three mis-placed readings were discarded.
- Explainer: https://claude.ai/artifact/LhL8X6itq5jEdf9TG5C3Ca

### 2026-11-03 — project planned
- `project-architect` read `TASK.md`, the project and academy `CLAUDE.md`, the board skill, the dental task list (format), the Lesson 1.3 host-test pattern, and verified the v6.0.2 APIs (`i2c_master`, `gptimer`, `ledc`, `esp_task_wdt` + ISR hook, `esp_sleep_enable_ext1_wakeup_io`, `esp_sleep_get_wakeup_causes`, `RTC_NOINIT_ATTR`, `esp_reset_reason`).
- Wrote `TASKS.md` (18 tasks, 5 stretch), `docs/pins.md`, `docs/bom.md`, six ADRs, this file.
- User answered the same day: BMP280 is the **4-pin** module (run from 3V3); the meter **has a temperature mode**, but the meter MCP server does not decode it (its `quantity` field knows only voltage/resistance/capacitance/diode/continuity), so before T05 either extend the server or let the user read the display; **100 Ω on every LED**. `docs/pins.md`, `docs/bom.md` and T01/T05 in `TASKS.md` were updated to match (red/yellow/green ≈ 12–14 mA, blue ≈ 4 mA).
- Next: **T01 bench check without the MCU** (meter only), then T02 skeleton.
