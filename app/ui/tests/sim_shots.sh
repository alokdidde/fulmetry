#!/usr/bin/env bash
# Simulate: the living-room drive test, under Xvfb + Mesa lavapipe, on MuJoCo when the binary has it.
#   app/ui/tests/sim_shots.sh <fulmetry_workbench> <output dir>
# Writes 1440 x 900 screenshots (each kept under 400 KB):
#   on-the-rug.png   the scripted drive 2.6 s in, crossing the rug
#   drive-done.png   the drive finished: what was measured
#   into-wall.png    driven straight at the brick wall by hand: the stall it measures
# then holds W in a real X11 window and checks the rover drives by hand, and presses Esc to stop.
# Needs: xvfb, mesa-vulkan-drivers, xdotool, imagemagick.
set -euo pipefail
BIN=${1:?fulmetry_workbench binary}
OUT=${2:?output dir}
mkdir -p "$OUT"
LOGS=${SIM_LOGS:-$(mktemp -d)}
mkdir -p "$LOGS"
DISP=${SHOTS_DISPLAY:-:93}
export VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}
export RAVEN_WINDOW_SYSTEM=x11 RAVEN_LIVE_RELOAD=0
export FULMETRY_CONFIG_DIR=$LOGS/config  # never the person's own settings
Xvfb "$DISP" -screen 0 1440x900x24 -nolisten tcp -noreset >/dev/null 2>&1 &
XVFB=$!
trap 'kill $XVFB 2>/dev/null || true' EXIT
sleep 1
export DISPLAY=$DISP

shrink() {
  convert "$1" -strip -define png:compression-level=9 PNG24:"$1"
  if [ "$(stat -c %s "$1")" -gt 390000 ]; then convert "$1" -strip -dither FloydSteinberg -colors 256 "$1"; fi
}
shot() {  # shot <name> [workbench args...]
  local name=$1; shift
  "$BIN" --project sample --frames 40 --strict --shot "$OUT/$name.png" --simulate "$@" >"$LOGS/$name.log" 2>&1 || { cat "$LOGS/$name.log"; exit 1; }
  shrink "$OUT/$name.png"
  echo "$name.png: $(stat -c %s "$OUT/$name.png") bytes"
  grep -E '^simulate: (MuJoCo|kinematic|t )|^finding' "$LOGS/$name.log"
}

shot on-the-rug --sim-seconds 2.6
grep -q '^simulate: t 2.60 s.*on Rug' "$LOGS/on-the-rug.log" || { echo "not on the rug at 2.6 s"; exit 1; }
shot drive-done --sim-seconds 7.3
grep -q '^finding surface' "$LOGS/drive-done.log" || { echo "the rug crossing was not measured"; exit 1; }
shot into-wall --sim-drive 1,0 --sim-seconds 7
grep -q '^finding stall1 (bad): Stalled at full throttle against the brick wall' "$LOGS/into-wall.log" ||
  { echo "the stall at the wall was not measured"; exit 1; }

# Keys in a real window: hold W, then Esc stops the simulation.
"$BIN" --project sample --simulate --run-seconds 40 >"$LOGS/keys.log" 2>&1 &
APP=$!
for _ in $(seq 1 60); do grep -q '^workbench ready' "$LOGS/keys.log" && break; sleep 0.5; done
sleep 4
WIN=$(xdotool search --name '^Fulmetry$' | head -1)
xdotool windowactivate --sync "$WIN" 2>/dev/null || xdotool windowfocus --sync "$WIN"
xdotool keydown w; sleep 4; xdotool keyup w
sleep 1
xdotool key Escape
sleep 2
kill "$APP" 2>/dev/null || true
wait "$APP" 2>/dev/null || true
grep -q '^simulate: driving by hand' "$LOGS/keys.log" || { echo "W did not drive"; cat "$LOGS/keys.log"; exit 1; }
grep -q '^simulate off' "$LOGS/keys.log" || { echo "Esc did not stop the simulation"; cat "$LOGS/keys.log"; exit 1; }
echo "keys: W drove by hand, Esc stopped"
