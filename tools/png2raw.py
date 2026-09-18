#!/usr/bin/env python3
"""
png2raw.py - build the .444 / .555 artwork caches the bootloader reads.

The loader normally converts PNG/JPG artwork on the card itself, the first time
a tile is shown (src/image_convert.cpp). This script does the same job on a
host, so the SD-card bundle can ship the caches ready-made and a fresh card does
not have to spend its first boot converting.

The output is byte-identical to what the board would have written: the pipeline
here mirrors image_convert.cpp step for step, including the RGB565 round-trip
that PNGdec's getLineAsRGB565() imposes on every PNG source pixel.

    tools/png2raw.py emu/assets/themes/0/Categories
    tools/png2raw.py --check emu/assets/themes/0/duke3d.png

File format (both extensions):

    uint16 width, uint16 height          little-endian
    width*height uint16 pixels           row-major, little-endian
      .444   0000 RRRR GGGG BBBB
      .555   0RRR RRGG GGGB BBBB

Requires ffmpeg/ffprobe (for decoding) and numpy.
"""

import argparse
import json
import os
import subprocess
import sys

import numpy as np

MAX_W, MAX_H = 320, 240

# image_convert.cpp:kMaxSrcW / kMaxSrcH -- larger sources are refused there, so
# refuse them here too rather than shipping a cache the board would never make.
MAX_SRC_W, MAX_SRC_H = 1280, 960

EXTS = (".png", ".jpg", ".jpeg")


def probe(path):
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0",
         "-show_entries", "stream=width,height", "-of", "json", path],
        check=True, capture_output=True, text=True).stdout
    s = json.loads(out)["streams"][0]
    return int(s["width"]), int(s["height"])


def decode_rgb24(path, w, h):
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path,
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        check=True, capture_output=True).stdout
    want = w * h * 3
    if len(raw) != want:
        raise RuntimeError(f"{path}: decoded {len(raw)} bytes, expected {want}")
    return np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 3)


def quantize_565(img):
    """PNGdec hands image_convert.cpp RGB565, which then expands back to 888.

    Only the PNG path goes through that (image_convert.cpp:335-341); the JPEG
    decoder delivers RGB8888 straight into the accumulator.
    """
    r5 = img[:, :, 0] >> 3
    g6 = img[:, :, 1] >> 2
    b5 = img[:, :, 2] >> 3
    out = np.empty_like(img)
    out[:, :, 0] = (r5 << 3) | (r5 >> 2)
    out[:, :, 1] = (g6 << 2) | (g6 >> 4)
    out[:, :, 2] = (b5 << 3) | (b5 >> 2)
    return out


def geometry(src_w, src_h, max_w, max_h, letterbox):
    """image_convert.cpp:compute_geometry() -- fit, never upscale, then centre."""
    scale = min(max_w / src_w, max_h / src_h, 1.0)
    dst_w = min(max(int(src_w * scale + 0.5), 1), max_w)
    dst_h = min(max(int(src_h * scale + 0.5), 1), max_h)
    if letterbox:
        return dst_w, dst_h, max_w, max_h, (max_w - dst_w) // 2, (max_h - dst_h) // 2
    return dst_w, dst_h, dst_w, dst_h, 0, 0


def downscale(img, dst_w, dst_h):
    """Box-average with the board's integer bucket mapping.

    Source pixel (sx, sy) lands in output cell (sx*dst_w/src_w, sy*dst_h/src_h),
    all C integer division; the cell is then sum/count, truncating. See
    image_convert.cpp:262-276 and the accumulate loops in the draw callbacks.
    """
    src_h, src_w = img.shape[:2]
    xo = np.minimum((np.arange(src_w) * dst_w) // src_w, dst_w - 1)
    yo = np.minimum((np.arange(src_h) * dst_h) // src_h, dst_h - 1)
    lin = (yo[:, None] * dst_w + xo[None, :]).ravel()
    cells = dst_w * dst_h

    count = np.bincount(lin, minlength=cells)
    out = np.zeros((cells, 3), dtype=np.uint32)
    for c in range(3):
        # Sums stay well inside float64's exact integer range (255 * pixels).
        s = np.bincount(lin, weights=img[:, :, c].ravel().astype(np.float64),
                        minlength=cells).astype(np.uint64)
        out[:, c] = np.where(count > 0, s // np.maximum(count, 1), 0)
    return out.reshape(dst_h, dst_w, 3)


def pack(rgb, canvas_w, canvas_h, off_x, off_y):
    """Pack to .444 and .555, placed on a black letterbox canvas."""
    dst_h, dst_w = rgb.shape[:2]
    R, G, B = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]

    p444 = ((R >> 4) << 8) | ((G >> 4) << 4) | (B >> 4)
    p555 = ((R >> 3) << 10) | ((G >> 3) << 5) | (B >> 3)

    out = []
    for plane in (p444, p555):
        canvas = np.zeros((canvas_h, canvas_w), dtype="<u2")
        canvas[off_y:off_y + dst_h, off_x:off_x + dst_w] = plane
        hdr = np.array([canvas_w, canvas_h], dtype="<u2").tobytes()
        out.append(hdr + canvas.tobytes())
    return out


def convert(path, max_w, max_h, letterbox):
    src_w, src_h = probe(path)
    if src_w > MAX_SRC_W or src_h > MAX_SRC_H:
        raise RuntimeError(f"{path}: {src_w}x{src_h} exceeds the "
                           f"{MAX_SRC_W}x{MAX_SRC_H} the board accepts")

    img = decode_rgb24(path, src_w, src_h)
    if path.lower().endswith(".png"):
        img = quantize_565(img)

    dst_w, dst_h, canvas_w, canvas_h, off_x, off_y = geometry(
        src_w, src_h, max_w, max_h, letterbox)
    rgb = downscale(img, dst_w, dst_h)
    return pack(rgb, canvas_w, canvas_h, off_x, off_y), (src_w, src_h, dst_w, dst_h)


def sources(paths):
    for p in paths:
        if os.path.isdir(p):
            for name in sorted(os.listdir(p)):
                if name.lower().endswith(EXTS):
                    yield os.path.join(p, name)
        else:
            yield p


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", help="image files, or directories of them")
    ap.add_argument("--check", action="store_true",
                    help="compare against the existing .444/.555 instead of writing")
    ap.add_argument("--max-w", type=int, default=MAX_W)
    ap.add_argument("--max-h", type=int, default=MAX_H)
    ap.add_argument("--no-letterbox", action="store_true",
                    help="emit the scaled size instead of padding to max (screensaver format)")
    args = ap.parse_args()

    rc = 0
    for src in sources(args.paths):
        base = os.path.splitext(src)[0]
        try:
            (d444, d555), (sw, sh, dw, dh) = convert(
                src, args.max_w, args.max_h, not args.no_letterbox)
        except Exception as e:                      # noqa: BLE001 - report and continue
            print(f"FAIL  {src}: {e}", file=sys.stderr)
            rc = 1
            continue

        if args.check:
            for ext, data in ((".444", d444), (".555", d555)):
                ref = base + ext
                if not os.path.exists(ref):
                    print(f"MISS  {ref}")
                    rc = 1
                elif open(ref, "rb").read() == data:
                    print(f"OK    {ref}")
                else:
                    print(f"DIFF  {ref}")
                    rc = 1
        else:
            for ext, data in ((".444", d444), (".555", d555)):
                with open(base + ext, "wb") as f:
                    f.write(data)
            print(f"{src}: {sw}x{sh} -> {dw}x{dh}  ({len(d444)} bytes each)")

    return rc


if __name__ == "__main__":
    sys.exit(main())
