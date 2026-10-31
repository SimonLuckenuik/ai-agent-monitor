# Sending a face fails while a big face is loaded

Status: Fixed in 1.9.1 (spec 024), to confirm on the clock

Date: 2026-10-31

## Summary

On the clock with 1.9.0, after flip-clock-wide had been shown, sending retro-runner
(3,963 B minified) or night-lake (3,689 B) through `POST /api/faces` failed: the clock
closed the connection without an answer. It did not restart. Free heap was 10 KB against
14.1 KB with only built-in faces drawn, and stayed there after flip-clock-wide was deleted
and a built-in face was on screen.

## Cause

The face last drawn stays parsed in `customDoc` (`src/faces.cpp`) after the clock moves to
another face, and after the face is deleted. Receiving a face then needs the body as a
String (about 4 KB, allocated by the web server before the handler runs) and a JsonDocument
to check it in `storeFace` (`src/integrations.cpp`), on top of the face still loaded.
Reproduced in the emulator (`EMU_HEAP` 25600): after flip-clock-wide, sending retro-runner
answers 400 "Expected JSON with an items array", which is `deserializeJson` running out
of memory reported as bad JSON. With retro-runner shown, 3.3 KB is free and night-lake's
upload closes the connection as on the clock.

## Workaround

Send a tiny face with `show=1` first (it replaces the loaded document and frees about
4 KB), delete the big one, then send the next face. All three faces went to the clock
this way.

## Fix (to propose)

Free the loaded face before a face upload is read and when the clock shows a built-in
face; check the upload with a filtered parse; answer "Not enough memory" instead of
"Expected JSON" when `deserializeJson` reports NoMemory. A face that needs most of the
heap while shown (retro-runner leaves about 7 KB) may also need a size budget.

## Fix

[Spec 024](../../specs/024-memory-headroom/SPEC.md), 1.9.1: `customRelease()` when a built-in
face is shown, and in a web server hook before an upload's body is read; `storeFace`
checks a filtered parse (the items' types only) and answers 507 "Not enough memory" on
NoMemory. Integration case `faces_beside_big_faces`: all 38 other example faces upload
beside retro-runner (6.9 KB free), flip-clock-wide and night-lake.
