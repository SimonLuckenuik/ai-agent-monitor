# Built-in faces stay in code

Date: 2026-10-29

Spec: [012](../../specs/012-face-model/SPEC.md)

## Question

Could the built-in faces be JSON custom faces, to shrink the firmware?

## Finding

They are about 11 KB of code; as JSON plus the generic renderer's extra paths they would
cost about the same or more, and draw slower. They stay in code; JSON versions of the Big
tiles exist as examples (big-time, big-date, big-weather) to show it can be done.
