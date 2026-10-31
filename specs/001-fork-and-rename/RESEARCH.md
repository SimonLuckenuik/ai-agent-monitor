# Research

- The clock: an ESP8266 (ESP-12F, 4 MB) with a 240×240 ST7789 IPS panel; its USB port is
  power only. Identified on 2026-10-05 as a GeekMagic SmallTV look-alike sold as "Smart
  Weather Clock" SD PRO; the stock firmware cannot be reinstalled over WiFi. Details:
  `docs/specifications/hardware.md`, `device-research.md`, `stock-firmware-analysis.md`.
- Several clocks in one room advertised the same SSID: the chip-id suffix fixes that.
- Face ids 0-6 are an API (stock theme numbers used by geekmagic-hacs), so ids are reused,
  never renumbered without a migration.
