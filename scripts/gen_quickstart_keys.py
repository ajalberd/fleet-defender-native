#!/usr/bin/env python3
"""Generate crisp, resolution-independent SVG and 1920x1080 PNG Quickstart Keyboard Layout
for Fleet Defender Gold (F-14 Tomcat) Native Host.

All bindings verified directly against F14.EXE CMDS.C / KEYCODES.H and native host main.cpp.
"""

from __future__ import annotations

import html
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO_ROOT = Path(__file__).resolve().parent.parent
DOCS_DIR = REPO_ROOT / "docs"
OUT_SVG = DOCS_DIR / "F14_Quickstart_Keys.svg"
OUT_PNG = DOCS_DIR / "F14_Quickstart_Keys.png"

CAT_STYLES = {
    "flight":   ("#173B5C", "#52B8FA", "Flight & Throttle"),
    "weapons":  ("#642023", "#F46862", "Weapons & Countermeasures"),
    "radar":    ("#184A32", "#4DE08D", "AWG-9 Radar & RIO"),
    "views":    ("#432864", "#BC86FA", "Views & Cameras"),
    "carrier":  ("#5C4418", "#F4C44E", "Carrier, Nav & Systems"),
    "native":   ("#0E5462", "#3BEAF2", "Native Host Enhancements"),
    "neutral":  ("#1E2633", "#4A596E", "Modifiers / Unbound"),
}

