#!/usr/bin/env python3
"""
make_category_art.py - render the category tiles for themes 0 and 1.

The picker draws one full-screen 320x240 tile per entry. Application tiles are
hand-made artwork; the six category tiles are generated here so they stay in the
idiom of the theme they sit in, instead of drifting into a style of their own.

    theme 0   flat saturated ground, a huge ghosted silhouette of the category
              icon, a small boxed badge on the left, wordmark + rule.  Matches
              themes/0/nes.png, md.png, ti99.png, ...

    theme 1   three slanted panels of pixel-art footage cropped out of theme 1's
              own application tiles, white wordmark across the middle.  Matches
              themes/1/nes.png, md.png, snes.png, ...

Rendering goes through ffmpeg's librsvg: the script writes an SVG to a temp file
and rasterises it.  ffmpeg supplies no base URI, so bitmaps must be inlined as
data: URIs -- external and relative hrefs do not resolve.

    tools/make_category_art.py                    # both themes
    tools/make_category_art.py --theme 0
    tools/make_category_art.py --theme 1 --only console --keep-svg /tmp/svg
    tools/make_category_art.py --preview          # also write the 320x240
                                                  # RGB444 previews the board
                                                  # actually displays

Writing the PNG is only half the job: the board reads the .444/.555 caches beside
it, and nothing invalidates a stale cache.  Regenerate them afterwards with

    tools/png2raw.py emu/assets/themes/0/Categories emu/assets/themes/1/Categories

Never point png2raw.py at a theme root -- it converts every image it finds, and
the theme folders hold unused spares that would then ship in the SD-card archive.

Requires ffmpeg/ffprobe and numpy, same as png2raw.py.
"""

import argparse
import base64
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
THEMES = os.path.join(REPO, "emu", "assets", "themes")

sys.path.insert(0, HERE)
import png2raw  # noqa: E402  - sibling tool, imported for the RGB444 preview

# Theme 0 is vector, so it is drawn at 2x and downscaled -- that is what gives
# the wordmark its antialiasing.  Theme 1 is built out of 320x240 pixel art and
# is drawn at 1:1: authoring it at 2x would force every crop to be magnified
# past the point where the footage is recognisable, and png2raw would only
# throw the extra resolution away again.
W, H = 640, 480            # theme 0 canvas
T1_W, T1_H = 320, 240      # theme 1 canvas

KEYS = ["arcade", "computer", "console", "handheld", "ports", "settings"]


# --------------------------------------------------------------------------
# Icons
#
# Each icon is drawn in its own 100x100 box.  `hull` is the solid silhouette --
# the theme-0 watermark needs a filled ghost, and the icons the tiles used
# before are outline drawings, so the silhouette has to be stated separately.
# `knock` is the device's own features, punched back out of the hull in the
# ground colour: a bare hull scales up into a featureless slab, whereas the
# application tiles' ghosts keep their d-pads, screens and buttons legible.
# `detail` is the outline rendering that goes in the badge, and is what the old
# tiles looked like.  {ink} and {acc} are substituted per theme.
# --------------------------------------------------------------------------

