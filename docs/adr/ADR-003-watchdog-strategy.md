# ADR-003: Self-monitoring — ESP-IDF task watchdog with feed-on-success

**Status:** Accepted
**Date:** 2026-11-03
**Deciders:** project-architect (proposed), learner (confirms in T14)

## Context

§7: if any part of the program hangs, the device notices within 1 s, logs exactly what hung (including a stalled sensor read) and reboots. Two test modes are required (both buttons 3 s → simulated hang, 6 s → simulated crash). §5 adds a reset-cause flash code on the red LED. The project CLAUDE.md already fixes "task watchdog, not a hand-rolled timer". ESP-IDF v6.0.2 provides `esp_task_wdt` (timeout, panic on trigger, per-task subscription, a weak ISR hook `esp_task_wdt_isr_user_handler()` and `esp_task_wdt_print_triggered_tasks()` to retrieve the names of the tasks that failed).

## Decision

Use **`esp_task_wdt` with a 1 s timeout and `trigger_panic = true`**, idle tasks **not** subscribed. Each long-lived task (sampler, control) subscribes itself and **feeds only after it has done its job successfully**: the sampler after a completed 1 kHz sample (or a completed standby cycle), the control task after each loop. A stalled sensor read is therefore not special-cased: no successful sample for 1 s → the sampler never feeds → TWDT fires with `sampler` as the culprit. In the ISR hook, the firmware captures the triggered task names with `esp_task_wdt_print_triggered_tasks()` into an `RTC_NOINIT_ATTR` struct; the next boot writes them into the first black-box entry together with `esp_reset_reason()`. A simulated hang is `while (1) {}` in the control task; a simulated crash is a deliberate `abort()`/null dereference → panic → `ESP_RST_PANIC`. After any fault reset, the red LED flashes `esp_reset_reason()` times (PANIC = 4, INT_WDT = 5, TASK_WDT = 6, WDT = 7, BROWNOUT = 9) before normal indication starts.

## Options Considered

### Option A: `esp_task_wdt`, feed-on-success (chosen)
| Dimension | Assessment |
|---|---|
| Complexity | Low: Kconfig + `esp_task_wdt_add(NULL)` + `esp_task_wdt_reset()` in each loop |
| "What hung" | Built in: the triggered task name is available in the ISR hook; the reset reason is `ESP_RST_TASK_WDT` |
| Covers stalled sensor read | Yes, through the feed-on-success rule |
| Learning value | Teaches the difference between "task alive" and "task doing useful work", and task vs interrupt watchdog |

**Pros:** official, panic + reboot path already prints a backtrace; culprit by name.
**Cons:** cannot see hangs *inside* an ISR (the interrupt watchdog does that, separately); a task that keeps looping but produces garbage is "alive" — mitigated by feeding only on success.

### Option B: hand-rolled `gptimer` watchdog
A 1 Hz timer ISR checks per-task "alive" flags and calls `esp_restart()`. It would reinvent Option A with less information (no backtrace, `ESP_RST_SW` instead of `ESP_RST_TASK_WDT`, so the reset-cause code would lie) and is explicitly ruled out by CLAUDE.md.

### Option C: hardware RTC watchdog only
The RWDT is a last resort that reboots without any task information and cannot say what hung; it stays enabled underneath as a safety net but is not the mechanism.

## Trade-off Analysis

A 1 s timeout is short for a system that also light-sleeps; it is acceptable because the TWDT is paused during light sleep (the timer group is clock-gated) and because the control task's standby loop is 100 ms. Idle tasks are left out so that a busy-loop hang names only the hung task, not `IDLE0` as well; that trades the free "CPU starvation" check for a clean culprit — the right trade for a device whose tasks are mostly blocked.

## Consequences

- `sdkconfig.defaults` (T14): `CONFIG_ESP_TASK_WDT_TIMEOUT_S=1`, `CONFIG_ESP_TASK_WDT_PANIC=y`, `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=n`, `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n`, `CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT=y`.
- The ISR hook must not log and must not block: it only copies strings into RTC RAM. `esp_task_wdt_print_triggered_tasks()` is documented for exactly this use.
- The hang test must spin with interrupts **enabled**; a hang inside a critical section triggers the interrupt watchdog instead (`ESP_RST_INT_WDT`, 5 flashes) — a useful observation for a control question.
- The blackbox write of the RESET entry happens at boot, not in the fault path, so the only fault-path write is the culprit struct (ADR-002).
- Deep sleep: the TWDT is de-initialised by the sleep path implicitly (chip resets on wake); light sleep: paused.
- `hal_wdt_*` wraps `esp_task_wdt_*` so no task file includes `esp_task_wdt.h`.

## Action Items
1. [ ] T14: Kconfig, subscriptions, feed-on-success in both tasks, ISR hook → RTC culprit struct, boot-time RESET entry with culprit, red flash code in `indicator`.
2. [ ] T14: three tests — chord 3 s (TASK_WDT, culprit `control`), chord 6 s (PANIC), INT wire pulled (TASK_WDT, culprit `sampler`).
3. [ ] T17: 10-minute soak with zero false triggers.
