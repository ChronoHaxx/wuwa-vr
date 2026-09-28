"""WuWa VR 60-second explainer renderer.

Draws every frame with Pillow (motion diagrams + typography, no game renders),
pipes raw RGB to ffmpeg/libx264, and writes captions.srt / transcript.md.
The only external media is a short, privacy-reviewed first-person clip from the
project website (read-only), labelled on screen as earlier-build footage.

Usage:  python render.py            (writes ../out/)
"""
import math
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "out"))
FFBIN = os.environ.get("FFBIN", "")
FFMPEG = os.path.join(FFBIN, "ffmpeg.exe")
CLIP = os.environ.get("WUWA_EXPLAINER_CLIP", os.path.abspath(os.path.join(HERE, "../../../site/media/feature-first-person.mp4")))
NOWIN = 0x08000000  # CREATE_NO_WINDOW

W, H, FPS, DUR = 1280, 720, 30, 60
NFRAMES = FPS * DUR

BG = (13, 17, 23)
PANEL = (22, 28, 38)
LINE = (48, 58, 72)
TEXT = (230, 237, 243)
MUTED = (139, 148, 158)
ACCENT = (79, 209, 197)
LEFT = (246, 173, 85)
RIGHT = (99, 179, 237)
WARN = (252, 129, 129)
OK = (104, 211, 145)

FD = "C:/Windows/Fonts/"


def font(name, size):
    return ImageFont.truetype(FD + name, size)


F = {
    "title": font("segoeuib.ttf", 92),
    "h1": font("segoeuib.ttf", 40),
    "h2": font("seguisb.ttf", 28),
    "body": font("segoeui.ttf", 22),
    "bodyb": font("seguisb.ttf", 22),
    "small": font("segoeui.ttf", 18),
    "smallb": font("seguisb.ttf", 18),
    "cap": font("seguisb.ttf", 27),
    "chap": font("seguisb.ttf", 16),
}

# (start, end, text) -- burned in and exported to captions.srt
CAPTIONS = [
    (0.3, 4.0, "WuWa VR: an unofficial, free fan mod that plays Wuthering Waves on a PC VR headset."),
    (4.0, 8.0, "It turns the game's single flat camera into two eye views with head tracking."),
    (8.0, 12.0, "The game renders a normal Unreal Engine camera, world and UI."),
    (12.0, 16.0, "UEVR plus our native hooks redirect the scene and the real game UI. Lua scripts handle camera, body and controls."),
    (16.0, 20.0, "A VR runtime (OpenXR) receives two eyes: the same moment, from slightly different viewpoints."),
    (20.0, 24.0, "19–26 Sep: real game UI in VR, first person, head, body and shadow improvements."),
    (24.0, 28.0, "27 Sep: turning Native Stereo Fix on repaired character materials (owner-reported)."),
    (28.0, 32.0, "28 Sep: the owner confirmed the ultimate camera fix in headset testing for the cases tested."),
    (32.0, 36.0, "Still open: one tree animates in both eyes up close…"),
    (36.0, 41.0, "…but freezes in the left eye far away, even with zero eye separation and equal projection scale."),
    (41.0, 45.0, "Still open: reflection submenus and some lighting and shadows."),
    (45.0, 50.0, "Next: name the actual failing asset, then compare what each eye's draw really receives."),
    (50.0, 55.0, "Then make the smallest proven fix, and test everything together in one grouped pass."),
    (55.0, 60.0, "Free and experimental. Source and beginner guide: github.com/ChronoHaxx/wuwa-vr"),
]

SCENES = [
    (0, 8, "What it is"),
    (8, 20, "How it works"),
    (20, 32, "What improved"),
    (32, 45, "What's still broken"),
    (45, 55, "What's next"),
    (55, 60, "Project status"),
]


# ---------------------------------------------------------------- helpers
def clamp(x, a=0.0, b=1.0):
    return a if x < a else b if x > b else x


def ease(x):
    x = clamp(x)
    return x * x * (3 - 2 * x)


def ramp(t, t0, dur=0.5):
    return ease((t - t0) / dur)


def mix(c, a, base=BG):
    return tuple(int(base[i] + (c[i] - base[i]) * a) for i in range(3))