ICONS = {
    "console": {
        "hull": (
            '<rect x="22" y="26" width="14" height="12" rx="3"/>'
            '<rect x="64" y="26" width="14" height="12" rx="3"/>'
            '<rect x="6" y="34" width="88" height="32" rx="16"/>'
        ),
        "knock": (
            '<path d="M24 44h8v-8h8v8h8v8h-8v8h-8v-8h-8z"/>'
            '<circle cx="70" cy="44" r="6.5"/>'
            '<circle cx="79" cy="56" r="6.5"/>'
        ),
        "detail": (
            '<g fill="none" stroke="{ink}" stroke-width="4">'
            '<rect x="22" y="26" width="14" height="12" rx="3"/>'
            '<rect x="64" y="26" width="14" height="12" rx="3"/>'
            '<rect x="6" y="34" width="88" height="32" rx="16"/></g>'
            '<path d="M24 44h8v-8h8v8h8v8h-8v8h-8v-8h-8z" fill="{ink}"/>'
            '<circle cx="70" cy="44" r="6.5" fill="{acc}"/>'
            '<circle cx="79" cy="56" r="6.5" fill="{acc}"/>'
        ),
    },
    "handheld": {
        "hull": '<rect x="28" y="5" width="44" height="90" rx="9"/>',
        "knock": (
            '<rect x="35" y="13" width="30" height="34"/>'
            '<path d="M36 60h6v-6h6v6h6v6h-6v6h-6v-6h-6z"/>'
            '<circle cx="60" cy="59" r="5"/>'
            '<circle cx="60" cy="71" r="5"/>'
            '<rect x="36" y="79" width="14" height="3"/>'
            '<rect x="36" y="84" width="14" height="3"/>'
            '<rect x="36" y="89" width="14" height="3"/>'
        ),
        "detail": (
            '<rect x="28" y="5" width="44" height="90" rx="9" fill="none" '
            'stroke="{ink}" stroke-width="4"/>'
            '<rect x="35" y="13" width="30" height="34" fill="none" '
            'stroke="{acc}" stroke-width="4"/>'
            '<path d="M36 60h6v-6h6v6h6v6h-6v6h-6v-6h-6z" fill="{ink}"/>'
            '<circle cx="60" cy="59" r="5" fill="{ink}"/>'
            '<circle cx="60" cy="71" r="5" fill="{ink}"/>'
            '<g stroke="{ink}" stroke-width="3">'
            '<path d="M36 80h14M36 85h14M36 90h14"/></g>'
        ),
    },
    # The marquee is deliberately wider than the cabinet: without it this
    # silhouette is just a tall rounded box and is indistinguishable from the
    # handheld one at watermark and badge size.
    "arcade": {
        "hull": (
            '<rect x="18" y="4" width="64" height="18" rx="2"/>'
            '<rect x="25" y="22" width="50" height="74" rx="2"/>'
        ),
        "knock": (
            '<rect x="24" y="9" width="52" height="8"/>'
            '<rect x="32" y="30" width="36" height="28"/>'
            '<rect x="25" y="62" width="50" height="3"/>'
            '<rect x="38.5" y="70" width="3" height="8"/>'
            '<circle cx="40" cy="70" r="4.5"/>'
            '<circle cx="55" cy="72" r="4"/>'
            '<circle cx="64" cy="72" r="4"/>'
            '<rect x="43" y="86" width="14" height="5" rx="2"/>'
        ),
        "detail": (
            '<g fill="none" stroke="{ink}" stroke-width="4">'
            '<rect x="18" y="4" width="64" height="18" rx="2"/>'
            '<rect x="25" y="22" width="50" height="74" rx="2"/></g>'
            '<rect x="32" y="30" width="36" height="28" fill="none" '
            'stroke="{acc}" stroke-width="4"/>'
            '<path d="M25 64h50" stroke="{ink}" stroke-width="4"/>'
            '<path d="M40 71v8" stroke="{ink}" stroke-width="3"/>'
            '<circle cx="40" cy="70" r="4.5" fill="{acc}"/>'
            '<circle cx="55" cy="72" r="4" fill="{ink}"/>'
            '<circle cx="64" cy="72" r="4" fill="{ink}"/>'
            '<rect x="43" y="86" width="14" height="5" rx="2" fill="{ink}"/>'
        ),
    },
    "computer": {
        "hull": (
            '<rect x="12" y="11" width="76" height="52" rx="6"/>'
            '<rect x="43" y="63" width="14" height="12"/>'
            '<rect x="31" y="75" width="38" height="7" rx="3"/>'
            '<rect x="16" y="86" width="68" height="11" rx="3"/>'
        ),
        "knock": (
            '<rect x="20" y="19" width="60" height="36"/>'
            '<g>'
            '<rect x="24" y="90" width="3" height="4"/>'
            '<rect x="33" y="90" width="3" height="4"/>'
            '<rect x="42" y="90" width="3" height="4"/>'
            '<rect x="51" y="90" width="3" height="4"/>'
            '<rect x="60" y="90" width="3" height="4"/>'
            '<rect x="69" y="90" width="3" height="4"/></g>'
        ),
        "detail": (
            '<rect x="12" y="11" width="76" height="52" rx="6" fill="none" '
            'stroke="{ink}" stroke-width="4"/>'
            '<rect x="20" y="19" width="60" height="36" fill="none" '
            'stroke="{acc}" stroke-width="4"/>'
            '<path d="M43 63h14v12h12a3 3 0 0 1 0 7H31a3 3 0 0 1 0-7h12z" '
            'fill="{ink}"/>'
            '<rect x="16" y="86" width="68" height="11" rx="3" fill="none" '
            'stroke="{ink}" stroke-width="4"/>'
            '<g fill="{ink}">'
            '<rect x="24" y="90" width="3" height="4"/>'
            '<rect x="33" y="90" width="3" height="4"/>'
            '<rect x="42" y="90" width="3" height="4"/>'
            '<rect x="51" y="90" width="3" height="4"/>'
            '<rect x="60" y="90" width="3" height="4"/>'
            '<rect x="69" y="90" width="3" height="4"/></g>'
        ),
    },
    "ports": {
        "hull": (
            '<rect x="43" y="8" width="14" height="40" rx="3"/>'
            '<path d="M31 44h38L50 72z"/>'
            '<path d="M26 70v24h48V70h6v30H20V70z"/>'
        ),
        "detail": (
            '<path d="M43 8h14v38h12L50 72 31 46h12z" fill="{ink}"/>'
            '<path d="M26 70v24h48V70" fill="none" stroke="{ink}" '
            'stroke-width="5"/>'
            '<rect x="34" y="82" width="32" height="5" fill="{acc}"/>'
        ),
    },
    "settings": {
        "hull": (
            '<rect x="10" y="23" width="80" height="6" rx="3"/>'
            '<rect x="10" y="47" width="80" height="6" rx="3"/>'
            '<rect x="10" y="71" width="80" height="6" rx="3"/>'
            '<rect x="28" y="15" width="16" height="22" rx="5"/>'
            '<rect x="58" y="39" width="16" height="22" rx="5"/>'
            '<rect x="40" y="63" width="16" height="22" rx="5"/>'
        ),
        "detail": (
            '<g stroke="{ink}" stroke-width="5" stroke-linecap="round">'
            '<path d="M12 26h76M12 50h76M12 74h76"/></g>'
            '<g fill="{acc}" stroke="{ink}" stroke-width="3">'
            '<rect x="28" y="15" width="16" height="22" rx="5"/>'
            '<rect x="58" y="39" width="16" height="22" rx="5"/>'
            '<rect x="40" y="63" width="16" height="22" rx="5"/></g>'
        ),
    },
}

