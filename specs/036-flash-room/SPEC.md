# 036 Flash room: zopfli, our own time zone parser, a minified page

Status: Done in 2.1.1 (2026-11-03)
is backward compatible: it is, see RESEARCH)

## Problem

An update must fit the plain image and its compressed copy together in the 1 MB update
area, less two sectors (`scripts/build_options.py`). After 2.1.0 that leaves 8,192 B: two
sectors. Each KB of new code costs about 1.7 KB of that room (it is in both images), and
data that does not compress, like the gzipped settings page, costs twice its size. The
next feature will not fit.

## Goals

1. **The update image compressed with zopfli** (standard deflate, smaller than zlib's
   level 9): the `.bin.gz` and the settings page's gzip. Clocks unpack the update with
   the bootloader already on them (uzlib, a 32 KB dictionary); a host check unpacks each
   build's `.bin.gz` with that same decoder, the way the bootloader does, and compares
   it with `firmware.bin` (in the safety tests).
2. **Our own POSIX TZ rule parser:** the clock sets its time zone without the C
   library's `tzset`, whose rule parser links `sscanf` (about 5 KB). It fills the same
   time zone structure the core's own `configTime(offset)` fills, from a rule such as
   `EST5EDT4,M3.2.0,M11.1.0` or `<+0545>-5:45`. Unit tests compare it with newlib's
   result for every zone the settings page can produce and for custom rules.
3. **The settings page minified at build time:** its script by terser (pinned version,
   names kept at the top level so `onclick` handlers still find their functions), its
   CSS and markup without comments and indentation. `web/index.html` stays the readable
   source; the page's own tests keep reading it. The firmware build, the emulator build
   and the release all embed the same minified page. A build without Node fails with a
   clear message rather than embed a bigger page.
4. **Measured:** the room won by each goal recorded in PROGRESS and the footprint doc.

## Non-goals

- Moving the settings page out of the firmware: the clock stays self-contained (the
  owner, 2026-11-03).
- Removing features or the TFT_eSPI fonts 2, 4 and 6 (faces use them).

## Expected

From measurements on 2.1.0 (RESEARCH): zopfli about 8 KB of room, the parser about 8 KB,
the minified page about 7.5 KB: about 24 KB in all, three times the room left today.
