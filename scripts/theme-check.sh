#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/theme-check.sh LAYOUT.fcl PROFILE_DIR [OUT_DIR]
Extracts every Spider Monkey Panel panel (script + properties; SMP 1.x binary or SMP 1.6 JSON
settings) from a Columns UI layout export with tools/fcl_extract.py, runs each one (a panel that
names a package runs PROFILE_DIR/foo_spider_monkey_panel/packages/<id>/main.js) in the JS Panel engine with tools/render_script
(fb.ProfilePath = PROFILE_DIR, the panel's properties, 900x600, timers fired) and prints one
line per panel: ok or the first error. The scripts can only write, move or delete files inside
PROFILE_DIR; they never reach shell commands, the network or the clipboard. Each panel's PNG and log land in OUT_DIR
(default build/theme-check). macOS only (the renderer draws with CoreGraphics).
USAGE
}
case "${1:-}" in -h|--help|'') usage; exit 0 ;; esac
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
LAYOUT="$1"
PROFILE="$(cd "${2:?$(usage)}" && pwd)"
OUT="${3:-$REPO/build/theme-check}"
rm -rf "$OUT"
mkdir -p "$OUT/panels"
make -C "$REPO" build/tools/render_script >/dev/null
python3 -I "$REPO/tools/fcl_extract.py" "$LAYOUT" "$OUT/panels" >"$OUT/index.txt"
ok=0 total=0
for panel in "$OUT"/panels/panel-*.js "$OUT"/panels/panel-*.package; do
  [ -e "$panel" ] || continue
  name="$(basename "${panel%.*}")"
  total=$((total + 1))
  js="$panel"
  if [ "${panel##*.}" = package ]; then
    js="$PROFILE/foo_spider_monkey_panel/packages/$(tr -d '\r\n' <"$panel")/main.js"
    if [ ! -f "$js" ]; then
      printf '%s  FAIL  package %s is not installed in %s\n' "$name" "$(tr -d '\r\n' <"$panel")" "$PROFILE"
      continue
    fi
  fi
  if "$REPO/build/tools/render_script" "$js" "$OUT/$name.png" --size 900x600 --rounds 300 --profile "$PROFILE/" \
       --properties "$OUT/panels/$name.properties.json" --sandbox "$PROFILE" >"$OUT/$name.log" 2>&1; then
    ok=$((ok + 1))
    printf '%s  ok    %s\n' "$name" "$(grep -m1 -F "$name " "$OUT/index.txt" | awk '{print $NF}')"
  else
    printf '%s  FAIL  %s\n' "$name" "$(grep -m1 -E 'error' "$OUT/$name.log" | cut -c1-150)"
  fi
done
echo "$ok/$total panels run"
