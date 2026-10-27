#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/mac-local.sh [--launch] [--panels-ui] [--skin DIR]
Installs build-mac/foo_js_panel.component into this Mac's foobar2000
(~/Library/foobar2000-v2/user-components/foo_js_panel), quitting foobar2000 first.
  --launch      start foobar2000 afterwards
  --panels-ui   also install ../foo_ui_panels/build-mac/foo_ui_panels.component (make mac-build there)
  --skin DIR    copy DIR into Panels UI's skins folder and make it the active skin; the previous
                active skin is saved to build/mac-local-previous-skin
  --script FILE make FILE JS Panel's default script (what a JS Panel without its own script runs);
                the previous one is saved to build/mac-local-previous-script
  --restore     put back what this script replaced: the active skin and every component folder
                it overwrote (saved under build/mac-local-backup; one that wasn't there is removed) and the default script
Installed components are never deleted: the folder being replaced is moved to the backup first.
USAGE
}

set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
PROFILE="$HOME/Library/foobar2000-v2"
DB="$PROFILE/config.sqlite"
ACTIVE_SKIN='cfg_var.2A7E4C11-8B3F-4D6A-9E52-1C8F3A6B0D24'
SKINS_ROOT='cfg_var.1F8A6C2E-4D9B-4E71-8C3A-5B2D9E0F6A17'
SAVED="$REPO/build/mac-local-previous-skin"
BACKUP="$REPO/build/mac-local-backup"
DEFAULT_SCRIPT='cfg_var.4876F6EE-F0D3-47D7-BFA4-58D677FC9ADF'
SAVED_SCRIPT="$REPO/build/mac-local-previous-script"
LAUNCH=0 PUI=0 SKIN="" SCRIPT="" RESTORE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --launch) LAUNCH=1 ;;
    --panels-ui) PUI=1 ;;
    --skin) SKIN="${2:?--skin needs a folder}"; shift ;;
    --script) SCRIPT="${2:?--script needs a file}"; shift ;;
    --restore) RESTORE=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

quit_fb2k() {
  pgrep -x foobar2000 >/dev/null || return 0
  osascript -e 'tell application "foobar2000" to quit' >/dev/null 2>&1 || true
  for _ in $(seq 1 20); do pgrep -x foobar2000 >/dev/null || return 0; sleep 0.5; done
  echo "foobar2000 didn't quit" >&2; exit 1
}

install_bundle() {
  local src="$1" name; name="$(basename "$src" .component)"
  [ -d "$src" ] || { echo "no $src (build it first)" >&2; exit 1; }
  local dest="$PROFILE/user-components/$name"
  mkdir -p "$BACKUP"
  if [ ! -e "$BACKUP/$name" ] && [ ! -e "$BACKUP/$name.absent" ]; then
    if [ -d "$dest" ]; then mv "$dest" "$BACKUP/$name"; else touch "$BACKUP/$name.absent"; fi
  fi
  rm -rf "$dest"
  mkdir -p "$dest"
  cp -R "$src" "$PROFILE/user-components/$name/"
  echo "installed: $PROFILE/user-components/$name/$(basename "$src")"
}

quit_fb2k
if [ "$RESTORE" = 0 ]; then
  install_bundle "$REPO/build-mac/foo_js_panel.component"
  if [ "$PUI" = 1 ]; then install_bundle "$REPO/../foo_ui_panels/build-mac/foo_ui_panels.component"; fi
fi

if [ -n "$SKIN" ]; then
  root="$(sqlite3 "$DB" "select value from configStrings where name='$SKINS_ROOT'")"
  [ -n "$root" ] || root="$PROFILE/foo_ui_panels-skins"
  name="$(basename "$SKIN")"
  mkdir -p "$root" "$(dirname "$SAVED")"
  rm -rf "${root:?}/$name"
  cp -R "$SKIN" "$root/$name"
  [ -f "$SAVED" ] || sqlite3 "$DB" "select value from configStrings where name='$ACTIVE_SKIN'" > "$SAVED"
  sqlite3 "$DB" "insert or replace into configStrings(name, value) values('$ACTIVE_SKIN', '$name')"
  echo "active skin: $name (was: $(cat "$SAVED"))"
fi

if [ -n "$SCRIPT" ]; then
  [ -f "$SCRIPT" ] || { echo "no $SCRIPT" >&2; exit 1; }
  case "$SCRIPT" in *"'"*) echo "a script path with ' isn't supported" >&2; exit 1 ;; esac
  SCRIPT="$(cd "$(dirname "$SCRIPT")" && pwd)/$(basename "$SCRIPT")"
  mkdir -p "$(dirname "$SAVED_SCRIPT")"
  [ -f "$SAVED_SCRIPT" ] || sqlite3 "$DB" "select value from configStrings where name='$DEFAULT_SCRIPT'" > "$SAVED_SCRIPT"
  sqlite3 "$DB" "insert or replace into configStrings(name, value) values('$DEFAULT_SCRIPT', '$SCRIPT')"
  echo "default script: $SCRIPT"
fi

if [ "$RESTORE" = 1 ]; then
  if [ -f "$SAVED_SCRIPT" ]; then
    prev="$(cat "$SAVED_SCRIPT")"
    if [ -n "$prev" ]; then sqlite3 "$DB" "insert or replace into configStrings(name, value) values('$DEFAULT_SCRIPT', '$prev')"
    else sqlite3 "$DB" "delete from configStrings where name='$DEFAULT_SCRIPT'"; fi
    rm -f "$SAVED_SCRIPT"
    echo "default script restored: ${prev:-(built-in sample)}"
  fi
  if [ -f "$SAVED" ]; then
    prev="$(cat "$SAVED")"
    sqlite3 "$DB" "insert or replace into configStrings(name, value) values('$ACTIVE_SKIN', '$prev')"
    rm -f "$SAVED"
    echo "active skin restored: $prev"
  fi
  if [ -d "$BACKUP" ]; then
    for b in "$BACKUP"/*; do
      [ -e "$b" ] || continue
      name="$(basename "$b" .absent)"
      rm -rf "${PROFILE:?}/user-components/$name"
      if [ "$b" = "$BACKUP/$name.absent" ]; then rm -f "$b"; echo "removed: $name (wasn't installed)"
      else mv "$b" "$PROFILE/user-components/$name"; echo "restored: $name"; fi
    done
    rmdir "$BACKUP" 2>/dev/null || true
  fi
fi

if [ "$LAUNCH" = 1 ]; then open -a foobar2000; echo "launched foobar2000"; fi
