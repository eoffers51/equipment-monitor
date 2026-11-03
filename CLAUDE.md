# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Equipment Health Monitor: ESP-IDF **v6.0.2**, plain C, on an **ESP32-S3-DevKitC-1** clone (YD-ESP32-S3, serial on COM5). Vibration (GY-521 / MPU-6050) and temperature (BMP280) on one I2C bus, two buttons, four LEDs, a crash-surviving black box, watchdog self-monitoring and deep sleep. It's a Beetroot Academy learning project; the parent `D:\BeetrootAcademy\CLAUDE.md` rules (eoffers51 account, no personal data, which subagent does what) apply here too.

- Spec: @TASK.md. Section numbers (§1 Measurement … §9 Timing) refer to it. Check every change against it.
- Task list (T01..T18 + stretch, Verified result + control questions per task): @TASKS.md. Its hard-constraints list at the top can be quoted verbatim. Tasks go in order.
- Pin budget: `docs/pins.md` (and, once T02 creates it, `main/board_pins.h`; the two must agree). Decisions: `docs/adr/`. Parts: `docs/bom.md`. Don't pick GPIOs that aren't in the pin budget; change the budget first.
- `SESSION_LOG.md` is Claude's handoff file: keep **Current state** and a dated History entry current whenever a task step finishes, a decision is made, or the user wraps up. `docs/log.md` is the learner's log, but Claude writes it (user's decision 2026-11-04): after each task add a dated entry with the user's measured numbers (from `meter-tech` or the user, never invented) and the control-question answers in the user's own words, only after the user approved the draft in chat. Never pre-write a control-question answer there or anywhere else.

## How to help: guide, don't solve

- Run each task with the `guided-learning` skill: brief with a visual explainer, hint ladder, debugging coaching, verify, control questions one at a time, wrap-up.
- Default to explaining, reviewing and hinting. Write or change firmware only when the user explicitly asks ("do it for me"), and then explain the diff as a lesson.
- Never write answers to control questions anywhere in the repo. Tasks go in order; don't jump ahead of the one the user is on.

## Build / flash

`idf.py` is not on PATH, and shell state doesn't persist between tool calls. Prefix every `idf.py` command with the env activation in the same PowerShell call:

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1 *> $null; idf.py build
```

- Activating also puts Espressif's `cmake`/`ninja` ahead of the STM32CubeCLT ones on PATH; never build without it.
- `sdkconfig` is gitignored. If it's missing, run `idf.py set-target esp32s3` first. Project defaults belong in `sdkconfig.defaults`, which T02 creates (`CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_FREERTOS_HZ=1000`, PSRAM off) and T14 extends (`CONFIG_ESP_TASK_WDT_TIMEOUT_S=1`, `CONFIG_ESP_TASK_WDT_PANIC=y`, idle-task checks off). Until then the generated `sdkconfig` still says 2 MB flash and HZ=100.
- Flash with `idf.py -p COM5 flash` or `/build-flash`. **Never run `idf.py monitor`** (interactive, hangs the tool). Ask the user to run it, or have `build-ops` capture a timed serial log.
- The installed IDF is v6.0.2, but the `esp-idf-source` skill reads the v6.1 source. Before using an API it shows, confirm it exists in `C:\esp\v6.0.2\esp-idf`.
- `.devcontainer/` pulls `espressif/idf:latest` (unpinned) and is for QEMU only, not the normal build.

## Layout: clean architecture

Follow the `esp-idf-clean-arch` skill: `components/` with explicit `REQUIRES`/`PRIV_REQUIRES`, `main/` holding only a thin `app_main` that wires components together.

- **One HAL component** (`components/hal`) wraps every peripheral: it is the only place for `driver/*`, `i2c_master`, `gptimer`, `ledc`, `rtc_io`, `esp_sleep`, `esp_timer`, `esp_task_wdt`, `esp_system` and `esp_attr`. Sensor drivers (`mpu6050`, `bmp280`) talk to the bus only through `hal_i2c.h` and may use `esp_err.h` / `esp_log.h`. `main/` (the sampler and control tasks) may use FreeRTOS, `esp_log` and `esp_err` as glue, nothing lower. See ADR-006.
- **Logic components are hardware-free**: `bmp280_calc`, `analysis` (RMS, thresholds, hysteresis, qualification), `button_decoder`, `monitor_fsm`, `indicator` and `blackbox` contain no `esp_*`, FreeRTOS or `driver/` includes and no `ESP_LOG*`. They take plain values and return results/actions, so they compile with any C11 compiler. The black-box CRC is hand-written for the same reason (no `esp_rom_crc`).
- v6.0.2 API notes: use `esp_sleep_enable_ext1_wakeup_io()` and `esp_sleep_get_wakeup_causes()`; the older singular forms are deprecated. Task-watchdog culprit capture goes through `esp_task_wdt_isr_user_handler()`.
- Host tests for the logic go in `host_test/` as a standalone CMake project with Unity from `$IDF_PATH/components/unity`, built with the MinGW gcc in `C:\CTOOLS\mingw32\bin` (run via `/host-test`). Copy the pattern from `..\Lesson_1_3_Blink\host_test\` (`run_tests.ps1`, `fakes/`). Code must compile cleanly with `-Wall -Wextra -Werror -std=c11`.

## Spec invariants (easy to get wrong; see @TASK.md for the full text)

- §1: accelerometer at 1 kHz driven by the MPU-6050 data-ready interrupt or its FIFO, never by polling. Count every lost sample (FIFO overflow, I2C error). BMP280 at ≥ 50 Hz. Both sensors share one I2C bus at 400 kHz.
- §2: thresholds have hysteresis; spikes ≤ 20 ms are ignored, > 200 ms triggers.
- §4: each button press triggers exactly once. MODE: short = active/standby, hold > 2 s = sleep, double press = black-box dump. Both buttons 3 s = simulated hang, 6 s = simulated crash (§7).
- §6: the black box (last 64 timestamped events) survives reboot, crash and sleep but starts clean after power loss, so it lives in RTC/noinit RAM with a checksum, not in NVS/flash. Its first entry after every start is the reset reason.
- §7: a hang is detected within 1 s, the culprit is logged, and the device reboots (task watchdog, not a hand-rolled timer). After a fault reboot the red LED flashes the reset-cause code (§5).
- §8: never sleep during an alarm. Standby samples at 10 Hz with light sleep between samples (MPU cycle mode, BMP280 forced mode); deep sleep after 60 s idle in standby, any button wakes, the sleep-cycle count is kept.
- §9: button→LED ≤ 20 ms active / ≤ 50 ms standby, sample jitter ≤ 50 µs, free heap must not drop over 10 min (no `malloc` in the steady-state path).

## Code style / lint

- Formatting follows `.clang-format` (ESP-IDF style: 4 spaces, 120 columns, function braces on their own line). A hook applies it after every edit of a `.c`/`.h` file.
- Lint with clang-tidy (`.clang-tidy`, esp-clang). It only works on a **clang-toolchain build in a separate dir**, not on the normal GCC `build/`, and `--run-clang-tidy-options` must be given or `pyclang` substitutes its own check list for `.clang-tidy`:

  ```powershell
  . C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1 *> $null; idf.py -B build_clang -D IDF_TOOLCHAIN=clang build; idf.py -B build_clang clang-check --run-clang-tidy-options "-quiet"
  ```

  Findings land in `warnings.txt` (gitignored). Run it before closing a task that added or changed C code, and fix what it reports rather than adding `// NOLINT`. `build_clang/` is lint-only; flash from `build/`.
- A GCC "internal compiler error: Segmentation fault" in an IDF component during a first build is transient: rerun `idf.py build`.
