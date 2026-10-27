#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: ./build.sh
Cross-builds foo_js_panel.dll (Windows) on Linux or macOS with clang-cl + lld-link + xwin.
  WIN_ARCH=x64      (default) -> build/foo_js_panel.dll
  WIN_ARCH=arm64ec  Windows on ARM, native -> build-arm64ec/foo_js_panel.dll
  WIN_ARCH=x86      32-bit foobar2000 -> build-x86/foo_js_panel.dll
  SDK_ROOT=/path    parent of foobar2000/{SDK,shared,...} + pfc/ (default: see scripts/toolchain.sh;
                    fetched from reupen/foobar2000-sdk-unmodified when missing)
  XWIN=/path        xwin splat (default: see scripts/toolchain.sh)
  PANELS_UI_SRC=/path  foo_ui_panels' src/ (default: ../foo_ui_panels/src)
Prereqs: clang-cl, lld-link, cmake, ninja (macOS: brew install llvm lld cmake ninja xwin), and
  cargo install xwin && xwin --accept-license --arch x86,x86_64,aarch64 splat --output ~/.xwin
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac

set -euo pipefail
cd "$(dirname "$0")"

. scripts/toolchain.sh
WIN_ARCH="${WIN_ARCH:-x64}"
case "$WIN_ARCH" in
  x64) BUILD_DIR=build ;;
  arm64ec) BUILD_DIR=build-arm64ec ;;
  x86) BUILD_DIR=build-x86 ;;
  *) echo "WIN_ARCH must be x64, arm64ec or x86 (got '$WIN_ARCH')" >&2; exit 1 ;;
esac

if [ ! -d "$SDK_ROOT/foobar2000/SDK" ] || [ ! -d "$SDK_ROOT/pfc" ]; then
  echo "foobar2000 SDK not found under $SDK_ROOT - fetching reupen/foobar2000-sdk-unmodified ..."
  staging="$(mktemp -d)"
  trap 'rm -rf "$staging"' EXIT
  git clone --depth 1 https://github.com/reupen/foobar2000-sdk-unmodified.git "$staging/sdk"
  mkdir -p "$SDK_ROOT/foobar2000"
  for d in SDK helpers shared foobar2000_component_client; do
    [ -e "$SDK_ROOT/foobar2000/$d" ] || mv "$staging/sdk/foobar2000/$d" "$SDK_ROOT/foobar2000/$d"
  done
  [ -e "$SDK_ROOT/pfc" ] || mv "$staging/sdk/pfc" "$SDK_ROOT/pfc"
fi

grep -rhoE '#include <[^>]+>' "$SDK_ROOT/pfc" "$SDK_ROOT/foobar2000/SDK" \
     "$SDK_ROOT/foobar2000/foobar2000_component_client" src 2>/dev/null \
  | sed -E 's/#include <([^>]+)>/\1/' | grep -E '[A-Z]' | sort -u \
  | while read -r inc; do
      base=$(basename "$inc")
      real=$(find "$XWIN" -type f -iname "$base" 2>/dev/null | head -1) || true
      [ -n "${real:-}" ] && [ ! -e "$(dirname "$real")/$base" ] && \
        ln -s "$(basename "$real")" "$(dirname "$real")/$base" || true
    done

if [ "$WIN_ARCH" = arm64ec ]; then scripts/ucrt-arm64ec-lib.sh "$XWIN"; fi

cmake -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-win64.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DWIN_ARCH="$WIN_ARCH" \
  -DSDK_ROOT="$SDK_ROOT" \
  -DPANELS_UI_SRC="$PANELS_UI_SRC"
ninja -C "$BUILD_DIR"
echo "built: $BUILD_DIR/foo_js_panel.dll ($WIN_ARCH)"
