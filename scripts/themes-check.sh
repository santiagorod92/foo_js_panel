#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/themes-check.sh [NAME...]
Runs other authors' themes and scripts in the JS Panel engine, offline. Each line of
tests/themes.tsv names a GitHub repository at a pinned commit and how to run it:
  fcl      a Columns UI layout: every Spider Monkey Panel panel in it (scripts/theme-check.sh),
           with the repository's packages/*/{GUID} installed as Spider Monkey Panel packages
  script   one script; fb.ProfilePath is the checkout's profile/ folder when it has one
  package  the checkout is a Spider Monkey Panel package: it's installed and its main.js runs
The checkouts land in build/themes/src (fetched once, never committed); every panel's PNG and
log in build/themes/<name>/, and build/themes/report.md sums up each theme (panels that load and
paint, the first error) and the API names the themes used that JS Panel doesn't implement.
The scripts run in render_script --sandbox: no shell, network or clipboard, and no writes
outside the theme's own profile. macOS only (the renderer draws with CoreGraphics).
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/build/themes"
SRC="$OUT/src"
RENDER="$REPO/build/tools/render_script"
make -C "$REPO" build/tools/render_script >/dev/null
mkdir -p "$SRC"

fetch() {
  local repo="$1" commit="$2" sparse="$3" dir="$4"
  if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)" = "$commit" ]; then
    git -C "$dir" checkout -q -- . && git -C "$dir" clean -qfd
    return
  fi
  rm -rf "$dir"
  git init -q "$dir"
  git -C "$dir" remote add origin "https://github.com/$repo"
  [ "$sparse" != - ] && git -C "$dir" sparse-checkout set ${sparse//,/ }
  git -C "$dir" fetch -q --depth 1 --filter=blob:none origin "$commit"
  git -C "$dir" checkout -q FETCH_HEAD
}

package_id() { python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8-sig"))["id"])' "$1"; }

install_package() {
  local from="$1" profile="$2" id
  id="$(package_id "$from/package.json")"
  mkdir -p "$profile/foo_spider_monkey_panel/packages"
  rm -rf "$profile/foo_spider_monkey_panel/packages/$id"
  cp -R "$from" "$profile/foo_spider_monkey_panel/packages/$id"
  echo "$profile/foo_spider_monkey_panel/packages/$id"
}

run_one() {
  local js="$1" profile="$2" out="$3" name="$4"
  if "$RENDER" "$js" "$out/$name.png" --size 1280x720 --rounds 300 --profile "$profile/" --sandbox "$profile" >"$out/$name.log" 2>&1; then
    printf '%s  ok\n' "$name"
  else
    printf '%s  FAIL  %s\n' "$name" "$(grep -m1 -E 'error' "$out/$name.log" | cut -c1-160)"
  fi
}

report="$OUT/report.md"
{
  echo "# Themes check"
  echo
  echo "| Theme | Repository | Panels | First error |"
  echo "|---|---|---|---|"
} >"$report.tmp"
: >"$OUT/missing.txt"
while IFS=$'\t' read -r name repo commit kind entry packages sparse; do
  [ "$name" = name ] && continue
  if [ $# -gt 0 ] && ! printf '%s\n' "$@" | grep -qx "$name"; then continue; fi
  echo "==> $name ($repo)"
  dir="$SRC/$name"
  fetch "$repo" "$commit" "$sparse" "$dir"
  out="$OUT/$name"
  rm -rf "$out"
  mkdir -p "$out"
  case "$kind" in
    fcl)
      profile="$out/profile"
      mkdir -p "$profile"
      if [ "$packages" != - ]; then
        for pkg in "$dir/$packages"/*/\{*\}; do install_package "$pkg" "$profile" >/dev/null; done
      fi
      "$REPO/scripts/theme-check.sh" "$dir/$entry" "$profile" "$out/panels" | tee "$out/result.txt"
      cat "$out"/panels/*.log >"$out/all.log" 2>/dev/null || true
      ;;
    script)
      profile="$dir"
      [ -d "$dir/profile" ] && profile="$dir/profile"
      run_one "$dir/$entry" "$profile" "$out" main | tee "$out/result.txt"
      cat "$out/main.log" >"$out/all.log"
      ;;
    package)
      profile="$out/profile"
      installed="$(install_package "$dir/$entry" "$profile")"
      run_one "$installed/main.js" "$profile" "$out" main | tee "$out/result.txt"
      cat "$out/main.log" >"$out/all.log"
      ;;
    *) echo "unknown kind $kind for $name" >&2; exit 2 ;;
  esac
  total="$(grep -cE '  (ok|FAIL)' "$out/result.txt" || true)"
  passed="$(grep -cE '  ok' "$out/result.txt" || true)"
  first="$(grep -m1 -E '  FAIL' "$out/result.txt" | sed 's/.*FAIL  //; s/|/\\|/g' || true)"
  echo "| $name | [$repo](https://github.com/$repo/tree/$commit) | $passed/$total | ${first:--} |" >>"$report.tmp"
  grep -a '^not implemented: ' "$out/all.log" | sed "s/^not implemented: //" | sort -u | sed "s/^/$name /" >>"$OUT/missing.txt" || true
done <"$REPO/tests/themes.tsv"
{
  echo
  echo "## Not implemented"
  echo
  if [ -s "$OUT/missing.txt" ]; then
    awk '{t[$2] = t[$2] (t[$2] ? ", " : "") $1} END {for (n in t) print "- `" n "`: " t[n]}' "$OUT/missing.txt" | sort
  else
    echo "Nothing."
  fi
} >>"$report.tmp"
mv "$report.tmp" "$report"
echo
cat "$report"
