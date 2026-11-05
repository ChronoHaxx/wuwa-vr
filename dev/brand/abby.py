"""The WuWa VR brand: Abby in a VR headset, constructed as SVG and rendered with Playwright's
headless Chromium shell (set WUWA_CHROMIUM to another chrome-headless-shell.exe).

    python dev/brand/abby.py assets   -> site/media/abby.svg, rings.svg, stars.svg, icon-32/64/180.png;
                                         launcher/native/assets/wuwa-vr.ico, abby-mark.png,
                                         abby-backdrop.png, stars.png; launcher/dev/portable/wuwa-vr.ico
    python dev/brand/abby.py mod      -> mod/uevr/src/assets/wuwa-crest-72/144/288.rgba (the VR menu and cards)
    python dev/brand/abby.py sheet    -> the presentation plate (needs the plate fonts, see FONTS)
    python dev/brand/abby.py mark|vr  -> the front and three-quarter crests on dark, 1000 px

Renders go to WUWA_BRAND_OUT (default: %TEMP%/wuwa-brand). This script is original project
tooling; the artwork it draws is fan art of Abby, a Wuthering Waves character (see LICENSE.md).

Reference: the official Abby card (fandom wiki Abby_Card.png): round white head, two-leaf
sprout, whiskers, flat heavy upper lids, orange (viewer left) and blue eyes with dark
Tacet-mark pupils, coral spots above the outer eye corners and on the cheeks, coral
Tacet mark on the forehead, :3 mouth, long ears that fall outward like wings with taupe
feathers underneath, gold rings and tassels at the ear tips, black collar with a gold charm.
"""
import math, os, shutil, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
OUT = Path(os.environ.get('WUWA_BRAND_OUT') or Path(os.environ.get('TEMP', '.')) / 'wuwa-brand')
OUT.mkdir(parents=True, exist_ok=True)
CHROME = Path(os.environ.get('WUWA_CHROMIUM') or Path(os.environ.get('LOCALAPPDATA', '')) /
              'ms-playwright/chromium_headless_shell-1228/chrome-headless-shell-win64/chrome-headless-shell.exe')
PROFILE = OUT / '.chrome-profile'

INK = '#2a1c1d'          # warm dark brown line, as in the official art
NIGHT = '#080b17'
GOLD = '#d9b465'
GOLD_HI = '#f8e7b0'
GOLD_LO = '#86652a'
PEARL = '#fcfbf8'
SHADE = '#e3e3ec'
SHADE_2 = '#cfd0dd'
TAUPE = '#bfae a2'.replace(' ', '')
TAUPE_LO = '#8f7d72'
CORAL = '#e8705e'
CORAL_LO = '#c94a3f'


# ---------- path helpers -------------------------------------------------------------
def fmt(v):
    return f'{v:.1f}'.rstrip('0').rstrip('.')


def P(*xy):
    return ' '.join(f'{fmt(x)} {fmt(y)}' for x, y in xy)


def mx(p):
    return (1000 - p[0], p[1])


def half_to_full(start, segs):
    """Right half from the top centre down to the bottom centre -> closed symmetric path."""
    d = [f'M{P(start)}']
    pts = [start]
    for s in segs:
        d.append(('C' + P(*s[1:])) if s[0] == 'C' else f'L{P(s[1])}')
        pts.append(s[-1])
    for i in range(len(segs) - 1, -1, -1):
        s = segs[i]
        a = mx(pts[i])
        d.append(('C' + P(mx(s[2]), mx(s[1]), a)) if s[0] == 'C' else f'L{P(a)}')
    return ' '.join(d) + ' Z'


def tacet(cx, cy, up, down, w, barb=.6, k=.16, kb=.42, tilt=0):
    """The Tacet mark: a long four-point star whose upper-right and lower-left edges carry
    a short extra spike, so it reads like a waveform rather than a plain sparkle."""
    tips = [(0, -up), (w * barb, -up * .5), (w, 0), (0, down), (-w * barb, down * .46), (-w, 0)]
    d = []
    for i, a in enumerate(tips):
        b = tips[(i + 1) % len(tips)]
        near_barb = i in (0, 1, 3, 4)
        f = kb if near_barb else k
        v = ((a[0] + b[0]) * f * .5, (a[1] + b[1]) * f * .5)
        if i == 0:
            d.append(f'M{P(a)}')
        d.append(f'Q{P(v, b)}')
    path = ' '.join(d) + ' Z'
    rot = f' rotate({tilt})' if tilt else ''
    return f'<path transform="translate({fmt(cx)} {fmt(cy)}){rot}" d="{path}"/>'


# ---------- geometry -----------------------------------------------------------------
HEAD = half_to_full((500, 298), [
    ('C', (632, 298), (724, 392), (730, 516)),
    ('C', (735, 604), (690, 680), (626, 714)),
    ('C', (590, 732), (548, 738), (500, 738)),
])

# Right ear (viewer right), mirrored for the left: a slim white blade that leaves the
# temple, arcs outward and droops to a narrow tip, with a fan of taupe feathers tucked
# under its lower edge (longest two thirds of the way out), a gold ring round the tip.
BLADE = ('M684 452 C774 414 874 442 926 532 C946 568 958 618 956 668 '
         'C942 640 920 604 884 584 C840 562 772 560 706 584 Z')
BLADE_LOWER = 'M956 668 C942 640 920 604 884 584 C840 562 772 560 706 584'
# (base x, base y, length, width, angle): angle < 0 swings the tip outward
FEATHER_SPEC = [(724, 566, 72, 62, 6), (770, 556, 106, 66, -3), (820, 554, 134, 68, -11),
                (868, 566, 132, 64, -19), (908, 590, 102, 56, -27)]


def feather(spec):
    x, y, L, w, a = spec
    h = w / 2
    # a primary feather: rounded, slightly off-centre tip, outer vane narrower
    d = (f'M{fmt(-h)} 0 C{fmt(-h)} {fmt(L*.6)} {fmt(-h*.62)} {fmt(L*.97)} {fmt(h*.12)} {fmt(L)} '
         f'C{fmt(h*.78)} {fmt(L*1.0)} {fmt(h*.98)} {fmt(L*.62)} {fmt(h)} 0 Z')
    quill = f'M{fmt(h*.08)} 4 C{fmt(h*.1)} {fmt(L*.4)} {fmt(h*.12)} {fmt(L*.7)} {fmt(h*.1)} {fmt(L*.9)}'
    barbs = ' '.join(f'M{fmt(h*.1)} {fmt(L*t)} L{fmt(-h*.62)} {fmt(L*(t+.16))}' for t in (.38, .58, .76))
    return f'translate({x} {y}) rotate({a})', d, quill + ' ' + barbs


FEATHER_SHAPES = [feather(f) for f in FEATHER_SPEC]
RING_C, RING_R = (948, 650), (21, 8)

