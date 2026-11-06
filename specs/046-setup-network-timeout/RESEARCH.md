# 046 Research: the setup network today, and turning it off

Read on 2026-11-06 from `faces-v2` at 2.3.1 (`5a9fb81`), the ESP8266 Arduino core in
`~/.platformio/packages/framework-arduinoespressif8266`, and
`docs/specifications/updates-and-recovery.md`.

## When the clock's own access point is up today

**Always.** `setup()` calls `startAP()` before anything else (`src/main.cpp`, "WiFi"):

```cpp
WiFi.persistent(false);
WiFi.mode(WIFI_AP_STA);   // "Keep recovery accessible even when the router disappears."
WiFi.softAPConfig(192.168.4.1, 192.168.4.1, 255.255.255.0);
restartAP();              // WiFi.softAP(setupSsid(), devicePassword), open in recovery
dns.start(53, "*", WiFi.softAPIP());   // captive DNS: every name is 192.168.4.1
```

Nothing ever calls `softAPdisconnect()` or changes the mode again (`grep`: no use in
`src/`). The loop calls `dns.processNextRequest()` on every pass, in every mode.

| State | AP | Security | Sign-in on the web server |
|---|---|---|---|
| Normal, joined to the router (`MODE_RUN`) | up, alongside the station | WPA2-PSK, key = the device password | needed (session cookie) |
| Normal, no router (`MODE_AP`, setup screen) | up; the station searches 30 s every 5 min so AP clients are not dropped by channel hops | WPA2-PSK, device password | needed |
| Recovery (3 quick power cycles, a crash in the first 30 s, a failed mount) | up until **Restart** | **open** (`restartAP()`: "open in recovery, so recovery is always reachable") | **none**: `requestAuthorized()` returns true in recovery, so anyone joined can upload firmware |
| Safe mode (5 crashes) | up | WPA2 | needed |

`docs/specifications/updates-and-recovery.md` still says "The firmware starts an open
AP"; that is true only in recovery (the setup network has had WPA2 since 0.1.2). Its
"Recovery access" section is to be corrected with this spec.

