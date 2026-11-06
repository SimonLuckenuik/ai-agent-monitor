# 046 Progress

## 2026-11-06

- Read how the setup network works (RESEARCH.md): up in every mode for the clock's whole
  life; WPA2 with the device password, open and with no sign-in in recovery; requests
  on it skip the DNS-rebinding guard. `updates-and-recovery.md` still calls it "an open
  AP" (true only in recovery): to correct with this spec.
- Options weighed (RESEARCH.md): recommended B, the AP off `apoff` minutes after the
  station link comes up, back when the link drops and at every boot.
- **Measured** a firmware-only prototype of B (setting `apoff`, `apOn()`,
  `serviceAp()`, `/status` `ap`; no settings page field), built on 2.3.1 and **not
  committed** (spec not approved):

  | Build | `firmware.bin` | `firmware.bin.gz` |
  |---|---|---|
  | 2.3.1 (`5a9fb81`) | 602,384 B | 427,332 B |
  | prototype | 602,880 B (+496) | 427,721 B (+389) |

  885 B of update room; 1 MB less two sectors less both images goes from 10,668 B to
  9,783 B, still two whole sectors (8,192 B) of room. The settings page's select adds
  perhaps 100-150 B more, in both images.
- Not done: the emulator and integration runs (the host core's WiFi mock does not model
  the AP; needs step 3 of the plan), and anything on the clock. The key unknown is
  whether `softAPdisconnect(true)` / `enableAP(true)` keep the station associated:
  only the clock can tell.
- Waiting for the owner: approval and the open questions in SPEC.md.