EYES = [dict(cx=396, cy=548, flip=True, iris=('#fff3c4', '#ffb24a', '#e0671d', '#6a2a0c'), uid='l'),
        dict(cx=604, cy=548, flip=False, iris=('#ecfbff', '#86d4ff', '#2f7fd0', '#102f66'), uid='r')]


def eye(e, uid):
    c0, c1, c2, c3 = e['iris']
    opening = 'M-58 -20 C-20 -23 24 -23 60 -19 C66 14 46 47 2 48 C-42 48 -64 16 -58 -20 Z'
    lid = 'M-66 -27 C-24 -35 30 -34 72 -26 C74 -22 74 -16 72 -11 C30 -20 -22 -21 -62 -11 C-66 -16 -67 -22 -66 -27 Z'
    ix, iy, ir = 4, 11, 43
    kx, ky = e.get('k', (1, 1))
    sx = (-1 if e['flip'] else 1) * kx
    blush = f'<ellipse cx="40" cy="80" rx="17" ry="10" fill="{CORAL}" fill-opacity=".9"/>' if e.get('blush', True) else ''
    fibres = ''.join(
        f'<line x1="{fmt(ix + math.cos(a) * 15)}" y1="{fmt(iy + math.sin(a) * 15)}" '
        f'x2="{fmt(ix + math.cos(a) * (ir - 5 - (i % 3) * 4))}" y2="{fmt(iy + math.sin(a) * (ir - 5 - (i % 3) * 4))}"/>'
        for i, a in enumerate([j * math.tau / 54 for j in range(54)]))
    return f'''
<g transform="translate({e['cx']} {e['cy']}) scale({sx} {ky})">
  <defs>
    <clipPath id="open{uid}"><path d="{opening}"/></clipPath>
    <linearGradient id="ir{uid}" x1="0" y1="{iy-ir}" x2="0" y2="{iy+ir}" gradientUnits="userSpaceOnUse">
      <stop offset=".12" stop-color="{c3}"/><stop offset=".42" stop-color="{c2}"/>
      <stop offset=".8" stop-color="{c1}"/><stop offset="1" stop-color="{c0}"/>
    </linearGradient>
    <radialGradient id="low{uid}"><stop offset="0" stop-color="{c0}" stop-opacity=".8"/><stop offset="1" stop-color="{c0}" stop-opacity="0"/></radialGradient>
  </defs>
  <circle cx="74" cy="-44" r="19" fill="{CORAL}"/>
  <path d="{opening}" fill="#f4f4f8"/>
  <g clip-path="url(#open{uid})">
    <path d="M-60 -24 L64 -24 L64 2 C30 -8 -24 -8 -60 2 Z" fill="#dcdcea"/>
    <circle cx="{ix}" cy="{iy}" r="{ir}" fill="url(#ir{uid})"/>
    <circle cx="{ix}" cy="{iy + 20}" r="30" fill="url(#low{uid})"/>
    <g stroke="{c0}" stroke-width="1.1" stroke-opacity=".28" stroke-linecap="round">{fibres}</g>
    <circle cx="{ix}" cy="{iy}" r="{ir-1.4}" fill="none" stroke="{c3}" stroke-width="2.8"/>
    <g fill="{c3}" fill-opacity=".9">{tacet(ix, iy + 1, 26, 26, 22, barb=.7, k=.12, kb=.3)}</g>
    <path d="M-64 -24 L66 -24 L66 -8 C30 -14 -24 -14 -64 -8 Z" fill="{INK}" fill-opacity=".38"/>
    <path d="M12 -10 C22 -14 36 -12 40 -4 C30 -1 20 -3 12 -10 Z" fill="#fff"/>
    <circle cx="-17" cy="31" r="3.4" fill="#fff" fill-opacity=".9"/>
    <circle cx="-8" cy="36" r="1.8" fill="#fff" fill-opacity=".7"/>
  </g>
  <path d="{opening}" fill="none" stroke="{INK}" stroke-width="2.2" stroke-opacity=".55"/>
  <path d="M24 42 C42 38 56 26 61 8" fill="none" stroke="{INK}" stroke-width="3.2" stroke-linecap="round"/>
  <path d="{lid}" fill="{INK}"/>
  <path d="M-30 -60 C-16 -66 4 -66 18 -61" fill="none" stroke="{INK}" stroke-width="3.6" stroke-linecap="round"/>
  {blush}
</g>'''


SPROUT_TALL = 'M478 308 C466 268 484 220 526 200 C534 238 524 280 508 310 Z'
SPROUT_SMALL = 'M500 306 C516 272 552 258 592 270 C570 294 538 310 506 312 Z'
COLLAR = half_to_full((500, 716), [('C', (550, 716), (590, 712), (612, 704)),
                                   ('C', (616, 712), (612, 722), (604, 728)),
                                   ('C', (572, 750), (540, 762), (500, 762))])
WHISKERS_R = ['M626 372 C664 338 700 312 744 290', 'M646 394 C690 376 730 368 778 366']


def ear_sil():
    return f'<path d="{BLADE}"/>' + ''.join(f'<path transform="{t}" d="{d}"/>' for t, d, _ in FEATHER_SHAPES)


def ear_art(uid):
    """The right ear (viewer right): ring back, feather fan, blade, ring front, tassel."""
    cx, cy = RING_C
    rx, ry = RING_R
    ring_back = f'<path d="M{cx-rx} {cy} A{rx} {ry} 0 0 1 {cx+rx} {cy}" transform="rotate(-10 {cx} {cy})"/>'
    ring_front = f'<path d="M{cx+rx} {cy} A{rx} {ry} 0 0 1 {cx-rx} {cy}" transform="rotate(-10 {cx} {cy})"/>'
    tassel = (f'<circle cx="{cx+2}" cy="{cy+17}" r="7" fill="url(#gold{uid})" stroke="{GOLD_LO}" stroke-width="1.5"/>'
              f'<path d="M{cx-7} {cy+25} L{cx+11} {cy+25} L{cx+9} {cy+40} L{cx-5} {cy+40} Z" fill="url(#gold{uid})" stroke="{GOLD_LO}" stroke-width="1.5"/>'
              f'<g stroke="#f1ebe1" stroke-width="2.1" stroke-linecap="round" fill="none">'
              + ''.join(f'<path d="M{cx-3+i*2.1} {cy+42} C{cx-3+i*2.3} {cy+72} {cx-6+i*3.2} {cy+96} {cx-9+i*4.1} {cy+122}"/>' for i in range(7))
              + '</g>')
    gold_ring = f'fill="none" stroke="url(#gold{uid})" stroke-width="6" stroke-linecap="round"'
    feathers = ''
    for t, d, q in reversed(FEATHER_SHAPES):
        feathers += (f'<g transform="{t}"><path d="{d}" fill="url(#taupe{uid})" stroke="#5a4a43" stroke-width="2.8" stroke-linejoin="round"/>'
                     f'<path d="{q}" fill="none" stroke="#f3ebe3" stroke-width="1.8" stroke-linecap="round" stroke-opacity=".75"/></g>')
    return (f'<g {gold_ring}>{ring_back}</g>{feathers}'
            f'<path d="{BLADE}" fill="url(#pearl{uid})"/>'
            f'<clipPath id="blade{uid}"><path d="{BLADE}"/></clipPath>'
            f'<g clip-path="url(#blade{uid})"><path d="{BLADE_LOWER}" fill="none" stroke="{SHADE}" stroke-width="30"/></g>'
            f'<path d="{BLADE}" fill="none" stroke="{INK}" stroke-width="5" stroke-linejoin="round"/>'
            f'<g {gold_ring}>{ring_front}</g>{tassel}')


