#!/usr/bin/env python3
# Test helper for vt_gif: count pixels in a rendered PNG that differ from the
# default background, and (optionally) assert a colourful (non-gray) region for
# emoji. Exit 0 if the assertion holds, 1 otherwise.
#   vtg_probe.py FILE.png nonbg MIN          -> >= MIN non-background pixels
#   vtg_probe.py FILE.png colour X0 Y0 X1 Y1 -> some pixel in the box is saturated
import sys
from PIL import Image

img = Image.open(sys.argv[1]).convert("RGB")
mode = sys.argv[2]
BG = (0x0E, 0x0E, 0x14)

if mode == "nonbg":
    want = int(sys.argv[3])
    n = sum(1 for p in img.getdata() if p != BG)
    print("non-bg pixels:", n)
    sys.exit(0 if n >= want else 1)

if mode == "colour":
    x0, y0, x1, y1 = (int(a) for a in sys.argv[3:7])
    px = img.load()
    sat = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = px[x, y]
            if max(r, g, b) - min(r, g, b) > 40:   # chromatic, not gray
                sat += 1
    print("saturated pixels:", sat)
    sys.exit(0 if sat >= 20 else 1)

print("unknown mode", mode)
sys.exit(2)