# Each key: (x, y, w, h, cap, line1, line2, category)
KEYS = [
    # Row 0: Function Row (y=0.0)
    (0.0,  0.0, 1.0, 0.92, "Esc", "Menu",     "",        "carrier"),
    (1.5,  0.0, 1.0, 0.92, "F1",  "Cockpit",  "S/A:Wng", "views"),
    (2.5,  0.0, 1.0, 0.92, "F2",  "HUD Only", "S/A:Wng", "views"),
    (3.5,  0.0, 1.0, 0.92, "F3",  "LSO Cam",  "S/A:Wng", "views"),
    (4.5,  0.0, 1.0, 0.92, "F4",  "Tower",    "S/A:Wng", "views"),
    (5.85, 0.0, 1.0, 0.92, "F5",  "Chase",    "S/A:Wng", "views"),
    (6.85, 0.0, 1.0, 0.92, "F6",  "Pilot",    "S/A:Wng", "views"),
    (7.85, 0.0, 1.0, 0.92, "F7",  "Missile",  "S/A:Wng", "views"),
    (8.85, 0.0, 1.0, 0.92, "F8",  "Padlock",  "S/A:Wng", "views"),
    (10.2, 0.0, 1.0, 0.92, "F9",  "Tac 1",    "S/A:Wng", "views"),
    (11.2, 0.0, 1.0, 0.92, "F10", "Tac 2",    "S/A:Wng", "views"),
    (12.2, 0.0, 1.0, 0.92, "F11", "Render",   "Native",  "native"),
    (13.2, 0.0, 1.0, 0.92, "F12", "Overlay",  "Native",  "native"),

    # Row 1: Number Row (y=1.18)
    (0.0,  1.18, 1.0, 1.0, "`",    "",        "",        "neutral"),
    (1.0,  1.18, 1.0, 1.0, "1",    "M61 Gun", "Wng #1",  "weapons"),
    (2.0,  1.18, 1.0, 1.0, "2",    "AIM-9",   "Wng #2",  "weapons"),
    (3.0,  1.18, 1.0, 1.0, "3",    "AIM-7",   "Wng #3",  "weapons"),
    (4.0,  1.18, 1.0, 1.0, "4",    "AIM-54",  "Wng #4",  "weapons"),
    (5.0,  1.18, 1.0, 1.0, "5",    "NAV",     "Wng #5",  "weapons"),
    (6.0,  1.18, 1.0, 1.0, "6",    "Wng #6",  "",        "carrier"),
    (7.0,  1.18, 1.0, 1.0, "7",    "Wng #7",  "",        "carrier"),
    (8.0,  1.18, 1.0, 1.0, "8",    "Wng #8",  "",        "carrier"),
    (9.0,  1.18, 1.0, 1.0, "9",    "Wng #9",  "",        "carrier"),
    (10.0, 1.18, 1.0, 1.0, "0",    "Wng #0",  "",        "carrier"),
    (11.0, 1.18, 1.0, 1.0, "-",    "Thr -",   "S:Idle",  "flight"),
    (12.0, 1.18, 1.0, 1.0, "=",    "Thr +",   "S:Mil",   "flight"),
    (13.0, 1.18, 2.0, 1.0, "Bksp", "Rdr Acq", "AWG-9",   "radar"),

    # Row 2: QWERTY (y=2.18)
    (0.0,  2.18, 1.5, 1.0, "Tab",  "",        "",        "neutral"),
    (1.5,  2.18, 1.0, 1.0, "Q",    "A:Quit",  "C:Azim",  "carrier"),
    (2.5,  2.18, 1.0, 1.0, "W",    "S:Wx",    "",        "carrier"),
    (3.5,  2.18, 1.0, 1.0, "E",    "EasyLnd", "S:EJECT", "carrier"),
    (4.5,  2.18, 1.0, 1.0, "R",    "Rdr Pwr", "C:Elv+",  "radar"),
    (5.5,  2.18, 1.0, 1.0, "T",    "1x Time", "S:Accel", "carrier"),
    (6.5,  2.18, 1.0, 1.0, "Y",    "",        "",        "neutral"),
    (7.5,  2.18, 1.0, 1.0, "U",    "",        "",        "neutral"),
    (8.5,  2.18, 1.0, 1.0, "I",    "IFF",     "Interr",  "radar"),
    (9.5,  2.18, 1.0, 1.0, "O",    "",        "",        "neutral"),
    (10.5, 2.18, 1.0, 1.0, "P",    "AutoPlt", "A:Pause", "flight"),
    (11.5, 2.18, 1.0, 1.0, "[",    "Trim -",  "S:Auto",  "flight"),
    (12.5, 2.18, 1.0, 1.0, "]",    "Trim +",  "S:Auto",  "flight"),
    (13.5, 2.18, 1.5, 1.0, "\\",   "",        "",        "neutral"),

    # Row 3: ASDF (y=3.18)
    (0.0,  3.18, 1.75, 1.0, "Caps", "",       "",        "neutral"),
    (1.75, 3.18, 1.0,  1.0, "A",    "A/B Max","C:Mode",  "flight"),
    (2.75, 3.18, 1.0,  1.0, "S",    "Next WP","S:Prev",  "carrier"),
    (3.75, 3.18, 1.0,  1.0, "D",    "Declut", "C:Spec",  "radar"),
    (4.75, 3.18, 1.0,  1.0, "F",    "Flare",  "C:Elv-",  "weapons"),
    (5.75, 3.18, 1.0,  1.0, "G",    "Gear",   "",        "carrier"),
    (6.75, 3.18, 1.0,  1.0, "H",    "Hook",   "S/C:HUD", "carrier"),
    (7.75, 3.18, 1.0,  1.0, "J",    "Jammer", "S:Jett",  "weapons"),
    (8.75, 3.18, 1.0,  1.0, "K",    "BrkLck", "",        "radar"),
    (9.75, 3.18, 1.0,  1.0, "L",    "Lock",   "C:Cat!",  "radar"),
    (10.75,3.18, 1.0,  1.0, ";",    "",       "",        "neutral"),
    (11.75,3.18, 1.0,  1.0, "'",    "RIO/Plt","",        "views"),
    (12.75,3.18, 2.25, 1.0, "Enter","Fire Gun","A:FullS","weapons"),

    # Row 4: ZXCV (y=4.18)
    (0.0,  4.18, 2.25, 1.0, "Shift","Shift",  "",        "neutral"),
    (2.25, 4.18, 1.0,  1.0, "Z",    "Zoom +", "",        "views"),
    (3.25, 4.18, 1.0,  1.0, "X",    "Zoom -", "",        "views"),
    (4.25, 4.18, 1.0,  1.0, "C",    "Chaff",  "",        "weapons"),
    (5.25, 4.18, 1.0,  1.0, "V",    "VDI",    "A:Vol",   "carrier"),
    (6.25, 4.18, 1.0,  1.0, "B",    "AirBrk", "",        "flight"),
    (7.25, 4.18, 1.0,  1.0, "N",    "",       "",        "neutral"),
    (8.25, 4.18, 1.0,  1.0, "M",    "MstArm", "A:Mem",   "weapons"),
    (9.25, 4.18, 1.0,  1.0, ",",    "Rud L",  "",        "flight"),
    (10.25,4.18, 1.0,  1.0, ".",    "Rud R",  "",        "flight"),
    (11.25,4.18, 1.0,  1.0, "/",    "RIOMap", "",        "radar"),
    (12.25,4.18, 2.75, 1.0, "Shift","Shift",  "",        "neutral"),

    # Row 5: Bottom Row (y=5.18)
    (0.0,  5.18, 1.5,  1.0, "Ctrl", "Ctrl",         "", "neutral"),
    (1.5,  5.18, 1.25, 1.0, "Win",  "",             "", "neutral"),
    (2.75, 5.18, 1.5,  1.0, "Alt",  "Alt",          "", "neutral"),
    (4.25, 5.18, 6.5,  1.0, "Space","FIRE MISSILE", "AIM-9 / AIM-7 / AIM-54 (Master Arm ON)", "weapons"),
    (10.75,5.18, 1.5,  1.0, "Alt",  "Alt",          "", "neutral"),
    (12.25,5.18, 1.25, 1.0, "Menu", "",             "", "neutral"),
    (13.5, 5.18, 1.5,  1.0, "Ctrl", "Ctrl",         "", "neutral"),

    # Nav Cluster (x=15.38)
    (15.38, 1.18, 1.0, 1.0, "Ins",  "Azim",  "Ctrl+Q", "radar"),
    (16.38, 1.18, 1.0, 1.0, "Home", "Bars",  "Ctrl+E", "radar"),
    (17.38, 1.18, 1.0, 1.0, "PgUp", "Elv +", "Ctrl+R", "radar"),
    (15.38, 2.18, 1.0, 1.0, "Del",  "Mode",  "Ctrl+A", "radar"),
    (16.38, 2.18, 1.0, 1.0, "End",  "ACM",   "Ctrl+D", "radar"),
    (17.38, 2.18, 1.0, 1.0, "PgDn", "Elv -", "Ctrl+F", "radar"),

    # Arrows (Virtual Stick)
    (16.38, 4.18, 1.0, 1.0, "Up",   "NoseDn","", "flight"),
    (15.38, 5.18, 1.0, 1.0, "Left", "Roll L","", "flight"),
    (16.38, 5.18, 1.0, 1.0, "Down", "NoseUp","", "flight"),
    (17.38, 5.18, 1.0, 1.0, "Right","Roll R","", "flight"),

    # Numpad (x=18.76)
    (18.76, 1.18, 1.0, 1.0, "Num",  "",       "", "neutral"),
    (19.76, 1.18, 1.0, 1.0, "/",    "",       "", "neutral"),
    (20.76, 1.18, 1.0, 1.0, "*",    "",       "", "neutral"),
    (21.76, 1.18, 1.0, 1.0, "-",    "Thr -",  "", "flight"),

    (18.76, 2.18, 1.0, 1.0, "7",    "",       "", "views"),
    (19.76, 2.18, 1.0, 1.0, "8",    "Lk Fwd", "", "views"),
    (20.76, 2.18, 1.0, 1.0, "9",    "",       "", "views"),
    (21.76, 2.18, 1.0, 2.0, "+",    "Thr +",  "", "flight"),

    (18.76, 3.18, 1.0, 1.0, "4",    "Lk Lft", "", "views"),
    (19.76, 3.18, 1.0, 1.0, "5",    "Center", "", "flight"),
    (20.76, 3.18, 1.0, 1.0, "6",    "Lk Rgt", "", "views"),

    (18.76, 4.18, 1.0, 1.0, "1",    "Rear L", "", "views"),
    (19.76, 4.18, 1.0, 1.0, "2",    "Lk Dwn", "", "views"),
    (20.76, 4.18, 1.0, 1.0, "3",    "Rear R", "", "views"),
    (21.76, 4.18, 1.0, 2.0, "Ent",  "Gun",    "", "weapons"),

    (18.76, 5.18, 2.0, 1.0, "0",    "",       "", "neutral"),
    (20.76, 5.18, 1.0, 1.0, ".",    "",       "", "neutral"),
]

