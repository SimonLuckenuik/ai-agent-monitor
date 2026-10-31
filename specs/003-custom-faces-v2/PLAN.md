# Plan

1. Expression parser (`parseNum()` instead of libc `strtof`, to save ~9 KB).
2. Path parser and scanline filler with coverage anti-aliasing; stroker.
3. Gradient paint (`Paint`, `span()`).
4. Wrapping; fallbacks; per-item content and geometry hashes.
5. Unit tests (expressions, paths, colours); face preview check that incremental equals
   full redraw.
6. Spec in `docs/specifications/integrations-api.md`; the plugin's skill (spec 006).
