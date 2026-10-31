# Current status and next work

Updated: 2026-10-30. Update this page when a release, a device result, a spec's state or
the next step changes; the history is in the [decision records](decisions/README.md),
the [specs](../specs/README.md) and `CHANGELOG.md`.

## Release and checkout

The latest release is **1.8.0** (2026-10-30, tag `v1.8.0`), published on GitHub as
`SimonLuckenuik/ai-agent-monitor` with `AIAgentMonitor_v1.8.0.bin.gz` and
`SHA256SUMS.txt`, built and tested by `.github/workflows/release.yml` on the push to
`main`. `main` and `faces-v2` are the same commit. The plugin is 0.5.2.

Releases since the fork: 1.4.0 (2026-10-28, the first AI Agent Monitor release and the
last with a plain first-install image), 1.5.0 (face model), 1.6.0 (palettes and roles,
display features, emulator scenarios), 1.7.0 and 1.8.0 (flicker-free redraws, flip
clocks), all 2026-10-29/30.

## The clock

The owner's clock (MAC 8C:8C:29:AD:EC:8A, `agentmonitor-adec8a`; address moves, last
seen at 192.168.66.57) last ran **1.4.0** builds. Shown there in the 1.4.0 work:

| Feature | Device result |
|---|---|
| Install over SmartClock-OSS, then updates | The owner installed both 1.4.0 images (`_nomqtt.bin`, then `.bin.gz`) |
| Line font and Big tiles | Drew glyph by glyph at 80 MHz; Big date tripped the watchdog; fixed by faster strokes and 160 MHz, re-measured with `drawMs` |
| Agent messages | Each icon sent in parallel and shown; solid backgrounds chosen on the panel |
| Night color | Dark grey at night, chosen on the panel |
| French | Shown |
| Setup network after a reboot | Once showed the old name in recovery ([issue](issues/2026-10-25-setup-network-kept-old-name-after-reboot.md)) |

**Not run on the clock:** everything from 1.5.0 on (face model, palettes and roles,
orientation, gamma, screen off, off-screen repaints, flip clocks), mDNS, MQTT with a
broker. That check is [spec 018](../specs/018-first-run-on-the-clock/SPEC.md).

## Checks

Before each commit of firmware: `pio run` (size guard), unit tests (274 checks), safety
tests, face preview, and for visible changes the integration tests (24 cases, Docker)
and `scripts/screenshots.py`. CI (`.github/workflows/build.yml`) runs the build, unit, safety, face preview and integration tests on every push.

## Flash budget

`firmware.bin` 589,456 B; `firmware.bin.gz` 419,030 B; compressed update limit
618,496 B, **95% used** (about 29 KB left). Off-screen repaints (1.8.0) cost 10.7 KB.

## Specs in flight

None in progress. Proposed: [018](../specs/018-first-run-on-the-clock/SPEC.md),
[019](../specs/019-claude-code-hooks/SPEC.md). Order: [roadmap](plans/roadmap.md).

## Open issues

See [issues](issues/README.md): the old setup network name after a reboot, and a new
variable dropped when the table is full.
