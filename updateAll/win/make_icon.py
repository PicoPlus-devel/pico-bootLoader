#!/usr/bin/env python3
"""
Draws the updateAll icon, an SD card with an arrow into it, and writes
updateAll.ico with every size Windows asks for. Plain Python, so nothing has
to be installed; run it again after changing the drawing.

Usage: make_icon.py [--preview PNG]
"""

import math
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SIZES = (16, 20, 24, 32, 40, 48, 64, 256)
SS = 8   # samples per pixel in each direction


def hexrgb(s):
    return tuple(int(s[i:i + 2], 16) / 255 for i in (1, 3, 5))


OUTLINE = hexrgb("#16324f")
BODY = hexrgb("#2f6496")
CONTACT = hexrgb("#e9c25c")
LABEL = hexrgb("#f2f6fa")
ARROW = hexrgb("#2e9e4f")
ARROW_DARK = hexrgb("#1f7a3a")


# --- Shapes, in a 0..1 square with y pointing down -------------------------

def card(x0, y0, x1, y1, r, cut):
    """The SD card outline: a rounded rectangle with the top right corner cut off."""
    pts = []

    def arc(cx, cy, a0, a1):
        n = 8
        for i in range(n + 1):
            a = math.radians(a0 + (a1 - a0) * i / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))

    pts.append((x1 - cut, y0))
    pts.append((x1, y0 + cut))
    arc(x1 - r, y1 - r, 0, 90)
    arc(x0 + r, y1 - r, 90, 180)
    arc(x0 + r, y0 + r, 180, 270)
    return pts


def rounded_rect(x0, y0, x1, y1, r):
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, 270), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90),
                       (x0 + r, y0 + r, 180)):
        for i in range(9):
            a = math.radians(a0 + 90 * i / 8)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def arrow(cx, top, bottom, shaft, head, head_len):
    """A down arrow: shaft half-width, head half-width, head length."""
    neck = bottom - head_len
    return [(cx - shaft, top), (cx + shaft, top), (cx + shaft, neck), (cx + head, neck),
            (cx, bottom), (cx - head, neck), (cx - shaft, neck)]


def offset(poly, d):
    """Grows a convex-ish outline by d along the vertex normals."""
    n = len(poly)
    cx = sum(p[0] for p in poly) / n
    cy = sum(p[1] for p in poly) / n
    out = []
    for i, (x, y) in enumerate(poly):
        px, py = poly[i - 1]
        nx, ny = poly[(i + 1) % n]
        ex, ey = nx - px, ny - py
        length = math.hypot(ex, ey) or 1
        mx, my = ey / length, -ex / length
        if mx * (x - cx) + my * (y - cy) < 0:
            mx, my = -mx, -my
        out.append((x + mx * d, y + my * d))
    return out


def drawing(size):
    """(polygon, colour) pairs, back to front, for one icon size."""
    px = 1 / size
    small = size < 32
    stroke = max(px, 1.6 / 48) if not small else px
    body = card(0.17, 0.05, 0.83, 0.95, 0.07, 0.17)
    shapes = [(offset(body, stroke), OUTLINE), (body, BODY)]
    if not small:
        for i in range(5):
            x = 0.27 + i * 0.075
            shapes.append((rect(x, 0.11, x + 0.045, 0.26), CONTACT))
    label = rounded_rect(0.25, 0.33 if not small else 0.28, 0.75, 0.88, 0.05)
    shapes.append((label, LABEL))
    a = arrow(0.5, 0.38 if not small else 0.32, 0.83, 0.075 if not small else 0.1,
              0.19 if not small else 0.22, 0.2)
    if not small:
        shapes.append((offset(a, 0.6 * stroke), ARROW_DARK))
    shapes.append((a, ARROW))
    return shapes


# --- Rasteriser -------------------------------------------------------------

