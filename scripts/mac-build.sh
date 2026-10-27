#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/mac-build.sh
Builds the macOS component bundle (universal arm64 + x86_64) -> build-mac/foo_js_panel.component.
On Linux it cross-compiles with clang + ld64.lld against MACOS_SDK (default
~/.macos-sdk/MacOSX.sdk, see scripts/extract-macos-sdk.py); on a Mac it uses the native tools.
  SDK_ROOT=/path   parent of foobar2000/ + pfc/ (default: see scripts/toolchain.sh)
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac

set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/toolchain.sh
OUT=build-mac/foo_js_panel.component
for arch in arm64 x86_64; do
  if [ "$(uname)" = Darwin ]; then
    cmake -S . -B "build-mac/$arch" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDK_ROOT="$SDK_ROOT" -DPANELS_UI_SRC="$PANELS_UI_SRC" \
      -DCMAKE_OSX_ARCHITECTURES=$arch -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 >/dev/null
  else
    cmake -S . -B "build-mac/$arch" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDK_ROOT="$SDK_ROOT" -DPANELS_UI_SRC="$PANELS_UI_SRC" \
      -DCMAKE_TOOLCHAIN_FILE=cmake/clang-macos.cmake -DMAC_ARCH=$arch >/dev/null
  fi
  ninja -C "build-mac/$arch" foo_js_panel
done
rm -rf "$OUT"
cp -R build-mac/arm64/foo_js_panel.component "$OUT"
LIPO=$(command -v lipo || command -v llvm-lipo)
"$LIPO" -create build-mac/arm64/foo_js_panel.component/Contents/MacOS/foo_js_panel \
  build-mac/x86_64/foo_js_panel.component/Contents/MacOS/foo_js_panel \
  -output "$OUT/Contents/MacOS/foo_js_panel"
if command -v codesign >/dev/null; then codesign --force --sign - "$OUT"; fi
archs=$("$LIPO" -archs "$OUT/Contents/MacOS/foo_js_panel")
for want in arm64 x86_64; do
  case " $archs " in
    *" $want "*) ;;
    *) echo "mac-build: missing $want slice (got: $archs)" >&2; exit 1 ;;
  esac
done
echo "built: $OUT ($archs)"
