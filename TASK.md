# Equipment Health Monitor: Task Specification

Your device monitors another piece of equipment. It tracks vibration (GY‑521 accelerometer/gyroscope) and temperature (BMP280), has operator buttons, and shows its state on LEDs. It must run for months on a battery. When something goes wrong, it keeps the most recent events, even if its own firmware hangs or crashes.

## Hardware

- ESP32‑S3‑DevKitC‑1
- **GY‑521 (MPU‑6050)**: 3‑axis accelerometer and gyroscope over I2C. This is the "vibration" sensor. Its INT pin goes to a GPIO for data‑ready signalling.
- **BMP280**: temperature (and pressure) sensor over I2C. This is the "temperature" sensor.
- Both sensors share one I2C bus at 400 kHz.
- 2 buttons: BTN_ACK and BTN_MODE.
- 4 LEDs:
  - green: heartbeat
  - yellow: warning
  - red: alarm
  - blue: load. Its brightness is set through LEDC PWM.

## Functional Requirements

### 1. Measurement

- The device continuously measures two signals: vibration and temperature.
- In active mode:
  - Vibration (accelerometer) is sampled 1000 times per second. Use the MPU‑6050 data‑ready interrupt or its FIFO.
  - Temperature is sampled at the highest rate the BMP280 supports, at least 50 times per second.
- No sample may be lost. If one is lost anyway, the device counts it. This includes MPU‑6050 FIFO overflows and I2C errors.

### 2. Analysis

- For vibration, the device computes the RMS of the dynamic acceleration, meaning the acceleration magnitude with gravity / DC offset removed.
- For temperature, the device computes the mean.
- Each signal has a warning threshold and an alarm threshold.
- A short spike of up to 20 ms does not trigger an alarm. An exceedance lasting longer than 200 ms does.
- When a signal hovers around a threshold, the state does not flicker (use hysteresis).

### 3. Device States

- The states are: Normal, Warning, Alarm, Alarm Acknowledged, Standby, and Sleep.
- An acknowledged alarm returns to Normal once the signals are back to normal. A new exceedance triggers the alarm again.

### 4. Buttons

- ACK button, short press: acknowledge the alarm.
- MODE button:
  - short press: toggle between active and standby mode;
  - hold for more than 2 s: go to sleep;
  - double press: dump the black box contents to the console.
- Each press always triggers exactly once.

### 5. Indication

- Green LED: "device alive", a steady slow blink.
- Yellow LED: warning.
- Red LED: fast blink during an alarm, solid on once the alarm is acknowledged.
- Blue LED: brightness proportional to the vibration level.
- After a reboot caused by a fault, the red LED shows the reset-cause code as a number of flashes.

### 6. Black Box

- The device stores the last 64 events, each with a timestamp. Events include:
  - state changes
  - button presses
  - threshold exceedances
  - faults, including sensor/I2C faults
  - going to sleep and waking up
  - reboots
- The log survives reboots, firmware crashes, and sleep.
- After a full power loss, the log starts from scratch, with no garbage.
- The device detects a corrupted log and does not display it.
- The first entry after every start is the reset reason.

### 7. Self‑Monitoring

- If any part of the program hangs, the device notices within 1 s. It then logs exactly what hung, including a stalled sensor read, and reboots.
- There are two fault modes for testing:
  - holding both buttons for 3 s simulates a processing hang;
  - holding both buttons for 6 s simulates a firmware crash.

### 8. Power Saving

- In active mode, vibration is sampled 1000 times per second.
- In standby mode:
  - Sampling drops to 10 times per second.
  - The CPU sleeps between samples.
  - The sensors are switched to their low‑power modes (MPU‑6050 cycle mode, BMP280 forced mode).
- During an alarm, the device does not sleep and responds without delay.
- If nothing happens for 60 s in standby mode:
  - The device enters deep sleep, and the sensors are put to sleep as well.
  - It wakes up on any button press.
  - The number of sleep cycles is preserved.

### 9. Timing and Resource Requirements

- Latency from a button press to the LED reaction is at most 20 ms in active mode and at most 50 ms in standby.
- The interval between vibration samples deviates from nominal by no more than 50 µs.
- Free memory does not decrease over 10 minutes of operation.
