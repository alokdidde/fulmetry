#!/usr/bin/env bash
# Packages the workbench as Fulmetry.app for Apple silicon and zips it.
#   tools/package/macos.sh <build dir> <out dir> <version>
# Needs WICKED_ROOT and MUJOCO_ROOT (tools/fetch_dep.py), and MoltenVK (brew install molten-vk).
# The bundle: Contents/MacOS (the app, Wicked's shader compilers), Contents/Frameworks (mujoco.framework,
# libMoltenVK.dylib), Contents/Resources (UI assets, the rover project, Wicked's shader sources). Signed ad hoc:
# not notarized, so the first open needs right-click > Open (or xattr -dr com.apple.quarantine).
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

codesign --force --deep --sign - "$APP"
codesign --verify --deep "$APP"
ZIP=$OUT/Fulmetry-${VERSION}-macos-arm64.zip
rm -f "$ZIP"
ditto -c -k --sequesterRsrc --keepParent "$APP" "$ZIP"
echo "packaged $ZIP ($(du -h "$ZIP" | cut -f1))"
