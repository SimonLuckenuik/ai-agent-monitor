# After a reboot the clock showed the old setup network name

Status: Open (not seen again; not explained)

Date: 2026-10-25

## Summary

After flashing a 1.4.0-dev build with the new line font, a reboot left the clock in
recovery mode advertising "Smart Weather Clock", although the setup network had been
renamed; joining it failed ("cannot connect to this network"). Another reboot brought it
back normally.

## Notes

- The stock firmware's name could come from the ESP8266's saved soft-AP configuration
  (SDK flash area) rather than our code, if the AP started before our name was applied.
- To check on the next device session (spec 018): the boot order of `WiFi.softAP()` and
  `WiFi.persistent(false)`.
