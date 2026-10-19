#!/usr/bin/env python3
"""check_layout.py -- measure the rendered menu and assert the text fits.

Eyeballing a scaled screenshot is how layout bugs survive review. This finds the
accent pill, finds the bright text inside it, and reports the insets:

    inset_top    = text_top    - pill_top     (positive = inside)
    inset_bottom = pill_bottom - text_bottom  (positive = inside)

Both must be >= 0. Exit code is 1 when they are not, so this can gate a build.

Usage: tools/check_layout.py preview.png [--min-inset 6]
"""
import argparse
import sys

try:
    from PIL import Image
except ImportError:
    raise SystemExit("need Pillow (apt install python3-pil)")

ACCENT = (76, 141, 255)   # C_ACCENT in src/ui.c


def is_accent(c):
    return (abs(c[0] - ACCENT[0]) < 26 and abs(c[1] - ACCENT[1]) < 26 and
            abs(c[2] - ACCENT[2]) < 26)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--min-inset", type=int, default=6)
    a = ap.parse_args()

    img = Image.open(a.image).convert("RGB")
    W, H = img.size
    px = img.load()

    # accent bands: rows with at least 200 accent pixels
    rows = []
    for y in range(H):
        n = sum(1 for x in range(0, W, 2) if is_accent(px[x, y]))
        if n * 2 >= 200:
            rows.append(y)
    if not rows:
        print("FAIL: no accent pill found")
        return 1

    bands = []
    cur = [rows[0]]
    for y in rows[1:]:
        if y == cur[-1] + 1:
            cur.append(y)
        else:
            bands.append(cur)
            cur = [y]
    bands.append(cur)

    # the pill is the tallest band; the others are the accent rule and the
    # countdown bar, which are deliberately thin
    pill = max(bands, key=lambda b: b[-1] - b[0])
    py0, py1 = pill[0], pill[-1]

    # text inside the pill: bright pixels, generous threshold so the dimmed
    # detail line still counts
    x0, x1 = W // 8, W - W // 8
    text_rows = []
    for y in range(py0, py1 + 1):
        n = 0
        for x in range(x0, x1, 2):
            r, g, b = px[x, y]
            if r > 170 and g > 170 and b > 190:
                n += 1
        if n > 3:
            text_rows.append(y)

    print("image      : %dx%d" % (W, H))
    print("accent bands: %s" % ", ".join("%d..%d(h=%d)" % (b[0], b[-1], b[-1] - b[0] + 1)
                                         for b in bands))
    print("pill       : y %d..%d (height %d)" % (py0, py1, py1 - py0 + 1))

    if not text_rows:
        print("FAIL: no text found inside the pill")
        return 1

    ty0, ty1 = text_rows[0], text_rows[-1]
    inset_top = ty0 - py0
    inset_bottom = py1 - ty1
    print("text       : y %d..%d" % (ty0, ty1))
    print("insets     : top %+d, bottom %+d" % (inset_top, inset_bottom))

    if inset_top < a.min_inset or inset_bottom < a.min_inset:
        print("FAIL: text is outside the pill or closer than %d px to its edge"
              % a.min_inset)
        return 1
    print("PASS: text sits %d px from the top and %d px from the bottom"
          % (inset_top, inset_bottom))
    return 0


if __name__ == "__main__":
    sys.exit(main())
