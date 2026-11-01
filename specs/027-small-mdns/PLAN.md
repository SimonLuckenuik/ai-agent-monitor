# 027 Plan

1. `src/mdns.cpp`, `src/mdns.h`: the parser (adapted from smalltv-pomodoro's `security.h`,
   credited), the answer builder for A, PTR, SRV, TXT and the services PTR, and the
   start, loop and goodbye. Same `mdnsHeap`, `mdnsState` and gate in `/status`.
2. `tests/unit`: the parser and builder on the host, with valid, malformed, truncated and
   looping packets.
3. Drop LEAmDNS from the build; measure flash before and after (`footprint_files.py`) and
   record it in `docs/specifications/footprint.md`.
4. Integration: `mqtt_and_mdns` as is; extend `mdns_query.py` to check the SRV port and the
   A address.
5. The clock: the owner installs it; `ping <host>.local` and a service browser from the
   PC; `mdnsHeap` from `/status`.
6. Docs: footprint, CHANGELOG, STATUS, the issue record if anything breaks.
