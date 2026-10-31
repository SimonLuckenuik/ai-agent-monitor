# 018 Check 1.5-1.8 on the real clock

Status: Draft (proposed 2026-10-30, not approved)

## Problem

Nothing after 1.4.0 has run on the owner's clock. The emulator models drawing speed
(±20%) but not heap pressure or the panel's own commands.

## Goals

1. Install the latest `.bin.gz` from the Firmware update card (the owner does it).
2. Check, and record in PROGRESS and STATUS: heap at idle and during a big face
   (`/status` heap, heapLow); draw times (`drawMs`) for Big time, Classic, flip-clock,
   Bauhaus; the 5 KB repaint strips with a large face loaded; orientation 90/180/270;
   gamma curves; screen off and wake; mDNS; MQTT with a broker; French labels.
3. File an issue record for each problem found.

## Non-goals

- New features. Fixes become their own specs or go in the spec they belong to.

## Open questions

- Which faces to keep in the rotation for the check.
