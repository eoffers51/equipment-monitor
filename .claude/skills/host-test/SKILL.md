---
name: host-test
description: Build and run the host (PC) unit tests for this project's hardware-free logic components with Unity and MinGW gcc, no board needed. Use when the user asks to run the tests, "test the logic", or after changing a logic component. Pass -Clean to rebuild from scratch.
---

Run the host tests in `host_test/`. Arguments: `$ARGUMENTS` (may contain `-Clean`).

1. If `host_test/run_tests.ps1` does not exist, the host-test project has not been created yet. Say so and stop. Do not scaffold it unless the user asks; when they do, copy the structure from `..\Lesson_1_3_Blink\host_test\` (`CMakeLists.txt`, `run_tests.ps1`, `test_main.c`, `fakes/`) and adapt the source list to this project's `components/`.
2. Run with the PowerShell tool (no IDF env activation needed; the script puts MinGW first on PATH itself):

   ```powershell
   .\host_test\run_tests.ps1 $ARGUMENTS
   ```

   It takes `IDF_PATH` from the env, defaulting to `C:\esp\v6.0.2\esp-idf`, and uses gcc from `C:\CTOOLS\mingw32\bin`.
3. Report every FAIL line with its test name and the assertion message. On a compile error, show the first error as `file:line`. Both MinGW and Linux gcc must accept the code with `-Wall -Wextra -Werror -std=c11`, so do not fix warnings with compiler-specific pragmas.
4. If a test file was added, remind that it also needs an entry in `host_test/CMakeLists.txt` and a `run_*_tests()` call in `test_main.c`.
5. Never edit a logic component to include `esp_*`, FreeRTOS or `driver/` headers to make a test pass; put a fake in `host_test/fakes/` instead.
