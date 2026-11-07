#!/usr/bin/env bash
# Runs the Raven spike under Xvfb with Mesa lavapipe, types into the chat composer with xdotool,
# takes a screenshot and prints the frame timings.
#   app/ui/spike/run_headless.sh <raven_spike binary> <screenshot.png> [extra raven_spike args...]
# Needs: xvfb, mesa-vulkan-drivers (lavapipe), xdotool, imagemagick (import).
set -euo pipefail
BIN=${1:?raven_spike binary}
SHOT=${2:?screenshot path}
shift 2
DISP=${SPIKE_DISPLAY:-:93}
export VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}
export RAVEN_WINDOW_SYSTEM=x11

Xvfb "$DISP" -screen 0 1440x900x24 -nolisten tcp -noreset >/dev/null 2>&1 &
XVFB=$!
trap 'kill $XVFB 2>/dev/null || true' EXIT
sleep 1
export DISPLAY=$DISP

LOG=$(mktemp)
"$BIN" --run-seconds 150 "$@" >"$LOG" 2>&1 &
APP=$!

# wait for the timing report (600 frames after warm-up), then type
for _ in $(seq 1 240); do grep -q "frames measured" "$LOG" && break; sleep 0.5; done
WIN=$(xdotool search --name "Fulmetry - Raven spike" | head -1 || true)
if [ -n "$WIN" ]; then
  xdotool windowfocus --sync "$WIN" || true
  # click into the composer (bottom of the chat column), then type and press Enter
  xdotool mousemove --window "$WIN" 1200 872 click 1
  sleep 0.3
  xdotool type --delay 40 "Make the wheels 70 mm"
  xdotool key Return
  sleep 0.5
  xdotool type --delay 40 "typed via xdotool"
  sleep 1.0
  # scroll the message list up a little with the wheel, then back down
  xdotool mousemove --window "$WIN" 1250 400 click 4 click 4 click 4 click 4 click 4
  sleep 1.5
  import -window root "${SHOT%.png}-scrolled.png"
  xdotool click 5 click 5 click 5 click 5 click 5 click 5 click 5 click 5
  sleep 1.0
fi
import -window root "$SHOT"
kill $APP 2>/dev/null || true
wait $APP 2>/dev/null || true
cat "$LOG"
rm -f "$LOG"
