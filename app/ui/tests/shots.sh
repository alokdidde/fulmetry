#!/usr/bin/env bash
# Screenshots of the workbench under Xvfb + Mesa lavapipe, and the interaction checks driven with xdotool.
#   app/ui/tests/shots.sh <fulmetry_workbench binary> <output dir>
# Writes default.png, actions-menu.png, parts-tray.png, chat-left.png (lossless; quantised only above 290 KB) and
# interaction.png, and fails when a click on a part chip or an object tab does not change the selection,
# or when typing in the chat and pressing Send adds no message.
# Needs: xvfb, mesa-vulkan-drivers, xdotool, imagemagick.
set -euo pipefail
BIN=${1:?fulmetry_workbench binary}
OUT=${2:?output dir}
mkdir -p "$OUT"
LOGS=$(mktemp -d)  # logs stay out of the screenshot folder
DISP=${SHOTS_DISPLAY:-:94}
export VK_ICD_FILENAMES=${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}
export RAVEN_WINDOW_SYSTEM=x11 RAVEN_LIVE_RELOAD=0
Xvfb "$DISP" -screen 0 1440x900x24 -nolisten tcp -noreset >/dev/null 2>&1 &
XVFB=$!
trap 'kill $XVFB 2>/dev/null || true' EXIT
sleep 1
export DISPLAY=$DISP

shrink() {  # keep each screenshot well under 300 KB
  convert "$1" -strip -define png:compression-level=9 PNG24:"$1"
  if [ "$(stat -c %s "$1")" -gt 290000 ]; then convert "$1" -colors 256 -strip "$1"; fi
}

shot() {  # shot <name> [workbench args...]
  local name=$1; shift
  "$BIN" --project sample --harnesses some --frames 40 --strict --shot "$OUT/$name.png" "$@" >"$LOGS/$name.log" 2>&1 || { cat "$LOGS/$name.log"; exit 1; }
  shrink "$OUT/$name.png"
  echo "$name.png: $(stat -c %s "$OUT/$name.png") bytes"
}

shot default
shot actions-menu --open menu
shot parts-tray --open tray --select part:rover/camera-head
shot chat-left --chat-left

# ---- interaction: real X11 clicks and keys (positions come from the app's own layout report)
LOG="$LOGS/interaction.log"
"$BIN" --project sample --harnesses some --run-seconds 25 >"$LOG" 2>&1 &
APP=$!
for _ in $(seq 1 150); do grep -q "^send-at" "$LOG" && break; sleep 0.2; done
sleep 1
WIN=$(xdotool search --name "^Fulmetry$" | head -1)
xdotool windowfocus --sync "$WIN" || true
at() { grep -m1 "^$1" "$LOG" | awk '{print $(NF-1), $NF}'; }
click() { xdotool mousemove --window "$WIN" $1 click 1; sleep "${2:-0.8}"; }
click "$(at 'tab-at 2 ')"                      # object tab: Wireless remote
click "$(at 'tab-at 1 ')"                      # object tab: Desk rover
click "$(at 'chip-at part:rover/camera-head')" # part chip in the chat
click "$(at 'composer-at')" 0.4                # type into the composer, then press Send
xdotool type --delay 30 "Make the wheels 70 mm"
click "$(at 'send-at')" 4                      # the fake agent runs: its card goes from Starting to Done
import -window root "$OUT/interaction.png"
shrink "$OUT/interaction.png"
wait $APP || true
cat "$LOG"
grep -q "^tab: 2" "$LOG" && grep -q "^tab: 1" "$LOG" || { echo "FAIL: object tab clicks"; exit 1; }
grep -q "^selection: part:rover/camera-head" "$LOG" || { echo "FAIL: part chip click"; exit 1; }
grep -q '^chat sent: "Make the wheels 70 mm"' "$LOG" || { echo "FAIL: chat send"; exit 1; }
grep -Eq "chat entries: ([7-9]|[1-9][0-9])," "$LOG" || { echo "FAIL: no message added"; exit 1; }
echo "interaction checks passed"