# The badge keeps the palette the tiles used before, so the old artwork survives
# as the "box art" the application tiles put in the same place.
BADGE_GROUND = "#1a1b2a"
BADGE_INK = "#e8e8e2"
BADGE_ACC = "#ff7a3c"


# --------------------------------------------------------------------------
# Theme 0 - flat ground, ghosted watermark, badge, wordmark, rule
#
# Ground/tint pairs are chosen to clear RGB444 (>=17 in at least one channel, so
# the quantiser cannot collapse the watermark into the ground) and to avoid the
# hues the application tiles already use.
# --------------------------------------------------------------------------

THEME0 = {
    "arcade":   {"ground": "#8e44ad", "tint": "#a162bd"},
    "computer": {"ground": "#34495e", "tint": "#46617f"},
    "console":  {"ground": "#b03a5b", "tint": "#c74e70"},
    "handheld": {"ground": "#0e7c86", "tint": "#159aa6"},
    "ports":    {"ground": "#b9770e", "tint": "#d48a14"},
    "settings": {"ground": "#4a4a52", "tint": "#5e5e68"},
}

# Measured off themes/0/md.png and 0/nes.png at 320x240, doubled to this canvas.
BADGE_X, BADGE_Y, BADGE_W, BADGE_H = 2, 186, 90, 108
BADGE_FRAME = 8
RULE_Y = 229
RULE_TH = 2
DASH_X0, DASH_X1 = 94, 108
TEXT_X = 114
TEXT_BASELINE = 246
TEXT_SIZE = 45

# Watermark placement, per icon: the box size the 100-unit icon is scaled to,
# its centre, and its rotation.  Sized from each icon's own content bbox so the
# silhouette reads as the device -- it bleeds off the edges the way the
# application tiles do, but never so far that only a featureless corner is left.
WATERMARK = {
    "console":  (836, 400, 262, -12),
    "handheld": (667, 432, 248, -10),
    "arcade":   (667, 430, 248,  -8),
    "computer": (670, 416, 250, -10),
    "ports":    (520, 430, 205, -10),
    "settings": (920, 380, 250, -12),
}


