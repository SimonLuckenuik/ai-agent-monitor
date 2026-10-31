# Progress

- 2026-10-31: proposed. The clock on 1.9.0 shows no weather (free heap under the 16 KB
  fetch threshold), night-lake has no sun, and face uploads fail after a big face.
  Measurements in RESEARCH. Waiting for approval.
- 2026-10-31: approved. Built the threshold (8 KB) with `weatherError` "Waiting for
  memory" and `weatherHeap` in `/status`; `customRelease()` when a built-in face is
  shown, before a fetch that is short of memory, and in a web server hook before a face
  upload is read; `storeFace` checks a filtered parse and answers 507 on NoMemory. The sun
  times (NOAA, ~0.8 KB of flash, unit-checked within 3 min of Open-Meteo) were written
  and then taken out at the owner's request. Emulator: a fetch took 6,096 B; every
  example face uploads beside each big face. Compressed image 425,569 B (97%).
