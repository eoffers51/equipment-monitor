# Session log

Claude's handoff notes between sessions. This is separate from `docs/log.md`, which is the user's own learning log (measured numbers, answered control questions).
Keep **Current state** short and up to date. Add new entries at the top of **History**, 3–6 lines each.

## Current state
- **T05 done** (2026-11-10, commit a24b04e pushed). Next: T06 MPU-6050 (wake, 1 kHz config, gravity check).
- The "BMP280" module is a **BME280** (id 0x60, BMP280 = 0x58). Calibration of this chip: dig_T1=27984, dig_T2=26460, dig_T3=50 (host tests use it).
- `bmp280` driver (components/bmp280): `bmp280_init(dev, &id)` (accepts 0x58/0x60), `bmp280_read_calib(dev, &calib)`, `bmp280_configure(dev, osrs_t, osrs_p, t_sb, filter, mode, &ctrl_meas, &config)` (reads back), `bmp280_read_raw_temp(dev, &adc_t)`. Errors: `BMP280_ERR_READBACK` (= ESP_ERR_NOT_FINISHED, stored values differ), `BMP280_ERR_NO_DATA` (= ESP_ERR_INVALID_SIZE, result registers still 0x80000).
- `bmp280_calc_temp_centi(calib, adc_t)` → int32 °C × 100, hardware-free (components/bmp280_calc).
- `host_test/` exists: 7 Unity tests in `test_bmp280_calc.c`, run by `host_test\run_tests.ps1`, built with `-std=c11 -Wall -Wextra -Werror`. Each later logic component adds its own test file and `test_main.c` entry.
- ADR-006 amendment: drivers may include `hal_i2c.h` and `hal_time.h` (`hal_time_delay_ms`) besides `esp_err.h` / `esp_log.h`.
- Temporary `temp_rate_task` in `main/main.c` (6 ms, `xTaskDelayUntil`, one stats line per second). T07's sampler replaces it (BMP280 read every 6th sample there).
- T06 must MOVE `APP_I2C_REG_MPU6050_WHO_AM_I` from `main/app_config.h` into the mpu6050 driver (and the bring-up ID read in main.c goes with it), not copy it.
- Logic-analyzer skill built (embedded-agents 0.3.0, claude-skills 59d8dcc): `la.py check / capture / i2c / timing / selftest`, D0=SCL D1=SDA, captures in `%LOCALAPPDATA%\logic-analyzer\equipment-monitor\`. sigrok-cli 0.7.2 installed (matches PulseView 0.4.2), analyzer found, bench-proven (claude-skills 84afd95). First capture 2026-11-10 (2 s, 8 MHz, T05 firmware): 333 × `S W76 FA Sr R76 xx xx 00N P`, 166.6 Hz, period 6.002 ms mean (5.951–6.055 ms, std 5.5 µs), 143.8 µs per read, SCL 400.0 kHz median (381–421 = ±1 sample at 8 MHz), 54/54 clocks, no anomalies; first read adc_T 0x7F000 -> 22.85 °C. Boot capture (claude-skills f7485ff, `--reset-port COM5`): reboot gap 426.7 ms, scan 0x08..0x77 = 112 probes in 17.2 ms at 100 kHz (IDF probe speed, confirms the hal_i2c.c:205 comment), ACK 0x68 + 0x76; then WHO_AM_I 0x68, id 0x60, soft reset B6, status 00 after 2.3 ms, 24-byte calibration (dig_T1/T2/T3 = 27984/26460/50, same as host test), config 00, ctrl_meas 27, readback 27 00, first temp read 9.1 ms later. Order matches bmp280.c:103/114/124/144/197/201/210. `timing` not bench-proven yet. The 104/129.5 µs period spread is the 6 ms temperature task (`xTaskDelayUntil`), NOT the §9 sample-jitter check: §9 (≤ 50 µs) needs a capture of the 1 kHz data-ready sampling (DBG pin, `la.py timing --period D2:r --nominal-us 1000`) once T07 exists. Open question for the user: raw 0x7F740 with the chip calibration gives 23.44 °C (driver formula, same as host test), not the 23.12 °C noted from the PulseView capture.
- Pins (from `docs/pins.md`): BTN_ACK GPIO4, BTN_MODE GPIO5, DBG GPIO6 (optional), SDA GPIO8, SCL GPIO9, MPU_INT GPIO10, LED green/yellow/red/blue GPIO11/12/13/14 (blue on LEDC). Both sensor modules on 3V3. All four LED resistors are 100 Ω.
- T07 notes in `hal_i2c.c`: error-counter race (task and ISR), bus-lock timeout must count as a fault, bus reset must not run without the lock.
- Measurement flow (user's request 2026-11-03): the coach reads the meter point by point and asks Save / Repeat / Cancel before logging; `meter-tech` is not used for multi-point runs. The MCP meter server does not decode temperature mode: the user reads the display.
- Parked recall: why the 5V pin reads 4.63–4.68 V (R010). Misconception to revisit: an ohmmeter across a pull-up on an LDO-powered board reads MΩ; measure SDA–SCL instead (R006).
- Plan exists: `TASKS.md` (T01–T18 + S1–S5; the coach ticks rows), `docs/pins.md`, `docs/bom.md`, `docs/adr/ADR-001…006`.
- Decisions to teach from: ADR-001 data-ready INT + per-sample read (FIFO is S1); ADR-002 black box in `RTC_NOINIT_ATTR` with CRC-16; ADR-003 `esp_task_wdt` 1 s, feed-on-success, culprit captured in the ISR hook, red LED flashes `esp_reset_reason()` times; ADR-004 100 ms light-sleep cycle in standby, MPU cycle 20 Hz, EXT1 deep sleep on GPIO4|5 with wait-for-release; ADR-005 1 kHz sampled two-button decoder, chord classified on release; ADR-006 board_hal / mpu6050 + bmp280 drivers / six host-tested logic components.
- Learner background to build on: Lesson 1.3 (components, Unity host tests, debounced button), Lesson 2.3 (superloop, `static const` config), dental curing light (gptimer 1 kHz ISR, shift-register debounce, ISR → queue → FSM task, reset-reason logging, power stage). New here: I2C, sensor registers, interrupts from a sensor, RMS/hysteresis, RTC RAM, task watchdog, light/deep sleep, LEDC.

## History

### 2026-11-10 — logic-analyzer skill
- Built and pushed the `logic-analyzer` skill to eoffers51/claude-skills (embedded-agents 0.3.0). Self-test on a synthetic copy of the reference read passes (S W76 FA Sr R76 7F 74 00N P, 54/54 data clocks, 400 kHz, adc_T 0x7F740). Register names come from this project's `#define`s (`la.py regmap`). Bench run pending sigrok-cli install.
- Found: 0x7F740 -> 23.44 °C with dig_T1=27984, dig_T2=26460, dig_T3=50; the 23.12 °C in the earlier note does not match, so its source needs checking before it goes into docs/log.md.