CARDS = [
    ("NATIVE HOST & MOUSE", "#3BEAF2", [
        "F11 : Cycle Render Mode (VGA / 3D / Hybrid / VR)",
        "F12 : Open Native Settings, HOTAS, Audio & Quick Keys",
        "Alt+Enter : Toggle Borderless Fullscreen",
        "Right-Drag : Smooth 360° 3D Free-Look (Mid/F1 recenter)",
        "Left-Click : Cockpit Switches & RIO DDD/TID Lock",
    ]),
    ("CARRIER OPS & MODIFIERS", "#F4C44E", [
        "Ctrl+L : Carrier Catapult Launch (when hooked up)",
        "H / G / E : Arresting Tailhook / Gear / Easy Landing",
        "Shift+E / Shift+J : EJECT! / Jettison All Stores",
        "Alt+P / Alt+Q : Pause Flight / End Flight to Debrief",
        "Shift+T / T : Time Accel Up / Reset to 1x Normal",
    ]),
    ("WINGMAN COMMS & HOTAS", "#4DE08D", [
        "Alt+F1..F10 : Open Wingman Command Menu",
        "Shift+F1..F10 : Direct Wingman Orders",
        "1..0 : Select Wingman Submenu Option (or Weapons)",
        "Arrow Keys + KP5 : Native Seamless Virtual Flight Stick",
        "USB HOTAS : Plug-and-play via F12 -> HOTAS Mapper",
    ]),
]


