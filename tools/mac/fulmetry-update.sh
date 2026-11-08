#!/bin/bash
# Keeps /Applications/Fulmetry.app on the newest GitHub release (prereleases included) on an Apple silicon Mac.
#
#   fulmetry-update.sh                  install the newest build if it is newer, then open it
#   fulmetry-update.sh --install-agent  also check every 10 minutes and at login (a launchd agent)
#   fulmetry-update.sh --remove-agent   stop checking
#
# One line from Terminal, no checkout needed:
#   curl -fsSL https://raw.githubusercontent.com/alokdidde/fulmetry/main/tools/mac/fulmetry-update.sh | bash -s -- --install-agent
#
# A running Fulmetry is asked to quit first and reopened on the new build. The app is downloaded by this script, not
# a browser, and the quarantine flag is cleared anyway, so Gatekeeper does not stop an unnotarized build.
set -euo pipefail

REPO=${FULMETRY_REPO:-alokdidde/fulmetry}
REF=${FULMETRY_UPDATER_REF:-main}  # the branch the agent copies this script from
APP=${FULMETRY_APP:-/Applications/Fulmetry.app}
LABEL=app.fulmetry.updater
HOME_DIR="$HOME/Library/Application Support/Fulmetry/updater"
SELF="$HOME_DIR/fulmetry-update.sh"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/fulmetry-updater.log"

log() { echo "$(date '+%F %T') $*"; }

install_agent() {
  mkdir -p "$HOME_DIR" "$(dirname "$PLIST")" "$(dirname "$LOG")"
  # Keep a copy of this script outside any checkout, so the agent survives the source moving.
  curl -fsSL "https://raw.githubusercontent.com/$REPO/$REF/tools/mac/fulmetry-update.sh" -o "$SELF.new" 2>/dev/null \
    || cp "${BASH_SOURCE[0]}" "$SELF.new"
  chmod +x "$SELF.new" && mv "$SELF.new" "$SELF"
  cat > "$PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key><array><string>/bin/bash</string><string>$SELF</string></array>
  <key>RunAtLoad</key><true/>
  <key>StartInterval</key><integer>600</integer>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
</dict>
</plist>
PLIST
  launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
  launchctl bootstrap "gui/$(id -u)" "$PLIST"
  echo "Fulmetry updater installed: checks now, then every 10 minutes. Log: $LOG"
}

case "${1:-}" in
  --install-agent) install_agent; exit 0 ;;
  --remove-agent)
    launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
    rm -f "$PLIST"
    echo "Fulmetry updater removed."
    exit 0 ;;
  "") ;;
  *) sed -n '2,12p' "$0"; exit 2 ;;
esac

# The newest release, prereleases included (/releases/latest skips them). No python: macOS may not have it.
tag=$(curl -fsSL "https://api.github.com/repos/$REPO/releases?per_page=1" \
  | grep -m1 -o '"tag_name": *"[^"]*"' | sed 's/.*"\([^"]*\)"$/\1/')
test -n "$tag"
url="https://github.com/$REPO/releases/download/$tag/Fulmetry-$tag-macos-arm64.zip"

have=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$APP/Contents/Info.plist" 2>/dev/null || true)
if [ "v$have" = "$tag" ]; then exit 0; fi
log "updating Fulmetry ${have:-none} -> ${tag#v}"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
curl -fsSL "$url" -o "$work/app.zip"
ditto -x -k "$work/app.zip" "$work"
test -d "$work/Fulmetry.app"

if pgrep -xq Fulmetry; then
  osascript -e 'tell application id "app.fulmetry.workbench" to quit' 2>/dev/null || true
  for _ in $(seq 20); do pgrep -xq Fulmetry || break; sleep 0.5; done
  pkill -x Fulmetry 2>/dev/null || true
fi

rm -rf "$APP.old"
[ -d "$APP" ] && mv "$APP" "$APP.old"
mv "$work/Fulmetry.app" "$APP"
rm -rf "$APP.old"
xattr -dr com.apple.quarantine "$APP" 2>/dev/null || true

osascript -e "display notification \"Updated to ${tag#v}\" with title \"Fulmetry\"" 2>/dev/null || true
open "$APP"
log "installed ${tag#v} and opened it"
