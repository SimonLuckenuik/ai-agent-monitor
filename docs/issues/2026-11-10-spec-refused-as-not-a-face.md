# A valid json-render spec was refused as "not a face" (400)

Date: 2026-11-10

Status: Open. Seen in the emulator (the 2.7.0 image), not on a clock; not reproduced since.

## Symptom

`POST /api/faces?name=pr-queue&show=1` with spec 049's first draft of pr-queue (5.5 KB
minified, 33 elements, no warnings on its own) answered 400 `Expected JSON with an "items"
array`: the message for a custom face without items, not for a spec. It happened in the
`json_render` integration case, after the case's other pushes (the 13 KB and 6.3 KB
bodies of the size checks, a dozen specs), with 17.5 KB of heap free (13.8 KB the least
since boot). Cut down by hand on the same running emulator: one row of four compiled,
then two rows compiled, then (once the spec's state had set its variables) two rows were
refused too, and the full spec after deleting every face and variable.

After a reset (21 KB free) the same spec compiled every time, including at 15.8 KB free
with another spec on screen, and in the next run of the case.

## Cause

Not found. The 400 comes from `storeFace()` (src/integrations.cpp) when the face the
compiler produced does not parse: so the compiler returned no error but a malformed
face. A likely way: an Arduino `String` append that fails for want of a large enough
free block leaves its string as it was, without an error, so a piece of the face's JSON
goes missing; that depends on how fragmented the heap is, which would explain why a
reset cured it. Not confirmed (a build that reports the compiled text was used only after
the reset, and then it never failed).

## What to do

- Make the compiler check its output's allocations (`String::concat` returns false; or
  `reserve()` the expected size once) and answer 507 "Not enough memory" instead, so a
  client can retry rather than fix a spec that is fine.
- Until then: a 400 "items" reply to a spec that compiles elsewhere means memory; the
  gallery's faces were kept well under the limits (pr-queue now 4.7 KB, 26 elements).
