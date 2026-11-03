# 027 Progress

- 2026-10-31: proposed, after reading smalltv-pomodoro's responder. The owner chose it as
  the first of their features to bring over, for the flash it frees.
- 2026-10-31: built. `src/mdns_packet.cpp` (queries and answers, no Arduino code) and
  `src/mdns.cpp` (the UDP side) replace LEAmDNS.
  - Flash: firmware.bin 595,472 -> 577,888 B; firmware.bin.gz 426,800 -> 415,485 B
    (-11.3 KB; 93% of the compressed limit, which grows as the plain image shrinks:
    610,304 -> 622,592 B). The responder with `WiFiUDP` is about 4.5 KB in the ELF.
  - Heap: 136 B held (emulator; LEAmDNS 560 B there, 944 B on the chip), 512 B while
    answering. Gate lowered from 4,096 to 2,048 B.
  - Tests: 123 unit checks (queries we answer and ignore, compression, pointer loops,
    truncation, 200,000 random and mangled packets, answers read back); `mqtt_and_mdns`
    now checks the A, PTR, SRV (port 80) and TXT records beside each big face.
  - Left as is: modem sleep. smalltv-pomodoro turns it off because multicast gets lost
    while the radio dozes; to watch on the clock (`ping <host>.local` over a few hours).
  - No question echo for legacy (port ≠ 5353) resolvers: they get the answer with their
    id and 10 s TTLs; to check with `dig -p 5353` if one complains.
- 2026-11-03: confirmed, on the clock (2.1.0, 7.6 hours up): `mdnsState` on, and `agentmonitor-adec8a.local` resolves and answers from the home network, through the gateway's mDNS repeater.
