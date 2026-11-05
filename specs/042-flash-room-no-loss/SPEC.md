# 042 Flash room without losing a feature: one JSON parser, no HTTPClient

Status: Draft (prototyped and measured 2026-11-05; waiting for approval)

## Problem

Spec 041 (the clock as a native OpenClaw node) needs about 23-26 KB of update room;
2.2.0 leaves 24,576 B. The owner asked to find the room first with changes that lose
nothing (2026-11-05), from the inventory in
[041's RESEARCH](../041-openclaw-node/RESEARCH.md#where-room-could-come-from-inventory-2026-11-05-220).

## Goals

1. **One JSON parser per input kind.** ArduinoJson compiles its parser for each input type
   and each filter kind it is called with: nine copies in 2.2.0 (`const char*`, `String`,
   `const String`, `fs::File`, `WiFiClient`, with and without a filter), 13.1 KB. Every parse
   goes through `parseJson()` (`src/json_parse.h`): text as `const char*`, files and
   sockets as `Stream&`, always through a filter (allow-all when nothing is filtered). Two
   copies remain.
2. **The weather fetch without `HTTPClient`** (4.3 KB for one GET): a plain `WiFiClient`
   request, HTTP/1.0 so the reply is not chunked, reading the status line and the `Date`
   header, then parsing the body straight off the socket, as before.
3. Nothing a user or an agent can see changes: the same API, the same weather, the same
   error messages.

## Non-goals

- Dropping or moving any feature (MQTT, mDNS, photos, icons, the page): kept for if this
  falls short (it does not).
- LTO: tried, the image grows by 14.9 KB ([RESEARCH](RESEARCH.md)).

## Requirements

1. Every `deserializeJson` call in `src/` goes through `parseJson()`; the ELF holds the
   parser for `const char*` and `Stream` only, filtered only (checked with `nm`).
2. Weather and city search work against Open-Meteo as before: a 200 reply parsed, the
   `Date` header passed to the time guard (spec 029), 404 as "City not found", no
   connection as "Weather service unreachable (-1)", no reply as "(-11)", other codes as
   "Weather service error <code>".
3. The heap a fetch takes (`weatherHeap`) is no larger than with `HTTPClient`.
4. All suites pass: unit, safety, face preview, web, integration.

## Flash and RAM

Measured with `pio run` (2026-11-05, from 2.2.0 fc657da):

| | `firmware.bin` | `firmware.bin.gz` | Update room |
|---|---|---|---|
| 2.2.0 | 593,328 B | 420,307 B | 24,576 B |
| One parser (goal 1) | 583,904 B | 413,895 B | 36,864 B |
| Both goals | **579,008 B** | **410,558 B** | **45,056 B** |

+20,480 B of room (the room moves in 4 KB sectors). RAM: no change in static data; the
fetch no longer holds an `HTTPClient` object (about 200 B) or its header buffers.

## Release

A PATCH-sized change with no visible effect: lands with the next release (2.2.1, or
with 041 in 2.3.0).

## Open questions

None.
