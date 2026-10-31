# The Flip face and font 8 leave the build

Date: 2026-10-26

Spec: [007](../../specs/007-big-tiles-and-line-font/SPEC.md)

## Decision

Classic and Simple draw their time in the Big tiles' line font; the Flip face and
TFT_eSPI font 8 are removed, freeing 3.6 KB of flash. Face id 6 stays reserved (ids are
an API); custom faces asking for font 8 get font 6.

## Alternatives

Keeping Flip with the line font: the owner preferred Classic with the line font and no
Flip. A flip clock came back on 2026-10-30 as an example custom face (no flash cost).