def text(d, xy, s, f, col, a=1.0, anchor="la", base=BG):
    if a <= 0.01:
        return
    d.text(xy, s, font=f, fill=mix(col, a, base), anchor=anchor)


def wrap(s, f, maxw):
    words, lines, cur = s.split(), [], ""
    for w_ in words:
        trial = (cur + " " + w_).strip()
        if f.getlength(trial) <= maxw:
            cur = trial
        else:
            lines.append(cur)
            cur = w_
    if cur:
        lines.append(cur)
    return lines


def box(d, xyxy, a, outline=LINE, fill=PANEL, r=14, width=2):
    if a <= 0.01:
        return
    d.rounded_rectangle(xyxy, r, fill=mix(fill, a), outline=mix(outline, a), width=width)


def arrow(d, p0, p1, prog, col, a=1.0, width=3):
    if prog <= 0 or a <= 0.01:
        return
    x0, y0 = p0
    x1 = x0 + (p1[0] - x0) * prog
    y1 = y0 + (p1[1] - y0) * prog
    c = mix(col, a)
    d.line([(x0, y0), (x1, y1)], fill=c, width=width)
    if prog > 0.95:
        ang = math.atan2(y1 - y0, x1 - x0)
        s = 11
        pts = [(x1, y1),
               (x1 - s * math.cos(ang - 0.45), y1 - s * math.sin(ang - 0.45)),
               (x1 - s * math.cos(ang + 0.45), y1 - s * math.sin(ang + 0.45))]
        d.polygon(pts, fill=c)


def pulse(d, p0, p1, t, col, a=1.0, period=1.2):
    if a <= 0.01:
        return
    u = (t % period) / period
    x = p0[0] + (p1[0] - p0[0]) * u
    y = p0[1] + (p1[1] - p0[1]) * u
    d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=mix(col, a * math.sin(math.pi * u)))


def frustum(d, apex, ang, half, length, col, a=1.0, width=2, fillc=None):
    """Top-down view frustum sketch. ang in radians (0 = up)."""
    ax, ay = apex
    l = (ax + length * math.sin(ang - half), ay - length * math.cos(ang - half))
    r = (ax + length * math.sin(ang + half), ay - length * math.cos(ang + half))
    if fillc is not None:
        d.polygon([apex, l, r], fill=mix(fillc, a))
    c = mix(col, a)
    d.line([apex, l], fill=c, width=width)
    d.line([apex, r], fill=c, width=width)
    d.line([l, r], fill=mix(col, a * 0.5), width=1)


def tree(d, cx, base_y, scale, t, animate, a, trunk=(120, 96, 72), crown=(88, 170, 120), bg=PANEL):
    """Simple schematic tree; 'animate' wiggles the crown like wind."""
    if a <= 0.01:
        return
    tw, th = 8 * scale, 34 * scale
    d.rectangle([cx - tw / 2, base_y - th, cx + tw / 2, base_y], fill=mix(trunk, a, bg))
    blobs = [(-16, -44, 22), (15, -46, 21), (0, -64, 24)]
    for i, (bx, by, br) in enumerate(blobs):
        wob = (math.sin(t * 5.2 + i * 1.7) * 4.5 * scale) if animate else 0.0
        x, y, r = cx + bx * scale + wob, base_y + by * scale, br * scale
        d.ellipse([x - r, y - r, x + r, y + r], fill=mix(crown, a, bg))


def eye_icon(d, c, r, col, a):
    x, y = c
    d.ellipse([x - r, y - r * 0.62, x + r, y + r * 0.62], outline=mix(col, a), width=2)
    d.ellipse([x - r * 0.33, y - r * 0.33, x + r * 0.33, y + r * 0.33], fill=mix(col, a))


