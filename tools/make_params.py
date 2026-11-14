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
] + [dict(readout("smp_l%d" % (r + 1), "Row %d" % (r + 1)), poll=False) for r in range(BROWSER_ROWS)]
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

# ---- 1.2: hitting a pad selects it (every page then shows that pad) --------------------------------------
section("Follow", [opts("pad_follow", "Pad Follows Hits", ONOFF, 1)])

# ---- 1.3: re-hits in SWAP groups, finger editing of slices, naming a kit ------------------------------------
KEY_CHARS = "1234567890-QWERTYUIOP+ASDFGHJKL&!ZXCVBNM.()"   # src/MpcHost.cpp kKeyChars: 4 rows of 11, the last one "None"
section("Edit", [
    opts("swap_rehit", "Swap Re-hit", ["Ignore", "Stop"], 0),
    opts("sl_tap", "Tap Does", ["Pick", "Move", "Add", "Delete"], 0),
    opts("kit_naming", "Naming", ONOFF, 0),
    opts("kit_key", "Name Key", ["Key %d" % (i + 1) for i in range(len(KEY_CHARS))] + ["None"], len(KEY_CHARS)),
    readout("kit_name", "Kit Name"),
    trig("kit_space", "Space"),
    trig("kit_bksp", "Delete Letter"),
    trig("kit_clear", "Clear Name"),
    trig("kit_cancel", "Cancel"),
    trig("kit_ok", "Save Named Kit"),
])

# ---- 1.4: FX EDIT (the selected effect's settings and key mode), SAVE AS ------------------------------------
section("FX edit", [
    opts("fx_sel", "Edit Effect", [n for _, n in FX], 1),
    opts("fx_on", "Effect On", ONOFF, 0),
    opts("fx_mode", "Key Mode", ["Hold", "Latch"], 1),
    num("fx_p1", "Setting 1", 0, 1, 0.5, dynamic_display=True),
    num("fx_p2", "Setting 2", 0, 1, 0.5, dynamic_display=True),
    num("fx_p3", "Setting 3", 0, 1, 0.5, dynamic_display=True),
    readout("fx_name", "Effect"),
    readout("fx_n1", "Setting 1 Name"),
    readout("fx_n2", "Setting 2 Name"),
    readout("fx_n3", "Setting 3 Name"),
    trig("kit_saveas", "Save Kit As"),
] + [num("fx_lit_%02d" % (i + 1), "FX %d Lit" % (i + 1), 0, 1, 0, display="int") for i in range(16)])

# ---- 1.5: the browser as a touch list; chop tools (deep zoom, nudge, snap, tighten) ---------------------------
LIST_ROWS = 8
section("Browser 2", [readout("smp_r_%d" % (r + 1), "List %d" % (r + 1)) for r in range(LIST_ROWS)] + [
    trig("smp_pgup", "Page Up"),
    trig("smp_pgdn", "Page Down"),
    trig("smp_home", "Places"),
])
section("Chop", [
    opts("sl_view", "Zoom", ["All", "Slice", "400 ms", "100 ms", "25 ms"], 0),
    opts("sl_snap", "Snap", ["Off", "Zero", "Hit"], 2),
    trig("sl_nl2", "Nudge Left x8"),
    trig("sl_nl1", "Nudge Left"),
    trig("sl_nr1", "Nudge Right"),
    trig("sl_nr2", "Nudge Right x8"),
    trig("sl_tight", "Tighten Marker"),
    trig("sl_tightall", "Tighten All"),
])

# ---- 1.7: MANGLE (Twisted Breaks 0.29 / 0.30, A15) ----------------------------------------------------------
# The engine's own mgl_* parameters (keys = their ids), then the MPC's: the bar as 16 picture cells, a tap row,
# the move to paint, JOIN, RANDOMIZE / MUTATE / REVERT / CLEAR, presets, and the pads as steps.
# Cell = move * 4 + tied * 2 + playing (0..35); 36 = past the bar's last step.
MANGLE_MOVES = ["Thru", "Repeat", "Reverse", "Half", "Stop", "Double", "Scratch", "Freeze", "Shuffle"]
section("Mangle", [
    opts("mgl_on", "Mangle", ONOFF, 0),
    opts("mgl_target", "Mangle Target", ["Master", "Group A", "Group B", "Group C", "Group D"], 0),
    opts("mgl_steps", "Mangle Steps", ["4 Steps", "8 Steps", "16 Steps"], 1),
    num("mgl_mix", "Mangle Mix", 0, 100, 100, "%", display="int"),
    num("mgl_amount", "Mangle Amount", 0, 100, 100, "%", display="int"),
    opts("mgl_fill_every", "Fill Every", ["Off", "2 Bars", "4 Bars", "8 Bars", "16 Bars"], 0),
    opts("mgl_fill_len", "Fill Length", ["Bar", "Half Bar"], 0),
    opts("mgl_hold", "Mangle Hold", ONOFF, 0),
    trig("mgl_fill_next", "Fill Next Bar"),
    opts("mgl_style", "Randomize Style", ["Gentle", "Medium", "Wild"], 1),
    opts("mgl_slot", "Mangle Slot", ["Slot A", "Slot B", "Slot C", "Slot D"], 0),
    opts("mgl_chain", "Mangle Chain", ONOFF, 0),
    opts("mgl_tex_mode", "Texture", ["Off", "Tight", "Scatter", "Cloud", "Reverse"], 0),
    num("mgl_tex_amount", "Texture Amount", 0, 100, 50, "%", display="int"),
    num("mgl_tex_size", "Texture Size", 10, 250, 40, "ms", display="int"),
    num("mgl_tex_density", "Texture Density", 2, 60, 16, "/s", display="int"),
    num("mgl_tex_scatter", "Texture Scatter", 0, 100, 30, "%", display="int"),
    opts("mgl_space_mode", "Space", ["Off", "Echo", "Ping-Pong", "Diffuse"], 0),
    opts("mgl_space_time", "Space Time", ["1/32", "1/16", "1/16 T", "1/16 D", "1/8", "1/8 T", "1/8 D", "1/4", "1/4 T", "1/4 D", "1/2"], 6),
    num("mgl_space_fb", "Space Feedback", 0, 90, 45, "%", display="int"),
    num("mgl_space_amount", "Space Amount", 0, 100, 35, "%", display="int"),
    opts("mgl_lock_tex", "Lock Texture", ONOFF, 0),
    opts("mgl_lock_space", "Lock Space", ONOFF, 0),
    opts("mg_brush", "Paint Move", MANGLE_MOVES, 1),
    opts("mg_join", "Join Steps", ONOFF, 0),
    opts("mg_tap", "Step Tap", ["Step %d" % (i + 1) for i in range(16)] + ["None"], 16),
] + [num("mg_c%02d" % (i + 1), "Mangle Step %d" % (i + 1), 0, 36, 0, display="int") for i in range(16)] + [
    trig("mg_rand", "Randomize"),
    trig("mg_mutate", "Mutate Bar"),
    trig("mg_revert", "Revert"),
    trig("mg_clear", "Clear Bar"),
    readout("mg_info", "Mangle Info"),
    num("mg_preset", "Mangle Preset", 0, 99, 0, display="int", dynamic_display=True, refresh_display=True),
    trig("mg_load", "Load Mangle Preset"),
    trig("mg_save", "Save Mangle Preset"),
    opts("mg_pads", "Pads Paint Steps", ONOFF, 0),
])

json.dump({"name": "Twisted Breaks", "params": params, "sections": sections}, open(OUT, "w"), indent=1)
print("%s: %d parameters" % (os.path.relpath(OUT), len(params)))
