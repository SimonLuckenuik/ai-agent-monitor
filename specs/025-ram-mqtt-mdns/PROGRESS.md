# Progress

- 2026-10-31: proposed after the footprint review. MQTT and mDNS have been held off by
  unmeasured gates (16 KB and 28 KB) since about 1.4.0. mDNS measured in the emulator
  (420 B), MQTT from the code (about 2.5 KB). About 12 KB of RAM can come back (the update
  buffer, our strings, the routes). Waiting for approval.
- 2026-10-31: approved. Step 1: the update check (`FirmwareValidation`, 4,148 B) is
  allocated when an upload starts and freed when it ends or fails; out of memory answers
  "Not enough memory for an update: try again". Static RAM 51,884 -> 47,796 B. Safety
  tests pass, and the harness now asserts the buffer is freed after every case.
