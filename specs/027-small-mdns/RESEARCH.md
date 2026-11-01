# 027 Research

## Ours today

`SMARTCLOCK_MDNS=1` builds LEAmDNS (`ESP8266mDNS`): `MDNS.begin(host)`,
`MDNS.addService("http", "tcp", 80)`, `MDNS.update()` in the loop. Started behind a
4,096 B heap gate (spec 025). Measured: 420-560 B in the emulator, 944 B on the chip.
`platformio.ini` puts it at about 13 KB of flash; the plain (no-MQTT) image leaves it out
for room.

## smalltv-pomodoro (GPL-3.0, commit 2f6836d)

`main.cpp` (`mdnsStart`, `mdnsLoop`, `mdnsSend`) with the parser in `security.h`
(`mdnsAsksFor`, `mdnsHasAnswer`, `mdnsProbeQuery`, `mdnsAnswer`):
- one `WiFiUDP` joined to 224.0.0.251:5353 on the station address; joined again when the
  IP changes or Wi-Fi comes back;
- answers A queries for its names, unicast when asked (QU) or for legacy resolvers
  (source port ≠ 5353, echoing the id), else multicast;
- probes its short name 3 × 250 ms and takes `-2`, `-3`… when taken (we do not need it);
- `WiFi.setSleepMode(WIFI_NONE_SLEEP)`: with modem sleep the ESP8266 misses multicast
  and the name "disappears sometimes" (about 15 mA more; the clock is mains powered).
  Check what ours sets.

Their tests (`sim/security_test.cjs`) feed malformed and looping packets to the parser.
The parser can be adapted (same licence, credited); the PTR, SRV and TXT answers are ours
to add (RFC 6762, RFC 6763).

## Packets

A query: 12-byte header, questions (name, type, class with the QU bit), maybe known
answers (to skip ours if listed with more than half our TTL). Answers set the AA bit,
the cache-flush bit on A, SRV and TXT (not on the shared PTR), TTL 120 s (A, SRV) and
4,500 s (PTR, TXT). One answer packet with the A record as an additional record fits in
about 200 B.
