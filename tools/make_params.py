#!/usr/bin/env python3
"""Writes vst/params.json: the controls Twisted Breaks shows on the MPC.

The order is the VST parameter index. MPC projects, automation and Q-Link maps
store parameters by index, so once a version is released: APPEND ONLY, never
insert or reorder (vst/params.lock, written by the toolkit, fails the build if
you do).

Key conventions read by src/MpcHost.cpp:
  sp_<name>   the SELECTED pad's Twisted Breaks parameter pad<NN>_<name>
  fx_<key>    the FX GROUP's effect on/off: grp_<g>_<key>
  macro_*, master_*, seq_*, mut_*, grp_* ...  a Twisted Breaks parameter of the same id
  anything in MpcHost.cpp's specials table (kit, pad, fx_group, ...) is the host's own
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "vst", "params.json")

ONOFF = ["Off", "On"]
params = []
sections = []


def section(label, items):
    sections.append({"label": label, "keys": [p["key"] for p in items]})
    params.extend(items)


def num(key, name, lo, hi, default, unit="", **kw):
    d = {"key": key, "name": name, "min": lo, "max": hi, "default": default}
    if unit:
        d["unit"] = unit
    d.update(kw)
    return d


def opts(key, name, options, default=0, **kw):
    d = {"key": key, "name": name, "options": options, "default": default}
    d.update(kw)
    return d


def trig(key, name):
    return {"key": key, "name": name, "options": ONOFF, "default": 0, "momentary": True}


def readout(key, name):
    return {"key": key, "name": name, "min": 0, "max": 0, "default": 0, "display": "string", "type": "readout"}


# ---- KIT: what the plugin plays ------------------------------------------------
section("Kit", [
    num("kit", "Kit", 0, 99, 0, display="int", dynamic_display=True, refresh_display=True),
    trig("kit_load", "Load Kit"),
    readout("status", "Status"),
    num("master_gain", "Level", -60, 6, 0, "dB", dynamic_display=True),
    opts("master_limiter", "Limiter", ONOFF, 1),
    opts("scene_view", "Pads", ["A", "B"], 0),
    num("xfade", "Crossfader", 0, 1, 0.5, dynamic_display=True),
    opts("xfade_curve", "Curve", ["Smooth", "Sharp"], 0),
])

# ---- PAD: the selected pad ---------------------------------------------------------
section("Pad", [
    num("pad", "Pad", 1, 32, 1, display="int", dynamic_display=True, refresh_display=True),
    num("sp_gain", "Gain", -48, 12, 0, "dB", dynamic_display=True),
    num("sp_pitch", "Pitch", -12, 12, 0, "st", dynamic_display=True),
    num("sp_pan", "Pan", -1, 1, 0, dynamic_display=True),
    opts("sp_mode", "Mode", ["One-shot", "Loop", "Slice", "Seq One", "Seq Loop", "Loop Cont"], 0),
    num("sp_slice", "Slice", 1, 16, 1, display="int"),
    opts("sp_reverse", "Reverse", ONOFF, 0),
    opts("sp_choke", "Choke", ["Off", "1", "2", "3", "4"], 0),
    num("sp_start", "Start", 0, 1, 0, dynamic_display=True),
    num("sp_end", "End", 0, 1, 1, dynamic_display=True),
    opts("sp_stretch", "Stretch", ["Off", "Repitch", "Beats", "Texture", "Akai Classic", "Akai Revised"], 0),
    opts("sp_char", "Character", ["Clean", "SP-1200", "MPC60", "S950", "Lo-Fi"], 0),
    opts("sp_swap", "Swap", ["Off", "1", "2", "3", "4"], 0),
    opts("sp_swapq", "Swap At", ["Slice", "1/16", "Beat", "Bar"], 0),
    opts("sp_group", "Group", ["A", "B", "C", "D"], 0),
    num("sp_velsens", "Velocity", 0, 1, 1, dynamic_display=True),
])

# ---- SEQUENCER ------------------------------------------------------------------------
section("Sequencer", [
    opts("seq_run", "Sequencer", ONOFF, 1),
    num("seq_length", "Length", 1, 32, 16, display="int"),
    opts("seq_res", "Step", ["1/8", "1/16", "1/32"], 1),
    opts("seq_fill", "Fill", ONOFF, 0),
    trig("mut_go", "Mutate"),
    num("mut_amount", "Amount", 0, 100, 35, "%", display="int"),
    opts("snapshot", "Snapshot", ["Off", "1", "2", "3", "4", "5", "6", "7", "8"], 0),
    opts("bank_active", "Break Bank", ["1", "2", "3", "4", "5", "6", "7", "8"], 0),
])

# ---- FX: the 16 performance effects of one group -------------------------------------
FX = [("fxmute", "Mute"), ("lpf", "LPF"), ("hpf", "HPF"), ("bpf", "BPF"),
      ("crush", "Crush"), ("rate", "Rate"), ("drive", "Drive"), ("tape", "Tape"),
      ("akaizer", "Akaizer"), ("repeat", "Repeat"), ("gate", "Gate"), ("pitch", "Pitch Drop"),
      ("delay", "Delay"), ("reverb", "Reverb"), ("comb", "Comb"), ("reverse", "Reverse")]
section("FX", [opts("fx_group", "FX Group", ["A", "B", "C", "D"], 0)]
        + [opts("fx_" + k, n, ONOFF, 0) for k, n in FX])

# ---- MACROS ----------------------------------------------------------------------------
MACROS = [("destroy", "Destroy"), ("mangle", "Amen Mangle"), ("rewind", "Rewind"), ("darken", "Darken"),
          ("tapestop", "Tape Stop"), ("dive", "Pitch Dive"), ("roll", "Jungle Roll"), ("scifi", "Sci-Fi")]
section("Macros", [opts("macro_" + k, n, ONOFF, 0) for k, n in MACROS]
        + [num("macro_amount", "Amount", 0, 100, 60, "%", display="int"),
           opts("macro_group", "Macro Group", ["A", "B", "C", "D", "All"], 4),
           opts("macro_latch", "Latch", ONOFF, 0)])

# ---- MIXER ------------------------------------------------------------------------------
mix = []
for g in "abcd":
    mix.append(num("grp_%s_level" % g, "Group %s" % g.upper(), -60, 6, 0, "dB", dynamic_display=True))
for g in "abcd":
    mix.append(opts("grp_%s_mute" % g, "Mute %s" % g.upper(), ONOFF, 0))
section("Mixer", mix)

json.dump({"name": "Twisted Breaks", "params": params, "sections": sections}, open(OUT, "w"), indent=1)
print("%s: %d parameters" % (os.path.relpath(OUT), len(params)))
