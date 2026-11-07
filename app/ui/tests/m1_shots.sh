#!/usr/bin/env bash
# M1 sign-off: the workbench with the live Wicked stage, under Xvfb + Mesa lavapipe.
#   app/ui/tests/m1_shots.sh <fulmetry_workbench built with Wicked and Raven> <output dir>
# Writes 1440 x 900 screenshots (each kept under 400 KB):
#   rover.png          the default view of the desk rover
#   camera-head.png    the camera head selected: red outline and part card
#   exploded.png       the rover exploded (Actions > Explode)
#   all-objects.png    the All objects tab: the rover and the remote side by side
#   actions-menu.png   the Actions menu open over the rover
# then runs the interaction check: real X11 drags orbit the camera, a click on the stage selects the part under
# the cursor (the model's selection changes), the wheel zooms. Prints the stage frame times.
# Needs: xvfb, mesa-vulkan-drivers, xdotool, imagemagick. FULMETRY_WICKED_SHADER_CACHE speeds up the first render.
set -euo pipefail
BIN=${1:?fulmetry_workbench binary}
OUT=${2:?output dir}
mkdir -p "$OUT"
LOGS=${M1_LOGS:-$(mktemp -d)}
mkdir -p "$LOGS"
DISP=${SHOTS_DISPLAY:-:95}
export VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}
export RAVEN_WINDOW_SYSTEM=x11 RAVEN_LIVE_RELOAD=0
Xvfb "$DISP" -screen 0 1440x900x24 -nolisten tcp -noreset >/dev/null 2>&1 &
XVFB=$!
trap 'kill $XVFB 2>/dev/null || true' EXIT
sleep 1
export DISPLAY=$DISP

shrink() {  # lossless when it fits, else 256 colours; always under 400 KB
  convert "$1" -strip -define png:compression-level=9 PNG24:"$1"
  if [ "$(stat -c %s "$1")" -gt 390000 ]; then convert "$1" -strip -dither FloydSteinberg -colors 256 "$1"; fi
}

shot() {  # shot <name> [workbench args...]
  local name=$1; shift
  "$BIN" --project sample --frames 30 --strict --shot "$OUT/$name.png" "$@" >"$LOGS/$name.log" 2>&1 || { cat "$LOGS/$name.log"; exit 1; }
  grep -q "^stage: Wicked" "$LOGS/$name.log" || { echo "FAIL: $name: no live stage"; cat "$LOGS/$name.log"; exit 1; }
  shrink "$OUT/$name.png"
  echo "$name.png: $(stat -c %s "$OUT/$name.png") bytes; $(grep '^stage frame times' "$LOGS/$name.log")"
}

shot rover
shot camera-head --select part:rover/camera-head
shot exploded --explode
shot all-objects --tab 0
shot actions-menu --open menu

# ---- interaction: X11 drag, wheel and click on the stage (positions come from the app's own report)
LOG="$LOGS/interaction.log"
"$BIN" --project sample --run-seconds 240 >"$LOG" 2>&1 &
APP=$!
settled() { grep -c "^stage-settled" "$LOG" || true; }
wait_settled() {  # wait_settled <count>
  for _ in $(seq 1 600); do [ "$(settled)" -ge "$1" ] && return 0; kill -0 $APP 2>/dev/null || break; sleep 0.2; done
  echo "FAIL: the stage did not settle ($1)"; cat "$LOG"; exit 1
}
wait_settled 1
WIN=$(xdotool search --name "^Fulmetry$" | head -1)
xdotool windowfocus --sync "$WIN" || true
camera() { grep "^stage-settled" "$LOG" | tail -1; }
before=$(camera)
# orbit: left-drag across the empty stage above the rover
xdotool mousemove --window "$WIN" 560 190 mousedown 1
for x in 580 610 640 670 700; do xdotool mousemove --window "$WIN" $x 200; sleep 0.3; done
xdotool mouseup 1
wait_settled 2
after=$(camera)
[ "$before" != "$after" ] || { echo "FAIL: orbit did not move the camera"; exit 1; }
echo "orbit: $before -> $after"
# click the camera head where the app says it is now
at=$(grep "^part-at part:rover/camera-head" "$LOG" | tail -1 | awk '{print $3, $4}')
[ -n "$at" ] || { echo "FAIL: no camera head position"; exit 1; }
xdotool mousemove --window "$WIN" $at click 1
wait_settled 3
# zoom with the wheel
xdotool mousemove --window "$WIN" 420 450 click 4 click 4
wait_settled 4
import -window root "$OUT/interaction.png"
shrink "$OUT/interaction.png"
kill -9 $APP 2>/dev/null || true  # Raven keeps running on SIGTERM
wait $APP 2>/dev/null || true
grep -E "^(stage-|selection:|part-at)" "$LOG"
grep -q "^stage-click .* -> part:rover/camera-head" "$LOG" || { echo "FAIL: click did not pick the camera head"; exit 1; }
grep -q "^selection: part:rover/camera-head" "$LOG" || { echo "FAIL: the model's selection did not change"; exit 1; }
zoomed=$(camera)
[ "$after" != "$zoomed" ] || { echo "FAIL: the wheel did not zoom"; exit 1; }
echo "interaction checks passed"
