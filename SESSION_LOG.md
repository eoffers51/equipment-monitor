# Session log

Claude's handoff notes between sessions. This is separate from `docs/log.md`, which is the user's own learning log (measured numbers, answered control questions).
Keep **Current state** short and up to date. Add new entries at the top of **History**, 3–6 lines each.

## Current state
- **T04 done** (2026-11-10, commit 7883623 pushed). Next: T05 BMP280 → actually a BME280 (id 0x60): temperature path as planned, driver must accept 0x60 (see notes below).
- `hal_i2c` (components/board_hal/hal_i2c.c, uncommitted): `hal_i2c_init(sda, scl, scl_hz)`, `hal_i2c_add_device(addr7, &dev)`, `hal_i2c_probe(addr7)` (fixed 100 kHz inside IDF v6.0.2 probes), `hal_i2c_read_regs(dev, reg, buf, len)` and `hal_i2c_write_reg` (20 ms xfer timeout), `hal_i2c_bus_reset()`, `hal_i2c_error_count()`. `HAL_I2C_DEV_INVALID` (0xFF) is rejected by every device function. Error rule: NACK at a registered address, or any result other than OK/NOT_FOUND, counts; NACK at an empty address does not.
- T05/T06 notes: the BMP280 module is a BME280 (chip id 0x60, BMP280 = 0x58). The T05 driver must accept 0x60, check the BME280 standby table and the ctrl_hum write ordering in its datasheet, and update `docs/bom.md`. The `APP_I2C_REG_*` constants in `main/app_config.h` must be MOVED (not copied) into the drivers at T05/T06. The 5 s rescan and ID read in `main.c` is bring-up only and goes before T07.
- T07 notes in `hal_i2c.c`: error-counter race (counter touched from task and ISR context), bus-lock timeout must be counted as a fault, and bus reset must not run without the lock.
- Pins (from `docs/pins.md`): BTN_ACK GPIO4, BTN_MODE GPIO5, DBG GPIO6 (optional), SDA GPIO8, SCL GPIO9, MPU_INT GPIO10, LED green/yellow/red/blue GPIO11/12/13/14 (blue on LEDC). Both sensor modules on 3V3. All four LED resistors are 100 Ω. T03 facts (LEDC 5 kHz, level-before-mode, camera setup) stay in History.
- Parked recall for later: why the 5V pin reads 4.63–4.68 V and 3V3 does not show the loss. Misconception to revisit: an ohmmeter across a pull-up on a powered-path (LDO) board reads MΩ and drifts; measure SDA–SCL instead.
- Measurement flow (user's request 2026-11-03): the coach reads the meter point by point and asks Save / Repeat / Cancel before logging; `meter-tech` is not used for multi-point runs.
- Plan exists: `TASKS.md` (T01–T18 + S1–S5), `docs/pins.md` (pin budget, everything on header J1), `docs/bom.md`, `docs/adr/ADR-001…006`.
- Pins (from `docs/pins.md`): BTN_ACK GPIO4, BTN_MODE GPIO5, DBG GPIO6 (optional), SDA GPIO8, SCL GPIO9, MPU_INT GPIO10, LED green/yellow/red/blue GPIO11/12/13/14 (blue on LEDC). Both sensor modules on 3V3 (BMP280 is the 4-pin module). All four LED resistors are 100 Ω.
- Decisions to teach from: ADR-001 data-ready INT + per-sample read (FIFO is S1); ADR-002 black box in `RTC_NOINIT_ATTR` with CRC-16; ADR-003 `esp_task_wdt` 1 s, feed-on-success, culprit captured in the ISR hook, red LED flashes `esp_reset_reason()` times; ADR-004 100 ms light-sleep cycle in standby, MPU cycle 20 Hz, EXT1 deep sleep on GPIO4|5 with wait-for-release; ADR-005 1 kHz sampled two-button decoder, chord classified on release; ADR-006 board_hal / mpu6050 + bmp280 drivers / six host-tested logic components.
- `host_test/` does not exist yet; it is created in T05 from the `Lesson_1_3_Blink` pattern.
- Learner background to build on: Lesson 1.3 (components, Unity host tests, debounced button), Lesson 2.3 (superloop, `static const` config), dental curing light (gptimer 1 kHz ISR, shift-register debounce, ISR → queue → FSM task, reset-reason logging, power stage). New here: I2C, sensor registers, interrupts from a sensor, RMS/hysteresis, RTC RAM, task watchdog, light/deep sleep, LEDC.

## History

### 2026-11-10 — T04 checkpoint 3 and wrap-up (uncommitted)
- Checkpoint 3 bench: who_am_i 0x68 (MPU-6050), id 0x60 (BME280), errors 0 after the ID reads. Meter idle SDA 3.277 V, SCL 3.276 V (both in the 3.1–3.3 V window).
- Task-check on T04: all steps and verified items evidenced; CQ1–3 saved in docs/log.md. Optional 400 kHz/100 kHz scope check not done (not required). The TASKS.md row tick and the commit are still to do.
- Misconceptions: CQ2 "a parallel pull-up slows the edge" (a parallel pull-up lowers R, so it is faster); "no pull-up → lots of charging current" (the line floats, same as R003). CQ3: counted 7 bytes instead of 9 for the 6-byte read, and mixed ms with µs.
- Explainer: https://claude.ai/artifact/YS9DuhmtG34CQ1BaqbbJhp
- Still open: ask the user whether to add a correction note to the T01 rise-time answer in docs/log.md (it uses 2.2·R·C; the spec's 0.847·R·C applies).

### 2026-11-09/10 — T04 checkpoints 1–2 (uncommitted)
- Recall: R003 corrected ("no pull-up → rises too fast" → it floats); R007 key fixed: the I2C spec measures t_r 30→70 % (0.847·R·C ≈ 154 ns, inside 300 ns). The coach first graded with 2.2·R·C, which was wrong; the learner's first answer "inside" was right. T01 answer in docs/log.md still uses 2.2·R·C: ask before adding a correction note.
- Prediction 112 probes / 2 ACK correct. Bench: found 0x68 + 0x76 (BMP280 SDO low), scan ≈ 19 ms (≈ 0.17 ms/probe), heap flat.
- v6.0.2 fact: `i2c_master_probe` always runs at 100 kHz (i2c_master.c:1385); 400 kHz only on device-handle transfers. TASKS.md optional scope check (400 kHz during scan) needs rewording: ask the user.
- User's error-counter rule: NACK at a registered address or any non-OK/NOT_FOUND result counts; NACK at an empty address does not. Step 4 bench: errors 0→1→2→3 during 3 unplugged scans, stays 3 after replug, no reboot; prediction fully correct.
- Probe timeout 50 ms (> 20 ms hardware SCL timeout); neither driver path resets the controller after a probe timeout in v6.0.2. Open for checkpoint 3: read/write regs + bus reset, an invalid device index (index 0 = MPU), meter idle levels.

### 2026-11-06 — T03 Four LEDs (checkpoints 1–5)
- Prediction: 1.65 V at duty 128 correct; blue sweep changes most at low duty, correct (camera ~60 % of change in the first 0.5 s).
- Meter on GPIO14 read 4 % low with the blue LED connected (3.148 V at 255); diagnosed as ~29 Ω pin output resistance at 3.6 mA (unloaded 3.254 V). T03 target for 255 amended to "LED disconnected" (user approved).
- Green 1.000 s period, 506 ms on-time over 15 cycles; three resets dark (by eye, camera retake skipped).
- Explainer: https://claude.ai/artifact/1CR5Pk7Qye5hUHnMxGTr9J

### 2026-11-06 — T02 checkpoints 1–4
- Prediction: learner thought `pdMS_TO_TICKS(5)` at 100 Hz waits 5 ms and mixed tick rate with CPU clock; worked through integer division to 0 → `vTaskDelay(0)` only yields. Misconception to revisit: tick rate vs CPU clock (CPU is 160 MHz here, not 240).
- Prediction: learner expected `EXT (2)` for the RST button/auto-reset; board showed POWERON (EN resets the whole chip; `ESP_RST_EXT` not applicable, esp_system.h:27).
- Recall R006 answered correctly (SDA–SCL, 2×2.2 k, LDO blocks the path); moved to box 2, next 2026-11-09. R007 not asked yet. R011–R014 added from T02 (box 1, next 2026-11-07).
- CQ1–3 answered and saved in docs/log.md (2026-11-06). TASKS.md T02 row not ticked by the examiner (no write permission there); the commit on eoffers51 is not yet made.
- Learner asked "create it for me" for each checkpoint; firmware-engineer + code-reviewer used for board_hal. Reviewer finding: with MINIMAL_BUILD an unrequired component is not compiled (proved with run 1 vs run 2). `main` no longer exports INCLUDE_DIRS.
- Explainer: https://claude.ai/artifact/Hd4JvmVxAUBga2NqidRSpn
- CQ1–3 answered and saved in docs/log.md. Task-check run: steps 1–4 evidenced; step 5 (commit on eoffers51) open. R011–R014 added, R006 advanced to box 2.

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
