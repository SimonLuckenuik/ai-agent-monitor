# Issue records

An issue record is a dated defect report or investigation. Its body is appended to as the
investigation moves, never rewritten. The record itself is authoritative. Write one for
every defect found (by the tests, the emulator, the clock or the owner), and move it
here from Open to Closed when it is fixed or explained.

## Open

- [2026-10-31 Sending a face fails while a big face is loaded](2026-10-31-face-upload-needs-the-loaded-face-memory.md)
- [2026-10-25 After a reboot the clock showed the old setup network name](2026-10-25-setup-network-kept-old-name-after-reboot.md)
- [2026-10-29 A new face variable is dropped silently when the table is full](2026-10-29-new-variable-dropped-when-table-full.md)

## Closed/explained

- [2026-10-31 Animated custom faces restarted the clock (stack overflow in the expression parser): fixed in 1.9.0, confirmed on the clock](2026-10-31-animated-faces-restart-the-clock.md)
- [2026-10-30 `{time.ms}` was not tied to the clock's second: fixed in 1.8.0](2026-10-30-time-ms-not-tied-to-the-second.md)
- [2026-10-30 Lines vanished in off-screen repaints (TFT_eSPI drawWedgeLine and the viewport datum): worked around in 1.8.0](2026-10-30-drawwedgeline-ignores-sprite-datum.md)
- [2026-10-29 The night palette showed empty in the emulator: dev-build data](2026-10-29-night-palette-empty-in-the-emulator.md)
- [2026-10-29 Countdown face flickered every second; animated sprites blinked: fixed in 1.5.0](2026-10-29-countdown-and-sprite-flicker.md)
- [2026-10-25 Big date reset the clock (software watchdog): fixed in 1.4.0](2026-10-25-big-date-tripped-the-watchdog.md)
- [2026-10-05 The setup AP is open and every endpoint is unauthenticated: fixed in 0.1.2 with a device password](2026-10-05-ap-and-ota-unauthenticated.md)
- [2026-10-05 `smartclock.local` did not resolve from Windows `ping`: explained, different subnet](2026-10-05-smartclock-local-not-resolving-on-windows.md)
- [2026-10-05 The clock is an hour behind during daylight saving: fixed in 0.1.1-dev, rule active on the device](2026-10-05-clock-one-hour-behind-during-dst.md)
