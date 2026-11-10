# 049 Research

## The gallery today (2026-11-10)

- `examples/faces/marketplace.json`: `{"format": 1, "repo", "about", "faces": [...]}`, 74
  entries; each has `name`, `title`, `description`, `category`, `author`, `source`,
  `license`, `size`, `variables`, `firmware`, `json`, `preview`, `path`, `previewPath`.
- `scripts/gen_marketplace.py` writes `"firmware": FIRMWARE`, the constant `"2.0.0"`
  ("every face here is tested on the emulator of this version or later").
- Readers: the settings page (`loadGallery()` in `web/index.html`: fetches the file from
  `main` on GitHub, installs with `POST /api/faces`, no version check); `clock.py gallery`
  and `install` (no version check). The gallery came in 2.1.0 (spec 026): no clock before
  2.1.0 reads it.

## Faces already newer than 2.0.0

From the CHANGELOG's 2.1.0 section (spec 031: digit fonts; rain and snow placeholders)
and a search of the face files:

| Feature | Since | Faces |
|---|---|---|
| `"font": "bebas"` | 2.1.0 | brutal, fitness, metro, paper, pill, stripes |
| `"font": "outfit"` | 2.1.0 | commit-streak, kpi, night-light, orbit, power-cell, sun-arc, synthwave, weather-poster, astronaut, bento, catnap, dune, greenhouse, koi, modular, rings, tide, vinyl, window, winter |
| `now.rain_in` and the like | 2.1.0 | umbrella |
| `msg.kind`, `msg.shown`, `msg.anim` | 2.2.0 | none |

All within the gallery's own floor (2.1.0): today's faces would all get 2.1.0, which is
right, and no existing settings page would refuse any of them. The table matters for what
comes next, json-render first. The API reference does not mark versions (only json-render,
"2.7.0 on"), so the CHANGELOG is the source.

## What an old clock does with a spec

Before 2.7.0 a spec is not a face: `storeFace()` answers 400 "Expected JSON with an
"items" array" and stores nothing. Safe; nothing is deleted (a spec has a body).

## Why a new key for specs

Settings pages from 2.1.0 to 2.6.0 iterate `g.faces` and install every entry. They read
the file from `main`, so whatever goes into `faces` reaches them, and they cannot be
changed. Entries under a key they do not read are invisible to them. The format number
stays 1: the change only adds a key.

## The catalog for the new faces

The components the four faces need are all in `integrations/json-render/catalog.json`:
Box, Text, Heading, Card, Table, List and ListItem, StatusLine, Badge, KeyValue,
ProgressBar, Metric, Timeline, Clock, WeatherIcon, Ring; live values through `$state` and
`$template`. Limits: 6 KB, 96 elements, 8 levels, a compiled face of 64 items and 8 KB.