def bell(uid, x=500, y=778):
    return (f'<g transform="translate({x - 500} {y - 778})">'
            f'<circle cx="500" cy="778" r="20" fill="url(#gold{uid})" stroke="{GOLD_LO}" stroke-width="2"/>'
            f'<path d="M481 777 C492 781 508 781 519 777" fill="none" stroke="{GOLD_LO}" stroke-width="2.2" stroke-linecap="round"/>'
            f'<path d="M500 781 L500 792" stroke="{GOLD_LO}" stroke-width="2.4" stroke-linecap="round"/>'
            f'<circle cx="500" cy="787" r="3.4" fill="{GOLD_LO}"/>'
            f'<ellipse cx="492" cy="769" rx="6" ry="3.6" transform="rotate(-20 492 769)" fill="#fffbe6" fill-opacity=".85"/></g>')


def crest(uid='a', keyline=True):
    flip = 'transform="matrix(-1 0 0 1 1000 0)"'
    both = lambda s: f'<g>{s}</g><g {flip}>{s}</g>'
    head_shadow = half_to_full((500, 708), [
        ('C', (600, 708), (680, 680), (724, 600)),
        ('L', (760, 700)), ('L', (500, 760))])
    out = []
    if keyline:
        sil = (f'<path d="{HEAD}"/><path d="{SPROUT_TALL}"/><path d="{SPROUT_SMALL}"/><path d="{COLLAR}"/>'
               f'<circle cx="500" cy="778" r="22"/>' + both(f'<path d="{BLADE}"/>' + ''.join(f'<path transform="{t}" d="{d}"/>' for t, d, _ in FEATHER_SHAPES)))
        out.append(f'<g fill="none" stroke="url(#gold{uid})" stroke-width="34" stroke-linejoin="round">{sil}</g>')
        out.append(f'<g fill="{NIGHT}" stroke="{NIGHT}" stroke-width="26" stroke-linejoin="round">{sil}</g>')
    out.append(both(ear_art(uid)))
    out.append(f'<path d="{COLLAR}" fill="#1d1824" stroke="{INK}" stroke-width="3"/>'
               f'<path d="M430 744 C462 755 538 755 570 744" fill="none" stroke="#5a5070" stroke-width="2" stroke-linecap="round"/>')
    for d, fill in ((SPROUT_SMALL, SHADE), (SPROUT_TALL, PEARL)):
        out.append(f'<path d="{d}" fill="{fill}" stroke="{INK}" stroke-width="5" stroke-linejoin="round"/>')
    out.append(f'<path d="{HEAD}" fill="url(#headfill{uid})"/>')
    out.append(f'<clipPath id="head{uid}"><path d="{HEAD}"/></clipPath>')
    out.append(f'<g clip-path="url(#head{uid})"><path d="{head_shadow}" fill="{SHADE}"/>'
               f'<ellipse cx="500" cy="300" rx="70" ry="20" fill="{SHADE}"/></g>')
    out.append(f'<path d="{HEAD}" fill="none" stroke="{INK}" stroke-width="5" stroke-linejoin="round"/>')
    out.append(both(''.join(f'<path d="{w}" fill="none" stroke="url(#whisk{uid})" stroke-width="2.6" stroke-linecap="round"/>' for w in WHISKERS_R)))
    out.append(f'<g fill="url(#coral{uid})">{tacet(500, 418, 34, 52, 20, barb=.78, k=.1, kb=.24)}</g>')
    for e in EYES:
        out.append(eye(e, uid + e['uid']))
    out.append(f'<path d="M489 590 C495 586 505 586 511 590 C510 598 504 602 500 603 C496 602 490 598 489 590 Z" fill="{INK}"/>')
    out.append(f'<path d="M470 614 C478 630 494 630 500 616 C506 630 522 630 530 614" fill="none" stroke="{INK}" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>')
    out.append(f'<circle cx="500" cy="778" r="20" fill="url(#gold{uid})" stroke="{GOLD_LO}" stroke-width="2"/>'
               f'<path d="M481 777 C492 781 508 781 519 777" fill="none" stroke="{GOLD_LO}" stroke-width="2.2" stroke-linecap="round"/>'
               f'<path d="M500 781 L500 792" stroke="{GOLD_LO}" stroke-width="2.4" stroke-linecap="round"/>'
               f'<circle cx="500" cy="787" r="3.4" fill="{GOLD_LO}"/>'
               f'<ellipse cx="492" cy="769" rx="6" ry="3.6" transform="rotate(-20 492 769)" fill="#fffbe6" fill-opacity=".85"/>')
    return crest_defs(uid) + '\n'.join(out)


