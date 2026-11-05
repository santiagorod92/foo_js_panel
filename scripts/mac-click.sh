#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/mac-click.sh X Y [right|double]
       scripts/mac-click.sh X Y drag X2 Y2
Brings this Mac's foobar2000 to the front and clicks at X,Y points from the top-left corner of
its main window (the coordinates of a make mac-shot screenshot at 1x), or drags with the left
button held from X,Y to X2,Y2. A negative coordinate counts from the right or bottom edge. Needs
the Accessibility permission for the terminal.
USAGE
}
case "${1:-}" in -h|--help|'') usage; exit 0 ;; esac
set -euo pipefail
X="$1"
Y="${2:?$(usage)}"
KIND="${3:-left}"
osascript -e 'tell application "foobar2000" to activate' >/dev/null 2>&1 || true
swift - "$X" "$Y" "$KIND" "${4:-0}" "${5:-0}" <<'SWIFT'
import CoreGraphics
import Foundation
let args = CommandLine.arguments
let dx = Double(args[1]) ?? 0, dy = Double(args[2]) ?? 0, kind = args[3], dx2 = Double(args[4]) ?? 0, dy2 = Double(args[5]) ?? 0
var origin = CGPoint.zero, size = CGSize.zero, area = 0.0
for _ in 0..<20 where area == 0 {
    let all = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
    for w in all where (w[kCGWindowOwnerName as String] as? String) == "foobar2000" && (w[kCGWindowLayer as String] as? Int) == 0 {
        let b = w[kCGWindowBounds as String] as? [String: Double] ?? [:]
        let a = (b["Width"] ?? 0) * (b["Height"] ?? 0)
        if a > area { area = a; origin = CGPoint(x: b["X"] ?? 0, y: b["Y"] ?? 0); size = CGSize(width: b["Width"] ?? 0, height: b["Height"] ?? 0) }
    }
    if area == 0 { usleep(250000) }
}
guard area > 0 else { FileHandle.standardError.write("no foobar2000 window on screen\n".data(using: .utf8)!); exit(1) }
func at(_ x: Double, _ y: Double) -> CGPoint {
    CGPoint(x: origin.x + (x < 0 ? size.width + x : x), y: origin.y + (y < 0 ? size.height + y : y))
}
let p = at(dx, dy)
if kind == "drag" {
    let q = at(dx2, dy2)
    CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: p, mouseButton: .left)?.post(tap: .cghidEventTap)
    usleep(150000)
    CGEvent(mouseEventSource: nil, mouseType: .leftMouseDown, mouseCursorPosition: p, mouseButton: .left)?.post(tap: .cghidEventTap)
    usleep(150000)
    for i in 1...20 {
        let t = Double(i) / 20
        let m = CGPoint(x: p.x + (q.x - p.x) * t, y: p.y + (q.y - p.y) * t)
        CGEvent(mouseEventSource: nil, mouseType: .leftMouseDragged, mouseCursorPosition: m, mouseButton: .left)?.post(tap: .cghidEventTap)
        usleep(40000)
    }
    usleep(300000)
    CGEvent(mouseEventSource: nil, mouseType: .leftMouseUp, mouseCursorPosition: q, mouseButton: .left)?.post(tap: .cghidEventTap)
    exit(0)
}
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
