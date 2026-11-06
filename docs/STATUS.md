# Current status and next work

Updated: 2026-11-06. Update this page when a release, a device result, a spec's state or
the next step changes; the history is in the [decision records](decisions/README.md),
the [specs](../specs/README.md) and `CHANGELOG.md`.

## Release and checkout

The latest version is **2.3.1** (2026-11-06: `clock.help` on the OpenClaw node, spec
045), released 2026-11-06 (tag `v2.3.1`, `AIAgentMonitor_v2.3.1.bin.gz`). Before it: **2.3.0** (2026-11-05, tag `v2.3.0`: the clock as an
OpenClaw node, flash room without losing a feature, the emulator's OpenClaw panel and an
OpenClaw skill), published on GitHub as `SimonLuckenuik/ai-agent-monitor` with
`AIAgentMonitor_v2.3.0.bin.gz`, `AIAgentMonitor_v2.3.0.elf` (for `scripts/decode_crash.py`)
and `SHA256SUMS.txt`, built and tested by `.github/workflows/release.yml` on the push to
`main`. The plugin on `main` is 0.7.3. Before it: 2.2.0 (2026-11-04, Moni and animated
messages).

Releases since the fork: 1.4.0 (2026-10-28, the first AI Agent Monitor release and the
last with a plain first-install image), 1.5.0 (face model), 1.6.0 (palettes and roles,
display features, emulator scenarios), 1.7.0 and 1.8.0 (flicker-free redraws, flip
clocks), all 2026-10-29/30; 1.9.0 (faces within the stack, crash records, crash-loop
protection), 1.9.1 (weather and face uploads within the heap) and 2.0.0 (RAM back, MQTT
and mDNS on again), all 2026-10-31; 2.1.0 (2026-11-03):

**In 2.1.0:** the face gallery and the converted smalltv-pomodoro
faces (026), a small mDNS responder (027), rain or snow within the hour (028), network
guards (029), faster big text (030), four digit fonts and a stroke weight (031), backups
with the custom faces (032), every time zone (033), one city search for the weather and
the time zone (035). None of it has run on the clock yet: the owner installs it next.

## The clock

**Three clocks** are on the IoT VLAN (2026-11-06, found by reading `/status` across
192.168.66.x; `.local` names do not resolve from the development machine):
`agentmonitor-ade9c0` (192.168.66.79) runs **2.3.1**, installed by the owner on
2026-11-06: 26.6 KB free and 25.5 KB at the lowest four minutes after the start, the Day
tiles in 87-131 ms, mDNS on, MQTT off, OpenClaw off (not paired: the node and
`clock.help` are not tested on a clock yet). `agentmonitor-adec8a` (now 192.168.66.229)
and `agentmonitor-ade859` (192.168.66.57; 46 hours up, 26.3 KB free, 18.9 KB at the
lowest) run 2.2.0.

The owner's clock (MAC 8C:8C:29:AD:EC:8A, `agentmonitor-adec8a`; the address moves) runs
**2.1.1** since 2026-11-03, installed by the owner and working as expected: five forecast
days, the Day 4 tile (147 ms to draw), 24.7 KB free and 21.4 KB at the lowest, mDNS on,
MQTT connected again, the time right in EST, no new crash. On 2.1.0, read from `/status` and
`/config` after 7.6 hours up (no sign-in): 23.9 KB free, 22.2 KB at the lowest; Big
time draws in 106 ms (148 ms at worst; 0.8 s on 1.8.0), the Day tiles in 98-132 ms;
mDNS on and `agentmonitor-adec8a.local` resolving from the home network; a request
naming another host refused (403); no Wi-Fi downgrade; the time right in EST after
the change from daylight saving time; the weather and the next hour's rain matching
Open-Meteo; the place picked from the city search. **MQTT does not connect** ("cannot
connect (-2)" to 192.168.66.59:1883: the broker is down, has moved, or is not reachable
from the clock's network). One crash is recorded (a panic, 4,958 s after a start,
no face drawing); its details need the signed-in page.

On 2.0.0 (2026-10-31): 25.3 KB free at rest, every example face of the time shown
without a restart (least free heap 16.3 KB), MQTT driving faces and messages (2026-11-01,
spec 025).

Earlier device results (1.4.0 to 1.9.1) are in the decision records and the closed
[issues](issues/README.md): Big date's watchdog reset (1.4.0), animated faces' stack
overflow (1.9.0), the weather and face uploads within the heap (1.9.1), all fixed and
confirmed on the clock.

**Still to see on the clock:** a backup and restore (needs the signed-in page), and rain
or snow actually announced. The crash recorded before 2.1.1 is cleared by the owner.

