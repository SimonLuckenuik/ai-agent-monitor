#!/bin/sh
# Builds the emulator binary from a source tree, on the core's host emulation.
#   emulator/build.sh <project dir> <output binary>
# Runs inside the image's "deps" stage (/esp8266 and /libs); the Dockerfile calls it,
# and so can a container with the repo mounted, to iterate without rebuilding the image.
set -eu
root=$(cd "$1" && pwd)
out=$2
work=/tmp/smartclock-build
mkdir -p "$work/mock" "$work/sketch"
cp "$root/tests/face_preview/mock/TFT_eSPI.h" "$root/tests/face_preview/mock/TFT_eSPI.cpp" "$work/mock/"
python3 "$root/emulator/gen_headers.py" "$root" "$work/generated"
touch "$work/sketch/smartclock.ino"

cd /esp8266/tests/host
# Flags from platformio.ini that change behaviour, not just size.
make -j"$(nproc)" FORCE32=0 OPTZ=-O1 R=noexec \
  ULIBDIRS=/libs/ArduinoJson:/libs/PubSubClient \
  USERCXXSOURCES="$root/emulator/emulator.cpp $root/emulator/fw_main.cpp $root/src/faces.cpp \
                  $root/src/integrations.cpp $root/src/weather.cpp $work/mock/TFT_eSPI.cpp" \
  USERCSOURCES=/libs/TJpg_Decoder/src/tjpgd.c \
  USERCFLAGS="-I$root/src -I$root/include -I$work/mock -I$work/generated -I/libs/TFT_eSPI \
              -I/libs/TJpg_Decoder/src -DARDUINOJSON_USE_LONG_LONG=0 -DARDUINOJSON_USE_DOUBLE=0" \
  "$work/sketch/smartclock"
cp -L bin/smartclock/smartclock "$out"
echo "build.sh: $out"
