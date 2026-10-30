#!/bin/sh
# Rebuild the emulator binary in the emu-dev container and swap it into the running
# emulator (the docker compose service) with the screen page, then reset the emulated clock to load it.
# From the repo root, once:
#   docker build -f emulator/Dockerfile --target deps -t smartclock-emu-deps .
#   docker run -d --name emu-dev -v "$PWD:/repo" smartclock-emu-deps sleep infinity
# then after each change:
#   sh emulator/dev-reload.sh
set -e
export MSYS_NO_PATHCONV=1   # Git Bash: keep container paths as they are
cd "$(dirname "$0")"
docker exec emu-dev sh /repo/emulator/build.sh /repo /tmp/smartclock | grep -E 'error|build.sh:'
docker cp emu-dev:/tmp/smartclock .smartclock.new
docker cp .smartclock.new emulator-smartclock-1:/emulator/smartclock.new
rm -f .smartclock.new
docker exec emulator-smartclock-1 mv /emulator/smartclock.new /emulator/smartclock
# The screen page, its presets and the example faces are read at each request.
docker cp screen.html emulator-smartclock-1:/emulator/screen.html
docker cp presets.json emulator-smartclock-1:/emulator/presets.json
docker cp ../examples/faces/. emulator-smartclock-1:/emulator/faces
curl -s -m 5 -X POST http://127.0.0.1:8080/reset >/dev/null || true
echo "dev-reload: emulator restarted with the new build"