def crest_defs(uid):
    return f'''<defs>
  <linearGradient id="gold{uid}" x1="0" y1="0" x2="1" y2="1">
    <stop offset="0" stop-color="{GOLD_LO}"/><stop offset=".32" stop-color="{GOLD}"/><stop offset=".5" stop-color="{GOLD_HI}"/>
    <stop offset=".7" stop-color="{GOLD}"/><stop offset="1" stop-color="{GOLD_LO}"/>
  </linearGradient>
  <linearGradient id="pearl{uid}" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="#eeedf3"/></linearGradient>
  <radialGradient id="headfill{uid}" cx=".42" cy=".3" r=".8">
    <stop offset="0" stop-color="#ffffff"/><stop offset=".55" stop-color="{PEARL}"/><stop offset="1" stop-color="#ebeaf1"/></radialGradient>
  <linearGradient id="taupe{uid}" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#ece3da"/><stop offset=".5" stop-color="#cdbdb1"/><stop offset="1" stop-color="#a8968b"/>
  </linearGradient>
  <linearGradient id="whisk{uid}" x1="620" y1="0" x2="790" y2="0" gradientUnits="userSpaceOnUse">
    <stop offset="0" stop-color="#7d7890"/><stop offset=".55" stop-color="#b9b5c8"/><stop offset="1" stop-color="#b9b5c8" stop-opacity="0"/></linearGradient>
  <linearGradient id="coral{uid}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ff8a6e"/><stop offset="1" stop-color="{CORAL_LO}"/></linearGradient>
  <linearGradient id="glass{uid}" x1="0" y1="0" x2=".35" y2="1">
    <stop offset="0" stop-color="#1b2347"/><stop offset=".5" stop-color="#0b1026"/><stop offset="1" stop-color="#05070f"/></linearGradient>
  <linearGradient id="shell{uid}" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#3a3f57"/><stop offset=".5" stop-color="#23273a"/><stop offset="1" stop-color="#14162280"/></linearGradient>
  <radialGradient id="screenfur{uid}"><stop offset="0" stop-color="#f6f5f2" stop-opacity=".95"/><stop offset=".62" stop-color="#e9ecf6" stop-opacity=".55"/><stop offset="1" stop-color="#c9d6ff" stop-opacity="0"/></radialGradient>
  <radialGradient id="glowA{uid}"><stop offset="0" stop-color="#ffb24a" stop-opacity=".34"/><stop offset="1" stop-color="#ffb24a" stop-opacity="0"/></radialGradient>
  <radialGradient id="glowB{uid}"><stop offset="0" stop-color="#86d4ff" stop-opacity=".34"/><stop offset="1" stop-color="#86d4ff" stop-opacity="0"/></radialGradient>
  <pattern id="scan{uid}" width="8" height="6" patternUnits="userSpaceOnUse"><rect width="8" height="1.4" fill="#ffffff" fill-opacity=".07"/></pattern>
</defs>'''


# ---------- three-quarter view in a VR headset -------------------------------------------
# Abby turns toward the viewer's right (toward a wordmark set beside him). The near ear
# (viewer left) spreads full size; the far one peeks out behind. A visor covers the eyes and
# its front glass shows them, like an eye-passthrough display: same heavy lids, same Tacet
# pupils, a soft fur patch, scanlines and a glass sheen.
HEAD34 = ('M480 290 C610 290 712 380 722 500 C728 560 716 620 690 660 C650 720 570 744 486 744 '
          'C380 744 290 690 262 600 C248 556 248 500 258 456 C284 352 372 290 480 290 Z')
VISOR = ('M376 438 C500 430 640 440 704 454 C730 460 744 476 744 500 L744 560 '
         'C744 586 730 600 704 604 C640 612 500 626 376 628 C348 628 330 610 330 584 '
         'L330 482 C330 456 348 440 376 438 Z')
VISOR_DEPTH = (-46, -8)
NEAR_EAR = 'translate(-30 2) matrix(-1 0 0 1 1000 0)'
FAR_EAR = 'translate(698 428) rotate(-8) scale(.72) translate(-684 -452)'
SPROUT_T = 'translate(40 -12)'
COLLAR_T = 'translate(-12 6)'
STRAP_TOP = 'M482 444 C470 380 432 318 350 302'
WHISK_NEAR = ['M330 392 C290 360 250 338 204 322', 'M312 418 C268 404 224 398 176 400']
WHISK_FAR = ['M676 392 C706 366 736 350 772 340', 'M690 416 C720 406 752 402 790 404']


