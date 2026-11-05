# The OpenClaw node takes 22.7 KB of flash, leaving 8 KB of update room

Date: 2026-11-05

Status: Accepted for the build (spec 041); the owner sees it before the release

## Context

Spec 041 estimated the native node at 14-15 KB of code, 23-26 KB of update room. Spec
042 had freed 20 KB with nothing lost, for 45,056 B of room. The first build of the
node was +32.6 KB of image and did not fit at all.

## Decision

Keep every command and fit the node by changing how it is written, not what it does:

- Frames out and command results are written as JSON text (`jsonAdd`, `jsonQuote` in
  `src/openclaw.cpp`) instead of built as ArduinoJson documents: each `doc[F("key")] = v`
  is 30-60 B of code where it is written.
- Every member read goes through one path helper (`jsonAt(v, F("payload.auth.deviceToken"))`).
- Commands are dispatched by their place in the declared list; error messages are short.
- `configSave()` in `main.cpp`: `Config::save()`, defined in `config.h`, was inlined
  (about 1.5 KB) into each file that calls it once. The weather code had its own copy;
  it now calls `configSave()` too.

## Evidence (measured)

| Build | `firmware.bin` | `.bin.gz` | Room |
|---|---|---|---|
| `faces-v2` with spec 042 | 579,008 B | 410,558 B | 45,056 B |
| First node build | 611,600 B | 431,770 B | does not fit |
| Final | 601,664 B | 426,724 B | 8,192 B |

The node is now about 4.6 KB of crypto, 8 KB of protocol, 5.2 KB of commands and 4 KB of
settings and library code (spec 041's table), less the 1.5 KB found in the weather.

## Alternatives

- Fewer commands (`display.status`, `clock.faces.list` and `device.info` are about 1.5 KB
  together): kept, they are what lets an agent see what the clock shows.
- More room first (MQTT about 11 KB, mDNS about 9 KB, the photos and dashboards about
  16 KB; spec 041 RESEARCH): every one loses a feature the owner uses or may use.

## Consequences

- 8 KB of room is left for the next features (two 4 KB sectors). The next change that
  needs more must find it first, from the inventory in spec 041's RESEARCH.
- ArduinoJson member access with `F()` keys is expensive: new code that writes many
  fields should write text or go through a helper.
