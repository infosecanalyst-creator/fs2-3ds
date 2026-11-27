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
# manual hint strip (top screen, 400 wide): the code uses the first wording that fits, so the last one must
foot = re.findall(r'static const char \*const h[rl]\[\] = \{(.*?)\};', man, re.S)
for grp in foot:
    opts = re.findall(r'"((?:[^"\\]|\\.)*)"', grp)
    check(F1, opts[-1], 400 - 8, "manual hint strip, shortest wording")
for lbl in ["LEFT HAND: OFF", "LEFT HAND: ON", "CLOSE"]:
    check(F1, lbl, 152 - 6, "manual button")

dash = open(f"{src}/n3ds_dash.cpp", encoding='latin1').read()
clock = F1.width("23:59")
for st, title in re.findall(r'\{ (GS_STATE_\w+), "([^"]+)"', dash):
    w = F2.width(title)
    if w > 320 - 20 - clock and F1.width(title) > 320 - 20 - clock:
        check(F1, title, 320 - 20 - clock, "dash title")
# fixed row: four buttons across 320; action rows: n buttons across 320 (the 400-wide top screen has more room)
def roww(n): return (320 - 12 - 4 * (n - 1)) // n
for lbl in re.findall(r'Base_labels\[4\] = \{(.*?)\}', dash)[0].replace('"', '').split(','):
    check(F1, lbl.strip(), roww(4) - 4, "dash button")
nrows = 0
for n, body, widths in re.findall(r'\{ GS_STATE_\w+, (\d), \{ (.*?) \} \}(?:, \{ ([\d, ]+) \})? \},\n', dash):
    labels = re.findall(r'\{ "([^"]+)", ACT_', body)
    assert len(labels) == int(n), (n, labels)
    nrows += 1
    ws = [int(x) for x in widths.split(',')] if widths else [roww(int(n))] * int(n)
    assert sum(ws) + 4 * (int(n) - 1) <= 308, ("action row too wide", labels, ws)
    for lbl, w in zip(labels, ws):
        check(F1, lbl, w - 8, f"dash action button ({w} px, 4 px each side)")
assert nrows == dash.count('{ GS_STATE_') - dash.count('"PILOT SELECT"') * 0 - len(re.findall(r'\{ (GS_STATE_\w+), "([^"]+)"', dash)), "an action row was not read"
# START menu, settings and message pages: rows are 308 wide, hints use the first that fits
for lbl in re.findall(r'Pm_main(?:_menus)?\[\] = \{(.*?)\}', dash):
    for l in lbl.replace('"', '').split(','):
        check(F1, l.strip(), 308 - 8, "START menu row")
for s in re.findall(r'centred\(sw, \d+, (?:[^"]*\? )?"([^"]+)"(?: : "([^"]+)")?\)', dash):
    for t in s:
        if t: check(F1, t, 320 - 12, "START menu / message page text")
for grp in re.findall(r'static const char \*const (?:main_r|main_l|sub_r|sub_l|hr|hl)\[\] = \{(.*?)\};', dash, re.S):
    opts = re.findall(r'"((?:[^"\\]|\\.)*)"', grp)
    check(F1, opts[-1], 320 - 8, "hint strip, shortest wording")
for s in ["LEFT HAND: OFF", "DEBUG LOG: OFF", "LEFT HAND: ON", "DEBUG LOG: ON", "BACK", "YES", "NO", "FREESPACE 2", "PAUSED", "MESSAGE"]:
    check(F1, s, 152 - 8, "START menu button / header")
for s in re.findall(r'"((?:D-Rt|A)[^"]*START: menu)"', dash):
    pass
for s in ["D-Rt: press   START: menu", "A: press   START: menu", "D-Rt/tap: click   START: menu", "A/tap: click   START: menu", "Pad / C-Stick / slide here: pointer"]:
    check(F1, s, 320 - 8, "dash footer")
check(F1, "Stylus: mouse   Sticks: move the view   L / R: zoom", 400 - 8, "dash footer (top screen)")

panel = open(f"{src}/n3ds_panel.cpp", encoding='latin1').read()
for x, y, w, h, lbl in re.findall(r'\{\s*(\d+), (\d+), (\d+), (\d+), "([^"]+)",', panel):
    check(F1, lbl, int(w) - 6, "panel button (3 px each side)")
for lbl in ["HULL", "BURN"]:
    check(F1, lbl, 44 - 4 - 2, "panel bar label (left)")
for lbl in ["SHLD", "WPN", "THR"]:
    check(F1, lbl, 162 - 122 - 2, "panel bar label (right)")
check(F1, "SPEED 9999", 122 - 4 - 4 - 8 - F1.width("M"), "panel speed (with the M mark)")
for lbl in ["PRI", "SEC"]:
    check(F1, lbl, 36 - 4 - 2, "panel weapon label")
check(F1, "PAGE 12/12  TAP FOR MORE", 226 - 4, "panel page mark")
for w_ in re.findall(r'"(! [A-Z ]+)"', panel):
    check(F1, w_, 226 - 4, "panel threat warning")
print("all text fits" if bad == 0 else f"{bad} problem(s)")
sys.exit(1 if bad else 0)
