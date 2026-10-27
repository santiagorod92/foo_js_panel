#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/run-unit-tests.sh [host|win|win11-arm]
Builds and runs the unit tests (tests/*.cpp over the SDK-free src/core sources + QuickJS).
  host       (default) native compiler ($CXX_HOST, default clang++; C with $CC_HOST, default clang)
             with ASan/UBSan on our code: Linux, macOS
  win        clang-cl + xwin, run under wine (Linux)
  win11-arm  clang-cl + xwin on a Mac, run in the Windows 11 ARM VM
             (../foo_navidrome/scripts/win11-arm.sh, override with WIN11_ARM)
The Windows modes run the _WIN32 branches (fs_util.h's wide-path file access) that the host
build never compiles; foo_ui_panels' src/core/skin_templates.cpp is copied next to the exe as
pui-src/ (a test runs the JS of its JS Panel template). QuickJS is built without sanitizers (third-party code).
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac

set -euo pipefail
cd "$(dirname "$0")/.."
. scripts/toolchain.sh
MODE="${1:-host}"
SRC=(tests/*.cpp src/core/js_engine.cpp src/core/sample_script.cpp src/core/log.cpp src/core/diagnostics.cpp)
QJS=(third_party/quickjs/dtoa.c third_party/quickjs/libregexp.c third_party/quickjs/libunicode.c third_party/quickjs/quickjs.c)
INC=(-I"$PWD/src" -I"$PANELS_UI_SRC" -I"$PWD/third_party/quickjs")
[ -f "$PANELS_UI_SRC/gfx/canvas.h" ] || { echo "no $PANELS_UI_SRC/gfx/canvas.h (clone foo_ui_panels next to this repo, or set PANELS_UI_SRC)" >&2; exit 1; }

win_build() {
  command -v clang-cl >/dev/null || { echo "clang-cl not found (Linux: pacman -S clang lld; macOS: brew install llvm lld)" >&2; exit 1; }
  [ -f "$XWIN/crt/include/vcruntime.h" ] || { echo "xwin splat not found at $XWIN (see build.sh --help)" >&2; exit 1; }
  local out=build/tests-win tp=()
  mkdir -p "$out"
  for f in "${SRC[@]}"; do tp+=("/Tp$PWD/$f"); done
  for f in "${QJS[@]}"; do tp+=("/Tc$PWD/$f"); done
  echo "==> unit tests: clang-cl (Windows x64)" >&2
  clang-cl --target=x86_64-pc-windows-msvc -fuse-ld=lld-link /std:c++20 /EHsc /MT /GR /O1 /W3 /utf-8 \
    -Wno-unused-command-line-argument /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN "/DPANELS_UI_SRC_DIR=\"${WIN_PUI_DIR:-pui-src}\"" "${INC[@]}" \
    "/imsvc$XWIN/crt/include" "/imsvc$XWIN/sdk/include/ucrt" "/imsvc$XWIN/sdk/include/um" "/imsvc$XWIN/sdk/include/shared" \
    "${tp[@]}" "/Fe$out/run_tests.exe" "/Fo$out/" /link \
    "/libpath:$XWIN/crt/lib/x86_64" "/libpath:$XWIN/sdk/lib/ucrt/x86_64" "/libpath:$XWIN/sdk/lib/um/x86_64" >&2
  echo "$out/run_tests.exe"
}

case "$MODE" in
  host)
    out=build/tests
    mkdir -p "$out/qjs"
    objs=()
    for f in "${QJS[@]}"; do
      o="$out/qjs/$(basename "${f%.c}").o"
      if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
        "${CC_HOST:-clang}" -std=gnu11 -O2 -w -D_GNU_SOURCE -c "$f" -o "$o"
      fi
      objs+=("$o")
    done
    "${CXX_HOST:-clang++}" -std=c++20 -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer \
      "-DPANELS_UI_SRC_DIR=\"$PANELS_UI_SRC\"" "${INC[@]}" "${SRC[@]}" "${objs[@]}" -o "$out/run_tests"
    "./$out/run_tests"
    ;;
  win)
    exe="$(WIN_PUI_DIR="$(winepath -w "$PANELS_UI_SRC" 2>/dev/null | sed 's/\\/\//g' || echo "$PANELS_UI_SRC")" win_build)"
    command -v wine >/dev/null || { echo "wine not found" >&2; exit 1; }
    WINEDEBUG=-all wine "$exe"
    ;;
  win11-arm)
    exe="$(win_build)"
    vm="${WIN11_ARM:-$(cd .. && pwd)/foo_navidrome/scripts/win11-arm.sh}"
    [ -x "$vm" ] || { echo "no $vm (clone foo_navidrome next to this repo, or set WIN11_ARM)" >&2; exit 1; }
    stage="$(mktemp -d)"
    mkdir -p "$stage/pui-src/core"
    cp "$PANELS_UI_SRC/core/skin_templates.cpp" "$stage/pui-src/core/"
    "$vm" push "$stage/pui-src" AppData/Local/Temp
    rm -rf "$stage"
    "$vm" run "$exe"
    ;;
  *) usage; exit 2 ;;
esac