def crest34(uid='v', keyline=True):
    dx, dy = VISOR_DEPTH
    out = []
    sil = (f'<path d="{HEAD34}"/><path d="{VISOR}"/><path transform="translate({dx} {dy})" d="{VISOR}"/>'
           f'<g transform="{SPROUT_T}"><path d="{SPROUT_TALL}"/><path d="{SPROUT_SMALL}"/></g>'
           f'<path transform="{COLLAR_T}" d="{COLLAR}"/><circle cx="488" cy="784" r="22"/>'
           f'<g transform="{NEAR_EAR}">{ear_sil()}</g><g transform="{FAR_EAR}">{ear_sil()}</g>')
    if keyline:
        out.append(f'<g fill="none" stroke="url(#gold{uid})" stroke-width="34" stroke-linejoin="round">{sil}</g>')
        out.append(f'<g fill="{NIGHT}" stroke="{NIGHT}" stroke-width="26" stroke-linejoin="round">{sil}</g>')
    out.append(f'<g transform="{FAR_EAR}">{ear_art(uid + "f")}</g>')
    out.append(f'<g transform="{NEAR_EAR}">{ear_art(uid + "n")}</g>')
    out.append(f'<g transform="{COLLAR_T}"><path d="{COLLAR}" fill="#1d1824" stroke="{INK}" stroke-width="3"/>'
               f'<path d="M430 744 C462 755 538 755 570 744" fill="none" stroke="#5a5070" stroke-width="2" stroke-linecap="round"/></g>')
    out.append(f'<g transform="{SPROUT_T}">' + ''.join(
        f'<path d="{d}" fill="{fill}" stroke="{INK}" stroke-width="5" stroke-linejoin="round"/>'
        for d, fill in ((SPROUT_SMALL, SHADE), (SPROUT_TALL, PEARL))) + '</g>')
    # head: shade base, lit body offset toward the light (upper left), visor shadow, screen glow
    out.append(f'<clipPath id="h34{uid}"><path d="{HEAD34}"/></clipPath>')
    out.append(f'<g clip-path="url(#h34{uid})"><path d="{HEAD34}" fill="#e2e2eb"/>'
               f'<path transform="translate(-18 -16)" d="{HEAD34}" fill="url(#headfill{uid})"/>'
               f'<path transform="translate(0 18)" d="{VISOR}" fill="{SHADE_2}" fill-opacity=".85"/>'
               f'<ellipse cx="440" cy="652" rx="120" ry="62" fill="url(#glowA{uid})"/>'
               f'<ellipse cx="654" cy="640" rx="84" ry="50" fill="url(#glowB{uid})"/>'
               f'<path d="{STRAP_TOP}" fill="none" stroke="{INK}" stroke-width="40"/>'
               f'<path d="{STRAP_TOP}" fill="none" stroke="#2c3044" stroke-width="32"/>'
               f'<path d="{STRAP_TOP}" fill="none" stroke="url(#gold{uid})" stroke-width="1.8" stroke-dasharray="7 6"/>'
               f'<path d="M300 540 C284 536 268 536 250 540" fill="none" stroke="#2c3044" stroke-width="58"/>'
               f'</g>')
    out.append(f'<path d="{HEAD34}" fill="none" stroke="{INK}" stroke-width="5" stroke-linejoin="round"/>')
    out.append(''.join(f'<path d="{w}" fill="none" stroke="url(#wkN{uid})" stroke-width="2.6" stroke-linecap="round"/>' for w in WHISK_NEAR))
    out.append(''.join(f'<path d="{w}" fill="none" stroke="url(#wkF{uid})" stroke-width="2.4" stroke-linecap="round"/>' for w in WHISK_FAR))
    out.append(f'<g fill="url(#coral{uid})" transform="translate(568 380) scale(.84 1) translate(-568 -380)">'
               f'{tacet(568, 380, 30, 44, 18, barb=.78, k=.1, kb=.24)}</g>')
    # visor body: extrusion from the back plate to the front glass
    steps = 18
    for i in range(steps + 1):
        t = i / steps
        c = tuple(int(a + (b - a) * t) for a, b in ((0x10, 0x30), (0x12, 0x35), (0x1c, 0x4c)))
        out.append(f'<path transform="translate({dx * (1 - t):.1f} {dy * (1 - t):.1f})" d="{VISOR}" '
                   f'fill="#{c[0]:02x}{c[1]:02x}{c[2]:02x}"' + (f' stroke="{INK}" stroke-width="5"' if i == 0 else '') + '/>')
    out.append(f'<path transform="translate({dx} {dy})" d="{VISOR}" fill="none" stroke="{INK}" stroke-width="5"/>')
    eyes = [dict(cx=436, cy=530, flip=True, k=(.86, .84), blush=False, iris=EYES[0]['iris'], uid='n'),
            dict(cx=646, cy=528, flip=False, k=(.68, .8), blush=False, iris=EYES[1]['iris'], uid='f')]
    out.append(f'<clipPath id="vis{uid}"><path d="{VISOR}"/></clipPath>')
    out.append(f'<path d="{VISOR}" fill="url(#glass{uid})"/>'
               f'<g clip-path="url(#vis{uid})">'
               f'<ellipse cx="540" cy="530" rx="226" ry="88" fill="url(#screenfur{uid})" fill-opacity=".42"/>'
               f'<ellipse cx="436" cy="536" rx="96" ry="64" fill="url(#glowA{uid})"/>'
               f'<ellipse cx="648" cy="532" rx="80" ry="60" fill="url(#glowB{uid})"/>'
               + ''.join(eye(e, uid + 's' + e['uid']) for e in eyes) +
               f'<rect x="300" y="420" width="470" height="230" fill="#7d93ff" fill-opacity=".07"/>'
               f'<rect x="300" y="420" width="470" height="230" fill="url(#scan{uid})"/>'
               f'<path d="M300 420 L452 420 L372 650 L300 650 Z" fill="#ffffff" fill-opacity=".05"/>'
               f'<path d="M470 420 L494 420 L414 650 L390 650 Z" fill="#ffffff" fill-opacity=".04"/>'
               f'<path d="{VISOR}" fill="none" stroke="#04050b" stroke-opacity=".55" stroke-width="22"/>'
               f'<path d="M392 447 C500 439 630 447 700 462" fill="none" stroke="#ffffff" stroke-opacity=".28" stroke-width="2"/>'
               f'</g>'
               f'<path d="{VISOR}" fill="none" stroke="{INK}" stroke-width="6"/>'
               f'<path d="{VISOR}" fill="none" stroke="url(#gold{uid})" stroke-width="2.6"/>')
    for x, y, r in ((352, 606, 6.5), (726, 588, 5)):
        out.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#05060c" stroke="url(#gold{uid})" stroke-width="1.4"/>'
                   f'<circle cx="{x - r * .3:.1f}" cy="{y - r * .3:.1f}" r="{r * .3:.1f}" fill="#9fc8ff" fill-opacity=".8"/>')
    out.append(f'<path d="M603 630 C608 627 616 627 621 630 C620 636 615 640 612 641 C609 640 604 636 603 630 Z" fill="{INK}"/>')
    out.append(f'<path d="M586 654 C593 668 606 668 611 656 C615 667 626 667 632 655" fill="none" stroke="{INK}" '
               f'stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>')
    out.append(f'<ellipse cx="444" cy="674" rx="18" ry="10" fill="{CORAL}" fill-opacity=".9"/>'
               f'<ellipse cx="684" cy="652" rx="10" ry="8" fill="{CORAL}" fill-opacity=".85"/>')
    out.append(bell(uid, 488, 784))
    defs = crest_defs(uid)
    for side, (x1, x2) in (('N', (334, 160)), ('F', (672, 800))):
        defs += (f'<defs><linearGradient id="wk{side}{uid}" x1="{x1}" y1="0" x2="{x2}" y2="0" gradientUnits="userSpaceOnUse">'
                 f'<stop offset="0" stop-color="#7d7890"/><stop offset=".55" stop-color="#b9b5c8"/>'
                 f'<stop offset="1" stop-color="#b9b5c8" stop-opacity="0"/></linearGradient></defs>')
    for suffix in ('f', 'n'):
        defs += crest_defs(uid + suffix)
    return defs + '\n'.join(out)


# ---------- rendering ----------------------------------------------------------------
def render(html, name, w, h, scale=1, transparent=False):
    page_file = OUT / f'{name}.html'
    page_file.write_text(html, encoding='utf-8')
    png = OUT / f'{name}.png'
    if png.exists():
        png.unlink()
    subprocess.run([str(CHROME), '--disable-gpu', '--hide-scrollbars'] + (['--default-background-color=00000000'] if transparent else []) + [
                    f'--user-data-dir={PROFILE}', f'--force-device-scale-factor={scale}',
                    f'--window-size={w},{h}', f'--screenshot={png}', page_file.as_uri()],
                   capture_output=True, timeout=120)
    assert png.exists(), name
    print(png)


def page(body, w, h, bg=NIGHT, extra_css=''):
    return f'''<!doctype html><meta charset="utf-8"><style>
html,body{{margin:0;width:{w}px;height:{h}px;overflow:hidden;background:{bg}}}
{extra_css}</style>{body}'''


FONTS = Path(r'C:/Users/ChronoHax/AppData/Roaming/Claude/local-agent-mode-sessions/skills-plugin/e7a9085f-12cf-49f3-8497-72f9b4f4db54/ddb57e49-8197-4218-8337-02bd7b758b89/skills/canvas-design/canvas-fonts')
KANIT = REPO / 'site/fonts/kanit-500.woff2'
FONT_CSS = f"""
@font-face{{font-family:Kanit;src:url("{KANIT.as_uri()}") format("woff2");font-weight:500}}
@font-face{{font-family:Jura;src:url("{(FONTS / 'Jura-Light.ttf').as_uri()}");font-weight:300}}
@font-face{{font-family:Jura;src:url("{(FONTS / 'Jura-Medium.ttf').as_uri()}");font-weight:500}}
@font-face{{font-family:DMMono;src:url("{(FONTS / 'DMMono-Regular.ttf').as_uri()}")}}
"""


def wave_ring(cx, cy, R, amp, n, phase=0.0, n2=0, amp2=0.0, steps=720):
    """A circle whose radius carries a wave: the Rabelle-curve idea, drawn as a measured signal."""
    pts = []
    for i in range(steps + 1):
        t = i / steps * math.tau
        r = R + amp * math.sin(n * t + phase) + amp2 * math.sin(n2 * t + phase * 1.7)
        pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
    return 'M' + ' L'.join(f'{x:.1f} {y:.1f}' for x, y in pts) + ' Z'


