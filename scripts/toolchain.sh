#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: . scripts/toolchain.sh   (sourced by build.sh, scripts/mac-build.sh, scripts/run-unit-tests.sh)
       scripts/toolchain.sh     (prints what it resolved)
Resolves the build inputs once, so every build and test entry point finds the same ones:
  SDK_ROOT  parent of foobar2000/ + pfc/: the checkout's parent when it has them, else
            ~/.local/share/foo_navidrome-sdk (the SDK copy foo_navidrome uses on a Mac),
            else the checkout's parent (build.sh then fetches the SDK there)
  XWIN      xwin splat: ~/.xwin, else ~/.local/share/xwin/sdk (foo_navidrome's Mac layout)
  PANELS_UI_SRC  foo_ui_panels' src/ (the shared gfx::Canvas + its GDI/CoreGraphics backends):
            ../foo_ui_panels/src next to this checkout
  PATH      on macOS, Homebrew's llvm (clang-cl, llvm-rc, llvm-mt) and lld (lld-link) first
USAGE
}

_jsp_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -z "${SDK_ROOT:-}" ]; then
  _jsp_parent="$(cd "$_jsp_root/.." && pwd)"
  _jsp_shared="$HOME/.local/share/foo_navidrome-sdk"
  if [ -d "$_jsp_parent/foobar2000/SDK" ] && [ -d "$_jsp_parent/pfc" ]; then SDK_ROOT="$_jsp_parent"
  elif [ -d "$_jsp_shared/foobar2000/SDK" ] && [ -d "$_jsp_shared/pfc" ]; then SDK_ROOT="$_jsp_shared"
  else SDK_ROOT="$_jsp_parent"; fi
fi
if [ -z "${PANELS_UI_SRC:-}" ]; then PANELS_UI_SRC="$(cd "$_jsp_root/.." && pwd)/foo_ui_panels/src"; fi
if [ -z "${XWIN:-}" ]; then
  if [ -d "$HOME/.xwin/crt" ] || [ ! -d "$HOME/.local/share/xwin/sdk/crt" ]; then XWIN="$HOME/.xwin"
  else XWIN="$HOME/.local/share/xwin/sdk"; fi
fi
if [ "$(uname)" = Darwin ] && command -v brew >/dev/null; then
  for _jsp_f in llvm lld; do
    _jsp_p="$(brew --prefix "$_jsp_f" 2>/dev/null)/bin"
    [ -d "$_jsp_p" ] && case ":$PATH:" in *":$_jsp_p:"*) ;; *) PATH="$_jsp_p:$PATH" ;; esac
  done
fi
export SDK_ROOT XWIN PANELS_UI_SRC PATH
unset _jsp_root _jsp_parent _jsp_shared _jsp_f _jsp_p

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  case "${1:-}" in -h|--help) usage; exit 0 ;; esac
  echo "SDK_ROOT=$SDK_ROOT"
  echo "XWIN=$XWIN"
  echo "PANELS_UI_SRC=$PANELS_UI_SRC"
  echo "clang-cl=$(command -v clang-cl || echo missing)"
  echo "lld-link=$(command -v lld-link || echo missing)"
fi
