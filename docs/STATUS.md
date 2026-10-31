# Current status and next work

Updated: 2026-10-30. Update this page when a release, a device result, a spec's state or
the next step changes; the history is in the [decision records](decisions/README.md),
the [specs](../specs/README.md) and `CHANGELOG.md`.

## Release and checkout

The latest release is **1.9.0** (2026-10-31, tag `v1.9.0`), published on GitHub as
`SimonLuckenuik/ai-agent-monitor` with `AIAgentMonitor_v1.9.0.bin.gz`,
`AIAgentMonitor_v1.9.0.elf` (for `scripts/decode_crash.py`) and `SHA256SUMS.txt`, built
and tested by `.github/workflows/release.yml` on the push to `main`. `main` and
`faces-v2` are the same commit. The plugin is 0.5.3.

Releases since the fork: 1.4.0 (2026-10-28, the first AI Agent Monitor release and the
last with a plain first-install image), 1.5.0 (face model), 1.6.0 (palettes and roles,
display features, emulator scenarios), 1.7.0 and 1.8.0 (flicker-free redraws, flip
clocks), all 2026-10-29/30; 1.9.0 (2026-10-31: faces within the stack, crash records,
crash-loop protection).

## The clock

The owner's clock (MAC 8C:8C:29:AD:EC:8A, `agentmonitor-adec8a`; address moves, last
seen at 192.168.66.229) runs **1.8.0** (build bed88cb, installed by the owner by
2026-10-31). With face 7 shown: free heap about 15.7 KB, Big time draws in 0.8 s.
Retro-runner, flip-clock-wide and night-lake restart it: a stack overflow in the
expression parser while repainting, reproduced in the emulator
([issue](issues/2026-10-31-animated-faces-restart-the-clock.md)); they were deleted from
the clock. Fixed in 1.9.0 (spec 023), with crash records and crash-loop protection
(spec 021). The clock runs **1.9.0** (build 9b10951) since 2026-10-31: the three faces
ran without a restart, least free stack 1,040 B. Sending a face after a big one was
shown fails for lack of heap ([issue](issues/2026-10-31-face-upload-needs-the-loaded-face-memory.md)). The weather
never loads on 1.9.0 (16 KB fetch threshold). Both fixed in 1.9.1 (spec 024). Shown there in the 1.4.0 work:

| Feature | Device result |
|---|---|
| Install over SmartClock-OSS, then updates | The owner installed both 1.4.0 images (`_nomqtt.bin`, then `.bin.gz`) |
| Line font and Big tiles | Drew glyph by glyph at 80 MHz; Big date tripped the watchdog; fixed by faster strokes and 160 MHz, re-measured with `drawMs` |
| Agent messages | Each icon sent in parallel and shown; solid backgrounds chosen on the panel |
| Night color | Dark grey at night, chosen on the panel |
| French | Shown |
| Setup network after a reboot | Once showed the old name in recovery ([issue](issues/2026-10-25-setup-network-kept-old-name-after-reboot.md)) |

**Not checked on the clock yet:** everything from 1.5.0 on (face model, palettes and roles,
orientation, gamma, screen off, off-screen repaints, flip clocks), mDNS, MQTT with a
broker. That check is [spec 018](../specs/018-first-run-on-the-clock/SPEC.md).

## Checks

Before each commit of firmware: `pio run` (size guard), unit tests (286 checks), safety
tests, face preview, and for visible changes the integration tests (26 cases, Docker)
and `scripts/screenshots.py`. CI (`.github/workflows/build.yml`) runs the build, unit, safety, face preview and integration tests on every push.

## Flash budget

1.9.0: `firmware.bin` 594,240 B; `firmware.bin.gz` 422,897 B; compressed update
limit 614,400 B (it shrinks as the image grows), **97% used** (about 18 KB left). Crash
records and protection cost about 3 KB compressed; 1.8.0 was 419,030 B.

## Specs in flight

Done: [023](../specs/023-renderer-stack/SPEC.md) (faces within the 4 KB stack, confirmed
on the clock). In 1.9.0 on the clock, not yet seen at work there (no crash since):
[021](../specs/021-crash-report/SPEC.md) (crash records, crash-loop protection, safe mode),
[022](../specs/022-emulator-device-memory/SPEC.md) (the emulator with the clock's memory;
the clock's 3,050 B against the emulator's 2,752 B leaves `EMU_STACK` at 4096). In 1.9.1, not yet on the clock: [024](../specs/024-memory-headroom/SPEC.md) (the weather
and face uploads within the heap). Proposed: [018](../specs/018-first-run-on-the-clock/SPEC.md), [019](../specs/019-claude-code-hooks/SPEC.md).
Dropped: [020](../specs/020-esp-now/SPEC.md). Order: [roadmap](plans/roadmap.md).

## Open issues

See [issues](issues/README.md): animated custom faces restart the clock, the old setup network name after a reboot, and a new
variable dropped when the table is full.
