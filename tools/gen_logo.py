#!/usr/bin/env python3
# Generate examples/assets/logo.png — the timui.h ("timmy") brand mark: a terminal
# window showing `t` + a green underscore cursor (t_). 5:3 so it isn't distorted
# in the chat's inline badge. Regenerate with `make gen-logo` (needs pillow via nix).
from PIL import Image, ImageDraw, ImageFont
import glob

W, H = 600, 360
BG      = (18, 18, 26, 255)      # terminal body
BAR     = (38, 40, 54, 255)      # title bar
FG      = (228, 228, 234, 255)   # the "t"
GREEN   = (74, 222, 128, 255)    # the "_" cursor (brand accent)
DOTS    = [(255, 95, 86), (255, 189, 46), (39, 201, 63)]

img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
m, rad, bar = 14, 30, 70

# window: dark body, then a lighter title bar squared-off at its lower edge
d.rounded_rectangle([m, m, W - m, H - m], radius=rad, fill=BG)
d.rounded_rectangle([m, m, W - m, m + bar], radius=rad, fill=BAR)
d.rectangle([m, m + bar - rad, W - m, m + bar], fill=BAR)
# traffic-light dots
for i, c in enumerate(DOTS):
    cx, cy, r = m + 34 + i * 42, m + bar // 2, 12
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=c + (255,))

# "t" in a bold monospace, then a green underscore cursor to its right
fp = sorted(glob.glob("/nix/store/*dejavu*/share/fonts/truetype/DejaVuSansMono-Bold.ttf"))[0]
font = ImageFont.truetype(fp, 210)
cy = m + bar + (H - m - (m + bar)) // 2 + 6          # vertical centre of the body
tw = d.textlength("t", font=font)
gap, uw, uh = 14, 96, 30
group_w = tw + gap + uw
x0 = (W - group_w) / 2
d.text((x0, cy), "t", fill=FG, font=font, anchor="lm")
ux = x0 + tw + gap
uy = cy + 46
d.rounded_rectangle([ux, uy, ux + uw, uy + uh], radius=7, fill=GREEN)   # the green "_"

img.save("examples/assets/logo.png")
print("wrote examples/assets/logo.png", img.size)
