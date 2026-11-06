#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/ui-test.sh <mac|mac-vm|win11-arm> smoke|dragdrop|jsp3|reload|import|editor (mac-vm: smoke|jsp3|reload|editor)
       scripts/ui-test.sh win11-arm menus|images|editor
Runs tests/scripts/smoke.js in a real foobar2000, hosted by Panels UI through the
tests/skins/panels-ui-embed skin, and asserts on foo_js_panel.log: the session banner, the
script loading (include + titleformat), on_size, two paints (the second after the interval timer
cleared itself), SetTimeout, whether foo_navidrome is reachable (its artists when configured), and from two
panels: properties, fb paths, a main menu command by path, a library query, plman and NotifyOthers. The active skin, the default script and the verbose-logging
setting are put back afterwards (on mac also every component it replaced); the screenshot lands in build/ui-test/.
menus (win11-arm) runs tests/scripts/menus.js instead: a popup menu with the Playback main menu,
checked/grayed items, a radio submenu and the playlist context menu (screenshot, then a click
outside it), then utils.InputBox (screenshot, types "hi" + Enter), then a click on the panel and
"a" (on_focus, on_char, on_key_up), then utils.ColourPicker (screenshot, Esc keeps the default), then window.ShowProperties
(screenshot, Esc);
it asserts each result in the log.
editor (win11-arm) runs tests/scripts/editor.js, whose line 3 doesn't parse, opens Edit script… from the
panel's menu and takes a screenshot: the editor opens on line 3, its number in red.
On mac and mac-vm, editor runs tests/scripts/editor-open.js instead, which opens its own editor with
window.EditScript() (mac-vm takes a screenshot: the code is highlighted).
import runs tests/scripts/import.js, which asks Panels UI (SKIN:IMPORT_FCL:<path>) to import Eole's
columnsUI_eole.fcl: on mac the copy in the profile, in the VM an unzipped copy outside it (so its
themes/ and wsh-data/ are copied in); it asserts that the new skin runs Eole's panels, then
removes the skins it made. Needs Eole in this Mac's profile.
reload runs a copy of tests/scripts/reload.js, changes it on disk while foobar2000 runs and asserts
that the panels reload it by themselves (Reload scripts when their files change).
jsp3 runs tests/scripts/jsp3.js, a JScript Panel 3 style script (preprocessor header, the bundled
lodash, string fonts, WriteText, Clear, centre/radius ellipses) and takes a screenshot.
dragdrop runs tests/scripts/dragdrop.js: the big panel copies two tracks to the clipboard
and reads them back, then a mouse drag from it to the small panel (fb.DoDragDrop) drops them into a
'jsp-drop' playlist through on_drag_drop; it asserts the clipboard, the drop and the source's effect (on mac the drag is asynchronous, so
DoDragDrop answers 0 there, and the clipboard's text is put back afterwards).
images (win11-arm) runs tests/scripts/images.js: off-screen images drawn with GetGraphics,
translucent shapes, a circle mask, a blurred shadow, a rotated image and a colour scheme, then
takes a screenshot (compare with make render SCRIPT=tests/scripts/images.js on macOS).
  mac        this Mac's foobar2000 (build-mac/, make mac-build first); needs Panels UI built
             in ../foo_ui_panels/build-mac
  mac-vm     the macOS VM on this Mac (terminal-mac, ../foo_navidrome/scripts/terminal-mac.sh,
             MAC_VM=...): the bundles, scripts/ and tests/ are copied to ~/jsp-test in the guest,
             mac-local.sh runs there over ssh, so this Mac's desktop and foobar2000 are never
             touched; screenshot in build/ui-test/. No dragdrop (terminal-mac's drag is one
             atomic action, too quick for an NSDraggingSession) and no import (no Eole there)
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
case "$PLATFORM:$SCENARIO" in mac-vm:import|mac-vm:dragdrop) usage >&2; exit 2 ;; *:smoke|*:dragdrop|*:jsp3|*:reload|*:import|win11-arm:menus|win11-arm:images|*:editor) ;; *) usage >&2; exit 2 ;; esac
SHOTS="$REPO/build/ui-test"
mkdir -p "$SHOTS"
ACTIVE_SKIN='cfg_var.2A7E4C11-8B3F-4D6A-9E52-1C8F3A6B0D24'
DEFAULT_SCRIPT='cfg_var.4876F6EE-F0D3-47D7-BFA4-58D677FC9ADF'
VERBOSE='cfg_var.BBE2ECF8-9204-42D9-A7F1-0D5293B95080'
SKIN=panels-ui-embed
EXPECT=('foo_js_panel .* QuickJS' 'smoke: loaded, include ok' 'smoke: size ' 'smoke: paint .* ticks=0'
        'smoke: timeout ok' 'smoke: timer ok' 'smoke: paint .* ticks=3' 'smoke: navidrome available='
        'smoke: properties runs=[0-9]+' 'smoke: paths profile=true foobar=true version=[0-9]' 'smoke: menu Playback/Stop=true nope=false'
        'smoke: library=[0-9]+ tracks=[0-9]+ query=[0-9]+' 'smoke: plman playlists=[0-9]+' 'smoke: notify from [0-9]+ to [0-9]+' 'smoke: menus built' 'smoke: pixels ff386898 fff80808:1$' 'smoke: activex fso=hello files=1 gone=true reg=10 ui=0 timers=function'
        'smoke: state outputs=[1-9][0-9]* active=1 dsp=[0-9]+ rg=true lock=AddItems\+RenamePlaylist/JS Panel unlocked=0 charset=0 map=AB' 'smoke: state-cb top=(true|false)'
        'smoke: script command run=true' 'smoke: main menu 2' 'smoke: tooltip smoke tooltip [0-9]+'
        'smoke: theme dui=ff[0-9a-f]{6},ff[0-9a-f]{6} cui=ff[0-9a-f]{6} font=.+ [0-9]+ sys=ff[0-9a-f]{6}'
        'smoke: fonts segoe=true semibold=true nope=false'
        'smoke: ini write=true read=[0-9]+,js panel missing=def package=null' 'smoke: queue found=true contents=[1-9][0-9]* removed=true'
        'smoke: queue-cb origin=0' 'smoke: add-locations items=1' 'smoke: module answer=42' 'smoke: fetch TypeError'
        'smoke: skin variable jsp.smoke=[0-9]+' 'smoke: skin message smoke-skin n=1 from=[0-9]+'
        'smoke: navidrome search (artists=[1-9][0-9]* albums=[0-9]+ tracks=[0-9]+ path=(navidrome|none)|skipped)')

