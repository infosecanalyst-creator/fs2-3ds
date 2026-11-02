#!/usr/bin/env python3
"""Check that every fixed on-screen string of the 3DS UI fits where it is drawn."""
import re, sys
sys.path.insert(0, sys.path[0])
from fontfit import Font
F1 = Font(sys.argv[1]); F2 = Font(sys.argv[2])
src = sys.argv[3]
bad = 0
def check(font, s, maxw, where):
    global bad
    w = font.width(s)
    if w > maxw:
        bad += 1
        print(f"TOO WIDE {w}>{maxw}  [{where}] {s!r}")

man = open(f"{src}/n3ds_manual.cpp", encoding='latin1').read()
for title, body in re.findall(r'\{ "([^"]+)", \{(.*?)NULL \} \}', man, re.S):
    check(F2, title, 390 - 8 - F1.width("6 / 6") - 8, "manual title (top)")
    check(F1, title, 152 - 6, "manual section button")
    for line in re.findall(r'"((?:[^"\\]|\\.)*)"', body):
        check(F1, line, 394 - 8 - 2, "manual text line (top)")
for s in ["D-Pad: choose   A: open   L / R: section", "Tap a section      B / START: close"]:
    check(F1, s, 320 - 6 - 4, "manual footer (bottom)")
check(F1, "Circle Pad / C-Stick: scroll", 400 - 16, "manual scroll hint")

dash = open(f"{src}/n3ds_dash.cpp", encoding='latin1').read()
clock = F1.width("23:59")
for st, title in re.findall(r'\{ (GS_STATE_\w+), "([^"]+)"', dash):
    w = F2.width(title)
    if w > 320 - 20 - clock and F1.width(title) > 320 - 20 - clock:
        check(F1, title, 320 - 20 - clock, "dash title")
for lbl in re.findall(r'\{\s*\d+, BTN_Y, (\d+), BTN_H, "([^"]+)" \}', dash):
    check(F1, lbl[1], int(lbl[0]) - 4, "dash button")
for s in re.findall(r'gr_string\(6, DASH_H - \d+, "([^"]+)"', dash):
    check(F1, s, 320 - 6 - 4, "dash footer")

panel = open(f"{src}/n3ds_panel.cpp", encoding='latin1').read()
for x, y, w, h, lbl in re.findall(r'\{\s*(\d+), (\d+), (\d+), (\d+), "([^"]+)",', panel):
    check(F1, lbl, int(w) - 2, "panel button")
for lbl in ["HULL", "AFTB"]:
    check(F1, lbl, 44 - 4 - 2, "panel bar label (left)")
for lbl in ["SHLD", "WPN", "THR"]:
    check(F1, lbl, 162 - 122 - 2, "panel bar label (right)")
check(F1, "SPD 9999", 122 - 4 - 4, "panel speed")
for lbl in ["PRI", "SEC"]:
    check(F1, lbl, 36 - 4 - 2, "panel weapon label")
print("all text fits" if bad == 0 else f"{bad} problem(s)")
sys.exit(1 if bad else 0)