def hex_to_rgb(h: str) -> tuple[int, int, int]:
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))


def generate_svg() -> None:
    W, H = 1920, 1080
    ox, oy = 48.0, 168.0
    u = 80.2
    gap = 6.5

    parts: list[str] = []
    parts.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">')
    parts.append("""  <defs>
    <linearGradient id="bgGrad" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0%" stop-color="#0B1320"/>
      <stop offset="100%" stop-color="#070D16"/>
    </linearGradient>
    <filter id="keyShadow" x="-4%" y="-4%" width="108%" height="112%">
      <feDropShadow dx="0" dy="3" stdDeviation="2" flood-color="#000000" flood-opacity="0.55"/>
    </filter>
  </defs>""")
    parts.append(f'  <rect width="{W}" height="{H}" fill="url(#bgGrad)"/>')
    parts.append(f'  <rect x="20" y="20" width="{W-40}" height="{H-40}" rx="16" fill="none" stroke="#243B56" stroke-width="2"/>')

    # Header
    parts.append('  <text x="48" y="72" fill="#4CE5FF" font-family="Segoe UI, Arial, sans-serif" font-size="36" font-weight="700" letter-spacing="1.2">FLEET DEFENDER GOLD (F-14 TOMCAT) — QUICKSTART KEYBOARD LAYOUT</text>')
    parts.append('  <text x="48" y="106" fill="#9BB6D1" font-family="Segoe UI, Arial, sans-serif" font-size="19">Native Host v0.1.0  •  Verified against F14.EXE  •  Modifiers: S = Shift, C = Ctrl, A = Alt  •  In-Game Interactive Scalable Map: F12 → Quick Keys</text>')

    # Legend strip
    lx = 48.0
    ly = 132.0
    for key in ["flight", "weapons", "radar", "views", "carrier", "native"]:
        fill, stroke, label = CAT_STYLES[key]
        parts.append(f'  <rect x="{lx:.1f}" y="{ly-14:.1f}" width="20" height="18" rx="4" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
        parts.append(f'  <text x="{lx+28:.1f}" y="{ly:.1f}" fill="#DCE9F6" font-family="Segoe UI, Arial, sans-serif" font-size="16" font-weight="600">{html.escape(label)}</text>')
        lx += 28 + len(label) * 9.6 + 26

    # Keyboard chassis
    kb_w = 22.76 * u
    kb_h = 6.20 * u
    parts.append(f'  <rect x="{ox-12:.1f}" y="{oy-12:.1f}" width="{kb_w+24:.1f}" height="{kb_h+24:.1f}" rx="14" fill="#0F1826" stroke="#2A425E" stroke-width="2"/>')

    # Keys
    for x, y, w, h, cap, l1, l2, cat in KEYS:
        fill, stroke, _ = CAT_STYLES[cat]
        kx = ox + x * u + gap / 2
        ky = oy + y * u + gap / 2
        kw = w * u - gap
        kh = h * u - gap
        parts.append(f'  <g filter="url(#keyShadow)">')
        parts.append(f'    <rect x="{kx:.1f}" y="{ky:.1f}" width="{kw:.1f}" height="{kh:.1f}" rx="8" fill="{fill}" stroke="{stroke}" stroke-width="1.8"/>')
        parts.append(f'    <line x1="{kx+8:.1f}" y1="{ky+2:.1f}" x2="{kx+kw-8:.1f}" y2="{ky+2:.1f}" stroke="#FFFFFF" stroke-opacity="0.22" stroke-width="1.2"/>')
        parts.append(f'    <text x="{kx+8:.1f}" y="{ky+23:.1f}" fill="#F6FAFF" font-family="Segoe UI, Arial, sans-serif" font-size="18" font-weight="700">{html.escape(cap)}</text>')
        if l1:
            parts.append(f'    <text x="{kx+8:.1f}" y="{ky+45:.1f}" fill="#D6ECFF" font-family="Segoe UI, Arial, sans-serif" font-size="14" font-weight="600">{html.escape(l1)}</text>')
        if l2:
            parts.append(f'    <text x="{kx+8:.1f}" y="{ky+kh-9:.1f}" fill="#FFD57E" font-family="Segoe UI, Arial, sans-serif" font-size="12.5" font-weight="600">{html.escape(l2)}</text>')
        parts.append('  </g>')

    # Bottom 3 Callout Cards
    card_y = 708.0
    card_h = 318.0
    card_gap = 24.0
    card_w = (W - 96.0 - 2 * card_gap) / 3.0
    for idx, (title, accent, lines) in enumerate(CARDS):
        cx = 48.0 + idx * (card_w + card_gap)
        parts.append(f'  <rect x="{cx:.1f}" y="{card_y:.1f}" width="{card_w:.1f}" height="{card_h:.1f}" rx="12" fill="#0F1A2A" stroke="#2B4463" stroke-width="1.8"/>')
        parts.append(f'  <rect x="{cx:.1f}" y="{card_y:.1f}" width="{card_w:.1f}" height="44" rx="12" fill="#15253A"/>')
        parts.append(f'  <rect x="{cx:.1f}" y="{card_y+32:.1f}" width="{card_w:.1f}" height="12" fill="#15253A"/>')
        parts.append(f'  <line x1="{cx:.1f}" y1="{card_y+44:.1f}" x2="{cx+card_w:.1f}" y2="{card_y+44:.1f}" stroke="{accent}" stroke-width="2.5"/>')
        parts.append(f'  <text x="{cx+18:.1f}" y="{card_y+29:.1f}" fill="{accent}" font-family="Segoe UI, Arial, sans-serif" font-size="19" font-weight="700" letter-spacing="0.8">{html.escape(title)}</text>')
        for li, line in enumerate(lines):
            ly_txt = card_y + 82.0 + li * 45.0
            parts.append(f'  <text x="{cx+20:.1f}" y="{ly_txt:.1f}" fill="#E4EFFA" font-family="Segoe UI, Arial, sans-serif" font-size="18">{html.escape(line)}</text>')

    parts.append('  <text x="48" y="1048" fill="#6F8AA6" font-family="Segoe UI, Arial, sans-serif" font-size="14">Fleet Defender Gold Native Host • Open-source native runtime (no copyrighted game assets included) • Scale this SVG cleanly to any resolution or print.</text>')
    parts.append("</svg>\n")

    DOCS_DIR.mkdir(parents=True, exist_ok=True)
    OUT_SVG.write_text("\n".join(parts), encoding="utf-8")
    print(f"Wrote vector SVG: {OUT_SVG} ({OUT_SVG.stat().st_size:,} bytes)")


def load_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    candidates = (
        ["C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf"]
        if bold
        else ["C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"]
    )
    for c in candidates:
        if Path(c).exists():
            return ImageFont.truetype(c, size)
    return ImageFont.load_default()


def generate_png() -> None:
    W, H = 1920, 1080
    img = Image.new("RGB", (W, H), (10, 17, 29))
    draw = ImageDraw.Draw(img)

    # Outer frame
    draw.rounded_rectangle([20, 20, W - 20, H - 20], radius=16, outline=(36, 59, 86), width=2)

    f_title = load_font(34, bold=True)
    f_sub   = load_font(18, bold=False)
    f_leg   = load_font(16, bold=True)
    f_cap   = load_font(18, bold=True)
    f_l1    = load_font(14, bold=True)
    f_l2    = load_font(12, bold=True)
    f_ctit  = load_font(19, bold=True)
    f_cbody = load_font(18, bold=False)
    f_foot  = load_font(14, bold=False)

    draw.text((48, 36), "FLEET DEFENDER GOLD (F-14 TOMCAT) — QUICKSTART KEYBOARD LAYOUT", font=f_title, fill=(76, 229, 255))
    draw.text((48, 82), "Native Host v0.1.0  •  Verified against F14.EXE  •  Modifiers: S = Shift, C = Ctrl, A = Alt  •  In-Game Scalable Map: F12 → Quick Keys", font=f_sub, fill=(155, 182, 209))

    # Legend
    lx, ly = 48, 118
    for key in ["flight", "weapons", "radar", "views", "carrier", "native"]:
        fill, stroke, label = CAT_STYLES[key]
        draw.rounded_rectangle([lx, ly, lx + 20, ly + 18], radius=4, fill=hex_to_rgb(fill), outline=hex_to_rgb(stroke), width=2)
        draw.text((lx + 28, ly - 1), label, font=f_leg, fill=(220, 233, 246))
        bbox = draw.textbbox((0, 0), label, font=f_leg)
        lx += 28 + (bbox[2] - bbox[0]) + 28

    ox, oy = 48.0, 168.0
    u = 80.2
    gap = 6.5
    kb_w = 22.76 * u
    kb_h = 6.20 * u
    draw.rounded_rectangle([ox - 12, oy - 12, ox + kb_w + 12, oy + kb_h + 12], radius=14, fill=(15, 24, 38), outline=(42, 66, 94), width=2)

    for x, y, w, h, cap, l1, l2, cat in KEYS:
        fill, stroke, _ = CAT_STYLES[cat]
        kx = ox + x * u + gap / 2
        ky = oy + y * u + gap / 2
        kw = w * u - gap
        kh = h * u - gap
        # Shadow
        draw.rounded_rectangle([kx + 2, ky + 3, kx + kw + 2, ky + kh + 3], radius=8, fill=(4, 7, 12))
        # Keycap
        draw.rounded_rectangle([kx, ky, kx + kw, ky + kh], radius=8, fill=hex_to_rgb(fill), outline=hex_to_rgb(stroke), width=2)
        draw.text((kx + 8, ky + 5), cap, font=f_cap, fill=(246, 250, 255))
        if l1:
            draw.text((kx + 8, ky + 28), l1, font=f_l1, fill=(214, 236, 255))
        if l2:
            draw.text((kx + 8, ky + kh - 20), l2, font=f_l2, fill=(255, 213, 126))

    # Bottom 3 Callout Cards
    card_y = 708
    card_h = 318
    card_gap = 24
    card_w = (W - 96 - 2 * card_gap) / 3.0
    for idx, (title, accent, lines) in enumerate(CARDS):
        cx = 48 + idx * (card_w + card_gap)
        ac_rgb = hex_to_rgb(accent)
        draw.rounded_rectangle([cx, card_y, cx + card_w, card_y + card_h], radius=12, fill=(15, 26, 42), outline=(43, 68, 99), width=2)
        draw.rounded_rectangle([cx + 1, card_y + 1, cx + card_w - 1, card_y + 44], radius=11, fill=(21, 37, 58))
        draw.rectangle([cx + 1, card_y + 30, cx + card_w - 1, card_y + 44], fill=(21, 37, 58))
        draw.line([cx, card_y + 44, cx + card_w, card_y + 44], fill=ac_rgb, width=3)
        draw.text((cx + 18, card_y + 11), title, font=f_ctit, fill=ac_rgb)
        for li, line in enumerate(lines):
            draw.text((cx + 20, card_y + 62 + li * 46), line, font=f_cbody, fill=(228, 239, 250))

    draw.text((48, 1038), "Fleet Defender Gold Native Host • Open-source native runtime (no copyrighted game assets included) • Also available as infinitely scalable SVG in docs/F14_Quickstart_Keys.svg", font=f_foot, fill=(111, 138, 166))

    DOCS_DIR.mkdir(parents=True, exist_ok=True)
    img.save(OUT_PNG, "PNG", optimize=True)
    print(f"Wrote 1920x1080 PNG: {OUT_PNG} ({OUT_PNG.stat().st_size:,} bytes)")


def main() -> None:
    generate_svg()
    generate_png()


if __name__ == "__main__":
    main()
