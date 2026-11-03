# 038 A fifth forecast day, and its tile

Status: Done in 2.1.1 (2026-11-03)

## Problem

The weather asks Open-Meteo for 4 days (today and three more), so the Day tiles stop at
Day 3. The owner wants a Day 4 tile; one more day costs the request a few values.

## Goals

1. `forecast_days=5`: `FORECAST_DAYS` 5, `day4.*` placeholders for custom faces.
2. A **Weather forecast - Day 4** tile, drawn like the other Day tiles, on face id 6
   (Flip's, retired in 1.x and unused since). It follows Day 3 in the rotation: in the
   default order, and in a saved order that lacks it. Settings from before (`facesv` < 5)
   turn it on when Day 3 is on.
3. Nothing else moves: the Forecast face and a `repeat` over `"days"` keep four entries,
   since their faces are laid out in four 58-pixel columns.

## Non-goals

- A fifth column on the Forecast face (it would not fit at the same size).