## Checks

Before each commit of firmware: `pio run` (size guard), unit tests (405 checks, plus 123
mDNS and 23 guard checks), safety tests, face preview, the settings page's own tests
(`node tests/web/*.test.js`: time zone rules, backups), and for visible changes the
integration tests (33 cases, Docker) and `scripts/screenshots.py`. CI
(`.github/workflows/build.yml`) runs all of them on every push.

## Flash budget

2.1.0 had 8,192 B of room (the plain and compressed images together must fit 1 MB less
two sectors). After [spec 036](../specs/036-flash-room/SPEC.md) on `main`: `firmware.bin`
589,344 B, `firmware.bin.gz` 418,987 B, **28,672 B of room**. New code costs about 1.7
times its size in room; data that does not compress, twice. The fonts took about 8 KB; the backups and city search about 2 KB. A libc float
parser costs about 9 KB: parse numbers with the face code's own (`readNumber`). Savings
to find are listed in the [footprint](specifications/footprint.md) and the
[roadmap](plans/roadmap.md).

On `faces-v2`, not released: [spec 042](../specs/042-flash-room-no-loss/SPEC.md) (nothing
lost): `firmware.bin` 579,008 B, `firmware.bin.gz` 410,558 B, 45,056 B of room. On branch
`openclaw-native`, with the OpenClaw node ([041](../specs/041-openclaw-node/SPEC.md)):
601,808 B and 427,067 B, 8,192 B of room
([decision](decisions/2026-11-05-openclaw-node-flash.md)). 2.3.1 (`clock.help`,
[045](../specs/045-openclaw-help/SPEC.md)): 602,384 B and 427,337 B, **4,096 B of room**
(the plain image crossed a sector boundary).

## Specs in flight

Done in 2.2.0, not yet seen on the clock: [039](../specs/039-animated-messages/SPEC.md)
(Moni and animated messages). Done in 2.1.1:
[036](../specs/036-flash-room/SPEC.md) (flash room), [038](../specs/038-fifth-forecast-day/SPEC.md)
(a fifth forecast day), and the weather icons fixed for Open-Meteo's codes. Done in 2.1.0
and confirmed on the clock: [026](../specs/026-face-gallery/SPEC.md),
[027](../specs/027-small-mdns/SPEC.md), [028](../specs/028-rain-snow-soon/SPEC.md),
[029](../specs/029-network-guards/SPEC.md), [030](../specs/030-faster-big-text/SPEC.md),
[031](../specs/031-more-fonts/SPEC.md), [032](../specs/032-backup-with-faces/SPEC.md),
[033](../specs/033-time-zone-from-city/SPEC.md), [035](../specs/035-city-search/SPEC.md).
Closed 2026-11-03 after the checks on the clock: [018](../specs/018-first-run-on-the-clock/SPEC.md),
[021](../specs/021-crash-report/SPEC.md), [022](../specs/022-emulator-device-memory/SPEC.md),
[024](../specs/024-memory-headroom/SPEC.md), [025](../specs/025-ram-mqtt-mdns/SPEC.md).
On `faces-v2`, not released: [042](../specs/042-flash-room-no-loss/SPEC.md) (flash room,
nothing lost). In progress on branch `openclaw-native`: [041](../specs/041-openclaw-node/SPEC.md)
(the clock as an OpenClaw node: built, tested in the emulator against a stand-in gateway
and the real OpenClaw 2026.11.1 gateway; not yet on the clock),
[043](../specs/043-emulator-openclaw-panel/SPEC.md) (the emulator's OpenClaw panel) and
[044](../specs/044-openclaw-skill/SPEC.md) (an OpenClaw skill that teaches the agents the
clock: installed and loaded on the real gateway, its `clock.py` saves faces from there;
no model was available to try an agent with), all done in 2.3.0; and
[045](../specs/045-openclaw-help/SPEC.md) (`clock.help`, links to the skill: done in 2.3.1).
Plugin only, on `main`: [019](../specs/019-claude-code-hooks/SPEC.md) (session status
hooks, plugin 0.7.2, seen on the real clock). Proposed:
[037](../specs/037-claude-usage-face/SPEC.md) (a Claude usage face, set aside for now),
[040](../specs/040-scrolling-text/SPEC.md) (scrolling text).
Deferred: [034](../specs/034-face-languages/SPEC.md). Dropped: [020](../specs/020-esp-now/SPEC.md).
Order: [roadmap](plans/roadmap.md).

## Open issues

See [issues](issues/README.md): the old setup network name after a reboot.