### 2026-11-10 — T05 wrap-up (task-check, CQ1–3 saved)
- Task-check on T05: every Verified-result item evidenced in `docs/log.md` and `docs/measurements.jsonl` (id 0x60, dig values, ctrl_meas 0x27 read back, ODR 166.7 Hz typ / 144.4 Hz worst, reads/s 166/167, finger +3.44 °C in 13 s and back down, 7 host tests 0 failing as logged; the examiner could not re-run them).
- Temperature judgement: the probe on the chip gave 23.0 °C against a sensor value of ≈23.2 °C (+0.2 °C, within ±1.0 °C; the sensor value was taken from the steady log, not at the same moment). In air the probe gave 21.5 °C (+1.73 °C), not like for like: the chip sits ≈1.5 °C above room air (board heat and/or self-heating), so the gap is physical, not a sensor error. Next time capture the sensor value at the same moment as the meter reading.
- CQ1–3 saved in `docs/log.md`. CQ3's first answer looked copied from a reference; the learner then gave own phrases and the draft was built from those.
- Explainers: T05 https://claude.ai/artifact/NjutphUZoRWoefLeuyzLaV ; "I2C Sensors for Newbies" ep. 1 https://claude.ai/artifact/PCTsHN4sGhLCDkMsdCQt1H
- Recall: R006 answered with a nudge 2026-11-10, stays box 2, next 2026-11-13. R023–R027 added from T05 (box 1, next 2026-11-11).
- Next: commit T05 on eoffers51 (not yet done), tick the TASKS.md row (coach), then T06.