def stars(seed, n, w, h):
    import random
    rnd = random.Random(seed)
    out = []
    for _ in range(n):
        x, y = rnd.uniform(0, w), rnd.uniform(0, h)
        r = rnd.choice((.6, .7, .8, 1.0, 1.2, 1.6))
        o = rnd.uniform(.15, .75)
        c = rnd.choice(('#dfe8ff', '#ffffff', '#f6e2a6', '#c9d8ff'))
        out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r}" fill="{c}" fill-opacity="{o:.2f}"/>')
    return ''.join(out)


def icon_svg(uid, size, crop='full', x=0, y=0):
    """App icon: night tile, wave rings, gold inset border, crest. The face fills the tile and the
    ears run off its edges; crop='head' (64 px) and 'visor' (16-32 px) zoom further in."""
    # (scale, crest point to centre, tile y it lands on)
    s, cx, cy, ty = {'full': (1.22, 490, 517, 530), 'head': (1.5, 500, 520, 520),
                     'visor': (1.95, 537, 540, 470)}[crop]
    inner = (f'<g transform="translate(500 {ty}) scale({s}) translate(-{cx} -{cy})">'
             f'{crest34(uid, keyline=crop == "full")}</g>')
    rings = ''.join(f'<path d="{wave_ring(500, 520, R, a, n, ph)}" fill="none" stroke="{GOLD}" stroke-opacity="{o}" stroke-width="2"/>'
                    for R, a, n, ph, o in ((300, 4, 12, 0, .16), (360, 6, 9, 1, .12), (420, 5, 15, 2, .09)))
    return f'''<svg x="{x}" y="{y}" width="{size}" height="{size}" viewBox="0 0 1000 1000">
  <defs>
    <radialGradient id="tile{uid}" cx=".5" cy=".42" r=".75">
      <stop offset="0" stop-color="#1c2a5c"/><stop offset=".55" stop-color="#0d1430"/><stop offset="1" stop-color="#05070f"/></radialGradient>
    <clipPath id="tclip{uid}"><rect width="1000" height="1000" rx="224"/></clipPath>
  </defs>
  <g clip-path="url(#tclip{uid})">
    <rect width="1000" height="1000" fill="url(#tile{uid})"/>
    {rings if crop == 'full' else ''}
    {inner}
  </g>
  <rect x="22" y="22" width="956" height="956" rx="204" fill="none" stroke="{GOLD}" stroke-opacity=".55" stroke-width="5"/>
</svg>'''


def label(x, y, t, size=15, fill='#8d8a80', ls='.32em', fam='Jura', wt=500, anchor='start'):
    return (f'<text x="{x}" y="{y}" font-family="{fam}" font-weight="{wt}" font-size="{size}" '
            f'letter-spacing="{ls}" fill="{fill}" text-anchor="{anchor}">{t}</text>')


