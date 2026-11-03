# 035 Progress

- 2026-11-02: proposed, from the owner's idea: one Open-Meteo city search for the weather
  and the time zone.
- 2026-11-02: approved by the owner, who asked to rethink the layout: goal 5 proposes one
  Location field for both, with the time zone as an override.
- 2026-11-02: the owner asked for the pick list to show every country's matches
  (Saint-Hubert is in Quebec and in Belgium): goal 1 says so. One setting, `place`, in
  place of `lat`, `lon` and `geoname`; no "Follow the location": each pick sets both.
- 2026-11-02: built. The page's search sends the browser's request (no country filter
  unless typed); a pick sets `city`, `place` and the zone (spec 033's `tzUse`). Firmware:
  `place` checked (ranges, 60 characters) and exported after `city`, so a backup keeps
  the exact place. On the dev emulator: "Saint-Hubert" listed Belgium, France, Quebec
  and Mauritius; the Quebec pick set the place, America/Toronto and loaded the weather.
  Integration case `city_place`.
