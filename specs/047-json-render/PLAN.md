# 047 Plan

A first plan for the recommendation in SPEC.md (option b). Nothing is built until the
owner approves the spec and answers its open questions.

## Step 1: catalog and compiler (host only)

1. `integrations/json-render/package.json`: pin `@json-render/core` and `zod`.
2. `catalog.mjs`: the components in requirement 1, with zod props and descriptions
   written for a model (the fonts, ASCII only, 240×240, `maxChars`).
3. `fonts.mjs`: glyph widths for fonts 2 and 4, taken from TFT_eSPI's tables in the
   build. Add the big font's cell and the digit fonts' advances.
4. `compile.mjs`: `validateSpec()` first, then the layout and the value translation
   (from `prototype/translate.mjs`), the limits, and `--push`.
5. `prompt.mjs`: prints `catalog.prompt()`.
6. Tests (`node --test`): each component, each dynamic value, the errors, and a
   golden spec → face test. Each output goes through `clock.py check`.
7. Integration: a case in `tests/integration/run_integration_tests.py` that compiles
   `integrations/json-render/examples/*.json` and captures each one.
8. Docs: `integrations/json-render/README.md`, a line in integrations-api.md, the
   CHANGELOG, and CLAUDE.md "Things to keep in step" (a new item type or placeholder
   group means a catalog change).

Checked by: unit tests, `clock.py check`, the emulator screenshots. No firmware, so no
release is needed. It ships with the next release's notes, or on its own.

## Step 2 (optional): agents

1. OpenClaw bridge: `clock.face.render {name, spec, show}` imports the compiler
   (branch `openclaw-node`), with its tests and the OpenClaw skill's command table.
2. Claude Code skill: a short "json-render" section pointing to the catalog prompt.
   Bump the plugin version.
