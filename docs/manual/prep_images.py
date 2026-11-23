#!/usr/bin/env python3
"""Screenshots for the manual, from a release's skin preview (TwistedBreaks-MPC-skin-preview.zip, unzipped).

    python3 docs/manual/prep_images.py <unzipped preview folder> <out folder>

page_0..13.png are the tabs in skin/make_skin.py's order. The MANGLE page gets a demo bar painted from
vst/art/mangle_cell.png, since the renderer shows whichever frame it picks.
"""
import os
import sys
from PIL import Image

NAMES = ["kit", "pad", "samples", "slice", "autoslice", "steps", "sequencer", "fx", "fxedit", "mangle", "manglefx",
         "macros", "mixer", "setup"]
HERE = os.path.dirname(os.path.abspath(__file__))
CELLS = os.path.join(HERE, "..", "..", "vst", "art", "mangle_cell.png")

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
for i, n in enumerate(NAMES):
    im = Image.open(os.path.join(src, "page_%d.png" % i)).convert("RGB")
    if n == "mangle":
        cells = Image.open(CELLS).convert("RGB")
        w, h = 66, 150
        demo = [4, 6, 0, 8, 1, 0, 16, 18] + [36] * 8   # REPEAT run, THRU, REVERSE, step 5 playing, STOP run, 8-step bar
        for s, f in enumerate(demo):
            im.paste(cells.crop((0, f * h, w, f * h + h)), (47 + s * 70, 163))
    im.save(os.path.join(out, n + ".jpg"), quality=88)
print("ok")
