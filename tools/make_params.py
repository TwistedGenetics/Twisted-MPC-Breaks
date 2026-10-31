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
    # not bare numbers: the toolkit's picture lines match "<key>:<n>" against option NAMES before indexes,
    # so options named "1".."8" would light the key one to the right of the setting
    opts("bank_active", "Break Bank", ["Bank 1", "Bank 2", "Bank 3", "Bank 4", "Bank 5", "Bank 6", "Bank 7", "Bank 8"], 0),
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

# ---- SLICE (0.2): the selected pad's slicer ---------------------------------------------
# Option names are never bare numbers: the toolkit's picture lines match "<key>:<n>" against names first.
section("Slice", [
    num("sl_sens", "Sensitivity", 0, 100, 50, "%", display="int"),
    opts("sl_count", "Slice Count", ["4 Slices", "8 Slices", "16 Slices"], 2),
    opts("sl_pick", "Slice", ["Slice %d" % (i + 1) for i in range(16)], 0),
    trig("sl_auto", "Auto Slice"),
    trig("sl_equal", "Equal Slice"),
    trig("sl_reset", "Reset Slices"),
    trig("sl_unslice", "Unslice"),
    trig("sl_spread", "Spread"),
    readout("sl_info", "Slicer"),
])

# ---- 1.0: SAMPLES (your own breaks from the SD card) ------------------------------------------
section("Samples", [
    num("smp_file", "Sample", 0, 998, 0, display="int", dynamic_display=True, nudge_pct=10),
    trig("smp_load", "Load Sample"),
    trig("smp_clear", "Clear Pad"),
    opts("smp_slice", "Slice On Load", ONOFF, 1),
    readout("smp_info", "Sample Browser"),
])

# ---- 1.0: STEPS (the selected pad's 32 steps) ---------------------------------------------------
section("Steps", [opts("st_%02d" % (i + 1), "Step %d" % (i + 1), ONOFF, 0) for i in range(32)]
        + [trig("st_clear", "Clear Steps"),
           readout("st_info", "Steps"),
           num("st_pos", "Playhead", 0, 32, 0, display="int")])

# ---- 1.0: SETUP (which notes play the pads) and SAVE KIT ----------------------------------------
section("Setup", [
    opts("note_base", "Pad Notes", ["36-51 (C1)", "37-52 (MPC)", "48-63 (C2)", "Custom"], 0),
    trig("note_learn", "Learn Pad Note"),
    readout("note_info", "MIDI In"),
    trig("kit_save", "Save Kit"),
])

# ---- 1.1: the SLICE screen (a waveform like the Mac app's CRT) ----------------------------------
# The waveform is WAVE_COLS picture columns, each a whole number the engine sets: frame = look * 9 + level,
# look = background (0 plain, 1 selected slice, 2 outside start/end) * 3 + mark (0 none, 1 slice marker, 2 playhead).
# Above them, one tab per column: 0 none, 1..16 the slice marker starting there, 17..32 the same, selected.
# Plain numbers, not options: an option the engine moves makes MPC rebuild its whole screen.
WAVE_COLS = 64
WAVE_LEVELS = 9
WAVE_LOOKS = 9
section("Slice screen", [
    num("sl_pos", "Move Marker", 0, 998, 0, display="int", dynamic_display=True, nudge_pct=10),
    trig("sl_add", "Add Marker"),
    trig("sl_del", "Delete Marker"),
    opts("sl_zoom", "Zoom", ["Whole", "Slice"], 0),
    trig("sl_play", "Play Slice"),
    readout("sl_head", "Screen Top"),
    readout("sl_foot", "Screen Bottom"),
    # a tap on the waveform: option c = column c; the engine puts it back on "None" so the same column taps again
    opts("wv_tap", "Wave Tap", ["Col %d" % (c + 1) for c in range(WAVE_COLS)] + ["None"], WAVE_COLS),
] + [num("wv_%02d" % (c + 1), "Wave %d" % (c + 1), 0, WAVE_LEVELS * WAVE_LOOKS - 1, 0, display="int")
     for c in range(WAVE_COLS)]
  + [num("wt_%02d" % (c + 1), "Marker %d" % (c + 1), 0, 32, 0, display="int") for c in range(WAVE_COLS)])

# ---- 1.1: SAMPLES becomes a browser of the MPC's own drives ------------------------------------------
BROWSER_ROWS = 7
section("Browser", [
    trig("smp_open", "Open"),
    trig("smp_back", "Back"),
    trig("smp_play", "Play Sample"),
    opts("smp_audition", "Audition", ONOFF, 1),
    readout("smp_path", "Folder"),
] + [readout("smp_l%d" % (r + 1), "Row %d" % (r + 1)) for r in range(BROWSER_ROWS)]
  + [trig("smp_row%d" % (r + 1), "Pick Row %d" % (r + 1)) for r in range(BROWSER_ROWS)])

# ---- 1.1: what shapes the sound, tempo sync, kit stepping ------------------------------------------------
section("Sound", [
    opts("sp_sync", "Tempo Sync", ONOFF, 1),
    readout("sp_info", "Pad Playback"),
    readout("snd_info1", "Sound 1"),
    readout("snd_info2", "Sound 2"),
    trig("kit_prev", "Previous Kit"),
    trig("kit_next", "Next Kit"),
    readout("kit_info", "Kit Info"),
])

json.dump({"name": "Twisted Breaks", "params": params, "sections": sections}, open(OUT, "w"), indent=1)
print("%s: %d parameters" % (os.path.relpath(OUT), len(params)))
