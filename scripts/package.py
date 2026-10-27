#!/usr/bin/env python3
import argparse, os, zipfile

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--dll")
ap.add_argument("--arm64ec")
ap.add_argument("--x86")
ap.add_argument("--mac")
a = ap.parse_args()
if not a.dll and not a.arm64ec and not a.x86 and not a.mac:
    ap.error("nothing to package")

def add(z, path, arc):
    zi = zipfile.ZipInfo.from_file(path, arc)
    zi.compress_type = zipfile.ZIP_DEFLATED
    with open(path, "rb") as f:
        z.writestr(zi, f.read())

if os.path.exists(a.out):
    os.remove(a.out)
with zipfile.ZipFile(a.out, "w") as z:
    if a.x86:
        add(z, a.x86, "foo_js_panel.dll")
    if a.dll:
        add(z, a.dll, "x64/foo_js_panel.dll")
    if a.arm64ec:
        add(z, a.arm64ec, "arm64ec/foo_js_panel.dll")
    if a.mac:
        base = os.path.dirname(os.path.abspath(a.mac))
        for root, _, files in os.walk(a.mac):
            for fn in sorted(files):
                p = os.path.join(root, fn)
                add(z, p, "mac/" + os.path.relpath(p, base))
for n in zipfile.ZipFile(a.out).namelist():
    print(n)