# ---------------------------------------------------------------- footage
def load_clip():
    """Decode ~6 s of the reviewed site clip at 416x312 into memory (read-only)."""
    w, h, n = 416, 312, 6 * FPS
    cmd = [FFMPEG, "-v", "error", "-i", CLIP, "-t", "6", "-vf", f"scale={w}:{h},fps={FPS}",
           "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True, creationflags=NOWIN).stdout
    count = len(raw) // (w * h * 3)
    arr = np.frombuffer(raw[: count * w * h * 3], np.uint8).reshape(count, h, w, 3)
    return [Image.fromarray(arr[i]) for i in range(min(n, count))]


# ---------------------------------------------------------------- scenes
def scene1(img, d, t):
    a = ramp(t, 0.2, 0.8)
    text(d, (W / 2, 120), "WuWa VR", F["title"], TEXT, a, "mm")
    text(d, (W / 2, 190), "An unofficial fan mod: Wuthering Waves on PC VR, built on UEVR",
         F["h2"], MUTED, ramp(t, 0.8), "mm")
    # one flat screen splits into two eye views
    sp = ease((t - 3.2) / 1.6)
    cy, sw, sh = 395, 420, 236
    if sp <= 0.001:
        box(d, [W / 2 - sw / 2, cy - sh / 2, W / 2 + sw / 2, cy + sh / 2], ramp(t, 1.0), outline=ACCENT)
        text(d, (W / 2, cy), "one flat camera", F["h2"], MUTED, ramp(t, 1.4), "mm", PANEL)
    else:
        off = 250 * sp
        pw = sw - 60 * sp
        for sgn, col, lab in ((-1, LEFT, "LEFT EYE"), (1, RIGHT, "RIGHT EYE")):
            cx = W / 2 + sgn * off
            box(d, [cx - pw / 2, cy - sh / 2, cx + pw / 2, cy + sh / 2], 1.0, outline=mix(col, 0.35 + 0.65 * sp, LINE))
            text(d, (cx, cy - sh / 2 + 26), lab, F["smallb"], col, sp, "mm", PANEL)
            # simple horizon + object with a small per-eye parallax shift
            d.line([(cx - pw / 2 + 20, cy + 50), (cx + pw / 2 - 20, cy + 50)], fill=mix(LINE, 1, PANEL), width=2)
            tree(d, cx - sgn * 10 * sp, cy + 50, 1.1, t, True, sp)
        eye_icon(d, (W / 2 - 34, cy + sh / 2 + 52), 18, LEFT, ramp(t, 4.6))
        eye_icon(d, (W / 2 + 34, cy + sh / 2 + 52), 18, RIGHT, ramp(t, 4.6))
        text(d, (W / 2, cy + sh / 2 + 90), "+ head tracking", F["small"], MUTED, ramp(t, 5.0), "mm")


def scene2(img, d, t):
    text(d, (60, 70), "How it works", F["h1"], TEXT, ramp(t, 0.1))
    text(d, (60, 118), "One game frame → two eye views", F["body"], MUTED, ramp(t, 0.4))
    top, bh = 200, 118
    cols = [(40, 260), (320, 580), (640, 900)]
    items = [
        (0.3, cols[0], top, "Wuthering Waves", "Unreal Engine camera, world and UI", MUTED),
        (3.2, cols[1], top, "UEVR + native hooks", "redirect the 3D scene and the real game UI", ACCENT),
        (4.8, cols[1], top + 180, "Lua scripts", "camera, first person, body, controls", ACCENT),
        (7.2, cols[2], top, "VR runtime (OpenXR)", "SteamVR, headset runtime or simulator", MUTED),
    ]
    for t0, (x0, x1), y, title, sub, oc in items:
        a = ramp(t, t0)
        box(d, [x0, y, x1, y + bh], a, outline=mix(oc, 0.8, LINE))
        text(d, (x0 + 18, y + 20), title, F["bodyb"], TEXT, a, base=PANEL)
        for i, ln in enumerate(wrap(sub, F["small"], x1 - x0 - 36)):
            text(d, (x0 + 18, y + 56 + i * 24), ln, F["small"], MUTED, a, base=PANEL)
    my = top + bh / 2
    arrow(d, (262, my), (316, my), ease((t - 2.6) / 0.6), TEXT)
    arrow(d, ((320 + 580) / 2, top + 180), ((320 + 580) / 2, top + bh + 4), ease((t - 5.6) / 0.6), ACCENT)
    arrow(d, (582, my), (636, my), ease((t - 6.6) / 0.6), TEXT)
    arrow(d, (902, my), (956, my), ease((t - 8.4) / 0.6), TEXT)
    if t > 3.2:
        pulse(d, (262, my), (316, my), t, ACCENT)
    if t > 7.2:
        pulse(d, (582, my), (636, my), t + 0.4, ACCENT)
    if t > 9.0:
        pulse(d, (902, my), (956, my), t + 0.8, ACCENT)
    # two-eye frustum sketch (top-down)
    a = ramp(t, 8.6, 0.7)
    if a > 0.01:
        cx, ay = 1112, 480
        sep = 26
        frustum(d, (cx - sep, ay), -0.05, 0.34, 230, LEFT, a, fillc=mix(LEFT, 0.10))
        frustum(d, (cx + sep, ay), 0.05, 0.34, 230, RIGHT, a, fillc=mix(RIGHT, 0.10))
        eye_icon(d, (cx - sep, ay + 16), 12, LEFT, a)
        eye_icon(d, (cx + sep, ay + 16), 12, RIGHT, a)
        text(d, (cx, ay + 52), "two eyes, one instant", F["smallb"], TEXT, a, "mm")
        text(d, (cx, ay + 76), "different position + projection", F["small"], MUTED, a, "mm")
        text(d, (cx, 228), "top-down sketch", F["small"], MUTED, a * 0.8, "mm")


def scene3(img, d, t, clip):
    text(d, (60, 70), "What improved", F["h1"], TEXT, ramp(t, 0.1))
    text(d, (60, 118), "19–28 September 2026", F["body"], MUTED, ramp(t, 0.3))
    items = [
        ("19–24 Sep", "Real game UI redirected into VR · first person · HUD", OK),
        ("24–26 Sep", "Head placement · hidden head, full shadow · recovery", OK),
        ("26–27 Sep", "Public beta, source and portable launcher", OK),
        ("27 Sep", "Native Stereo Fix ON: character materials repaired*", OK),
        ("28 Sep", "Ultimate camera fix: owner-confirmed in headset*", OK),
    ]
    x = 88
    y0, step = 180, 84
    la = ramp(t, 0.3)
    d.line([(x, y0 - 6), (x, y0 - 6 + (len(items) - 1) * step * ease(t / 9.5) + 12)], fill=mix(LINE, la), width=3)
    for i, (when, what, col) in enumerate(items):
        a = ramp(t, 0.6 + i * 1.8)
        y = y0 + i * step
        if a > 0.01:
            d.ellipse([x - 8, y - 8, x + 8, y + 8], fill=mix(col, a))
            text(d, (x + 26, y - 16), when, F["bodyb"], ACCENT, a)
            text(d, (x + 26, y + 12), what, F["body"], TEXT, a)
    text(d, (x + 26, y0 + 5 * step - 8), "* reported by the project owner; not an all-character test",
         F["small"], MUTED, ramp(t, 8.0))
    # earlier-build footage inset
    a = ramp(t, 0.8, 0.6)
    if a > 0.01 and clip:
        fr = clip[min(len(clip) - 1, int(max(0.0, t - 0.8) * FPS) % len(clip))]
        x0, y0i = 808, 170
        region = img.crop((x0, y0i, x0 + fr.width, y0i + fr.height))
        img.paste(Image.blend(region, fr, a), (x0, y0i))
        d.rectangle([x0 - 1, y0i - 1, x0 + fr.width, y0i + fr.height], outline=mix(LINE, a), width=2)
        d.rounded_rectangle([x0 + 10, y0i + 10, x0 + 228, y0i + 38], 8, fill=mix((0, 0, 0), a * 0.8))
        text(d, (x0 + 20, y0i + 24), "EARLIER-BUILD FOOTAGE", F["chap"], LEFT, a, "lm", (0, 0, 0))
        text(d, (x0, y0i + fr.height + 16), "First-person view from an earlier build (single eye, site clip)",
             F["small"], MUTED, a)


def scene4(img, d, t):
    text(d, (60, 70), "What's still broken", F["h1"], TEXT, ramp(t, 0.1))
    # top-down retreat diagram
    a = ramp(t, 0.3)
    ex, ey = 270, 572
    frustum(d, (ex, ey), -0.004, 0.46, 420, LEFT, a)
    frustum(d, (ex + 1, ey), 0.004, 0.46, 420, RIGHT, a)
    eye_icon(d, (ex, ey + 16), 12, TEXT, a)
    text(d, (ex + 26, ey + 16), "L = R  (zero eye separation)", F["small"], MUTED, a, "lm")
    retreat = ease((t - 2.2) / 3.0)
    ty = 440 - 250 * retreat
    far = retreat > 0.6
    if a > 0.01:
        for yy, lab in ((440, "near"), (190, "far")):
            d.line([(ex - 190, yy), (ex - 170, yy)], fill=mix(LINE, a), width=2)
            text(d, (ex - 200, yy), lab, F["small"], MUTED, a, "rm")
        r = 16 - 6 * retreat
        d.ellipse([ex - r, ty - r, ex + r, ty + r], fill=mix((88, 170, 120), a), outline=mix(TEXT, a), width=2)
        text(d, (ex + 24, ty), "same tree", F["small"], TEXT, a, "lm")
        if 2.2 < t < 5.4:
            text(d, (ex, ey - 40), "moving back ↑", F["smallb"], ACCENT, a, "mm")
    # per-eye views
    va = ramp(t, 0.6)
    for i, (col, lab) in enumerate(((LEFT, "LEFT EYE"), (RIGHT, "RIGHT EYE"))):
        x0 = 560 + i * 250
        box(d, [x0, 110, x0 + 230, 300], va, outline=col)
        text(d, (x0 + 14, 128), lab, F["smallb"], col, va, base=PANEL)
        scale = 1.25 - 0.55 * retreat
        moving = not (far and i == 0)
        tree(d, x0 + 115, 270 - 30 * retreat, scale, t, moving, va)
        if t > 0.8:
            st = "static" if not moving else "moving"
            text(d, (x0 + 216, 128), st, F["smallb"], WARN if not moving else OK, va, "ra", PANEL)
    # facts, then "also open" cards
    fa = ramp(t, 5.4) * (1 - ramp(t, 8.6, 0.4))
    facts = [
        (OK, "tested", "Zero eye separation: still fails far away"),
        (OK, "tested", "Main-eye projection scale: equal in both eyes"),
        (LEFT, "unknown", "Which asset / representation is drawn far away"),
        (LEFT, "unknown", "The final GPU inputs each eye's draw receives"),
    ]
    for i, (col, tag, s) in enumerate(facts):
        y = 340 + i * 56
        ai = fa * ramp(t, 5.4 + i * 0.5)
        if ai > 0.01:
            d.rounded_rectangle([560, y, 660, y + 34], 8, outline=mix(col, ai), width=2)
            text(d, (610, y + 17), tag, F["smallb"], col, ai, "mm")
            text(d, (678, y + 17), s, F["body"], TEXT, ai, "lm")
    ca = ramp(t, 9.0)
    cards = [
        ("Reflection submenus", "weapon / lower pages offset or freeze"),
        ("Some lighting & shadows", "no repair demonstrated yet"),
    ]
    if ca > 0.01:
        text(d, (560, 336), "Also open", F["h2"], WARN, ca)
        for i, (h, s) in enumerate(cards):
            y = 382 + i * 64
            ai = ca * ramp(t, 9.2 + i * 0.4)
            box(d, [560, y, 1220, y + 54], ai, outline=mix(WARN, 0.6, LINE), r=10)
            text(d, (578, y + 27), h, F["bodyb"], TEXT, ai, "lm", PANEL)
            text(d, (860, y + 27), s, F["small"], MUTED, ai, "lm", PANEL)


def scene5(img, d, t):
    text(d, (60, 70), "What's next", F["h1"], TEXT, ramp(t, 0.1))
    text(d, (60, 118), "Evidence first, then one targeted change. No more blind loops.",
         F["body"], MUTED, ramp(t, 0.3))
    steps = [
        ("1", "Identify the actual asset", "bounded inventory + FModel: which mesh and material is that tree?"),
        ("2", "Compare each eye's draw", "representation, shader and View / LOD inputs, left vs right"),
        ("3", "Smallest targeted fix", "keep true per-eye stereo: no flattening, no disabling wind"),
        ("4", "One grouped test", "near/far trees, props, shadows, ultimate, UI, first person"),
    ]
    bw, gap, y = 272, 26, 220
    x0 = (W - (4 * bw + 3 * gap)) / 2
    for i, (n, h, s) in enumerate(steps):
        t0 = 0.6 + i * 2.1
        a = ramp(t, t0)
        x = x0 + i * (bw + gap)
        box(d, [x, y, x + bw, y + 250], a, outline=mix(ACCENT, 0.7, LINE))
        if a > 0.01:
            d.ellipse([x + 20, y + 22, x + 64, y + 66], fill=mix(ACCENT, a, PANEL))
            text(d, (x + 42, y + 44), n, F["h2"], BG, a, "mm", PANEL)
            for j, ln in enumerate(wrap(h, F["bodyb"], bw - 40)):
                text(d, (x + 20, y + 88 + j * 28), ln, F["bodyb"], TEXT, a, base=PANEL)
            for j, ln in enumerate(wrap(s, F["small"], bw - 40)):
                text(d, (x + 20, y + 150 + j * 23), ln, F["small"], MUTED, a, base=PANEL)
        if i < 3:
            arrow(d, (x + bw + 3, y + 125), (x + bw + gap - 3, y + 125), ease((t - t0 - 1.2) / 0.5), ACCENT, width=2)
    na = ramp(t, 8.6)
    d.rounded_rectangle([x0, 500, W - x0, 546], 10, outline=mix(LEFT, na), width=2)
    text(d, (W / 2, 523), "The 28 Sep 16:28 package includes the camera fix and test tools; foliage remains unresolved.",
         F["body"], TEXT, na, "mm")


def scene6(img, d, t):
    a = ramp(t, 0.2)
    text(d, (W / 2, 170), "Free fan project · Experimental", F["h1"], TEXT, a, "mm")
    text(d, (W / 2, 232), "Not affiliated with or approved by Kuro Games. Injection carries account risk.",
         F["body"], MUTED, ramp(t, 0.6), "mm")
    b = ramp(t, 1.2)
    box(d, [W / 2 - 400, 300, W / 2 + 400, 430], b, outline=ACCENT, r=18)
    text(d, (W / 2, 338), "Source + beginner guide", F["h2"], ACCENT, b, "mm", PANEL)
    text(d, (W / 2, 390), "github.com/ChronoHaxx/wuwa-vr", F["h1"], TEXT, b, "mm", PANEL)
    text(d, (W / 2, 486), "Built on UEVR by praydog and community work. Credits in the repository.",
         F["small"], MUTED, ramp(t, 1.8), "mm")


# ---------------------------------------------------------------- chrome
def chrome(d, t):
    # chapter label
    for i, (s, e, name) in enumerate(SCENES):
        if s <= t < e or (i == len(SCENES) - 1 and t >= s):
            text(d, (W - 60, 40), f"{i + 1}/6  {name.upper()}", F["chap"], MUTED, 1.0, "ra")
            break
    # progress bar with chapter ticks
    d.line([(0, H - 3), (W, H - 3)], fill=(28, 34, 44), width=4)
    d.line([(0, H - 3), (W * t / DUR, H - 3)], fill=ACCENT, width=4)
    for s, _, _ in SCENES[1:]:
        x = W * s / DUR
        d.line([(x, H - 7), (x, H)], fill=BG, width=2)
    # caption
    for s, e, cap in CAPTIONS:
        if s <= t < e:
            ca = min(ramp(t, s, 0.2), 1 - ramp(t, e - 0.2, 0.2)) if e < DUR else ramp(t, s, 0.2)
            lines = wrap(cap, F["cap"], 1080)
            lh = 36
            bh = lh * len(lines) + 22
            y1 = H - 22
            y0 = y1 - bh
            wmax = max(F["cap"].getlength(l) for l in lines)
            d.rounded_rectangle([W / 2 - wmax / 2 - 22, y0, W / 2 + wmax / 2 + 22, y1], 10,
                                fill=mix((4, 6, 9), max(ca, 0.02)))
            for i, ln in enumerate(lines):
                text(d, (W / 2, y0 + 11 + lh / 2 + i * lh), ln, F["cap"], (255, 255, 255), ca, "mm", (4, 6, 9))
            break


def frame(t, clip, base):
    img = base.copy()
    d = ImageDraw.Draw(img)
    for i, (s, e, _) in enumerate(SCENES):
        if s <= t < e or (i == len(SCENES) - 1 and t >= s):
            lt = t - s
            [scene1, scene2, lambda im, dd, tt: scene3(im, dd, tt, clip), scene4, scene5, scene6][i](img, d, lt)
            # dip between scenes (not at the very end, so the last frame stays readable)
            fade = min(1.0, lt / 0.35) if i > 0 else min(1.0, t / 0.3 + 0.02)
            if i < len(SCENES) - 1:
                fade = min(fade, (e - t) / 0.35)
            if fade < 1.0:
                img = Image.blend(base, img, clamp(fade))
                d = ImageDraw.Draw(img)
            break
    chrome(d, t)
    return img


def make_base():
    arr = np.zeros((H, W, 3), np.float32)
    arr[:] = BG
    yy, xx = np.mgrid[0:H, 0:W]
    glow = np.exp(-(((xx - W * 0.5) / (W * 0.6)) ** 2 + ((yy - H * 0.35) / (H * 0.7)) ** 2))
    arr += glow[..., None] * np.array([6, 10, 14], np.float32)
    grid = ((xx % 40 == 0) | (yy % 40 == 0)) * 3.0
    arr += grid[..., None]
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))