### 2026-11-10 — T05 checkpoints 2–4 + bench (uncommitted)
- cp2: `ctrl_meas=0x27 config=0x00` read back. Learner built 0x27 by hand; computed ODR typ 5.5+0.5 ms → 166.7 Hz (worst 144.4 Hz; both go into docs/log.md). Read-back mismatch has its own code BMP280_ERR_READBACK (= ESP_ERR_NOT_FINISHED).
- cp3: bmp280_calc + host_test/ (7 tests pass: datasheet 2508, own calib vs double at 500k/520k/540k/484319, rounding 520008 → 2280, monotonic 400k–600k). Learner: "shifts throw away remainder bits" (correct).
- cp4: 6 ms task with xTaskDelayUntil (pdFALSE → resync, skip not burst); 0x80000 = 24.15 °C on this chip, so NO_DATA only until the first real result, cleared again on any bus error. Periodic T04 rescan removed.
- Bench: reads/s 166/167, no_data 0, err 0; 23.2 °C; finger +3.44 °C in 13 s, falls back; heap 389896 flat (task 3448 B); stack_free_min 1464 of 3072 B. Meter (read by eye): 21.5 °C in air (+1.73, not like for like), 23.0 °C on the chip vs ≈23.2 (learner did not catch the exact value, chose 23.2) → chip ≈1.5 °C above air (board heat/self-heating). Learner named ESP32 heat + self-heating as hypotheses.
- Misconceptions: reads/s computed as 400000/6 (mixed the I2C clock with the task period); prediction "is 0x75 WHO_AM_I's value" (register address vs content, corrected).
- Learner asked for a general I2C-integration page: "I2C Sensors for Newbies" ep. 1 https://claude.ai/artifact/PCTsHN4sGhLCDkMsdCQt1H
- Next: CQ1–3, then docs/log.md T05 entry (incl. both ODR values), wrap-up, commit.
- After T05 (user decision): build a `logic-analyzer` skill for the embedded-agents plugin (the local claude-skills project next to equipment-monitor, push to eoffers51/claude-skills): sigrok-cli capture (fx2lafw "Saleae Logic" clone, D0=SCL, D1=SDA), I2C decode, transaction table checked against the project drivers, .sr file for PulseView. sigrok-cli is NOT installed yet (only PulseView); PulseView must be closed while capturing. Learner capture already decoded one temp read: FA → 7F 74 00 = 0x7F740 → 23.12 °C, ≈145 µs, ≈400 kHz.

### 2026-11-10 — T05 checkpoint 1 (uncommitted)
- Recall R006 needed a nudge (first answer "measure the regulator drop"); stays in box 2.
- Prediction 20-bit max 1 048 575, room raw ≈ middle: correct. Little-endian 0x70/0x6B → 0x6B70 = 27504: correct.
- Bench: `id=0x60`; **this chip's calibration (use in host tests): dig_T1=27984 dig_T2=26460 dig_T3=50**; dig_P1=36540 P2=-10596 P3=3024 P4=5539 P5=51 P6=-7 P7=11700 P8=-11800 P9=5000. Reset wait ≈ 3 ms in the log. Heap 393344 unchanged.
- ADR-006 amended (user decision): drivers may include `hal_time.h` (`hal_time_delay_ms`) besides `hal_i2c.h`; CLAUDE.md updated to match.
- Explainer: https://claude.ai/artifact/NjutphUZoRWoefLeuyzLaV
- ODR (learner computed typ: 5.5 ms + 0.5 ms → 166.7 Hz). Decision: write both into docs/log.md at wrap-up: typ 166.7 Hz meets ≥ 150 Hz; worst case (t_max 6.425 ms) 144.4 Hz, still ≈ 3× the §1 50 Hz.

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
