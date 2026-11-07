#!/usr/bin/env bash
# Harness onboarding and picker, under Xvfb + Mesa lavapipe.
#   app/ui/tests/harness_shots.sh <fulmetry_workbench> <output dir>
# Writes 1440 x 900 screenshots (each kept under 400 KB):
#   first-run.png    nothing connected yet: the onboarding screen opens by itself
#   device-code.png  Codex signing in with a one-time code
#   api-key.png      Raven asking for a model provider key
#   installing.png   Cursor Agent installing
#   ready.png        Claude Code signed in, its models, the default
#   registry.png     adding a harness from the ACP registry
#   picker.png       the picker under the composer (live stage when the binary has Wicked)
# then clicks the harness chip under the composer with a real X11 click and checks the picker opens.
# Needs: xvfb, mesa-vulkan-drivers, xdotool, imagemagick.
set -euo pipefail
BIN=${1:?fulmetry_workbench binary}
OUT=${2:?output dir}
mkdir -p "$OUT"
LOGS=${HARNESS_LOGS:-$(mktemp -d)}
mkdir -p "$LOGS"
DISP=${SHOTS_DISPLAY:-:94}
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
  rm -rf "$FULMETRY_CONFIG_DIR"
  "$BIN" --project sample --harnesses some --frames 40 --strict --shot "$OUT/$name.png" "$@" >"$LOGS/$name.log" 2>&1 || { cat "$LOGS/$name.log"; exit 1; }
  shrink "$OUT/$name.png"
  echo "$name.png: $(stat -c %s "$OUT/$name.png") bytes; $(grep '^new agents run on' "$LOGS/$name.log")"
}

shot first-run --still --harnesses fresh
grep -q "^onboarding open" "$LOGS/first-run.log" || { echo "FAIL: first run did not open the onboarding screen"; exit 1; }
shot device-code --still --harnesses fresh --do signin:codex --onboard codex
shot api-key --still --harnesses fresh --do signin:raven --onboard raven
shot installing --still --do install:cursor --onboard cursor
shot ready --still --onboard claudeAgent
shot registry --still --onboard registry
shot picker --harnesses all --do choose:codex/gpt-6-luna --open picker
grep -q "^new agents run on: codex/gpt-6-luna" "$LOGS/picker.log" || { echo "FAIL: choice not applied"; exit 1; }

# ---- a real click on the harness chip opens the picker
LOG="$LOGS/click.log"
rm -rf "$FULMETRY_CONFIG_DIR"
"$BIN" --project sample --harnesses some --still --run-seconds 30 >"$LOG" 2>&1 &
APP=$!
for _ in $(seq 1 100); do grep -q "^harness-chip-at" "$LOG" && break; sleep 0.2; done
read -r _ CX CY < <(grep "^harness-chip-at" "$LOG" | tail -1)
WIN=$(xdotool search --name "^Fulmetry$" | head -1)
xdotool windowfocus --sync "$WIN" || true
xdotool mousemove --window "$WIN" "$CX" "$CY" click 1
for _ in $(seq 1 50); do grep -q "^picker open" "$LOG" && break; sleep 0.2; done
kill $APP 2>/dev/null || true
wait $APP 2>/dev/null || true
grep -q "^picker open" "$LOG" || { echo "FAIL: clicking the harness chip did not open the picker"; cat "$LOG"; exit 1; }
echo "click: the harness chip opens the picker"
