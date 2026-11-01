#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/mac-click.sh X Y [right|double]
Brings this Mac's foobar2000 to the front and clicks at X,Y points from the top-left corner of
its main window (the coordinates of a make mac-shot screenshot at 1x). Needs the Accessibility
permission for the terminal.
USAGE
}
case "${1:-}" in -h|--help|'') usage; exit 0 ;; esac
set -euo pipefail
X="$1"
Y="${2:?$(usage)}"
KIND="${3:-left}"
osascript -e 'tell application "foobar2000" to activate' >/dev/null 2>&1 || true
swift - "$X" "$Y" "$KIND" <<'SWIFT'
import CoreGraphics
import Foundation
let args = CommandLine.arguments
let dx = Double(args[1]) ?? 0, dy = Double(args[2]) ?? 0, kind = args[3]
let all = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
var origin = CGPoint.zero, area = 0.0
for w in all where (w[kCGWindowOwnerName as String] as? String) == "foobar2000" && (w[kCGWindowLayer as String] as? Int) == 0 {
    let b = w[kCGWindowBounds as String] as? [String: Double] ?? [:]
    let a = (b["Width"] ?? 0) * (b["Height"] ?? 0)
    if a > area { area = a; origin = CGPoint(x: b["X"] ?? 0, y: b["Y"] ?? 0) }
}
guard area > 0 else { FileHandle.standardError.write("no foobar2000 window on screen\n".data(using: .utf8)!); exit(1) }
let p = CGPoint(x: origin.x + dx, y: origin.y + dy)
let down: CGEventType = kind == "right" ? .rightMouseDown : .leftMouseDown
let up: CGEventType = kind == "right" ? .rightMouseUp : .leftMouseUp
let button: CGMouseButton = kind == "right" ? .right : .left
CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: p, mouseButton: button)?.post(tap: .cghidEventTap)
usleep(150000)
for n in 1...(kind == "double" ? 2 : 1) {
    let d = CGEvent(mouseEventSource: nil, mouseType: down, mouseCursorPosition: p, mouseButton: button)
    d?.setIntegerValueField(.mouseEventClickState, value: Int64(n))
    d?.post(tap: .cghidEventTap)
    usleep(60000)
    let u = CGEvent(mouseEventSource: nil, mouseType: up, mouseCursorPosition: p, mouseButton: button)
    u?.setIntegerValueField(.mouseEventClickState, value: Int64(n))
    u?.post(tap: .cghidEventTap)
    usleep(60000)
}
SWIFT
