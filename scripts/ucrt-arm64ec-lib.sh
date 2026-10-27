#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/ucrt-arm64ec-lib.sh [XWIN]
Writes $XWIN/sdk/lib/ucrt/arm64ec/ucrt.lib, an ARM64EC import library for the UCRT, unless it is
already newer than the x64 one it is made from. XWIN defaults to what scripts/toolchain.sh picks.

xwin's aarch64 UCRT libraries carry no ARM64EC symbols, and linking the x64 static UCRT into an
ARM64EC DLL builds but the DLL then fails to initialize on Windows on ARM (error 1114). So the
ARM64EC build links the CRT dynamically: vcruntime/msvcp import libs come from xwin's aarch64 CRT
(they have ARM64EC symbols), and this script turns the x64 ucrt.lib's export list (one .def per
api-ms-win-crt-*.dll, data exports kept as DATA) into ARM64EC import libraries with
llvm-lib /machine:arm64ec, merged into one ucrt.lib. Needs llvm-readobj and llvm-lib on PATH.
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac

set -euo pipefail
. "$(dirname "$0")/toolchain.sh"
XWIN="${1:-$XWIN}"
SRC="$XWIN/sdk/lib/ucrt/x86_64/ucrt.lib"
OUT_DIR="$XWIN/sdk/lib/ucrt/arm64ec"
OUT="$OUT_DIR/ucrt.lib"
[ -f "$SRC" ] || { echo "no $SRC (xwin splat with --arch x86_64)" >&2; exit 1; }
[ -f "$OUT" ] && [ "$OUT" -nt "$SRC" ] && exit 0
for t in llvm-readobj llvm-lib; do command -v "$t" >/dev/null || { echo "$t not found on PATH" >&2; exit 1; }; done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
llvm-readobj "$SRC" 2>/dev/null | awk -v dir="$work" '
  /^File: /                    { f = $2 }
  /^Format: COFF-import-file/  { imp = 1; next }
  /^Format: /                  { imp = 0 }
  imp && /^Type: /             { t = $2 }
  imp && /^Export name: /      {
    if (!(f in seen)) { seen[f] = 1; order[++n] = f }
    body[f] = body[f] "  " $3 (t == "data" ? " DATA" : "") "\n"
    imp = 0
  }
  END {
    for (i = 1; i <= n; i++) {
      d = order[i]; def = d; sub(/\.dll$/, ".def", def)
      printf "LIBRARY %s\nEXPORTS\n%s", d, body[d] > (dir "/" def)
    }
  }'
ls "$work"/*.def >/dev/null 2>&1 || { echo "no imports read from $SRC" >&2; exit 1; }
for def in "$work"/*.def; do
  llvm-lib /machine:arm64ec "/def:$def" "/out:${def%.def}.lib" >/dev/null
done
mkdir -p "$OUT_DIR"
llvm-lib "/out:$OUT.tmp" "$work"/*.lib
mv "$OUT.tmp" "$OUT"
echo "wrote $OUT ($(ls "$work"/*.def | wc -l | tr -d ' ') api-set DLLs)"
