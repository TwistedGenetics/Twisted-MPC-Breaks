#!/usr/bin/env python3
"""Run by the toolkit after it builds the skin (vst.json "skin_post"): python3 skin_post.py <skin dir>.

Touching a control on the MPC focuses it (the data wheel then turns that one). The toolkit draws that focus
invisibly; here it becomes an amber frame with a faint amber wash, so you can see which control is being
controlled. Fails when the skin has no focus parts to change (a toolkit change moved them).

It also gives the data wheel's push a job in the sample browser (below)."""
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

# The data wheel in the sample browser (1.9). The wheel turns the focused control, and its push is that control's
# "Enter Pressed" action (a knob's is the value overlay). On every SCROLL knob (smp_file: the browser's selection)
# the push instead toggles smp_enter, a momentary trigger, through an extra handle "Press": open the folder, or load
# the file onto the pad. (The same action and handle as the toolkit's own "wheel" layout kind.)
here = os.path.dirname(os.path.abspath(__file__))
keys = [p["key"] for p in json.load(open(os.path.join(here, "params.json")))["params"]]
scroll, enter = "Parameter %d" % keys.index("smp_file"), "Parameter %d" % keys.index("smp_enter")
defs = data["pageData"]["componentDefinitions"]["localComponentDefinitions"]
by_key = {d["key"]: d for d in defs}
wheels = 0


def wheel_def(ctype):
    """A copy of knob definition ctype whose Enter Pressed toggles the "Press" handle."""
    key = ctype + "_wheelpush"
    if key not in by_key:
        d = json.loads(json.dumps(by_key[ctype]))
        d["key"] = key
        acts = [x for x in d["value"]["actions"] if x["onAction"] != "Enter Pressed"]
        acts.append({"version": 2, "onAction": "Enter Pressed", "handler": "Toggle Switch", "handleName": "Press",
                     "additionalData": "", "handle remapping": {"version": 1, "map": []}})
        d["value"]["actions"] = acts
        defs.append(d)
        by_key[key] = d
    return key


def walk_scroll(o):
    global wheels
    if isinstance(o, dict):
        hr = o.get("handle remapping")
        cd = o.get("componentData")
        if hr and isinstance(cd, dict) and cd.get("type") in by_key and \
                any(m.get("key") == "Data" and m.get("value") == scroll for m in hr.get("map", [])):
            cd["type"] = wheel_def(cd["type"])
            hr["map"] = [m for m in hr["map"] if m.get("key") != "Press"] + [{"key": "Press", "value": enter}]
            wheels += 1
        for v in list(o.values()):
            walk_scroll(v)
    elif isinstance(o, list):
        for v in list(o):
            walk_scroll(v)


walk_scroll(data["pageData"])
if wheels == 0:
    sys.exit("skin_post: no SCROLL knob (smp_file) found in %s" % tui)
json.dump(data, open(tui, "w"), indent=1)
print("skin_post: %d focus frames made visible (amber); the wheel's push opens / loads on %d SCROLL knobs" % (changed, wheels))
