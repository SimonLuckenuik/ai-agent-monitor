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

reset=power
rm -f rtc.bin
while true; do
  rm -f next-reset
  # -L 3048: the clock's 3 MB LittleFS volume (0x100000-0x3FA000); no SPIFFS.
  EMU_RESET=$reset /emulator/smartclock -P "$EMU_DATA" -L 3048 -S 0 "$@" &
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
  fi
  [ "$reset" = power ] && rm -f rtc.bin
  sleep 1
done
