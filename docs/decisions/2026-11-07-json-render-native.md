# json-render is rendered on the clock, even at the cost of features

Date: 2026-11-07

Status: Owner's direction for spec 047 (the spec itself is still a draft)

## Context

Spec 047's first draft scoped json-render (Vercel Labs' generative UI standard) on the
clock. With 2.3.1 at 10.7 KB of raw update room (4 KB in whole sectors), and native
parsing estimated at 8-11 KB of code (14-19 KB of room), it recommended compiling
json-render to our face format on the host, with no firmware change.

## Decision

The owner chose the standard over the room: "Willing to ignore the available space for
now and to drop features if it means supporting a standard rendering. Could even be a
fork or two different builds. Full catalog exposed (non interactive, just display
considering the device as no input) + support as much rendering as possible."

So spec 047 plans:

- a catalog served by the clock (`GET /api/catalog`), display-only;
- json-render specs accepted by `POST /api/faces`, compiled on the clock into the face
  items the existing renderer draws;
- `@json-render/image`'s components first, then the display parts of
  `@json-render/shadcn`, then clock-specific ones;
- room made by a separate build that can leave out OpenClaw (22.7 KB), MQTT (about
  7 KB) and mDNS (about 4.5 KB), rather than by holding the feature to the standard
  build's room.

## Alternatives

- Host-side compiler only (the first draft's recommendation): no firmware cost, but the
  clock itself does not speak the standard, and agents need the plugin or the bridge.
  Kept as the prototype of the on-device compiler.
- Rendering to an image on the host (`@json-render/image` → JPEG → photo): static only,
  and each update writes flash.
- A second drawing engine for json-render: rejected in favour of compiling to the
  existing items, which keeps the flicker-free redraws and the night tint.

## Evidence

specs/047-json-render/RESEARCH.md: the catalogs, the mapping, the prototype (a 1,882 B
spec → a 1,275 B face drawn in the emulator), and the size estimates.