say() { printf '==> %s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

all_seen() {
  local re
  for re in "${EXPECT[@]}"; do grep -aEq "$re" <<<"$1" || return 1; done
}

check_log() {
  local log="$1" missing=0
  for re in "${EXPECT[@]}"; do
    if grep -aEq "$re" <<<"$log"; then echo "  ok   $re"; else echo "  FAIL $re"; missing=1; fi
  done
  local errors; errors="$(grep -a '  ERROR  ' <<<"$log" | grep -avE "${EXPECTED_ERRORS:-^$}" || true)"
  if [ -n "$errors" ]; then echo "  FAIL errors in the log:"; head -5 <<<"$errors"; missing=1; fi
  return "$missing"
}

IMPORT_EXPECT=('foo_js_panel .* QuickJS' 'import: layout found' 'import: requested' 'loaded .*foo_ui_panels-skins.Eole[^/\\]*.js.titlebar[.]js')
RELOAD_EXPECT=('foo_js_panel .* QuickJS' 'reload: generation 1' 'reload: generation 2')
JSP3_EXPECT=('foo_js_panel .* QuickJS' 'jsp3: loaded lodash=function chunks=2 dui=true dark=boolean files=number now=true' 'jsp3: paint w=[1-9][0-9]*')
DRAGDROP_EXPECT=('foo_js_panel .* QuickJS' 'dragdrop: clipboard copied=true has=true count=2' 'dragdrop: ready source'
                 'dragdrop: ready target' 'dragdrop: enter effect=[1-3]' 'dragdrop: drop at ' 'dragdrop: added 2')
MAC_PROFILE="$HOME/Library/foobar2000-v2"
MAC_VM="${MAC_VM:-$REPO/../foo_navidrome/scripts/terminal-mac.sh}"
MAC_REPO="$REPO"
m() { bash -c "$1"; }
mac_vm_init() {
  [ -x "$MAC_VM" ] || fail "no $MAC_VM (clone foo_navidrome next to this repo, or set MAC_VM)"
  m() { "$MAC_VM" ssh "$1"; }
  local home; home="$(m 'echo $HOME')"
  [ -n "$home" ] || fail "no ssh into the VM ($MAC_VM status)"
  MAC_PROFILE="$home/Library/foobar2000-v2"
  MAC_REPO="$home/jsp-test/foo_js_panel"
  [ -d "$REPO/../foo_ui_panels/build-mac/foo_ui_panels.component" ] || fail "no ../foo_ui_panels/build-mac/foo_ui_panels.component (make mac-build there)"
  say "push the bundles, scripts and tests"
  m "rm -rf ~/jsp-test && mkdir -p ~/jsp-test/foo_js_panel ~/jsp-test/foo_ui_panels"
  tar -C "$REPO" -cf - scripts tests build-mac/foo_js_panel.component | m "tar -xf - -C '$MAC_REPO'"
  tar -C "$REPO/../foo_ui_panels" -cf - build-mac/foo_ui_panels.component | m "tar -xf - -C ~/jsp-test/foo_ui_panels"
}
mac_quit() {
  if [ "$PLATFORM" = mac-vm ]; then "$MAC_VM" fb2k stop >/dev/null 2>&1 || true; return 0; fi
  osascript -e 'tell application "foobar2000" to quit' >/dev/null 2>&1 || true
  for _ in $(seq 1 20); do pgrep -x foobar2000 >/dev/null || break; sleep 0.5; done
}
mac_launch() {
  if [ "$PLATFORM" = mac-vm ]; then "$MAC_VM" fb2k start; else open -a foobar2000; fi
}
mac_crashes() { m 'ls ~/Library/Logs/DiagnosticReports 2>/dev/null | grep -c "^foobar2000" || true'; }
mac_restore() {
  mac_quit
  m "MAC_LOCAL_STATE='$MAC_LOCAL_STATE' '$MAC_REPO/scripts/mac-local.sh' --restore"
  if [ -n "$MAC_PREV_VERBOSE" ]; then m "sqlite3 '$MAC_DB' \"insert or replace into configInts(name,value) values('$VERBOSE',$MAC_PREV_VERBOSE);\""
  else m "sqlite3 '$MAC_DB' \"delete from configInts where name='$VERBOSE';\""; fi
  if [ -n "${MAC_SKINS_BEFORE:-}" ]; then
    local d
    for d in $(m "ls '$MAC_PROFILE/foo_ui_panels-skins'"); do
      grep -qxF "$d" "$MAC_SKINS_BEFORE" || m "rm -rf '$MAC_PROFILE/foo_ui_panels-skins/$d'"
    done
    rm -f "$MAC_SKINS_BEFORE"
  fi
  m "rm -rf '$MAC_PROFILE/foo_ui_panels-skins/$SKIN' '$MAC_LOCAL_STATE'"
  [ -n "${MAC_CLIP:-}" ] && { m pbcopy < "$MAC_CLIP"; rm -f "$MAC_CLIP"; }
  [ "$PLATFORM" = mac-vm ] && m 'rm -rf ~/jsp-test'
  return 0
}

mac_smoke() {
  local log
  [ -d "$REPO/build-mac/foo_js_panel.component" ] || fail "no build-mac/foo_js_panel.component (make mac-build)"
  [ "$PLATFORM" = mac-vm ] && mac_vm_init
  MAC_DB="$MAC_PROFILE/config.sqlite"
  MAC_LOG="$MAC_PROFILE/foo_js_panel.log"
  MAC_LOCAL_STATE="$(m 'mktemp -d')"
  MAC_PREV_VERBOSE="$(m "sqlite3 '$MAC_DB' \"select value from configInts where name='$VERBOSE'\"")"
  local crashes_before; crashes_before="$(mac_crashes)"
  trap mac_restore EXIT
  if [ "$SCENARIO" = import ]; then
    MAC_SKINS_BEFORE="$(mktemp)"
    m "ls '$MAC_PROFILE/foo_ui_panels-skins'" > "$MAC_SKINS_BEFORE"
  fi
  if [ "$SCENARIO" = dragdrop ]; then
    MAC_CLIP="$(mktemp)"
    m pbpaste > "$MAC_CLIP" 2>/dev/null || true
  fi
  local script="$MAC_REPO/tests/scripts/$SCENARIO.js"
  [ "$SCENARIO" = editor ] && script="$MAC_REPO/tests/scripts/editor-open.js"
  if [ "$SCENARIO" = reload ]; then
    script="$MAC_LOCAL_STATE/reload.js"
    m "cp '$MAC_REPO/tests/scripts/reload.js' '$script'"
  fi
  mac_quit
  m "MAC_LOCAL_STATE='$MAC_LOCAL_STATE' '$MAC_REPO/scripts/mac-local.sh' --panels-ui --skin '$MAC_REPO/tests/skins/$SKIN' --script '$script'"
  m "sqlite3 '$MAC_DB' \"insert or replace into configInts(name,value) values('$VERBOSE',1);\""
  local start; start=$(( $(m "wc -c < '$MAC_LOG' 2>/dev/null || echo 0") + 1 ))
  mac_log() { m "tail -c +$start '$MAC_LOG' 2>/dev/null" || true; }
  say "launch foobar2000"
  mac_launch
  local ready='smoke: paint .* ticks=3'
  [ "$SCENARIO" = dragdrop ] && ready='dragdrop: ready target'
  [ "$SCENARIO" = jsp3 ] && ready='jsp3: paint'
  [ "$SCENARIO" = reload ] && ready='reload: generation 1'
  [ "$SCENARIO" = editor ] && ready='editor: open' && EXPECT=('foo_js_panel .* QuickJS' 'editor: open editor 42 true')
  [ "$SCENARIO" = import ] && ready='titlebar[.]js'
  [ "$SCENARIO" = import ] && EXPECT=("${IMPORT_EXPECT[@]}")
  [ "$SCENARIO" = reload ] && EXPECT=("${RELOAD_EXPECT[@]}")
  [ "$SCENARIO" = jsp3 ] && EXPECT=("${JSP3_EXPECT[@]}")
  for _ in $(seq 1 60); do
    log="$(mac_log)"
    grep -aq "$ready" <<<"$log" && break
    sleep 1
  done
  if [ "$SCENARIO" = reload ]; then
    sleep 2
    m "sed -i '' 's/generation = 1/generation = 2/' '$script'"
    for _ in $(seq 1 15); do
      log="$(mac_log)"
      grep -aq 'reload: generation 2' <<<"$log" && break
      sleep 1
    done
  fi
  if [ "$SCENARIO" = dragdrop ]; then
    EXPECT=("${DRAGDROP_EXPECT[@]}" 'dragdrop: source effect=0')
    sleep 2
    "$REPO/scripts/mac-click.sh" 200 200 drag 200 -40 || say "the drag couldn't be posted"
    for _ in $(seq 1 15); do
      log="$(mac_log)"
      grep -aq 'dragdrop: added' <<<"$log" && break
      sleep 1
    done
  fi
  for _ in $(seq 1 15); do
    all_seen "$log" && break
    sleep 1
    log="$(mac_log)"
  done
  [ "$SCENARIO" = editor ] && sleep 2
  if [ "$PLATFORM" = mac-vm ]; then
    "$MAC_VM" shot "$SHOTS/mac-vm-$SCENARIO.png" >/dev/null 2>&1 && say "screenshot $SHOTS/mac-vm-$SCENARIO.png"
  fi
  say "log"
  check_log "$log"
  [ "$(mac_crashes)" = "$crashes_before" ] || fail "foobar2000 wrote a crash report (~/Library/Logs/DiagnosticReports)"
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
  local skins
  if [ "$arch" = x86 ]; then
    export FB2K_ARCH=x86
    skins='C:/fb2k-x86/profile/foo_ui_panels-skins'
    skins_win='C:\fb2k-x86\profile\foo_ui_panels-skins'
    crashes_win='C:\fb2k-x86\profile\crash reports'
    VM_DIRS=('C:\fb2k-x86\profile\user-components\foo_js_panel' 'C:\fb2k-x86\profile\foo_ui_panels-skins\panels-ui-embed')
  else
    export FB2K_ARCH=arm64ec
    skins='AppData/Roaming/foobar2000-v2/foo_ui_panels-skins'
    skins_win='%APPDATA%\foobar2000-v2\foo_ui_panels-skins'
    crashes_win='%APPDATA%\foobar2000-v2\crash reports'
    VM_DIRS=('%APPDATA%\foobar2000-v2\user-components-arm64ec\foo_js_panel' '%APPDATA%\foobar2000-v2\foo_ui_panels-skins\panels-ui-embed')
  fi
  VM_DIRS+=('%LOCALAPPDATA%\Temp\jsp-scripts')
  vm_crashes() { "$VM" ssh "dir /b \"$crashes_win\\*.txt\"" 2>/dev/null | tr -d '\r' | grep -c . || true; }
  local crashes_before; crashes_before="$(vm_crashes)"
  VM_STAGE="$(mktemp -d)"
  local stage="$VM_STAGE" vm="$VM"
  VM_PREV="$("$vm" cfg "select value from configStrings where name='$ACTIVE_SKIN'" 2>/dev/null | tail -1 | tr -d '\r')"
  trap win11_arm_restore EXIT
  cp -R "$REPO/tests/scripts" "$stage/jsp-scripts"
  say "push skin + scripts"
  "$vm" push "$REPO/tests/skins/$SKIN" "$skins" >/dev/null
  "$vm" push "$stage/jsp-scripts" AppData/Local/Temp >/dev/null
  if [ "$SCENARIO" = import ]; then
    [ -f "$MAC_PROFILE/themes/eole/columnsUI_eole.fcl" ] || fail "no Eole in $MAC_PROFILE/themes/eole to import"
    mkdir -p "$stage/EoleImport/themes" "$stage/EoleImport/wsh-data"
    cp -R "$MAC_PROFILE/themes/eole" "$stage/EoleImport/themes/"
    [ -d "$MAC_PROFILE/wsh-data/eole-settings" ] && cp -R "$MAC_PROFILE/wsh-data/eole-settings" "$stage/EoleImport/wsh-data/"
    say "push an unzipped Eole"
    "$vm" push "$stage/EoleImport" AppData/Local/Temp >/dev/null
    VM_DIRS+=('%LOCALAPPDATA%\Temp\EoleImport')
    VM_SKINS_BEFORE="$("$vm" ssh "dir /b \"$skins_win\"" 2>/dev/null | tr -d '\r')"
  fi
  if [ "$SCENARIO" = import ] || [ "$SCENARIO" = smoke ]; then
    local pui="$REPO/../foo_ui_panels/build-$arch/foo_ui_panels.dll"
    [ "$arch" = x64 ] && pui="$REPO/../foo_ui_panels/build/foo_ui_panels.dll"
    [ -f "$pui" ] || fail "no $pui (build foo_ui_panels for $arch)"
    say "deploy $(basename "$pui") ($arch)"
    "$vm" deploy "$pui" >/dev/null
  fi
  sql="insert or replace into configStrings(name,value) values('$ACTIVE_SKIN','$SKIN');
       insert or replace into configStrings(name,value) values('$DEFAULT_SCRIPT','C:\\Users\\tester\\AppData\\Local\\Temp\\jsp-scripts\\$SCENARIO.js');
       insert or replace into configInts(name,value) values('$VERBOSE',1);"
  "$vm" cfg "$sql" >/dev/null 2>&1
  say "deploy $(basename "$dll") ($arch)"
  "$vm" deploy "$dll" >/dev/null
  local logcmd='type "%APPDATA%\foobar2000-v2\foo_js_panel.log"'
  [ "$arch" = x86 ] && logcmd='type C:\fb2k-x86\profile\foo_js_panel.log'
  vm_log() { "$vm" ssh "$logcmd" 2>/dev/null | awk '/==== foo_js_panel session/{b=""} {b=b $0 "\n"} END{printf "%s", b}'; }
  wait_log() {
    for _ in $(seq 1 45); do
      log="$(vm_log)"
      grep -aEq "$1" <<<"$log" && return 0
      sleep 2
    done
    return 1
  }
  if [ "$SCENARIO" = menus ]; then
    EXPECT=('foo_js_panel .* QuickJS' 'menus: showing' 'menus: picked 0' 'inputbox: showing' 'inputbox: value=hi$'
            'menus: focus true' 'menus: char a' 'menus: key_up 65' 'picker: showing' 'picker: colour=ff123456' 'properties: showing')
    wait_log 'menus: showing' || fail "the popup menu never opened"
    sleep 2
    "$vm" shot "$SHOTS/win11-arm-$arch-menu.png" >/dev/null 2>&1 && say "screenshot $SHOTS/win11-arm-$arch-menu.png"
    "$vm" move 85 50 >/dev/null
    "$vm" click >/dev/null
    wait_log 'inputbox: showing' || fail "the input box never opened"
    sleep 2
    "$vm" shot "$SHOTS/win11-arm-$arch-inputbox.png" >/dev/null 2>&1 && say "screenshot $SHOTS/win11-arm-$arch-inputbox.png"
    "$vm" key h i ret >/dev/null
    wait_log 'inputbox: (value|cancelled)' || true
    "$vm" click >/dev/null
    sleep 1
    "$vm" key a >/dev/null
    wait_log 'menus: key_up' || true
    wait_log 'picker: showing' || true
    sleep 2
    "$vm" shot "$SHOTS/win11-arm-$arch-picker.png" >/dev/null 2>&1 && say "screenshot $SHOTS/win11-arm-$arch-picker.png"
    "$vm" key esc >/dev/null
    wait_log 'properties: showing' || true
    sleep 2
    "$vm" shot "$SHOTS/win11-arm-$arch-properties.png" >/dev/null 2>&1 && say "screenshot $SHOTS/win11-arm-$arch-properties.png"
    "$vm" key esc >/dev/null
  elif [ "$SCENARIO" = editor ]; then
    EXPECT=('foo_js_panel .* QuickJS' 'editor.js.*SyntaxError')
    EXPECTED_ERRORS='editor[.]js'
    wait_log 'SyntaxError' || true
    sleep 2
    "$vm" tap '50%,40%,right;wait,800;53%,43%;wait,2500' >/dev/null
    "$vm" shot "$SHOTS/win11-arm-$arch-editor.png" >/dev/null 2>&1 && say "screenshot $SHOTS/win11-arm-$arch-editor.png"
    "$vm" key esc >/dev/null
  elif [ "$SCENARIO" = import ]; then
    EXPECT=("${IMPORT_EXPECT[@]}")
    wait_log 'titlebar[.]js' || true
    sleep 4
    "$vm" shot "$SHOTS/win11-arm-$arch-import.png" >/dev/null 2>&1 && say "screenshot $SHOTS/win11-arm-$arch-import.png"
    local now d; now="$("$vm" ssh "dir /b \"$skins_win\"" 2>/dev/null | tr -d '\r')"
    while IFS= read -r d; do
      [ -n "$d" ] && ! grep -qxF "$d" <<<"$VM_SKINS_BEFORE" && VM_DIRS+=("$skins_win\\$d")
    done <<<"$now"
  elif [ "$SCENARIO" = reload ]; then
    EXPECT=("${RELOAD_EXPECT[@]}")
    wait_log 'reload: generation 1' || fail "the script never loaded"
    sleep 2
    "$vm" ssh "powershell -NoProfile -Command \"\$f = \$env:LOCALAPPDATA + '\\Temp\\jsp-scripts\\reload.js'; (Get-Content \$f) -replace 'generation = 1','generation = 2' | Set-Content \$f\"" >/dev/null
    wait_log 'reload: generation 2' || true
  elif [ "$SCENARIO" = jsp3 ]; then
    EXPECT=("${JSP3_EXPECT[@]}")
    wait_log 'jsp3: paint' || true
    "$vm" shot "$SHOTS/win11-arm-$arch-jsp3.png" >/dev/null 2>&1 && say "screenshot $SHOTS/win11-arm-$arch-jsp3.png"
  elif [ "$SCENARIO" = dragdrop ]; then
    EXPECT=("${DRAGDROP_EXPECT[@]}" 'dragdrop: source effect=1')
    wait_log 'dragdrop: ready target' || fail "the panels never loaded"
    sleep 2
    local drop_y=96%; [ "$arch" = x86 ] && drop_y=85%
    "$vm" tap "drag,50%,45%,50%,$drop_y" >/dev/null
    wait_log 'dragdrop: added' || true
    "$vm" shot "$SHOTS/win11-arm-$arch-dragdrop.png" >/dev/null 2>&1 && say "screenshot $SHOTS/win11-arm-$arch-dragdrop.png"
  elif [ "$SCENARIO" = images ]; then
    EXPECT=('foo_js_panel .* QuickJS' 'images: built art=' 'images: painted')
    wait_log 'images: painted' || true
    sleep 2
    "$vm" shot "$SHOTS/win11-arm-$arch-images.png" >/dev/null 2>&1 && say "screenshot $SHOTS/win11-arm-$arch-images.png"
  else
    wait_log 'smoke: paint .* ticks=3' || true
    "$vm" shot "$SHOTS/win11-arm-$arch.png" >/dev/null 2>&1 && say "screenshot $SHOTS/win11-arm-$arch.png"
    "$vm" tap 'hover,50%,50%;wait,1500' >/dev/null
    "$vm" shot "$SHOTS/win11-arm-$arch-tooltip.png" >/dev/null 2>&1 && say "screenshot $SHOTS/win11-arm-$arch-tooltip.png"
  fi
  for _ in $(seq 1 8); do
    all_seen "$log" && break
    sleep 2
    log="$(vm_log)"
  done
  say "log"
  local banner="$arch"; [ "$arch" = arm64ec ] && banner=ARM64EC
  grep -aq "($banner)" <<<"$log" || fail "the session banner isn't the $arch build"
  check_log "$log"
  [ "$(vm_crashes)" = "$crashes_before" ] || fail "foobar2000 wrote a crash report ($crashes_win)"
}

case "$PLATFORM" in
  mac|mac-vm) mac_smoke ;;
  win11-arm) win11_arm_smoke ;;
  *) usage >&2; exit 2 ;;
esac
say "$PLATFORM $SCENARIO: ok"
