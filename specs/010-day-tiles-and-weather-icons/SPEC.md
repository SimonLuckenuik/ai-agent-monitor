# 010 Day tiles and Weather Icons

Status: Done in 1.4.0 (written after the fact, 2026-10-30)

## Problem

The owner wants one forecast day per screen in the Big tiles' style, and the icons of
Erik Flowers' Weather Icons, chosen from the WMO code with the companion app's
TypeScript table (day and night variants).

## Requirements

1. Faces 10-13: Weather today, Weather forecast - Day 1, Day 2, Day 3.
2. The big number is the feels-like temperature (what matters to go out); unlabelled.
3. A facts line: W wind | H humidity | two sun times, with drawn separators. Today: the
   next two sun events (before sunrise: sunrise, sunset; after: sunset, tomorrow's
   sunrise; after sunset: tomorrow's two). The current temperature under the big number
   on Weather today; low, high and precipitation at the bottom.
4. Icons: the SVGs turned into polygons drawn at any size; `{now.icon}` is a name such as
   `wi-day-rain`. Do not compose icons if the compact data is small enough.
