# Progress

- 2026-10-31: proposed after the footprint review. MQTT and mDNS have been held off by
  unmeasured gates (16 KB and 28 KB) since about 1.4.0. mDNS measured in the emulator
  (420 B), MQTT from the code (about 2.5 KB). About 12 KB of RAM can come back (the update
  buffer, our strings, the routes). Waiting for approval.
