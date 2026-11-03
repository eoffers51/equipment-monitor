---
name: build-flash
description: Build this ESP-IDF project and flash it to the ESP32-S3 board on COM5. Use when the user asks to build, flash, upload, or "run it on the board". Optional argument is the serial port (e.g. COM5).
disable-model-invocation: true
---

Build and flash the firmware. Port argument: `$ARGUMENTS` (may be empty).

Run every step with the PowerShell tool, prefixing each command with the env activation in the same call:

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1 *> $null; <command>
```

1. If `sdkconfig` does not exist at the project root, run `idf.py set-target esp32s3`.
2. Run `idf.py build` (timeout 600000; first builds are slow). If it fails, show the first compiler error as `file:line`, fix it if the cause is clear, and rebuild. Stop here if the user only asked to build.
3. Pick the port:
   - If `$ARGUMENTS` is non-empty, use it.
   - Otherwise the board's CH343 bridge is normally `COM5`. Confirm it is listed by `[System.IO.Ports.SerialPort]::GetPortNames()`. If it is missing, ask the user to plug the board into the port labelled "UART". If several ports are listed and COM5 is not one of them, ask which to use.
4. COM5 is shared with the bench camera's serial capture. Make sure no `bench-eye` / `build-ops` capture is running before flashing.
5. Run `idf.py -p <PORT> flash`. If it cannot connect, tell the user to hold BOOT, tap RESET, release BOOT, then retry once.
6. Report the result. Do NOT run `idf.py monitor` (interactive, will hang). Tell the user to run `idf.py -p <PORT> monitor` themselves (Ctrl+] to exit), or ask `build-ops` for a timed serial log.
