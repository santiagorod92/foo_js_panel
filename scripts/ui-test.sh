#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/ui-test.sh <mac|win11-arm> smoke
Runs tests/scripts/smoke.js in a real foobar2000, hosted by Panels UI through the
tests/skins/panels-ui-embed skin, and asserts on foo_js_panel.log: the session banner, the
script loading (include + titleformat), on_size, two paints (the second after the interval timer
cleared itself), SetTimeout and whether foo_navidrome is reachable (its artists when configured). The active skin, the default script and the verbose-logging
setting are put back afterwards (on mac also every component it replaced); the screenshot lands in build/ui-test/.
  mac        this Mac's foobar2000 (build-mac/, make mac-build first); needs Panels UI built
             in ../foo_ui_panels/build-mac
  win11-arm  the Windows 11 ARM VM (../foo_navidrome/scripts/win11-arm.sh, WIN11_ARM=...);
             ARCH=arm64ec (default), x64 (same ARM foobar2000) or x86 (portable 32-bit install);
             Panels UI must already be the guest's UI module
USAGE
}
case "${1:-}" in -h|--help|'') usage; exit 0 ;; esac

set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
PLATFORM="$1"
SCENARIO="${2:-smoke}"
[ "$SCENARIO" = smoke ] || { usage >&2; exit 2; }
SHOTS="$REPO/build/ui-test"
mkdir -p "$SHOTS"
ACTIVE_SKIN='cfg_var.2A7E4C11-8B3F-4D6A-9E52-1C8F3A6B0D24'
DEFAULT_SCRIPT='cfg_var.4876F6EE-F0D3-47D7-BFA4-58D677FC9ADF'
VERBOSE='cfg_var.BBE2ECF8-9204-42D9-A7F1-0D5293B95080'
SKIN=panels-ui-embed
EXPECT=('foo_js_panel .* QuickJS' 'smoke: loaded, include ok' 'smoke: size ' 'smoke: paint .* ticks=0'
        'smoke: timeout ok' 'smoke: timer ok' 'smoke: paint .* ticks=3' 'smoke: navidrome available=')

