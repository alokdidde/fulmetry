#!/usr/bin/env bash
# Packages the workbench as Fulmetry.app for Apple silicon and zips it.
#   tools/package/macos.sh <build dir> <out dir> <version>
# Needs WICKED_ROOT and MUJOCO_ROOT (tools/fetch_dep.py), and MoltenVK (brew install molten-vk).
# The bundle: Contents/MacOS (the app, Wicked's shader compilers), Contents/Frameworks (mujoco.framework,
# libMoltenVK.dylib), Contents/Resources (UI assets, the rover project, Wicked's shader sources and compiled Metal shaders).
# With MACOS_SIGN_IDENTITY ("Developer ID Application: ...") in the keychain it is signed for notarization (hardened
# runtime, timestamp); release.yml then notarizes and staples it. Without it, it is signed ad hoc and the first open
# needs Open Anyway in System Settings (or xattr -dr com.apple.quarantine).
set -euo pipefail
BUILD=${1:?build dir}
OUT=${2:?out dir}
VERSION=${3:?version}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
: "${WICKED_ROOT:?}" "${MUJOCO_ROOT:?}"
MOLTENVK=${MOLTENVK_LIB:-$(brew --prefix molten-vk)/lib/libMoltenVK.dylib}

APP=$OUT/Fulmetry.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources/fixtures"

cp "$BUILD/app/ui/fulmetry_workbench" "$APP/Contents/MacOS/Fulmetry"
cp "$WICKED_ROOT/WickedEngine/libdxcompiler.dylib" "$WICKED_ROOT/WickedEngine/libmetalirconverter.dylib" "$APP/Contents/MacOS/"
ditto "$MUJOCO_ROOT/mujoco.framework" "$APP/Contents/Frameworks/mujoco.framework"
cp "$MOLTENVK" "$APP/Contents/Frameworks/libMoltenVK.dylib"
chmod u+w "$APP/Contents/Frameworks/libMoltenVK.dylib"
install_name_tool -id @rpath/libMoltenVK.dylib "$APP/Contents/Frameworks/libMoltenVK.dylib"

ditto "$ROOT/app/ui/assets" "$APP/Contents/Resources/assets"
ditto "$ROOT/fixtures/rover" "$APP/Contents/Resources/fixtures/rover"
ditto "$WICKED_ROOT/WickedEngine/shaders" "$APP/Contents/Resources/wicked-shaders"
# The Metal shaders CI compiled (release.yml, "Metal shaders compile"). Without their .wishadermeta files Wicked
# treats them as up to date, so the app loads them instead of compiling on the user's Mac.
METAL_SHADERS=${METAL_SHADERS:-$BUILD/metal-shaders/shaders/metal}
test -d "$METAL_SHADERS" || { echo "no compiled Metal shaders at $METAL_SHADERS" >&2; exit 1; }
ditto "$METAL_SHADERS" "$APP/Contents/Resources/wicked-shaders-metal"
find "$APP/Contents/Resources/wicked-shaders-metal" -name "*.wishadermeta" -delete
test -f "$APP/Contents/Resources/wicked-shaders-metal/impostorPS.cso"

# T3 Code's server (tools/package/t3-stage.mjs) and the Node.js that runs it; the app finds both here
# (app/net/src/AgentClient.cpp). Without T3_STAGE the app falls back to simulated agents.
if [ -n "${T3_STAGE:-}" ]; then
  test -f "$T3_STAGE/bin.mjs" || { echo "no bin.mjs in T3_STAGE=$T3_STAGE" >&2; exit 1; }
  ditto "$T3_STAGE" "$APP/Contents/Resources/t3"
  mkdir -p "$APP/Contents/Resources/node/bin"
  cp "${NODE_BIN:-$(command -v node)}" "$APP/Contents/Resources/node/bin/node"
  chmod 755 "$APP/Contents/Resources/node/bin/node"
else
  echo "T3_STAGE not set: the app will run simulated agents" >&2
fi

# The build tree's rpath points at MUJOCO_ROOT; the bundle's framework is in Contents/Frameworks.
install_name_tool -add_rpath @executable_path/../Frameworks "$APP/Contents/MacOS/Fulmetry"
install_name_tool -delete_rpath "$MUJOCO_ROOT" "$APP/Contents/MacOS/Fulmetry" 2>/dev/null || true

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Fulmetry</string>
  <key>CFBundleDisplayName</key><string>Fulmetry</string>
  <key>CFBundleIdentifier</key><string>app.fulmetry.workbench</string>
  <key>CFBundleExecutable</key><string>Fulmetry</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${VERSION#v}</string>
  <key>CFBundleVersion</key><string>${VERSION#v}</string>
  <key>LSMinimumSystemVersion</key><string>${FULMETRY_MACOS_DEPLOYMENT:-15.0}</string>
  <key>LSArchitecturePriority</key><array><string>arm64</string></array>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST

if [ -n "${MACOS_SIGN_IDENTITY:-}" ]; then
  # Inside out: notarization rejects --deep and needs every Mach-O signed with the hardened runtime.
  ENT=$ROOT/tools/package/macos.entitlements
  sign() { codesign --force --timestamp --options runtime --sign "$MACOS_SIGN_IDENTITY" "$@"; }
  sign "$APP"/Contents/MacOS/*.dylib "$APP/Contents/Frameworks/libMoltenVK.dylib"
  # Every Mach-O in T3 Code's packages (native addons, helper binaries), then Node with the JIT it needs.
  if [ -d "$APP/Contents/Resources/t3" ]; then
    find "$APP/Contents/Resources/t3" -type f -print0 | while IFS= read -r -d '' f; do
      if file -b "$f" | grep -q 'Mach-O'; then sign "$f"; fi
    done
    sign --entitlements "$ROOT/tools/package/node.entitlements" "$APP/Contents/Resources/node/bin/node"
  fi
  sign "$APP/Contents/Frameworks/mujoco.framework"
  sign --entitlements "$ENT" "$APP"
  codesign --verify --strict --deep "$APP"
else
  codesign --force --deep --sign - "$APP"
  codesign --verify --deep "$APP"
fi
ZIP=$OUT/Fulmetry-${VERSION}-macos-arm64.zip
rm -f "$ZIP"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"
echo "packaged $ZIP ($(du -h "$ZIP" | cut -f1))"
