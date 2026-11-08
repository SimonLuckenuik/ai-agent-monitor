#!/bin/sh
# Supervisor: plays the role of the clock's power supply. The firmware "reboots" by
# exiting (ESP.restart(), or the screen page's Power/Reset buttons); this loop starts
# it again with the matching reset reason. State lives in $EMU_DATA:
#   smartclock-littlefs3048KB (photos, faces, config.json), eeprom (password, boot counter),
#   rtc.bin (RTC user memory: kept across restarts, cleared on power-on).
set -u
EMU_DATA=${EMU_DATA:-/data}
export EMU_DATA
mkdir -p "$EMU_DATA"
cd "$EMU_DATA"

# docker stop: let the firmware exit cleanly, so the core writes the flash image back.
child=
stop() {
  [ -n "$child" ] && kill -TERM "$child" 2>/dev/null && wait "$child"
  exit 0
}
trap stop TERM INT

# Which firmware the clock runs: EMU_FIRMWARE=starter starts a clock "fresh from stock"
# on the starter (spec 048). Kept in $EMU_DATA/firmware, so an update the starter
# installs moves the clock to the full firmware for good, as on the chip (the starter
# leaves installed-update behind: emulator/flash.cpp). The full firmware then starts
# without this build's settings-page files, as a clock does after the starter.
[ -f firmware ] || echo "${EMU_FIRMWARE:-smartclock}" > firmware
reset=power
rm -f rtc.bin
while true; do
  rm -f next-reset
  if [ -f installed-update ]; then
    rm -f installed-update
    [ "$(cat firmware)" = starter ] && echo smartclock > firmware && echo "emulator: the starter installed an update: the full firmware from now on"
  fi
  bin=/emulator/$(cat firmware)
  [ "$(cat firmware)" = starter ] || [ "${EMU_FIRMWARE:-smartclock}" != starter ] || export EMU_SEED_FILES=0
  # -L 3048: the clock's 3 MB LittleFS volume (0x100000-0x3FA000); no SPIFFS.
  EMU_RESET=$reset "$bin" -P "$EMU_DATA" -L 3048 -S 0 "$@" &
  child=$!
  wait "$child"
  code=$?
  child=
  if [ -f next-reset ]; then
    reset=$(cat next-reset)
  elif [ "$code" -eq 0 ]; then
    reset=soft
  else
    reset=crash
    echo "emulator: firmware exited with $code, restarting as after a crash"
    # A panic writes its own record (memory.cpp); this is any other crash.
    printf '{"reason":"crash (exit code %s)","detail":"see the emulator log","at":%s,"trace":[]}\n' \
      "$code" "$(date +%s)" > last-crash.json
  fi
  [ "$reset" = power ] && rm -f rtc.bin
  sleep 1
done