def hex_to_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def check_palette():
    """RGB444 keeps 4 bits per channel; a pair closer than 17 can collapse."""
    bad = []
    for key, pal in THEME0.items():
        g, t = hex_to_rgb(pal["ground"]), hex_to_rgb(pal["tint"])
        if max(abs(a - b) for a, b in zip(g, t)) < 17:
            bad.append(f"{key}: {pal['ground']} vs {pal['tint']}")
    return bad


def icon_group(key, x, y, size, fill=None, knock=None, ink=None, acc=None):
    """Place an icon's 100x100 box at (x, y) scaled to `size`.

    `fill` draws the silhouette; `knock` then punches the device's features back
    out of it in that colour.  Without `fill`, the badge's outline drawing.
    """
    s = size / 100.0
    place = f'transform="translate({x},{y}) scale({s:.5f})"'
    if not fill:
        body = ICONS[key]["detail"].format(ink=ink, acc=acc)
        return f"<g {place}>{body}</g>"

    body = f'<g fill="{fill}">{ICONS[key]["hull"]}</g>'
    if knock and ICONS[key].get("knock"):
        body += f'<g fill="{knock}">{ICONS[key]["knock"]}</g>'
    return f"<g {place}>{body}</g>"


def theme0_svg(key, label, rule_x0):
    """rule_x0 is where the long rule starts; None on the measuring pass."""
    pal = THEME0[key]
    size, wcx, wcy, rot = WATERMARK[key]

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}">',
        f'<rect width="{W}" height="{H}" fill="{pal["ground"]}"/>',
    ]

    # Ghosted silhouette, bled off the edges.
    parts.append(
        f'<g transform="rotate({rot} {wcx} {wcy})">'
        + icon_group(key, wcx - size / 2, wcy - size / 2, size,
                     fill=pal["tint"], knock=pal["ground"])
        + "</g>"
    )

    if rule_x0 is None:
        # Measuring pass: the wordmark alone, so its ink bbox can be read back.
        parts.append(
            f'<rect width="{W}" height="{H}" fill="#000"/>'
            f'<text x="{TEXT_X}" y="{TEXT_BASELINE}" font-family="Liberation Sans" '
            f'font-weight="bold" font-size="{TEXT_SIZE}" letter-spacing="2" '
            f'fill="#ffffff">{label}</text></svg>'
        )
        return "".join(parts)

    # Badge: white frame, dark plate, the icon as box art, a hue footer bar.
    ix, iy = BADGE_X + BADGE_FRAME, BADGE_Y + BADGE_FRAME
    iw, ih = BADGE_W - 2 * BADGE_FRAME, BADGE_H - 2 * BADGE_FRAME
    art = iw * 0.86
    parts.append(
        f'<rect x="{BADGE_X}" y="{BADGE_Y}" width="{BADGE_W}" height="{BADGE_H}" '
        f'fill="#ffffff"/>'
        f'<rect x="{ix}" y="{iy}" width="{iw}" height="{ih}" fill="{BADGE_GROUND}"/>'
        + icon_group(key, ix + (iw - art) / 2, iy + 4, art,
                     ink=BADGE_INK, acc=BADGE_ACC)
        + f'<rect x="{ix + 3}" y="{iy + ih - 15}" width="{iw - 6}" height="11" '
          f'fill="{pal["ground"]}"/>'
    )

    # Leader dash, wordmark, long rule to the right edge.
    parts.append(
        f'<rect x="{DASH_X0}" y="{RULE_Y}" width="{DASH_X1 - DASH_X0}" '
        f'height="{RULE_TH}" fill="#ffffff"/>'
        f'<text x="{TEXT_X}" y="{TEXT_BASELINE}" font-family="Liberation Sans" '
        f'font-weight="bold" font-size="{TEXT_SIZE}" letter-spacing="2" '
        f'fill="#ffffff">{label}</text>'
        f'<rect x="{rule_x0}" y="{RULE_Y}" width="{637 - rule_x0}" '
        f'height="{RULE_TH}" fill="#ffffff"/>'
    )

    parts.append("</svg>")
    return "".join(parts)