def sheet():
    W, H = 2400, 1500
    hs = 1.06                            # scale of the dial and rings, centred in the left panel
    cs = 1.3                             # the crest itself is drawn larger and breaks out of the dial
    hx, hy = 720 - 500 * hs, 650 - 500 * hs
    rings = ''.join(
        f'<path d="{wave_ring(500, 500, R, a, n, ph, n2, a2)}" fill="none" stroke="{GOLD}" stroke-opacity="{o}" vector-effect="non-scaling-stroke" stroke-width="1"/>'
        for R, a, n, ph, n2, a2, o in (
            (300, 3, 18, 0, 5, 2, .3), (338, 5, 12, .8, 7, 2, .24), (380, 7, 9, 1.6, 4, 3, .2),
            (426, 9, 7, 2.4, 11, 2, .16), (476, 11, 6, 3.1, 3, 4, .12)))
    ticks = ''.join(
        f'<line x1="{500 + 556 * math.cos(t):.1f}" y1="{500 + 556 * math.sin(t):.1f}" '
        f'x2="{500 + (556 - (16 if i % 6 == 0 else 7)) * math.cos(t):.1f}" y2="{500 + (556 - (16 if i % 6 == 0 else 7)) * math.sin(t):.1f}"/>'
        for i, t in enumerate([k * math.tau / 72 for k in range(72)]))
    construction = f'''<g fill="none" stroke="{GOLD}" stroke-opacity=".32" stroke-width="1" vector-effect="non-scaling-stroke">
      <circle cx="500" cy="500" r="556" stroke-opacity=".28"/>
      <line x1="500" y1="-40" x2="500" y2="1040" stroke-dasharray="1 7"/>
      <line x1="-40" y1="530" x2="1040" y2="530" stroke-dasharray="1 7"/>
    </g>
    <g stroke="{GOLD}" stroke-opacity=".4" vector-effect="non-scaling-stroke" stroke-width="1">{ticks}</g>'''
    pal = [('NIGHT', '#080b17'), ('PEARL', PEARL), ('GOLD', GOLD), ('AMBER EYE', '#ffb24a'), ('ICE EYE', '#86d4ff'), ('TACET CORAL', CORAL)]
    palette = ''
    for i, (name, hexv) in enumerate(pal):
        x = 1500 + i * 132
        palette += (f'<circle cx="{x + 26}" cy="1046" r="26" fill="{hexv}" stroke="{GOLD}" stroke-opacity=".45" stroke-width="1"/>'
                    + label(x + 26, 1108, name, 15, '#c4bead', '.14em', anchor='middle')
                    + label(x + 26, 1134, hexv.upper(), 15, '#85817a', '.06em', 'DMMono', 400, 'middle'))
    ladder = (icon_svg('i1', 300, 'full', 1500, 610)
              + icon_svg('i2', 128, 'full', 1846, 610) + icon_svg('i3', 64, 'head', 2002, 610)
              + icon_svg('i4', 32, 'visor', 2094, 610) + icon_svg('i5', 16, 'visor', 2154, 610))
    ladder_labels = ''.join(label(x, 952, t, 16, '#85817a', '.06em', 'DMMono', 400, 'middle')
                            for x, t in ((1650, '1024'), (1910, '128'), (2034, '64'), (2110, '32'), (2162, '16')))
    return f'''<svg viewBox="0 0 {W} {H}" width="{W}" height="{H}" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <radialGradient id="sky" cx=".31" cy=".42" r=".85">
      <stop offset="0" stop-color="#1a2752"/><stop offset=".42" stop-color="#0c1330"/><stop offset="1" stop-color="#04060d"/></radialGradient>
    <radialGradient id="halo" cx="500" cy="500" r="520" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#cfe0ff" stop-opacity=".28"/><stop offset=".45" stop-color="#8fb0ff" stop-opacity=".1"/><stop offset="1" stop-color="#8fb0ff" stop-opacity="0"/></radialGradient>
    <linearGradient id="horizon" x1="0" x2="1"><stop offset="0" stop-color="{GOLD}" stop-opacity="0"/><stop offset=".3" stop-color="{GOLD}" stop-opacity=".5"/>
      <stop offset=".55" stop-color="{GOLD_HI}" stop-opacity=".8"/><stop offset="1" stop-color="{GOLD}" stop-opacity="0"/></linearGradient>
    <linearGradient id="wordgold" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#a47c33"/><stop offset=".25" stop-color="#e6c477"/><stop offset=".5" stop-color="#f9e8b4"/><stop offset=".75" stop-color="#d6af5b"/><stop offset="1" stop-color="#9c7530"/></linearGradient>
    <filter id="grain" x="0" y="0" width="100%" height="100%">
      <feTurbulence type="fractalNoise" baseFrequency=".9" numOctaves="2" seed="7" stitchTiles="stitch"/>
      <feColorMatrix values="0 0 0 0 .85  0 0 0 0 .9  0 0 0 0 1  0 0 0 .06 0"/></filter>
  </defs>
  <rect width="{W}" height="{H}" fill="url(#sky)"/>
  <g>{stars(4, 420, W, H)}</g>
  <rect x="0" y="1296" width="1440" height="1" fill="url(#horizon)"/>
  <rect x="0" y="1297" width="1440" height="203" fill="#04060d" fill-opacity=".35"/>
  <g transform="translate({hx} {hy}) scale({hs})">
    <circle cx="500" cy="500" r="520" fill="url(#halo)"/>
    {rings}
    {construction}
  </g>
  <g transform="translate({720 - 4 * hs:.1f} {650 + 17 * hs:.1f}) scale({cs}) translate(-490 -517)">
    <circle cx="490" cy="517" r="268" fill="none" stroke="{GOLD}" stroke-opacity=".32" stroke-dasharray="2 6" vector-effect="non-scaling-stroke"/>
    {crest34('h')}
  </g>
  <g transform="translate({hx} {hy}) scale({hs})" font-family="DMMono" font-size="14" fill="{GOLD}" fill-opacity=".6">
    <text x="790" y="150">Ø 536</text>
    <text x="510" y="-30">AXIS 0</text>
  </g>
  <line x1="1440" y1="140" x2="1440" y2="1360" stroke="{GOLD}" stroke-opacity=".22"/>
  {label(1500, 176, 'PLATE I', 18, GOLD, '.4em')}
  {label(2290, 176, 'ABRAXAS · ABBY', 18, '#8d8a80', '.4em', anchor='end')}
  <rect x="1500" y="196" width="790" height="1" fill="{GOLD}" fill-opacity=".25"/>
  {icon_svg('w', 132, 'full', 1500, 262)}
  <text x="1662" y="356" font-family="Kanit" font-weight="500" font-size="92" letter-spacing=".14em" fill="url(#wordgold)">WUWA VR</text>
  {label(1666, 392, 'UNOFFICIAL VR MOD · WUTHERING WAVES', 18, '#a9a395', '.3em')}
  <rect x="1500" y="470" width="790" height="1" fill="{GOLD}" fill-opacity=".18"/>
  {label(1500, 560, 'ICON · LEGIBILITY LADDER', 18, '#8d8a80', '.4em')}
  {ladder}
  {ladder_labels}
  {label(1500, 1000, 'PALETTE', 18, '#8d8a80', '.4em')}
  {palette}
  <rect x="1500" y="1210" width="790" height="1" fill="{GOLD}" fill-opacity=".18"/>
  {label(1500, 1252, 'HEAVY LIDS · TACET PUPILS · EYES ON THE GLASS', 16, '#9a968b', '.28em')}
  {label(1500, 1288, 'ONE METAL, TWO EYES, ONE CORAL MARK', 16, '#85817a', '.28em')}
  {label(120, 1420, 'FAN WORK · NOT AFFILIATED WITH KURO GAMES · ABBY AND WUTHERING WAVES © KURO GAMES', 16, '#85817a', '.2em')}
  {label(2290, 1420, 'WUWA-VR / 2026.11.04', 16, '#85817a', '.12em', 'DMMono', 400, 'end')}
  <rect width="{W}" height="{H}" filter="url(#grain)"/>
</svg>'''


def rings_svg(steps=360):
    """The hero's wave rings and dial, centred on (500, 500), gold hairlines."""
    rings = ''.join(
        f'<path d="{wave_ring(500, 500, R, a, n, ph, n2, a2, steps)}" stroke-opacity="{o}"/>'
        for R, a, n, ph, n2, a2, o in (
            (300, 3, 18, 0, 5, 2, .34), (338, 5, 12, .8, 7, 2, .27), (380, 7, 9, 1.6, 4, 3, .22),
            (426, 9, 7, 2.4, 11, 2, .17), (476, 11, 6, 3.1, 3, 4, .12)))
    ticks = ''.join(
        f'<line x1="{500 + 556 * math.cos(t):.1f}" y1="{500 + 556 * math.sin(t):.1f}" '
        f'x2="{500 + (556 - (16 if i % 6 == 0 else 7)) * math.cos(t):.1f}" y2="{500 + (556 - (16 if i % 6 == 0 else 7)) * math.sin(t):.1f}"/>'
        for i, t in enumerate([k * math.tau / 72 for k in range(72)]))
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-70 -70 1140 1140" width="1140" height="1140">'
            f'<g fill="none" stroke="{GOLD}" stroke-width="1.4" vector-effect="non-scaling-stroke">{rings}'
            f'<circle cx="500" cy="500" r="556" stroke-opacity=".26"/>'
            f'<g stroke-opacity=".38">{ticks}</g></g></svg>')


def stars_svg(seed, n, w, h):
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}">{stars(seed, n, w, h)}</svg>'


def ico(entries, target):
    """Pack PNG images into a Windows .ico (PNG-compressed entries, Vista and later)."""
    import struct
    head = struct.pack('<HHH', 0, 1, len(entries))
    offset, table, blobs = 6 + 16 * len(entries), b'', b''
    for size, data in entries:
        d = 0 if size >= 256 else size
        table += struct.pack('<BBBBHHII', d, d, 0, 0, 1, 32, len(data), offset)
        offset += len(data); blobs += data
    target.write_bytes(head + table + blobs)