def coverage(poly, size):
    """Fraction of each pixel the polygon covers (non-zero winding)."""
    cov = [[0.0] * size for _ in range(size)]
    pts = [(x * size, y * size) for x, y in poly]
    edges = [(pts[i - 1], pts[i]) for i in range(len(pts))]
    w = 1 / SS
    for sy in range(size * SS):
        y = (sy + 0.5) / SS
        xs = []
        for (x0, y0), (x1, y1) in edges:
            if (y0 <= y < y1) or (y1 <= y < y0):
                xs.append((x0 + (y - y0) * (x1 - x0) / (y1 - y0), 1 if y1 > y0 else -1))
        xs.sort()
        row = cov[int(y)]
        wind, start = 0, 0.0
        for x, d in xs:
            before, wind = wind, wind + d
            if not before and wind:
                start = x
            elif before and not wind:
                a, b = max(start, 0.0), min(x, float(size))
                p = int(a)
                while p < b and p < size:
                    row[p] += (min(b, p + 1) - max(a, p)) * w
                    p += 1
    return cov


def render(size):
    """RGBA rows, premultiplied while compositing, straight alpha out."""
    img = [[[0.0, 0.0, 0.0, 0.0] for _ in range(size)] for _ in range(size)]
    for poly, (r, g, b) in drawing(size):
        cov = coverage(poly, size)
        for y in range(size):
            for x in range(size):
                a = min(cov[y][x], 1.0)
                if a <= 0:
                    continue
                p = img[y][x]
                p[0] = r * a + p[0] * (1 - a)
                p[1] = g * a + p[1] * (1 - a)
                p[2] = b * a + p[2] * (1 - a)
                p[3] = a + p[3] * (1 - a)
    rows = []
    for y in range(size):
        row = bytearray()
        for r, g, b, a in img[y]:
            if a > 0:
                r, g, b = r / a, g / a, b / a
            row += bytes(round(min(max(v, 0), 1) * 255) for v in (r, g, b, a))
        rows.append(bytes(row))
    return rows


def png(rows, width, height):
    raw = b"".join(b"\0" + row for row in rows)

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def ico(images):
    """An .ico of PNG images, which Windows reads from Vista on."""
    head = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, data = b"", b""
    for size, blob in images:
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
        data += blob
    return head + entries + data


def main():
    images, renders = [], {}
    for size in SIZES:
        rows = render(size)
        renders[size] = rows
        images.append((size, png(rows, size, size)))
    out = os.path.join(HERE, "updateAll.ico")
    with open(out, "wb") as f:
        f.write(ico(images))
    print(f"wrote {out} ({os.path.getsize(out)} bytes)")

    if len(sys.argv) == 3 and sys.argv[1] == "--preview":
        # Every size side by side on a light and a dark background, each
        # small size also enlarged 4x, for a look at the result.
        width = sum(SIZES) + sum(s * 4 for s in SIZES if s < 64) + 10 * (2 * len(SIZES))
        height = 256 + 20
        canvas = []
        for bg in ((236, 236, 236), (32, 32, 32)):
            band = [[list(bg) for _ in range(width)] for _ in range(height)]
            x = 10
            for size in SIZES:
                for scale in ((1, 4) if size < 64 else (1,)):
                    rows = renders[size]
                    for y in range(size * scale):
                        src = rows[y // scale]
                        for xx in range(size * scale):
                            r, g, b, a = src[(xx // scale) * 4:(xx // scale) * 4 + 4]
                            d = band[10 + y][x + xx]
                            for c, v in enumerate((r, g, b)):
                                d[c] = round(v * a / 255 + d[c] * (1 - a / 255))
                    x += size * scale + 10
            canvas += band
        rows = [bytes(v for px in row for v in px + [255]) for row in canvas]
        with open(sys.argv[2], "wb") as f:
            f.write(png(rows, width, len(canvas)))
        print(f"wrote {sys.argv[2]}")


if __name__ == "__main__":
    main()