# --------------------------------------------------------------------------
# Theme 1 - three slanted panels of pixel-art footage
#
# Every theme-1 application tile is itself a 3-panel collage with its own
# wordmark baked across the middle, so every crop below is chosen to stay clear
# of that text -- measured, not guessed.
#
# The side panels are not darkened here.  The hand-made tiles' sides are dark
# because their footage was darkened when they were made, and the side crops
# are taken from exactly those panels, so they arrive at the right brightness
# already (luma ~25-35, against a median of 32 across theme 1's application
# tiles).  Dimming them again made them about four times too dark.  The
# boot-screen crops (computer, arcade) are left as ti99.png and outrun.png
# present them.
# --------------------------------------------------------------------------

# key -> (left, middle, right); each entry is (source, crop x, y, w, h).
# Side crops take a source's full height, so the footage stays near 1:1 and
# reads as game footage rather than as a magnified detail.  Middle crops are
# picked for being clear of their own tile's wordmark -- measured, not guessed;
# where that means dodging above or below the wordmark band the crop is shorter
# and the footage ends up around 2x, which still reads as pixel art.
THEME1 = {
    # col.png is the one theme-1 tile with no wordmark anywhere, so it carries
    # both sides; the middle is Mario, from below the NES tile's wordmark.
    "console": [
        ("col.png",     0,   0, 116, 240),
        ("nes.png",    96, 132, 116, 108),
        ("col.png",   204,   0, 116, 240),
    ],
    # gb_gbc's own wordmark runs nearly its full width, so every crop here is
    # taken from the clean band above it -- which is also where the Game Boy
    # screen itself sits.
    "handheld": [
        ("gb_gbc.png",   0, 0, 116, 83),
        ("gb_gbc.png", 102, 0, 116, 83),
        ("gb_gbc.png", 196, 0, 116, 83),
    ],
    # The TI-99 boot screen is theme 1's own idiom for a home computer.
    "computer": [
        ("ti99.png",    0, 0, 232, 480),
        ("ti99.png",  200, 0, 232, 480),
        ("ti99.png",  408, 0, 232, 480),
    ],
    # Doom's wordmark is too big to crop around, so Duke supplies the first
    # person view, taken from below the band its own wordmark sits in.
    "ports": [
        ("doom.png",     0,   0, 116,  72),
        ("duke3d.png", 100, 132, 116, 108),
        ("duke3d.png", 204, 132, 116, 108),
    ],
    # The car and road below OutRun's logo, with its status columns either side.
    "arcade": [
        ("outrun.png",   0,   0, 232, 480),
        ("outrun.png", 204, 236, 232, 244),
        ("outrun.png", 408,   0, 232, 480),
    ],
}

# 8.5 degrees off vertical: the divider drifts 36px right over the 240px height.
PANEL_CLIP = {
    "L": "0,0 120,0 84,240 0,240",
    "M": "120,0 236,0 200,240 84,240",
    "R": "236,0 320,0 320,240 200,240",
}
PANEL_BOX = {"L": (0, 120), "M": (84, 236), "R": (200, 320)}

T1_TEXT_Y = 114
T1_TEXT_SIZE = 33
T1_STROKE = "#101820"

# themes/1/Categories used these; the drawn settings tile keeps them.
T1_INK = "#e5e4d5"
T1_ACC = "#0085c3"
T1_PANEL_BG = "#04070b"

_b64_cache = {}


def data_uri(path):
    if path not in _b64_cache:
        with open(path, "rb") as f:
            _b64_cache[path] = base64.b64encode(f.read()).decode("ascii")
    return "data:image/png;base64," + _b64_cache[path]


def text_load(path, crop):
    """Fraction of a crop that is pure white -- a proxy for baked-in wordmark."""
    cx, cy, cw, ch = crop
    img = decode(path)[cy:cy + ch, cx:cx + cw]
    return float((img > 235).all(axis=2).mean())


