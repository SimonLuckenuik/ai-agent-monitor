# 025 RAM back, and MQTT and mDNS on again

Status: Done in 2.0.0 (confirmed on the clock)

## Problem

MQTT and mDNS have not run on the clock since about 1.4.0. Each waits for free heap
behind a gate set in 0.1.1 without measuring, when the clock had about 32 KB free:

- **MQTT:** 16 KB gate. It needs about 2-2.5 KB held, plus 2-3 KB while connecting.
- **mDNS:** 28 KB gate. It needs 420 B held (measured in the emulator), under about 2 KB
  in all.

The clock has 14.1-14.6 KB free at rest on 1.9.1, and about 7 KB with a big custom face
on screen. Both features fit at rest, but beside a big face little would be left. Three
holders account for about 12 KB of RAM that does not need to be held
([footprint](../../docs/specifications/footprint.md)):

- the update check buffer (4,148 B in `.data`), used only during an update;
- our string literals copied into RAM at boot (7.6 KB);
- about 60 web server routes, each a handler object, a `Uri` and a `std::function` on the
  heap (about 4.7 KB).

## Goals

1. **MQTT connects** to a broker at rest and with any example face on screen. It
   publishes Home Assistant discovery and state, and answers its command topics.
2. **mDNS answers** `<host>.local` and advertises `_http._tcp`, at rest and with any example
   face on screen.
3. **Measured gates.** `/status` reports what each took when it started (`mqttHeap`,
   `mdnsHeap`, as `weatherHeap` does). Each gate is that, measured on the clock, plus a
   margin. A feature waiting on its gate says so (`mqtt`: "waiting for memory"; an
   `mdns` field).
4. **At least 10 KB of RAM back.** Measured as free heap at rest with built-in faces, MQTT
   and mDNS off:
   - the update buffer is allocated only while an update runs (4.1 KB);
   - our strings stay in flash, except the keyword tables a face reads every frame
     (6-7 KB);
   - one request handler dispatches by path instead of the routes (about 4 KB).
5. **No behaviour change.** Every API path, method, auth rule and answer stays the same;
   the integration suite passes unchanged.
6. **Within the budgets.** The update image stays within the limit (97% today), and the
   stack's deepest use does not grow.

## Non-goals

- Face uploads over MQTT larger than the 1,536 B message buffer. Documented as a limit;
  faces go through HTTP.
- Moving the SDK's or the libraries' own strings.
- New MQTT or mDNS features.
