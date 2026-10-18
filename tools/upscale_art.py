#!/usr/bin/env python3
"""Generate graphics_4x/ from graphics/ for the HD renderer.

Every PNG is magnified 4x with Scale4x (Scale2x applied twice). Scale2x
only ever copies colours of neighbouring pixels, never blends them, which
matters here: the game recolours walls, floors and characters at runtime by
looking for exact key colours (greys, pure red channel, skin/hair/body keys),
so a smoothing upscaler would leave wrongly coloured fringes.

Spritesheets (files named foo_WxH.png) are scaled frame by frame so frames
do not bleed into each other, and are saved as foo_{4W}x{4H}.png.

Usage: python upscale_art.py <graphics dir> <output dir>
"""
import re
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image

SCALE = 4
# Images read pixel by pixel on the CPU by index; keep them at 1x
SKIP = {"palette.png", "font.png"}
SHEET_RE = re.compile(r"^_(\d+)x(\d+)")


def scale2x(img):
    """img: HxWx4 uint8 array; returns 2Hx2Wx4."""
    p = np.pad(img, ((1, 1), (1, 1), (0, 0)), mode="edge")
    e = p[1:-1, 1:-1]
    b = p[:-2, 1:-1]  # up
    d = p[1:-1, :-2]  # left
    f = p[1:-1, 2:]  # right
    h = p[2:, 1:-1]  # down

    def eq(x, y):
        return np.all(x == y, axis=2)

    # Scale2x: only act where the pixel is not on a straight line
    edge = ~eq(b, h) & ~eq(d, f)

    def pick(match, src):
        return np.where((edge & match)[..., None], src, e)

    e0 = pick(eq(d, b), d)  # top-left
    e1 = pick(eq(b, f), f)  # top-right
    e2 = pick(eq(d, h), d)  # bottom-left
    e3 = pick(eq(h, f), f)  # bottom-right

    out = np.empty((e.shape[0] * 2, e.shape[1] * 2, 4), dtype=img.dtype)
    out[0::2, 0::2] = e0
    out[0::2, 1::2] = e1
    out[1::2, 0::2] = e2
    out[1::2, 1::2] = e3
    return out


def scale4x(img):
    return scale2x(scale2x(img))


def normalise(img):
    # Fully transparent pixels compare equal whatever their RGB
    img = img.copy()
    img[img[..., 3] == 0] = 0
    return img


def sheet_size(stem):
    """Mirror PicManagerAdd: foo_WxH means W by H sprites."""
    underscore = stem.rfind("_")
    x = stem.rfind("x")
    if underscore < 0 or x < 0 or not (underscore + 1 < x < len(stem) - 1):
        return None
    m = SHEET_RE.match(stem[underscore:])
    if not m:
        return None
    return underscore, int(m.group(1)), int(m.group(2))


def upscale_file(src, dst):
    img = normalise(np.asarray(Image.open(src).convert("RGBA")))
    stem = src.stem
    sheet = sheet_size(stem)
    if sheet is None:
        out = scale4x(img)
        name = src.name
    else:
        underscore, w, h = sheet
        rows, cols = img.shape[0] // h, img.shape[1] // w
        out = np.zeros((rows * h * SCALE, cols * w * SCALE, 4), np.uint8)
        for r in range(rows):
            for c in range(cols):
                cell = img[r * h:(r + 1) * h, c * w:(c + 1) * w]
                out[r * h * SCALE:(r + 1) * h * SCALE,
                    c * w * SCALE:(c + 1) * w * SCALE] = scale4x(cell)
        name = f"{stem[:underscore]}_{w * SCALE}x{h * SCALE}{src.suffix}"
    dst.mkdir(parents=True, exist_ok=True)
    Image.fromarray(out, "RGBA").save(dst / name, optimize=True)


def main():
    src_root, dst_root = Path(sys.argv[1]), Path(sys.argv[2])
    count = 0
    for src in sorted(src_root.rglob("*.png")):
        if src.name in SKIP:
            continue
        upscale_file(src, dst_root / src.relative_to(src_root).parent)
        count += 1
    for lic in ("LICENSE.md", "originals.txt"):
        if (src_root / lic).exists():
            shutil.copy(src_root / lic, dst_root / lic)
    print(f"{count} images upscaled {SCALE}x into {dst_root}")


if __name__ == "__main__":
    main()