def t1_panel(slot, src, crop):
    """Scale the crop to cover the panel box, then clip it to the slant."""
    cx, cy, cw, ch = crop
    img = decode(src)
    sh, sw = img.shape[:2]
    bx0, bx1 = PANEL_BOX[slot]
    bw = bx1 - bx0
    # "slice" semantics: uniform scale, cover the box, centre the overflow.
    scale = max(bw / cw, T1_H / ch)
    dw, dh = sw * scale, sh * scale
    x = bx0 + bw / 2 - (cx + cw / 2) * scale
    y = T1_H / 2 - (cy + ch / 2) * scale
    return (
        f'<g clip-path="url(#p{slot})">'
        f'<image xlink:href="{data_uri(src)}" x="{x:.2f}" y="{y:.2f}" '
        f'width="{dw:.2f}" height="{dh:.2f}" preserveAspectRatio="none" '
        f'image-rendering="pixelated"/></g>'
    )


# Boot-screen column colours, sampled from themes/1/ti99.png.
BOOT_LABEL = "#40b8c0"
BOOT_VALUE = "#d0b868"
BOOT_OK = "#50c868"
BOOT_HEAD = "#b0b0b0"
BOOT_CHIP = "#5a5a5a"
BOOT_STRIPE = ["#702020", "#703070", "#808080", "#107020", "#806818", "#204880"]


def _mono(x, y, text, fill, size=11, anchor="start"):
    return (f'<text x="{x}" y="{y}" font-family="DejaVu Sans Mono" '
            f'font-size="{size}" text-anchor="{anchor}" fill="{fill}">{text}</text>')


def _stripe(x0, width):
    """The coloured bar down a boot-screen column's outer edge."""
    return "".join(
        f'<rect x="{x0}" y="{y}" width="{width}" height="12" '
        f'fill="{BOOT_STRIPE[(y // 12) % len(BOOT_STRIPE)]}"/>'
        for y in range(0, T1_H, 12))


def t1_settings_panels():
    """No application tile to crop, so borrow theme 1's boot-screen idiom.

    The columns copy what makes ti99.png's read as lit rather than as black:
    an edge stripe, a ruled header, rows packed down the full height in three
    colours, a chip and a boot bar.  Sparse text on the bare ground came out
    at a quarter of the brightness of the real thing.
    """
    left = (
        f'<g clip-path="url(#pL)">'
        f'<rect width="{T1_W}" height="{T1_H}" fill="{T1_PANEL_BG}"/>'
        + _stripe(0, 4)
        + _mono(9, 17, "SYSTEM", BOOT_HEAD)
        + f'<rect x="8" y="21" width="64" height="1" fill="#406070"/>'
    )
    rows = [("CPU", "2350"), ("VID", "HSTX"), ("SND", "I2S"), ("USB", "OK"),
            ("SD", "OK"), ("PAD", "OK"), ("CFG", "RW")]
    for i, (lab, val) in enumerate(rows):
        y = 36 + i * 15
        left += (_mono(9, y, lab, BOOT_LABEL)
                 + _mono(44, y, val, BOOT_OK if val in ("OK", "RW")
                         else BOOT_VALUE))
    # Chip, with its pins, then the boot bar.
    left += (f'<rect x="12" y="148" width="40" height="28" rx="2" '
             f'fill="#1c1c20" stroke="{BOOT_CHIP}" stroke-width="1.5"/>')
    for i in range(5):
        py = 152 + i * 5
        left += (f'<rect x="8" y="{py}" width="4" height="2" fill="{BOOT_CHIP}"/>'
                 f'<rect x="52" y="{py}" width="4" height="2" fill="{BOOT_CHIP}"/>')
    left += (_mono(32, 160, "RP", "#909090", 9, "middle")
             + _mono(32, 171, "2350", "#909090", 9, "middle")
             + _mono(9, 204, "BOOT", BOOT_LABEL))
    for i in range(6):
        left += (f'<rect x="{10 + i * 7}" y="210" width="5" height="6" '
                 f'fill="{BOOT_OK}"/>')
    left += "</g>"

    right = (
        f'<g clip-path="url(#pR)">'
        f'<rect width="{T1_W}" height="{T1_H}" fill="{T1_PANEL_BG}"/>'
        + _stripe(316, 4)
        + _mono(311, 17, "MEM", BOOT_HEAD, anchor="end")
        + f'<rect x="262" y="21" width="50" height="1" fill="#406070"/>'
    )
    for i, addr in enumerate(["0000", "2000", "4000", "6000",
                              "8000", "A000", "C000", "E000"]):
        right += _mono(311, 36 + i * 15, addr, BOOT_OK, anchor="end")
    for i, (lab, val) in enumerate([("DVI", "OK"), ("PSRAM", "OK"),
                                    ("FLASH", "OK")]):
        y = 168 + i * 14
        right += (_mono(260, y, lab, BOOT_LABEL, 10)
                  + _mono(311, y, val, BOOT_OK, 10, "end"))
    right += (_mono(260, 222, "&gt;", BOOT_LABEL)
              + f'<rect x="270" y="214" width="6" height="9" fill="{BOOT_HEAD}"/>'
              + "</g>")
    out = [left, right]

    # The middle is the lit screen, as in ti99.png and outrun.png: a blue setup
    # screen with scanlines.  Left dark, it became the darkest part of the tile
    # once the columns either side were lit.
    scan = "".join(f'<rect y="{y}" width="{T1_W}" height="1" fill="#000" '
                   f'opacity="0.12"/>' for y in range(0, T1_H, 3))
    mid = (f'<g clip-path="url(#pM)">'
           f'<rect width="{T1_W}" height="{T1_H}" fill="{T1_ACC}"/>'
           + icon_group("settings", 104, 62, 112, ink=T1_INK, acc=BOOT_VALUE)
           + scan + "</g>")
    return [out[0], mid, out[1]]


