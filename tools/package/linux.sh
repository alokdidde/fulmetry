#!/usr/bin/env bash
# Packages the workbench for Linux x86_64 as a tarball.
#   tools/package/linux.sh <build dir> <out dir> <version>
# Needs WICKED_ROOT and MUJOCO_ROOT (tools/fetch_dep.py) and patchelf. The machine running it needs a Vulkan 1.3
# driver and SDL2 (libsdl2-2.0-0). Layout: fulmetry-<version>-linux-x86_64/{fulmetry, bin/, lib/, share/fulmetry/}.
set -euo pipefail
BUILD=${1:?build dir}
OUT=${2:?out dir}
VERSION=${3:?version}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
: "${WICKED_ROOT:?}" "${MUJOCO_ROOT:?}"

NAME=fulmetry-${VERSION}-linux-x86_64
DIR=$OUT/$NAME
rm -rf "$DIR"
mkdir -p "$DIR/bin" "$DIR/lib" "$DIR/share/fulmetry/fixtures"
cp "$BUILD/app/ui/fulmetry_workbench" "$WICKED_ROOT/WickedEngine/libdxcompiler.so" "$DIR/bin/"
cp -P "$MUJOCO_ROOT"/lib/libmujoco.so* "$DIR/lib/"
patchelf --set-rpath '$ORIGIN/../lib' "$DIR/bin/fulmetry_workbench"
cp -r "$ROOT/app/ui/assets" "$DIR/share/fulmetry/assets"
cp -r "$ROOT/fixtures/rover" "$DIR/share/fulmetry/fixtures/rover"
cp -r "$WICKED_ROOT/WickedEngine/shaders" "$DIR/share/fulmetry/wicked-shaders"
cat > "$DIR/fulmetry" <<'SH'
#!/bin/sh
exec "$(dirname "$(readlink -f "$0")")/bin/fulmetry_workbench" "$@"
SH
chmod +x "$DIR/fulmetry"
tar -C "$OUT" -czf "$OUT/$NAME.tar.gz" "$NAME"
echo "packaged $OUT/$NAME.tar.gz ($(du -h "$OUT/$NAME.tar.gz" | cut -f1))"