def srt_time(x):
    ms = int(round(x * 1000))
    return f"{ms // 3600000:02d}:{ms // 60000 % 60:02d}:{ms // 1000 % 60:02d},{ms % 1000:03d}"


def write_text_outputs():
    with open(os.path.join(OUT, "captions.srt"), "w", encoding="utf-8") as f:
        for i, (s, e, c) in enumerate(CAPTIONS, 1):
            f.write(f"{i}\n{srt_time(s)} --> {srt_time(e)}\n{c}\n\n")
    with open(os.path.join(OUT, "transcript.md"), "w", encoding="utf-8") as f:
        f.write("# WuWa VR explained in 60 seconds: transcript\n\n")
        f.write("Silent video (no audio track). All information is carried by the burned-in captions below "
                "and the on-screen diagrams. The only footage is a short, labelled first-person clip from an "
                "earlier build (20–32 s), reused from the project website.\n\n")
        for s, e, name in SCENES:
            f.write(f"## {s:02d}–{e:02d} s · {name}\n\n")
            for cs, ce, c in CAPTIONS:
                if s <= cs < e:
                    f.write(f"- `{srt_time(cs)[3:8]}` {c}\n")
            f.write("\n")


def main():
    os.makedirs(OUT, exist_ok=True)
    only = sys.argv[1:]  # optional: timestamps to dump as PNG stills for preview
    clip = load_clip()
    base = make_base()
    if only:
        for s in only:
            frame(float(s), clip, base).save(os.path.join(OUT, f"preview-{s}.png"))
        return
    write_text_outputs()
    frame(7.6, clip, base).save(os.path.join(OUT, "poster.png"))
    mp4 = os.path.join(OUT, "WuWa-VR-Explained-60s.mp4")
    cmd = [FFMPEG, "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}",
           "-r", str(FPS), "-i", "-", "-frames:v", str(NFRAMES), "-c:v", "libx264", "-preset", "slow",
           "-crf", "20", "-tune", "animation", "-pix_fmt", "yuv420p", "-threads", "2",
           "-movflags", "+faststart", "-metadata", "title=WuWa VR explained in 60 seconds", mp4]
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, creationflags=NOWIN)
    for n in range(NFRAMES):
        p.stdin.write(frame(n / FPS, clip, base).tobytes())
    p.stdin.close()
    if p.wait() != 0:
        raise SystemExit("ffmpeg failed")
    print("wrote", mp4)


if __name__ == "__main__":
    main()
