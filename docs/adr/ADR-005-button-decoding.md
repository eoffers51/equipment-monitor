# ADR-005: Button decoding — 1 kHz sampled decoder, press-edge events for latency, chord classified on release

**Status:** Accepted
**Date:** 2026-11-03
**Deciders:** project-architect (proposed), learner (confirms in T10)

## Context

§4: ACK short press acknowledges; MODE short press toggles active/standby, hold > 2 s goes to sleep, double press dumps the black box; both buttons held 3 s / 6 s simulate a hang / a crash (§7); **each press triggers exactly once**. §9: press → LED reaction ≤ 20 ms active, ≤ 50 ms standby. Tactile switches bounce for 1–10 ms. A double press can only be recognised after waiting for a possible second press, so a "short press" decision is inherently delayed by the double-press window. The learner has already built a shift-register debounce sampled from a 1 kHz `gptimer` ISR and an ISR → queue → FSM pipeline (dental T09–T13).

## Decision

Sample both buttons at **1 kHz from the `gptimer` tick ISR** and feed a **hardware-free `button_decoder`** (plain C, host-tested) with the two raw levels every tick. The decoder debounces with 8 identical samples (8 ms) and emits:

| Event | When | Used for |
|---|---|---|
| `ACK_PRESS` | debounced press edge | acknowledge (the action itself; ACK has no long/double meaning) |
| `MODE_PRESS` | debounced press edge | immediate LED feedback + latency measurement only |
| `MODE_SHORT` | release before 2000 ms **and** no second press within 300 ms after release | active ↔ standby toggle |
| `MODE_DOUBLE` | second press edge ≤ 300 ms after the first release | black-box dump |
| `MODE_LONG` | 2000 ms of continuous hold while ACK is not held, emitted once | go to sleep |
| `CHORD_HANG` | both were held ≥ 3000 ms and < 6000 ms, emitted when either is released | simulated hang |
| `CHORD_CRASH` | both were held ≥ 6000 ms, emitted on release | simulated crash |

While both buttons are held the decoder exposes `chord_held_ms` (a single aligned 32-bit value) so the control task can light yellow at ≥ 3 s and red at ≥ 6 s as "you may release now" feedback. Each raw press edge is timestamped in the ISR (`t_edge`); the control task logs `now − t_edge` when it sets the LED, which is the §9 latency figure. Events go through one FreeRTOS queue (`xQueueSendFromISR`) to the control task; overflow is counted, never silently dropped.

## Options Considered

### Option A: periodic 1 kHz sampling + decoder (chosen)
| Dimension | Assessment |
|---|---|
| Exactly once | Deterministic: an event is an edge of the *debounced* level, bounces never reach the decoder |
| Latency | 1 ms sample + 8 ms debounce + queue + FSM ≈ 10 ms active; + light-sleep wake ≈ 12–15 ms standby |
| Hold / double / chord | Natural: the decoder has a time base (ticks) and both buttons in one place |
| Testability | Fully on the host: feed tick sequences, assert the event list |
| Learning value | Builds on the dental debounce and adds classification and a two-button chord |

**Pros:** no GPIO interrupt storms, one time base for all button timing, host tests for every rule.
**Cons:** needs the tick running; in light sleep the tick stops, so the GPIO wake source carries the first edge (ADR-004).

### Option B: GPIO edge interrupts + lock-out debounce
Fastest first reaction (< 1 ms), but a single noise glitch becomes a press, holds and doubles still need a timer, and ISR-side state for two buttons gets messy. "Exactly once" relies on the lock-out time being longer than the worst bounce — a guess, not a measurement.

### Option C: a FreeRTOS task polling every 10 ms
Simple but the 10 ms granularity eats half the 20 ms budget before debouncing even starts; the dental project already showed the ISR path.

## Trade-off Analysis

The 20 ms budget is met for every *press edge* (ACK action, MODE feedback). The MODE *short* action is deliberately delayed by the 300 ms double-press window; this is inherent to any button that also has a double-press meaning and is documented as such — the LED reaction that §9 measures is the press-edge feedback (the green LED restarts its ON phase), and ACK's action itself. Chord actions are classified on release so that the 6 s test can actually be reached (a hang at the 3 s mark would reboot the device before 6 s). MODE_LONG is suppressed when ACK is also held so the chord cannot be mistaken for "go to sleep".

## Consequences

- `button_decoder_feed(dec, ack_level, mode_level, now_ms)` returns at most one event per tick; the ISR pushes it to the queue. The decoder never allocates.
- The ISR stays short (two GPIO reads + decoder + optional queue send, target < 5 µs, measured like dental T06/T18).
- Going to deep sleep while MODE is still held would wake the chip immediately (`ANY_LOW`): the sleep path waits for release (ADR-004).
- The raw edge timestamp is taken before debouncing, so the logged latency includes the full 8 ms debounce — the honest number.
- Double-press window (300 ms) and debounce (8 ms) are `#define`s in `app_config.h` and test parameters on the host.

## Action Items
1. [ ] T10: `hal_tick` (gptimer 1 kHz) + `hal_gpio_button_read()`; decoder + ≥ 10 host tests; serial log of every event with `lat_us`.
2. [ ] T12: FSM consumes the events; latency table (20 presses each mode).
3. [ ] T15: latency re-measured in standby (≤ 50 ms).
