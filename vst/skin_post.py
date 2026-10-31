#!/usr/bin/env python3
"""Run by the toolkit after it builds the skin (vst.json "skin_post"): python3 skin_post.py <skin dir>.

Touching a control on the MPC focuses it (the data wheel then turns that one). The toolkit draws that focus
invisibly; here it becomes an amber frame with a faint amber wash, so you can see which control is being
controlled. Fails when the skin has no focus parts to change (a toolkit change moved them)."""
import json
import os
import sys

OUTLINE = "ffffa928"      # ARGB: the skin's amber
WASH = "26ffa928"         # ~15% amber over the control
THICKNESS = 3.0

skin = sys.argv[1]
tui = os.path.join(skin, "Plugin Skins", "TUI.json")
data = json.load(open(tui))
changed = 0


def walk(o):
    global changed
    if isinstance(o, dict):
        cd = o.get("componentData")
        if isinstance(cd, dict) and cd.get("type") == "Focus":
            d = cd["data"]
            d["outlineColour"] = OUTLINE
            d["backgroundColour"] = WASH
            d["outlineThickness"] = THICKNESS
            d["backgroundInset"] = 2.0
            changed += 1
        for v in o.values():
            walk(v)
    elif isinstance(o, list):
        for v in o:
            walk(v)


walk(data)
if changed == 0:
    sys.exit("skin_post: no Focus parts found in %s" % tui)
json.dump(data, open(tui, "w"), indent=1)
print("skin_post: %d focus frames made visible (amber)" % changed)