The device password: 10 characters from a 31-letter alphabet (~49 bits) from the
hardware RNG on first boot, in the EEPROM sector at 0xFC0; shown on the setup screen,
the recovery screen, and for 60 s on "Show on clock". The same secret is the WPA2 key
and the settings sign-in (5 tries, then a minute's lockout).

### What a client of the AP reaches

- The whole web server: the same routes as on the LAN, with sign-in where the LAN needs
  it. Unauthenticated routes (`/status`, `/version`, `/config`, the login page,
  `/api/auth/reveal`) answer there too.
- `hostAllowed()` (spec 029's DNS-rebinding guard) is skipped for requests arriving on the
  AP address, and in recovery, because the captive portal sends every name to the clock.
- Nothing beyond the clock: lwIP is built without `LWIP_FEATURES`
  (`platformio.ini`), so there is no IP forwarding or NAPT to the home network.

So the AP is a second way in that **does not go through the owner's VLAN, firewall or
router at all**: anyone within radio range who has (or guesses, or cracks) the device
password talks to the clock directly. In recovery anyone within range does, with no
password.

### Other observations

- **The SDK's saved AP does not start before our code** (checked 2026-11-06, after the
  first draft suggested it might). `WiFi.persistent(false)` means our AP settings are
  never written to the SDK's configuration sector, so whatever the stock firmware saved
  there ("Smart Weather Clock", presumably AP_STA) is still there. But the Arduino core
  we build with (framework 3.30102.0, core 3.1.2) calls `__disableWiFiAtBootTime()` in
  `user_init()` (`cores/esp8266/core_esp8266_main.cpp`): `wifi_set_opmode_current(0)`
  and forced modem sleep, *"Starting from arduino core v3: wifi is disabled at boot
  time"*, before the SDK starts the radio. Only our `WiFi.mode()` / `softAP()` wake it.
  So nothing saved is broadcast at boot, and writing a clean saved configuration once is
  not needed. This does not explain the open issue
  [2026-10-25 setup network kept its old name](../../docs/issues/2026-10-25-setup-network-kept-old-name-after-reboot.md),
  which stays open.
- **No 802.11w (protected management frames)** in the NONOS SDK the core uses: anyone in
  range can send deauthentication frames and knock the station off the router. This
  matters for any design where "the station link is down" reopens the AP (see options).
- `softAPdisconnect(true)` (core, `ESP8266WiFiAP.cpp`) clears the soft-AP configuration
  with `wifi_softap_set_config_current()` (not saved, since `persistent(false)`), then
  `enableAP(false)`, which calls `mode(STA)`. Bringing it back is `enableAP(true)` (or
  `mode(WIFI_AP_STA)`), `softAPConfig()`, `softAP()`. `softAPgetStationNum()` returns
  how many clients are joined.
- **Does turning the AP off drop the station link?** Neither the NONOS SDK API reference
  nor the Arduino core documents it either way (searched 2026-11-06: Espressif's API
  reference, the core's docs and issues, esp8266.com). The best evidence is **ESPHome**,
  which runs on a great many ESP8266 devices and does exactly this switch: when its
  station connects while the fallback AP is up, `check_connecting_finished()` calls
  `disable_ap_()`, which calls `wifi_mode_({}, false)`; on ESP8266 that is
  `wifi_set_opmode_current(STATION_MODE)` with no reconnect or DHCP restart for the
  station (it stops the DHCP client only when the *station* is turned off). Our
  `softAPdisconnect(true)` ends in the same `wifi_set_opmode_current()` (through
  `enableAP(false)` and `mode()`, which also waits up to 1 s for the mode to take).
  Old reports of trouble are about other things: `softAPdisconnect` returning false and
  leaving the AP up on core 2.1.0 (esp8266/Arduino#3793), a station dropping every few
  minutes *in* AP_STA on core 2.3.0 (#3054), errors going from AP to STA and then
  calling `begin()` (esp8266.com, SDK 1.5.1). So: very likely fine, to confirm on the
  clock (the emulator's WiFi is a mock and always "connected"). The implementation
  should check the return value and that `WiFi.getMode()` is `WIFI_STA` afterwards.
- **Turning the AP back on while the station is connected never happens** in the chosen
  design: the AP returns when the link is down, or at boot. (Had it been needed: in
  AP_STA the AP must use the station's channel, so it starts on whatever channel the
  router uses.)

## Options weighed

| Option | Exposure | Recovery without opening the case | Verdict |
|---|---|---|---|
| A. Status quo: AP always up | WPA2 AP within radio range for the clock's life; open, no sign-in, in recovery until Restart | always | the baseline |
| B. **AP off N minutes after the station link comes up; back at once when the link drops; back at every boot** | AP for the first N minutes after each boot, and whenever the router is gone | the router gone or changed: the link drops, the AP is back with the setup screen, as today | **recommended** |
| C. AP off N minutes after boot, whatever the link (the owner's first idea, literally) | as B | a clock that never joined, or whose router changed, has no AP after N minutes: a power cycle reopens it for N minutes | loses to B: a clock with no network would also have no AP; the setup screen would point at a network that is not there |
| D. AP only while there is no link (N = 0) | smallest | as B | first setup breaks: joining the router closes the AP under the browser before it can show the new address. Kept as "the smallest N", not as 0 |
| E. As B, but the AP returns only on a boot, never on a link drop | smaller than B: a deauth attack cannot reopen it | router changed: the owner power cycles once; the screen says so | strongest; the owner chose B (both a link drop and a boot reopen it) |
| F. Hide the SSID (`softAP(..., hidden)`) | none really: the SSID shows in probe responses and to any sniffer | always | security by obscurity; not worth the confusion |
| G. A LAN button / API to reopen the AP for N minutes | adds a way to open it, signed in only | adds one | not wanted (the owner, 2026-11-06): reaching the settings page already means a network |

**Why B is not weaker than today when it reopens the AP.** A deauth attacker in range
can force the station off the router and so bring the AP back, but the AP is still
WPA2 with the device password: the attacker is back to exactly today's position, never
better. Everyone else gets a clock whose AP is off almost all of the time.

**Why "back at every boot" is safe as the recovery gesture.** A power cycle needs
physical access; anyone with that already has the clock's screen (which shows the
password on request) and can do the three-cycle recovery gesture anyway.

**Keep the AP while a client is joined.** If the timer runs out while someone is on the
setup network (setting up, uploading firmware over the AP), turning it off would cut
them off mid-task. Deferring the switch while `softAPgetStationNum() > 0` costs nothing
in security: a client has to know the password to be joined. An update in progress
already stops the loop (`otaFiles || otaStarted`).

### Recovery

Recovery's AP is open and its web server needs no sign-in, so that a lost or corrupted
password never locks the owner out. Turning it off after N minutes would undo that
purpose when the router is gone (recovery is then the only way in). Two separate
hardenings are possible and are listed as open questions rather than goals:

- WPA2 on the recovery AP too, with the device password that the recovery screen
  already shows (`drawRecoveryScreen()`); `loadPassword()` regenerates a missing or
  invalid one, so there is always one on the screen. About 0 bytes of code.
- Leaving recovery after a long idle time (no client, no request for, say, 30 minutes)
  with a normal restart. Risky if the normal boot is what is broken (a crash loop
  then re-enters recovery after a few boots anyway).

## Costs on the ESP8266

- **Flash**: the switch is a few lines in the loop, one setting (load, save, JSON, the
  settings page), and `softAPdisconnect` / `softAPgetStationNum` from the core. See the
  measurement in PROGRESS.md.
- **Heap**: turning the AP off frees what the SDK holds for it (the DHCP server's lease
  table, the soft-AP's buffers) and the DNSServer's UDP socket. Expected around 1-2 KB;
  not measurable in the emulator (mock WiFi). To measure on the clock: `/status`'s
  `heap` with the AP on and off.
- **Radio**: none to speak of. In AP_STA the AP is tied to the router's channel; STA alone
  is no different for the clock's traffic. Beacons every 100 ms stop.
- **Time**: one comparison per loop pass.
- **RAM for the setting**: one `uint16_t` in `Config`.

## Links

- ESP8266 Arduino core soft-AP API: `libraries/ESP8266WiFi/src/ESP8266WiFiAP.cpp`
  (`softAPdisconnect`, `softAPgetStationNum`), `ESP8266WiFiGeneric.cpp` (`enableAP`, `mode`).
- ESPHome's Wi-Fi component: `esphome/components/wifi/wifi_component.cpp`
  (`check_connecting_finished`, `disable_ap_`) and `wifi_component_esp8266.cpp`
  (`wifi_mode_`): https://github.com/esphome/esphome ,
  https://api-docs-beta.esphome.io/wifi__component__esp8266_8cpp_source
- Arduino core issues: https://github.com/esp8266/Arduino/issues/3793 ,
  https://github.com/esp8266/Arduino/issues/3054 ; core docs on `persistent()`:
  https://esp8266-arduino.readthedocs.io/en/stable/esp8266wifi/generic-class.html
- Spec 029 (network guards: DNS rebinding, WPA downgrade), spec 018 (first run on the
  clock), `docs/specifications/updates-and-recovery.md`.