def repo_assets():
    """Brand files for the site and the launcher (fan art: see LICENSE.md)."""
    from PIL import Image
    media, assets = REPO / 'site/media', REPO / 'launcher/native/assets'
    crest_svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 150 1000 720" width="1000" height="720">'
                 f'{crest34("s")}</svg>')
    (media / 'abby.svg').write_bytes(crest_svg.encode('utf-8'))
    (media / 'rings.svg').write_bytes(rings_svg().encode('utf-8'))
    (media / 'stars.svg').write_bytes(stars_svg(11, 70, 640, 640).encode('utf-8'))

    def icon_png(size, crop):
        name = f'icon-{crop}-{size}'
        render(page(f'<svg width="{size}" height="{size}" viewBox="0 0 {size} {size}">{icon_svg(name.replace("-", ""), size, crop)}</svg>',
                    size, size, bg='transparent'), name, size, size, transparent=True)
        return (OUT / f'{name}.png').read_bytes()

    crops = {16: 'visor', 24: 'visor', 32: 'visor', 48: 'head', 64: 'head', 128: 'full', 180: 'full', 256: 'full'}
    pngs = {size: icon_png(size, crop) for size, crop in crops.items()}
    (media / 'icon-32.png').write_bytes(pngs[32])
    (media / 'icon-64.png').write_bytes(pngs[64])
    (media / 'icon-180.png').write_bytes(pngs[180])
    ico([(s, pngs[s]) for s in (16, 24, 32, 48, 64, 128, 256)], assets / 'wuwa-vr.ico')
    shutil.copyfile(assets / 'wuwa-vr.ico', REPO / 'launcher/dev/portable/wuwa-vr.ico')
    (assets / 'abby-mark.png').write_bytes(icon_png(136, 'head'))
    # Launcher backdrop: halo, rings and horizon around the crest, transparent, 2x for 640 x 420 dip.
    W, H = 1280, 840
    back = f'''<svg width="{W}" height="{H}" viewBox="0 0 {W} {H}">
  <defs>
    <radialGradient id="bhalo" cx="640" cy="400" r="440" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#cfe0ff" stop-opacity=".22"/><stop offset=".5" stop-color="#8fb0ff" stop-opacity=".07"/><stop offset="1" stop-color="#8fb0ff" stop-opacity="0"/></radialGradient>
    <linearGradient id="bhor" x1="0" x2="1"><stop offset="0" stop-color="{GOLD}" stop-opacity="0"/><stop offset=".3" stop-color="{GOLD}" stop-opacity=".45"/>
      <stop offset=".5" stop-color="{GOLD_HI}" stop-opacity=".8"/><stop offset=".7" stop-color="{GOLD}" stop-opacity=".45"/><stop offset="1" stop-color="{GOLD}" stop-opacity="0"/></linearGradient>
    <linearGradient id="bground" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#04060d" stop-opacity=".5"/><stop offset="1" stop-color="#04060d" stop-opacity="0"/></linearGradient>
  </defs>
  <circle cx="640" cy="400" r="440" fill="url(#bhalo)"/>
  <g transform="translate(640 400) scale(.62) translate(-500 -500)">{rings_svg(360).split(">", 1)[1].rsplit("</svg>", 1)[0]}</g>
  <rect x="0" y="{H - 61}" width="{W}" height="2" fill="url(#bhor)"/>
  <g transform="translate(640 410) scale(.7) translate(-490 -517)">{crest34('b')}</g>
</svg>'''
    render(page(back, W, H, bg='transparent'), 'launcher-backdrop', W, H, transparent=True)
    Image.open(OUT / 'launcher-backdrop.png').save(assets / 'abby-backdrop.png', optimize=True)
    render(page(stars_svg(23, 90, 512, 512), 512, 512, bg='transparent'), 'launcher-stars', 512, 512, transparent=True)
    Image.open(OUT / 'launcher-stars.png').save(assets / 'stars.png', optimize=True)
    for f in ('abby.svg', 'rings.svg', 'stars.svg', 'icon-32.png', 'icon-64.png', 'icon-180.png'):
        print(media / f, (media / f).stat().st_size)
    for f in ('wuwa-vr.ico', 'abby-mark.png', 'abby-backdrop.png', 'stars.png'):
        print(assets / f, (assets / f).stat().st_size)


def mod_assets():
    """The crest for the VR mod's menu and cards: raw straight-alpha RGBA at three heights (72, 144, 288 px),
    embedded as RCDATA 4840-4842 (mod/uevr/src/assets/wuwa-artwork.rc) and packed into the ImGui font atlas
    (WuWaBrand.hpp). Raw pixels need no image decoder in the game: 'WWIM', u32 width, u32 height, RGBA bytes."""
    import struct
    from PIL import Image
    crest_svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 150 1000 720" width="1000" height="720">'
                 f'{crest34("m")}</svg>')
    render(page(crest_svg.replace('width="1000" height="720"', 'width="2000" height="1440"'), 2000, 1440, bg='transparent'),
           'mod-crest', 2000, 1440, transparent=True)
    full = Image.open(OUT / 'mod-crest.png').convert('RGBA')
    full = full.crop(full.getbbox())
    assets = REPO / 'mod/uevr/src/assets'
    for rc, height in ((4840, 72), (4841, 144), (4842, 288)):
        width = round(full.width * height / full.height)
        img = full.resize((width, height), Image.LANCZOS)
        path = assets / f'wuwa-crest-{height}.rgba'
        path.write_bytes(b'WWIM' + struct.pack('<II', width, height) + img.tobytes())
        print(path, width, height, path.stat().st_size, f'RCDATA {rc}')


if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'mark'
    if what == 'assets':
        repo_assets()
    if what == 'mod':
        mod_assets()
    if what == 'mark':
        svg = f'<svg viewBox="0 0 1000 1000" width="1000" height="1000"><g transform="translate(500 520) scale(.88) translate(-500 -520)">{crest()}</g></svg>'
        render(page(svg, 1000, 1000), 'mark', 1000, 1000)
    if what == 'vr':
        svg = f'<svg viewBox="0 0 1000 1000" width="1000" height="1000"><g transform="translate(530 520) scale(.88) translate(-500 -520)">{crest34()}</g></svg>'
        render(page(svg, 1000, 1000), 'vr', 1000, 1000)
    if what == 'export':
        crest_svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="20 160 960 700" width="960" height="700">'
                     f'{crest34("x")}</svg>')
        (OUT / 'abby-vr.svg').write_text(crest_svg, encoding='utf-8')
        render(page(crest_svg.replace('width="960" height="700"', 'width="1920" height="1400"'), 1920, 1400, bg='transparent'),
               'abby-vr-transparent', 1920, 1400, transparent=True)
        render(page(f'<svg width="1024" height="1024" viewBox="0 0 1024 1024">{icon_svg("e", 1024)}</svg>', 1024, 1024, bg='transparent'),
               'abby-icon-1024', 1024, 1024, transparent=True)
        front = f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="40 160 920 680" width="920" height="680">{crest("y")}</svg>'
        (OUT / 'abby-front.svg').write_text(front, encoding='utf-8')
    if what == 'sheet':
        render(page(sheet(), 2400, 1500, extra_css=FONT_CSS), 'sheet', 2400, 1500)
