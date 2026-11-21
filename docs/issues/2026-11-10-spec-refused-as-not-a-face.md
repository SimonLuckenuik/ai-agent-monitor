# A valid json-render spec was refused as "not a face" (400)

Date: 2026-11-10

Status: Fixed in the source (2026-11-21, `feat/047-json-render`), not released: a spec the clock lacks the memory to
compile answers 507, not 400. Reproduced and checked in the emulator, not on a clock.

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

## 2026-11-21: reproduced, cause found, fixed

- **Reproduced** with the emulator's heap made smaller (`EMU_HEAP`), each of the 12 gallery
  specs pushed to a fresh emulator with a build that returns the compiled face in the
  error: at 25,600 B (24 KB free) all 12 compile; at 17,000 (15.5 KB free, a 13.7 KB
  block) six are refused with 400, their faces cut off: 303 to 1,871 bytes of about
  3 KB, `IncompleteInput` (the end lost) or `InvalidInput` (a piece in the middle lost);
  at 15,500 and 14,000 the rest fail too, some with a correct 507 (an allocation that
  reports its failure).
- **Cause, confirmed:** the compiler writes the face into one `String` with hundreds of
  appends (`itemStart`, `fNum`, `fStr`, `quote()` a character at a time;
  src/jsonrender.cpp). The core's `String` grows by 16 bytes at a time, and an append
  whose reallocation fails returns false and leaves the string as it was: the piece is
  dropped and compiling goes on. `storeFace()` then cannot parse the face and answered
  "Expected JSON with an "items" array".
- **Fix:** `storeFace()` answers "Not enough memory" (507) when a spec's compiled face
  does not parse: the compiler only writes valid JSON, so that means a lost append. The
  HTTP handler already maps that message to 507. +64 B of flash (607,488 B).
- **Tried and dropped:** growing the face in 1 KB steps (four reallocations instead of
  hundreds): the same six specs failed at 17,000, so it does not lower what a compile
  needs, and it was left out.
- **What it needs:** a gallery-sized spec (2.3 to 4.7 KB) compiles with about 17 KB free;
  the clock has about 24 KB at rest and 21 KB with a spec on screen (spec 047's clock
  check), so it fits, with less room when MQTT, OpenClaw or a weather fetch hold memory.
  After a 507 the client can retry later; the face of that name is left as it was.
- Checked: the 12 specs at 25,600 (all OK) and 17,000 (six 507, no 400) with the fix;
  unit tests (436), safety tests, the `json_render` integration case. Not checked: on a
  clock; no integration case lowers the heap (the emulator's heap is set per container).
