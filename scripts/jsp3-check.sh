#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/jsp3-check.sh JSP3_DIR [OUT_DIR]
Runs every JScript Panel 3 sample in the JS Panel engine with tools/render_script and prints one
line per sample: ok or the first error. JSP3_DIR is an unzipped foo_jscript_panel3
.fb2k-component (helpers.txt + samples/); it's copied into OUT_DIR/profile/foo_js_panel/jsp3/,
where %fb2k_component_path% points. The samples can only write, move or delete files inside that
profile; they never reach shell commands, the network or the clipboard. Each sample's PNG and log
land in OUT_DIR (default build/jsp3-check). macOS only (the renderer draws with CoreGraphics).
USAGE
}
case "${1:-}" in -h|--help|'') usage; exit 0 ;; esac
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$1"
OUT="${2:-$REPO/build/jsp3-check}"
[ -f "$SRC/helpers.txt" ] && [ -d "$SRC/samples" ] || { echo "no helpers.txt + samples/ in $SRC" >&2; exit 2; }
rm -rf "$OUT"
mkdir -p "$OUT/profile/foo_js_panel/jsp3"
cp -R "$SRC/helpers.txt" "$SRC/samples" "$OUT/profile/foo_js_panel/jsp3/"
make -C "$REPO" build/tools/render_script >/dev/null
ok=0 total=0
for js in "$SRC"/samples/*.txt; do
  name="$(basename "$js" .txt)"
  total=$((total + 1))
  if "$REPO/build/tools/render_script" "$js" "$OUT/$name.png" --size 500x200 --playing --length 200 --time 60 \
       --profile "$OUT/profile/" --sandbox "$OUT/profile" >"$OUT/$name.log" 2>&1; then
    ok=$((ok + 1))
    printf 'ok    %s\n' "$name"
  else
    printf 'FAIL  %s  %s\n' "$name" "$(grep -m1 -E 'error' "$OUT/$name.log" | cut -c1-150)"
  fi
done
echo "$ok/$total samples run"