def theme1_svg(key, label):
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" '
        f'xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{T1_W}" height="{T1_H}">',
        "<defs>",
    ]
    for slot, pts in PANEL_CLIP.items():
        parts.append(f'<clipPath id="p{slot}"><polygon points="{pts}"/></clipPath>')
    parts.append("</defs>")
    parts.append(f'<rect width="{T1_W}" height="{T1_H}" fill="#000"/>')

    if key == "settings":
        parts += t1_settings_panels()
    else:
        for slot, (name, *crop) in zip("LMR", THEME1[key]):
            src = os.path.join(THEMES, "1", name)
            parts.append(t1_panel(slot, src, crop))

    # 2px of black along each divider, matching the hand-made tiles' seams.
    parts.append(
        '<g stroke="#000" stroke-width="2" fill="none">'
        '<path d="M120 0L84 240M236 0L200 240"/></g>'
    )
    parts.append(
        f'<text x="{T1_W // 2}" y="{T1_TEXT_Y}" text-anchor="middle" '
        f'font-family="Liberation Sans Narrow" font-weight="bold" '
        f'font-style="italic" font-size="{T1_TEXT_SIZE}" letter-spacing="1" '
        f'fill="#ffffff" stroke="{T1_STROKE}" stroke-width="3" '
        f'paint-order="stroke fill">{label}</text>'
    )
    return "".join(parts) + "</svg>"


# --------------------------------------------------------------------------
# Rasterising
# --------------------------------------------------------------------------

def decode(path):
    """Decode an image to an (h, w, 3) uint8 array."""
    import json
    meta = json.loads(subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=width,height", "-of", "json", path],
        check=True, capture_output=True, text=True).stdout)["streams"][0]
    w, h = int(meta["width"]), int(meta["height"])
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-f", "rawvideo",
         "-pix_fmt", "rgb24", "-"], check=True, capture_output=True).stdout
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3)


def rasterise(svg, out_png, keep_svg=None, tag="tile", expect=None):
    """SVG text -> PNG, via ffmpeg's librsvg. librsvg reads the intrinsic size,
    so no -s flag is wanted; adding one would drag swscale into the path."""
    tmp = tempfile.mkdtemp(prefix="catart-")
    svg_path = os.path.join(tmp, tag + ".svg")
    with open(svg_path, "w") as f:
        f.write(svg)
    subprocess.run(
        ["ffmpeg", "-y", "-v", "error", "-i", svg_path, "-frames:v", "1",
         "-update", "1", "-pix_fmt", "rgb24", "-compression_level", "9",
         out_png], check=True)
    if keep_svg:
        os.makedirs(keep_svg, exist_ok=True)
        os.replace(svg_path, os.path.join(keep_svg, tag + ".svg"))
    else:
        os.remove(svg_path)
    os.rmdir(tmp)

    if expect:
        img = decode(out_png)
        if (img.shape[1], img.shape[0]) != expect:
            raise RuntimeError(
                f"{out_png}: rendered {img.shape[1]}x{img.shape[0]}, "
                f"expected {expect[0]}x{expect[1]}")


