# 027 A small mDNS responder

Status: Done in 2.1.0 (confirmed on the clock 2026-11-03)

## Problem

The compressed update is at 98% of its limit, so every new feature has to free flash
first. mDNS (`<host>.local` and the `_http._tcp` service) uses the core's LEAmDNS
library: about 13 KB of flash (`platformio.ini`), and 944 B of heap on the clock
(`mdnsHeap`, 2.0.0). It is the largest feature that could be much smaller.

smalltv-pomodoro answers `.local` with its own responder: about 1.7 KB instead of about
21 KB, with a packet parser tested against malformed and looping packets. It answers
only the A record; ours also advertises the web service, which Home Assistant and
service browsers use.

## Goals

1. **The same answers.**
   - `<host>.local`: an A record.
   - `_http._tcp.local`: a PTR to `<host>._http._tcp.local`, with its SRV (port 80) and
     TXT records.
   - `_services._dns-sd._udp.local`: a PTR to `_http._tcp.local`.
   - Unicast replies to queries that ask for them (the QU bit, or source port ≠ 5353).
   - Announced when Wi-Fi connects and when the IP changes; a goodbye (TTL 0) before a
     restart.
2. **At least 9 KB of flash back** on the compressed image, measured.
3. **No more heap than today** (`mdnsHeap` ≤ 944 B on the clock), reported as now.
4. **A safe parser.** Bounds-checked reads, compression pointers followed at most a few
   times and only backwards, packets over the buffer dropped. Host-built tests with
   malformed, truncated and looping packets, in the unit suite.
5. **The integration case passes** (`mqtt_and_mdns`: `mdns_query.py` gets answers for the
   name and the service), and the clock answers `ping agentmonitor-xxxxxx.local` and shows
   in a service browser.

## Non-goals

- Probing for name conflicts: the name carries the chip's id.
- Answering for other services, or browsing (the clock never looks names up).
- IPv6.