say() { printf '==> %s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

check_log() {
  local log="$1" missing=0
  for re in "${EXPECT[@]}"; do
    if grep -aEq "$re" <<<"$log"; then echo "  ok   $re"; else echo "  FAIL $re"; missing=1; fi
  done
  if grep -aq '  ERROR  ' <<<"$log"; then echo "  FAIL errors in the log:"; grep -a '  ERROR  ' <<<"$log" | head -5; missing=1; fi
  return "$missing"
}

MAC_PROFILE="$HOME/Library/foobar2000-v2"
MAC_DB="$MAC_PROFILE/config.sqlite"
mac_restore() {
  osascript -e 'tell application "foobar2000" to quit' >/dev/null 2>&1 || true
  for _ in $(seq 1 20); do pgrep -x foobar2000 >/dev/null || break; sleep 0.5; done
  "$REPO/scripts/mac-local.sh" --restore
  sqlite3 "$MAC_DB" "delete from configStrings where name='$DEFAULT_SCRIPT'; delete from configInts where name='$VERBOSE';"
  rm -rf "$MAC_PROFILE/foo_ui_panels-skins/$SKIN"
}

mac_smoke() {
  local log
  [ -d "$REPO/build-mac/foo_js_panel.component" ] || fail "no build-mac/foo_js_panel.component (make mac-build)"
  trap mac_restore EXIT
  "$REPO/scripts/mac-local.sh" --panels-ui --skin "$REPO/tests/skins/$SKIN"
  sqlite3 "$MAC_DB" "insert or replace into configStrings(name,value) values('$DEFAULT_SCRIPT','$REPO/tests/scripts/smoke.js');
                 insert or replace into configInts(name,value) values('$VERBOSE',1);"
  local start; start=$(( $(wc -c < "$MAC_PROFILE/foo_js_panel.log" 2>/dev/null || echo 0) + 1 ))
  say "launch foobar2000"
  open -a foobar2000
  for _ in $(seq 1 60); do
    log="$(tail -c +"$start" "$MAC_PROFILE/foo_js_panel.log" 2>/dev/null || true)"
    grep -aq 'smoke: paint .* ticks=3' <<<"$log" && break
    sleep 1
  done
  say "log"
  check_log "$log"
}

VM="${WIN11_ARM:-$REPO/../foo_navidrome/scripts/win11-arm.sh}"
VM_STAGE=""
VM_PREV=""
VM_DIRS=()
win11_arm_restore() {
  "$VM" cfg "insert or replace into configStrings(name,value) values('$ACTIVE_SKIN','$VM_PREV');
             delete from configStrings where name='$DEFAULT_SCRIPT'; delete from configInts where name='$VERBOSE';" >/dev/null 2>&1 || true
  local d
  for d in "${VM_DIRS[@]+"${VM_DIRS[@]}"}"; do "$VM" ssh "rmdir /s /q \"$d\"" >/dev/null 2>&1 || true; done
  "$VM" fb2k start >/dev/null 2>&1 || true
  [ -n "$VM_STAGE" ] && rm -rf "$VM_STAGE"
  say "restored skin '$VM_PREV'"
}

win11_arm_smoke() {
  local arch="${ARCH:-arm64ec}" dll log sql
  [ -x "$VM" ] || fail "no $VM (clone foo_navidrome next to this repo, or set WIN11_ARM)"
  case "$arch" in x64) dll="$REPO/build/foo_js_panel.dll" ;; *) dll="$REPO/build-$arch/foo_js_panel.dll" ;; esac
  [ -f "$dll" ] || fail "no $dll (WIN_ARCH=$arch ./build.sh)"
  if [ "$arch" = x86 ]; then
    export FB2K_ARCH=x86
    VM_DIRS=('C:\fb2k-x86\profile\user-components\foo_js_panel')
  else
    export FB2K_ARCH=arm64ec
    VM_DIRS=('%APPDATA%\foobar2000-v2\user-components-arm64ec\foo_js_panel')
  fi
  VM_DIRS+=('%APPDATA%\foobar2000-v2\foo_ui_panels-skins\panels-ui-embed' '%LOCALAPPDATA%\Temp\jsp-scripts')
  VM_STAGE="$(mktemp -d)"
  local stage="$VM_STAGE" vm="$VM"
  VM_PREV="$("$vm" cfg "select value from configStrings where name='$ACTIVE_SKIN'" 2>/dev/null | tail -1 | tr -d '\r')"
  trap win11_arm_restore EXIT
  cp -R "$REPO/tests/scripts" "$stage/jsp-scripts"
  say "push skin + scripts"
  "$vm" push "$REPO/tests/skins/$SKIN" AppData/Roaming/foobar2000-v2/foo_ui_panels-skins >/dev/null
  "$vm" push "$stage/jsp-scripts" AppData/Local/Temp >/dev/null
  sql="insert or replace into configStrings(name,value) values('$ACTIVE_SKIN','$SKIN');
       insert or replace into configStrings(name,value) values('$DEFAULT_SCRIPT','C:\\Users\\tester\\AppData\\Local\\Temp\\jsp-scripts\\smoke.js');
       insert or replace into configInts(name,value) values('$VERBOSE',1);"
  "$vm" cfg "$sql" >/dev/null 2>&1
  say "deploy $(basename "$dll") ($arch)"
  "$vm" deploy "$dll" >/dev/null
  local logcmd='type "%APPDATA%\foobar2000-v2\foo_js_panel.log"'
  [ "$arch" = x86 ] && logcmd='type C:\fb2k-x86\profile\foo_js_panel.log'
  for _ in $(seq 1 45); do
    log="$("$vm" ssh "$logcmd" 2>/dev/null | awk '/==== foo_js_panel session/{b=""} {b=b $0 "\n"} END{printf "%s", b}')"
    grep -aq 'smoke: paint .* ticks=3' <<<"$log" && break
    sleep 2
  done
  "$vm" shot "$SHOTS/win11-arm-$arch.png" >/dev/null 2>&1 && say "screenshot $SHOTS/win11-arm-$arch.png"
  say "log"
  local banner="$arch"; [ "$arch" = arm64ec ] && banner=ARM64EC
  grep -aq "($banner)" <<<"$log" || fail "the session banner isn't the $arch build"
  check_log "$log"
}

case "$PLATFORM" in
  mac) mac_smoke ;;
  win11-arm) win11_arm_smoke ;;
  *) usage >&2; exit 2 ;;
esac
say "$PLATFORM smoke: ok"
