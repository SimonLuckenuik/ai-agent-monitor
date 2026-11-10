# The firmware is built with link-time optimisation

Date: 2026-11-08

Status: Adopted (spec 047). Not yet run on a clock.

## Context

json-render with every feature kept (spec 047, on its own branch) needs room under the update limit (the image and its `.bin.gz` in 1 MB). Spec 047
measured link-time optimisation (LTO) as a lever that costs no feature; the owner asked
to try it ("try lto and leaner compiler after"), then to wire it in.

## Decision

`platformio.ini` runs `scripts/lto_options.py` for the firmware: `-flto` on `src/` and on
the libraries, except ESP8266WiFi, the core and the SDK. The starter is built without it
(it has 131 KB to spare under the plain limit).

## Evidence

| Build | Image | `.bin.gz` | Update room |
|---|---|---|---|
| Firmware without LTO | 576,768 B | 404,515 B | 56 KB |
| **Firmware with LTO** | **563,344 B** | **396,172 B** | **78 KB** |
| With json-render, no LTO | 621,712 B | 435,613 B | 20 KB over |
| With json-render, LTO | 606,592 B | 426,109 B | 3.7 KB |

IRAM the same (28,831 B); RAM about 60 B more (`.data`, `.rodata`, `.bss`); our string
literals in RAM 4,629 B (4,709 B before). Unit, safety (the update cases, the image
unpacked as the bootloader does, strings in flash), and web tests pass. Crash addresses
still decode to functions (`addr2line`, with a DWARF warning).

## Consequences

- What LTO needs, all in `scripts/lto_options.py`: the core's linker script gets a pattern
  for LTO's `*.ltrans.o` objects (or their code lands in IRAM and overflows); library
  archives are built with `gcc-ar`; ESP8266WiFi stays out (LTO loses the
  `SigningVerifier_verify` only the core's Updater calls, and forcing it in pulls 27 KB of
  BearSSL); json-render's stack switch marks the symbols only its asm uses as
  `used, externally_visible`.
- A C++17 `inline` variable loses its PROGMEM section under LTO and lands in RAM: the
  default palettes are `static` now. New flash data in headers: not `inline`.
- Builds take longer (about 1.5 minutes more for a full build).
- Inlining across files changes the stack frames and timing a little: to watch on the
  clock (`/status` drawMs, crash records) after the first release with it.

## Alternatives

- LTO on `src/` only: 10 KB smaller instead of 13.4 KB, and the same risks.
- LTO over the core and the SDK: their link order breaks (undefined `ets_intr_lock`,
  `vPortFree` ...).
- No LTO: json-render does not fit.
