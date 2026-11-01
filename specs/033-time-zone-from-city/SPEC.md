# 033 Every time zone, and the city's own

Status: Approved (2026-11-01; the owner agreed, loaded only on demand, from a source that
stays up to date)

## Problem

The settings page offers 13 time zones (`<select id="tz">`) and a field for a POSIX rule.
An owner elsewhere must write a rule such as `EST5EDT,M3.2.0,M11.1.0` by hand, and the
clock's time zone and its weather city are set apart, so they can disagree without
anyone noticing.

## Goals

1. **Every zone, from the browser's own time zone database:** the settings page lists the
   zones `Intl.supportedValuesOf("timeZone")` gives (about 420), and turns the chosen one
   into the POSIX rule the clock takes, from `Intl.DateTimeFormat`'s offsets: the standard
   and summer offsets, and the dates daylight saving starts and ends in the coming year,
   as "nth weekday of the month" rules. The browser's database is kept current by its
   own updates, so the repo holds no table to maintain.
2. **Loaded only on demand:** the picker gains "More time zones…". Choosing it lists every
   zone by region, searchable by typing. Nothing is computed or fetched when the page
   opens.
3. **The city's zone:** a "Use the city's time zone" button looks the city up with
   Open-Meteo's geocoding (from the browser; it answers with the IANA zone, from
   GeoNames, kept current), builds its rule and selects it. When the city changes and
   its zone differs from the clock's, the page says so and offers the button.
4. **Checked:** a rule built for a zone gives the same local times as the browser for
   every hour of the coming year (the page checks before it saves, and refuses a zone it
   cannot express, such as one whose rule changes year to year).
5. **The firmware is unchanged:** the setting stays a POSIX rule (`tz`); the 13 zones
   stay in the page for use without the Internet.

## Non-goals

- The clock changing its own time zone without the owner choosing.
- Zones in the firmware.
- A table of rules in the repo (the first version of this spec; the browser's database
  replaced it, 2026-11-01).
