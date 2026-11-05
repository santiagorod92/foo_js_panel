#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/mac-shot.sh [OUT.png] [TITLE]
Saves a screenshot of this Mac's foobar2000 main window (default build/ui-test/mac-shot.png),
even when it isn't the frontmost window, or of its window whose title contains TITLE. Needs
Screen Recording permission for the terminal.
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$REPO/build/ui-test/mac-shot.png}"
mkdir -p "$(dirname "$OUT")"
id="$(swift - "${2:-}" <<'SWIFT'
import CoreGraphics
let title = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : ""
let all = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as? [[String: Any]] ?? []
var best: (Int, Double) = (0, 0)
for w in all where (w[kCGWindowOwnerName as String] as? String) == "foobar2000" && (w[kCGWindowLayer as String] as? Int) == 0 {
    let b = w[kCGWindowBounds as String] as? [String: Double] ?? [:]
    if !title.isEmpty && !((w[kCGWindowName as String] as? String) ?? "").contains(title) { continue }
    let area = (b["Width"] ?? 0) * (b["Height"] ?? 0)
    if area > best.1, let n = w[kCGWindowNumber as String] as? Int { best = (n, area) }
}
print(best.0)
SWIFT
)"
[ "$id" != 0 ] || { echo "no foobar2000 window" >&2; exit 1; }
screencapture -x -o -l "$id" "$OUT"
echo "$OUT"
