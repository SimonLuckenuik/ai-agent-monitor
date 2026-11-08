# json-render goes in the one firmware, with every feature kept

Date: 2026-11-07

Status: Owner's direction for spec 047 (the spec is still a draft). Supersedes the
"separate build" part of [json-render is rendered on the clock, even at the cost of
features](2026-11-07-json-render-native.md).

## Context

The earlier record planned room for json-render from a separate build leaving out
OpenClaw, MQTT and mDNS. The prototype built for the clock then measured 63 KB more
image than 2.3.1, 92 KB over the update limit (image and `.bin.gz` together in 1 MB).

## Decision

The owner: "dont build the json-render device build variant, try to keep everything",
and "investigate how to reduce size without dropping features". One firmware for every
clock, with every current feature, plus json-render.

## Consequences

The room has to come from how the firmware is built and stored, not from features
(spec 047 RESEARCH.md, "Room for json-render with every feature kept"):

- done: the catalog and prompt stored gzipped (12 KB of room);
- measured, not adopted: LTO of our own sources (about 20 KB of room);
- estimated: the settings page, the catalog and other read-only data as LittleFS files
  that come with the update (40-70 KB of room), and a size-minded compiler (15-20 KB).

With all of them json-render fits with a margin. Without the files in LittleFS it does
not. That mechanism and LTO change the standard firmware, so each needs its own
decision (a spec for the files) and a run on a real clock.

The heap is still short: a 3.9 KB spec needs about 10.8 KB at once and fails on the
clock's arena. Leaving OpenClaw and MQTT out was the answer, and is no longer one.

## Alternatives

- A separate json-render build without OpenClaw, MQTT and mDNS (the earlier record):
  set aside by the owner.
- Compiling on the host (the first draft): no flash cost, but the clock does not speak
  the standard itself.

## Evidence

specs/047-json-render/RESEARCH.md and PROGRESS.md (2026-11-07): the builds measured with
`pio run` (2.3.1 602,384 B and 427,330 B; with json-render 665,216 B and 463,935 B; with
the gzipped catalog and LTO 642,336 B and 457,900 B) and the symbol sizes.
