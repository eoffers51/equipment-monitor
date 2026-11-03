# Learning log

Measured numbers and control-question answers per task. Numbers come from the bench meter (`docs/measurements.jsonl` has every raw reading); answers are the learner's own words, written down by the coach after approval in chat.

## T01 Bench check without the MCU (2026-11-03, questions 2026-11-04)

### Measurements

Pull-ups, modules unpowered:

| Point | Measured | Note |
|---|---|---|
| GY-521 SDA–VCC, SCL–VCC | 3 MΩ, drifting | wrong method: the module's regulator sits in the VCC path |
| GY-521 SDA–SCL | 4.45 kΩ | two pull-ups in series → 2.22 kΩ each |
| BMP280 SDA–VCC, SCL–VCC | 10.0 kΩ, 10.0 kΩ | |
| Parallel on the shared bus | 1 / (1/2.22k + 1/10k) = **1.82 kΩ** | computed |

Rails, board on USB, nothing wired: 3V3 = 3.283 V, 5V = 4.683 V (the 5V pin loss is a parked question for T02).

SDA idle with both modules on 3V3 and GND only: GY-521 3.277 V, BMP280 3.279 V.

LEDs, each from 3V3 through its 100 Ω resistor, I = V_R / R:

| LED | R (Ω mode) | V_f | V_R | V_f + V_R | I |
|---|---|---|---|---|---|
| red | 98.99 Ω | 2.091 V | 1.177 V | 3.268 V | 11.9 mA |
| yellow | 98.96 Ω | 2.013 V | 1.256 V | 3.269 V | 12.7 mA |
| green | 98.98 Ω | 2.655 V | 0.614 V | 3.269 V | 6.2 mA |
| blue | 98.96 Ω | 2.790 V | 0.471 V | 3.261 V | 4.8 mA, lowest |

The green LED is an InGaN type (V_f 2.655 V instead of the classic 2.1 V), so it gets only 6.2 mA with 100 Ω. (3.28 − 2.655) / 0.012 A = 52 Ω → 56 Ω would give ≈ 11 mA. Decision: keep 100 Ω, a heartbeat may be dim.

Button (tactile, 4 pins, numbered 1 top-left, 2 top-right, 3 bottom-left, 4 bottom-right): 1-3 and 2-4 always connected; 1-2 open released, connected pressed. So four pairs close on press, not two as predicted. For wiring, GPIO and GND go to different sides, for example pins 1 and 2.

### Control questions

**1. Why does the GY-521's SDA idle voltage read slightly below 3.3 V although the pull-ups "go to VCC"?**
At idle no I2C device pulls SDA down (open-drain drivers only pull low, never high), so the only current through the pull-up is leakage into the sensor inputs and the meter. Measured 3.277 V against a 3.283 V rail: 5 mV / 2.22 kΩ ≈ 2.3 µA. The 5 mV is leakage, not a load. A visible drop below 3.3 V appears only when something pulls, for example SDA low during a transfer (≈ 0.4 V).

**2. Two pull-ups in parallel: what happens to the rise time on SDA, and when would that become a problem for 400 kHz?**
A larger pull-up charges the bus capacitance more slowly, so the rise time grows; a smaller (parallel) pull-up makes it faster. At 400 kHz a bit lasts 2.5 µs and I2C fast mode allows a rise time of at most 300 ns. With t_r ≈ 2.2 · R · C and C ≈ 100 pF: 1.82 kΩ → 400 ns, 10 kΩ → 2.2 µs, almost a full bit. Neither is strictly under 300 ns, but 1.82 kΩ works with short wires; 10 kΩ alone would not. The GY-521's 2.2 kΩ carries the bus.

**3. The spec wants a few mA per LED: why control the current and not the voltage, and what would 3.3 V directly on the blue LED do?**
An LED's voltage is almost constant (blue 2.79 V at 4.8 mA, red 2.09 V at 11.9 mA) while its current changes a lot with tiny voltage changes. So setting a voltage cannot set the brightness; setting the current can. The series resistor turns the fixed 3.3 V into a chosen current: I = (3.3 − V_f) / R. With 3.3 V directly on the blue LED the 0.5 V excess has nothing to drop across, the current is limited only by wiring and the chip, and the LED burns out.