def measure_text_end(svg, tag):
    """Render the wordmark alone and read back where its ink stops."""
    tmp = tempfile.mkdtemp(prefix="catart-m-")
    probe = os.path.join(tmp, "probe.png")
    rasterise(svg, probe, tag=tag)
    img = decode(probe)
    cols = np.where((img > 200).all(axis=2).any(axis=0))[0]
    os.remove(probe)
    os.rmdir(tmp)
    if len(cols) == 0:
        raise RuntimeError("wordmark probe rendered nothing")
    return int(cols.max())


def write_preview(png_path, out_path):
    """Run the board's own pipeline and expand back to 8-bit, so the preview
    shows what RGB444 actually leaves on a PicoDVI panel."""
    img = png2raw.quantize_565(decode(png_path))
    dw, dh, cw, ch, ox, oy = png2raw.geometry(
        img.shape[1], img.shape[0], png2raw.MAX_W, png2raw.MAX_H, True)
    small = png2raw.downscale(img, dw, dh).astype(np.uint8)
    r, g, b = small[:, :, 0] >> 4, small[:, :, 1] >> 4, small[:, :, 2] >> 4
    out = np.zeros((ch, cw, 3), np.uint8)
    out[oy:oy + dh, ox:ox + dw, 0] = (r << 4) | r
    out[oy:oy + dh, ox:ox + dw, 1] = (g << 4) | g
    out[oy:oy + dh, ox:ox + dw, 2] = (b << 4) | b
    subprocess.run(
        ["ffmpeg", "-y", "-v", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
         "-s", f"{cw}x{ch}", "-i", "-", out_path],
        input=out.tobytes(), check=True)


# --------------------------------------------------------------------------

def build(theme, keys, keep_svg, preview):
    outdir = os.path.join(THEMES, str(theme), "Categories")
    if not os.path.isdir(outdir):
        raise SystemExit(f"no such directory: {outdir}")

    for key in keys:
        label = key.upper()
        out = os.path.join(outdir, key + ".png")
        tag = f"theme{theme}-{key}"

        if theme == 0:
            end = measure_text_end(theme0_svg(key, label, None), tag + "-probe")
            rasterise(theme0_svg(key, label, end + 12), out, keep_svg, tag,
                      expect=(W, H))
        else:
            if key != "settings":
                # The bright middle panel carries the tile; guard against a crop
                # that is mostly somebody else's wordmark.
                name, *crop = THEME1[key][1]
                load = text_load(os.path.join(THEMES, "1", name), crop)
                if load > 0.06:
                    raise SystemExit(
                        f"{key}: middle crop of {name} is {load:.1%} pure white "
                        f"-- pick a slice with less of that tile's own wordmark")
            rasterise(theme1_svg(key, label), out, keep_svg, tag,
                      expect=(T1_W, T1_H))

        note = ""
        if preview:
            prev = os.path.join(keep_svg or tempfile.gettempdir(),
                                f"{tag}-preview.png")
            os.makedirs(os.path.dirname(prev), exist_ok=True)
            write_preview(out, prev)
            note = f"   preview {prev}"
        print(f"theme {theme}  {key:9s} -> {os.path.relpath(out, REPO)}{note}")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--theme", type=int, choices=(0, 1), action="append",
                    help="render one theme (default: both)")
    ap.add_argument("--only", action="append", choices=KEYS,
                    help="render one category (default: all)")
    ap.add_argument("--keep-svg", metavar="DIR",
                    help="keep the generated SVGs here")
    ap.add_argument("--preview", action="store_true",
                    help="also write the 320x240 RGB444 preview the board shows")
    args = ap.parse_args()

    bad = check_palette()
    if bad:
        raise SystemExit("palette pairs too close for RGB444:\n  "
                         + "\n  ".join(bad))

    for theme in (args.theme or [0, 1]):
        build(theme, args.only or KEYS, args.keep_svg, args.preview)
    return 0


if __name__ == "__main__":
    sys.exit(main())
