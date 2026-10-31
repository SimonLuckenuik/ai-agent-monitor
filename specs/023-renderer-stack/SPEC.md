# 023 Custom faces within the 4 KB stack

Status: Done (approved 2026-10-31: the owner asked to fix the stack overflow; released in
1.9.0, confirmed on the clock the same day)

## Problem

Retro-runner, flip-clock-wide and night-lake restart the clock
([issue](../../docs/issues/2026-10-31-animated-faces-restart-the-clock.md)). The emulator
with the clock's memory ([spec 022](../022-emulator-device-memory/SPEC.md)) shows a stack
overflow; the chip's own frame sizes (`-fstack-usage`) agree: the expression parser
recursed through eight functions per level of parentheses (about 500 B a level), under a
`renderItems` frame of 1,008 B and a `drawItem` of 448 B, about 3.8 KB of the 4 KB stack.

## Goals

1. Expressions take the same stack however deeply they nest.
2. The custom-face drawing path leaves at least 1 KB of the stack free for every example
   face, measured on the chip's frames.
3. Every example face runs under the emulator's clock memory (the integration case
   `example_faces_memory` with no known crashes).
4. Expressions mean what they meant: the unit tests pass unchanged, plus checks for
   deep nesting.

## Non-goals

- Crash records and crash-loop protection: [spec 021](../021-crash-report/SPEC.md).
