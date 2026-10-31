# Custom face placeholders get grouped names; the old ones are dropped

Date: 2026-10-29

Spec: [012](../../specs/012-face-model/SPEC.md)

## Decision

Placeholders move to short semantic groups (`time.hm`, `now.temp`, `day0.high`, `sun.up`
…). The old names are not kept as aliases ("drop old names"): faces saved on a clock
show them empty until sent again. The 1.5.0 CHANGELOG lists old to new.

## Why

Clear names matter more for agents writing faces than compatibility with the few faces
saved on one clock; aliases would cost flash and keep two vocabularies alive.
