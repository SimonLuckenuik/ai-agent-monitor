# 028 Rain or snow within the hour

Status: Approved (2026-10-31)

## Problem

The clock knows today's chance of rain (`{day0.rain}`). It does not know whether rain or
snow will start in the next hour, which is what decides taking an umbrella or bringing
the laundry in. smalltv-pomodoro asks Open-Meteo for its 15-minute precipitation for the
coming hour and shows "rain 15 min".

## Goals

1. **The weather fetch asks for the next hour** in 15-minute steps:
   `minutely_15=rain,snowfall,precipitation_probability,weather_code&forecast_minutely_15=4`
   (checked 2026-10-31: Open-Meteo returns all four, rain in mm, snowfall in cm). It is
   read through the existing filter, with no more than 512 B added to `weatherHeap`.
2. **Placeholders** for faces:
   - `{now.rain_in}`, `{now.snow_in}`: minutes until it starts (`0` now, `15`, `30`, `45`),
     empty when none is expected within the hour;
   - `{now.rain_mm}`: the most rain expected in a quarter hour, in mm (in at imperial);
   - `{now.snow_cm}`: the same for snow, in cm (in at imperial).

   A quarter hour counts as rain with at least 0.1 mm of rain, or a rain weather code
   (51-67, 80-82, 95-99) at a 50% chance or more. It counts as snow with at least 0.1 cm
   of snow, or a snow code (71-77, 85-86) at 50% or more. Sleet counts as both.
3. **The built-in Weather today face** shows "Rain in 15 min" or "Snow in 15 min" ("Pluie
   dans 15 min", "Neige dans 15 min") in place of its condition line while one is due
   within the hour, and "Rain now" / "Snow now" while it falls.
4. **Documented** in the integrations API (placeholders) and the skill's face reference.
   An example face (`examples/faces/umbrella.json`) shows an umbrella or a snowflake with
   `when` on the two placeholders.
5. **Tested.** The emulator's fixed weather gains the 15-minute block. Integration cases
   cover dry, rain in 30 min, snow in 15 min, and raining now.

## Non-goals

- Alerts, notifications or MQTT for it (a face with `when` covers the display; MQTT state
  could add it later).
- Forecasts beyond the hour.
