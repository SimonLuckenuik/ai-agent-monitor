# 003 Custom faces v2: dynamic rendering

Status: Done in 1.4.0 (written after the fact, 2026-10-30)

## Problem

Upstream custom faces were static layouts with a few placeholders. The owner wants
agents (Claude) to draw what makes sense: icons inspired by well-known formats, pixel
art, gradients, animation, vector shapes, all built on the existing renderer.

## Goals

1. Expressions in number and colour fields; conditions on any item.
2. SVG path data, so Lucide or Material Symbols icons can be used as they are.
3. Gradients; wrapped text; animation through `fps` and a millisecond placeholder.
4. Fit the flash budget (no frame buffer; ~24 KB of heap).

## Requirements

1. Any string field takes placeholders; number and colour fields take expressions
   (`+ - * / %`, comparisons, `&& || !`, `?:`, `sin cos min max abs round`).
2. Every item takes `show`. Faces take `fps` (1 to 20).
3. `path`: all SVG commands, arcs included, from a viewBox; anti-aliased fill and round
   stroke.
4. Linear gradients (2 to 4 stops, any angle) for the background, rects, circles, paths.
5. `text` wraps with `w`, `lines`, `lh`. `{name|fallback}`.
6. Redraw only items whose resolved content changed; a change of place redraws the face.

## Non-goals

- A full-screen frame buffer (115 KB). Base64 images in faces.
