# 035 One city search, for the weather and the time zone

Status: Approved (2026-11-02; the layout of goal 5 to confirm)

## Problem

The weather city is a free-text field ("Saint-Hubert, CA"). The clock geocodes it itself,
taking Open-Meteo's first match (`count=1`, with the country code when given), so a name
shared by several places in one country can land on the wrong one, and the owner sees
which only after the weather loads. The time zone is chosen apart (spec 033), from a
second lookup of the same name.

## Goals

1. **A city search on the settings page:** typing in the city field lists Open-Meteo
   geocoding's matches (https://open-meteo.com/en/docs/geocoding-api, `count=10`, in
   the clock's language): name, region (`admin1`), country, and population for telling
   them apart. Looked up only while typing (after 3 letters, debounced), from the browser.
2. **One choice sets both:** picking a match sets the weather place (its name with the
   country code, and its exact coordinates) and the time zone (its `timezone`, through
   spec 033's rule builder), in one step. "Weather only" leaves the time zone as it is.
3. **The clock uses the chosen place:** new settings `lat`, `lon` and `geoname`; when set
   for the current city, the clock skips its own geocoding (`geoFor` already marks
   which city the coordinates belong to). A city typed by hand, with no pick, still
   geocodes on the clock as today.
4. **Shown:** the page shows the chosen place ("Saint-Hubert, Quebec, CA · America/Toronto").
5. **One place, set once:** the city and the time zone stop being two fields in two cards.
   "Time & region" opens with a **Location** field (the search of goal 1); under it, one
   line says what it set: "Weather: Saint-Hubert, Quebec, CA · Time: America/Toronto
   (EST/EDT)". The time zone follows the location by default; **Change time zone…**
   opens today's zone list and custom POSIX rule, for a clock that should show another
   place's time than its weather (and "Follow the location" goes back). The Weather card
   keeps only its last update and Sync now; "Use the city's time zone" goes away.

## Non-goals

- Searching from the clock (the browser does it; the clock only stores the result).
- Places that are not in GeoNames (coordinates typed by hand: a later idea).
