# ADR-002: Black box storage — RTC noinit RAM with magic and CRC

**Status:** Accepted
**Date:** 2026-11-03
**Deciders:** project-architect (proposed), learner (confirms in T13)

## Context

§6: keep the last 64 timestamped events; the log must survive reboots, firmware crashes and sleep (including deep sleep, §8); after a **full power loss** it must start from scratch with no garbage; a corrupted log must be detected and not displayed; the first entry after every start is the reset reason. §7 adds that after a watchdog reboot the device must log *what* hung. The record must be writable from a watchdog ISR context (no allocation, no flash access) and the whole thing must be host-testable as plain C.

## Decision

Keep the black box in **RTC slow memory marked `RTC_NOINIT_ATTR`**, as one `struct` with a magic word, a sequence number, the ring buffer of 64 fixed-size records, the sleep-cycle counter, and a CRC-16 over everything before it. The hardware-free `blackbox` component works on a caller-supplied buffer; the HAL only supplies the pointer to the RTC variable. At boot, `blackbox_open()` validates magic + CRC: valid → keep and append the reset-reason entry; invalid → wipe and start with the reset-reason entry. A small second `RTC_NOINIT_ATTR` struct holds the watchdog culprit name written from the TWDT ISR (ADR-003).

## Options Considered

### Option A: `RTC_NOINIT_ATTR` (RTC slow RAM) + CRC (chosen)
| Dimension | Assessment |
|---|---|
| Survives reboot / panic / TWDT reset | Yes: the RTC domain is not reset by a digital reset |
| Survives deep sleep | Yes: RTC slow memory stays powered in deep sleep |
| Survives power loss | No, by design: contents are random → CRC fails → fresh log (exactly §6) |
| Write cost | A RAM store; safe from any context including the watchdog ISR |
| Size | 8 KB available; 64 × 8-byte records + header ≈ 540 bytes |
| Testability | The logic sees only a byte buffer; corruption tests flip bytes on the host |

**Pros:** matches every clause of §6 with no extra mechanism; zero wear; instant.
**Cons:** no history across power cycles (not required); the learner must understand that "noinit" means *nobody* zeroes it, so validation is mandatory.

### Option B: `__NOINIT_ATTR` (main DRAM)
Survives software resets and panics but **not deep sleep** (DRAM is powered down), so §8 + §6 together rule it out. Otherwise identical in handling.

### Option C: NVS / flash partition
| Dimension | Assessment |
|---|---|
| Survives power loss | Yes, which §6 explicitly does **not** want ("starts from scratch after a full power loss") |
| Write cost | ms-range flash writes, forbidden in ISR/panic context; cache disabled during writes disturbs the 1 kHz sampling |
| Wear | 64 events of churn per alarm episode would wear a sector over months of operation |

**Pros:** familiar API. **Cons:** wrong persistence class for this spec, and impossible to write from the watchdog ISR.

## Trade-off Analysis

The spec draws a line between "reset of any kind, including sleep" (keep) and "power loss" (forget). That line is exactly the boundary of the RTC power domain, so the RTC RAM gives the required behaviour for free; the only engineering is making corruption *detectable* (magic + CRC + bounded indices) rather than preventing it. NVS would need extra logic to forget on power loss and still could not be written from the fault path.

## Consequences

- `blackbox` is a pure-C component: `blackbox_open(buf, len)`, `blackbox_add(type, arg, aux, t_ms)`, `blackbox_count()`, `blackbox_get(i)`, `blackbox_dump(print_fn)` and its own CRC-16 (no `esp_rom_crc`, so it compiles on the host).
- Record: `uint32_t t_ms` (ms since boot), `uint8_t type`, `uint8_t arg`, `uint16_t aux` = 8 bytes. Ordering across reboots comes from the RESET records plus a boot counter in the header.
- Every `blackbox_add()` recomputes the CRC, so the struct is consistent at any instant; a crash between the record store and the CRC store makes the next boot discard the log — acceptable and visible (`blackbox: CRC mismatch, starting fresh`).
- The sleep-cycle counter (§8) lives in the same header, so it is protected by the same CRC and survives deep sleep.
- A deliberate one-byte corruption (temporary test line in `app_main`) is the on-target proof; the power-loss proof is "USB unplugged for 10 s → only the RESET entry remains".
- Reads from the dump path happen in the control task; the only ISR writer is the watchdog culprit struct, which is separate and tiny.

## Action Items
1. [ ] T13: `hal_rtcmem_blackbox()` returns the `RTC_NOINIT_ATTR` buffer; `blackbox` component + host tests (ring wrap, CRC detect, fresh start, 64-entry limit).
2. [ ] T13: dump on MODE double press; first entry after boot = reset reason (+ culprit from ADR-003).
3. [ ] T16: sleep-cycle counter in the header; SLEEP/WAKE entries.
