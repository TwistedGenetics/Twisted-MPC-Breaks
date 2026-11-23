#!/usr/bin/env python3
"""The user manual (PDF), printer-friendly: dark text on white.

    python3 docs/manual/prep_images.py <unzipped skin preview> <img folder>
    python3 docs/manual/make_manual.py <img folder> <out.pdf>

Needs reportlab and the DejaVu Sans / Poppins fonts. Version from CMakeLists.txt.
"""
import os
import re
import sys

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.pdfmetrics import registerFontFamily
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, Frame, Image, KeepTogether, NextPageTemplate, PageBreak, PageTemplate,
                                Paragraph, Spacer, Table, TableStyle)
from reportlab.platypus.tableofcontents import TableOfContents

IMG = sys.argv[1].rstrip("/") + "/"
OUT = sys.argv[2]
HERE = os.path.dirname(os.path.abspath(__file__))
VERSION = re.search(r"project\(TwistedMpcBreaks VERSION ([0-9.]+)",
                    open(os.path.join(HERE, "..", "..", "CMakeLists.txt")).read()).group(1)

F = "/usr/share/fonts/truetype/"
pdfmetrics.registerFont(TTFont("Body", F + "dejavu/DejaVuSans.ttf"))
pdfmetrics.registerFont(TTFont("Body-B", F + "dejavu/DejaVuSans-Bold.ttf"))
pdfmetrics.registerFont(TTFont("Head", F + "google-fonts/Poppins-Bold.ttf"))
pdfmetrics.registerFont(TTFont("Head-M", F + "google-fonts/Poppins-Medium.ttf"))
pdfmetrics.registerFont(TTFont("Mono", F + "dejavu/DejaVuSansMono.ttf"))
registerFontFamily("Body", normal="Body", bold="Body-B", italic="Body", boldItalic="Body-B")

INK = colors.HexColor("#1A1A1A")
AMBER = colors.HexColor("#C77700")
RUST = colors.HexColor("#8A3B12")
GREY = colors.HexColor("#F0F0F0")
LINE = colors.HexColor("#C8C8C8")
MID = colors.HexColor("#555555")

body = ParagraphStyle("body", fontName="Body", fontSize=10.5, leading=15, textColor=INK, spaceAfter=6)
small = ParagraphStyle("small", parent=body, fontSize=9, leading=12.5, textColor=MID)
cell = ParagraphStyle("cell", parent=body, fontSize=9.5, leading=13, spaceAfter=0)
cellb = ParagraphStyle("cellb", parent=cell, fontName="Body-B")
h1 = ParagraphStyle("h1", fontName="Head", fontSize=22, leading=27, textColor=INK, spaceBefore=4, spaceAfter=10)
h2 = ParagraphStyle("h2", fontName="Head", fontSize=14.5, leading=19, textColor=RUST, spaceBefore=12, spaceAfter=5)
h3 = ParagraphStyle("h3", fontName="Head-M", fontSize=11.5, leading=15, textColor=INK, spaceBefore=8, spaceAfter=3)
bullet = ParagraphStyle("bullet", parent=body, leftIndent=14, bulletIndent=3, spaceAfter=3)
num_st = ParagraphStyle("num", parent=body, leftIndent=18, bulletIndent=2, spaceAfter=4)
tip = ParagraphStyle("tip", parent=body, fontSize=10, leading=14, spaceAfter=0)
mono = ParagraphStyle("mono", fontName="Mono", fontSize=8.8, leading=12, textColor=INK)


class Doc(BaseDocTemplate):
    def afterFlowable(self, f):
        if isinstance(f, Paragraph) and f.style.name in ("h1", "h2"):
            lvl = 0 if f.style.name == "h1" else 1
            key = "k%d" % id(f)
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(f.getPlainText(), key, level=lvl, closed=lvl > 0)
            self.notify("TOCEntry", (lvl, f.getPlainText(), self.page, key))


def on_page(c, d):
    w, h = A4
    c.saveState()
    c.setStrokeColor(AMBER); c.setLineWidth(1.6)
    c.line(18 * mm, h - 14 * mm, w - 18 * mm, h - 14 * mm)
    c.setFont("Head-M", 8.5); c.setFillColor(MID)
    c.drawString(18 * mm, h - 12 * mm, "TWISTED BREAKS  ·  MPC EDITION")
    c.drawRightString(w - 18 * mm, h - 12 * mm, "User manual  ·  version " + VERSION)
    c.setStrokeColor(LINE); c.setLineWidth(0.6)
    c.line(18 * mm, 14 * mm, w - 18 * mm, 14 * mm)
    c.setFont("Body", 8.5)
    c.drawCentredString(w / 2, 9 * mm, str(d.page))
    c.restoreState()


def on_cover(c, d):
    w, h = A4
    c.saveState()
    c.setFillColor(AMBER); c.rect(0, h - 22 * mm, w, 22 * mm, stroke=0, fill=1)
    c.setFillColor(RUST); c.rect(0, h - 25 * mm, w, 3 * mm, stroke=0, fill=1)
    c.setFillColor(INK); c.setFont("Head-M", 10)
    c.drawString(20 * mm, h - 14 * mm, "TWISTED GENETICS")
    c.drawRightString(w - 20 * mm, h - 14 * mm, "MPC PLUGIN  ·  GEN1 AND GEN2 MPC / FORCE")
    c.setStrokeColor(LINE); c.line(20 * mm, 20 * mm, w - 20 * mm, 20 * mm)
    c.setFont("Body", 8.5); c.setFillColor(MID)
    c.drawString(20 * mm, 14 * mm, "Version %s  ·  github.com/TwistedGenetics/Twisted-MPC-Breaks" % VERSION)
    c.restoreState()


def P(t, s=body): return Paragraph(t, s)
def B(t): return Paragraph(t, bullet, bulletText="•")
def N(i, t): return Paragraph(t, num_st, bulletText="%d." % i)


def shot(name, caption=None, width=160 * mm):
    im = Image(IMG + name + ".jpg", width=width, height=width * 628 / 1280)
    im.hAlign = "CENTER"
    t = Table([[im]], colWidths=[width + 2])
    t.setStyle(TableStyle([("BOX", (0, 0), (-1, -1), 0.6, LINE), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                           ("RIGHTPADDING", (0, 0), (-1, -1), 0), ("TOPPADDING", (0, 0), (-1, -1), 0),
                           ("BOTTOMPADDING", (0, 0), (-1, -1), 0)]))
    out = [t]
    if caption:
        out.append(Spacer(1, 3)); out.append(P(caption, ParagraphStyle("cap", parent=small, alignment=TA_CENTER)))
    out.append(Spacer(1, 8))
    return out


def controls(rows, widths=(44 * mm, 130 * mm), head=("Control", "What it does")):
    data = [[P(head[0], cellb), P(head[1], cellb)]]
    for a, b in rows:
        data.append([P("<b>%s</b>" % a, cell), P(b, cell)])
    t = Table(data, colWidths=widths, repeatRows=1)
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#E4E4E4")),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#F7F7F7")]),
        ("LINEBELOW", (0, 0), (-1, -1), 0.4, LINE), ("BOX", (0, 0), (-1, -1), 0.6, LINE),
        ("VALIGN", (0, 0), (-1, -1), "TOP"), ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
        ("LEFTPADDING", (0, 0), (-1, -1), 6), ("RIGHTPADDING", (0, 0), (-1, -1), 6)]))
    return [t, Spacer(1, 8)]


def box(title, text):
    """A tip panel: light amber wash, dark text."""
    inner = [P("<b>%s</b>" % title, tip)] + ([P(x, tip) for x in text] if isinstance(text, list) else [P(text, tip)])
    t = Table([[inner]], colWidths=[174 * mm])
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), colors.HexColor("#FFF4E0")),
                           ("LINEBEFORE", (0, 0), (0, -1), 3, AMBER),
                           ("LEFTPADDING", (0, 0), (-1, -1), 10), ("RIGHTPADDING", (0, 0), (-1, -1), 10),
                           ("TOPPADDING", (0, 0), (-1, -1), 7), ("BOTTOMPADDING", (0, 0), (-1, -1), 7)]))
    return [Spacer(1, 2), t, Spacer(1, 8)]


def code(text):
    t = Table([[Paragraph(text.replace(" ", "&nbsp;").replace("\n", "<br/>"), mono)]], colWidths=[174 * mm])
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), GREY), ("BOX", (0, 0), (-1, -1), 0.6, LINE),
                           ("LEFTPADDING", (0, 0), (-1, -1), 8), ("TOPPADDING", (0, 0), (-1, -1), 6),
                           ("BOTTOMPADDING", (0, 0), (-1, -1), 6)]))
    return [t, Spacer(1, 8)]


S = []
# ---------------------------------------------------------------- cover
S += [Spacer(1, 38 * mm),
      P("TWISTED BREAKS", ParagraphStyle("c1", fontName="Head", fontSize=40, leading=44, textColor=INK)),
      P("MPC EDITION", ParagraphStyle("c2", fontName="Head", fontSize=26, leading=32, textColor=AMBER)),
      Spacer(1, 6),
      P("User manual  ·  version " + VERSION, ParagraphStyle("c3", fontName="Head-M", fontSize=14, leading=18, textColor=MID)),
      Spacer(1, 14)]
S += shot("kit", None, 174 * mm)
S += [P("The jungle / drum &amp; bass break sampler, step sequencer and performance-FX instrument, running inside "
        "the MPC's own software as a native plugin on standalone MPCs, Gen1 and Gen2, and the Force.", body),
      NextPageTemplate("main"), PageBreak()]

# ---------------------------------------------------------------- contents
toc = TableOfContents()
toc.levelStyles = [ParagraphStyle("t0", fontName="Head-M", fontSize=11, leading=16, leftIndent=0, textColor=INK),
                   ParagraphStyle("t1", fontName="Body", fontSize=9.5, leading=11.8, leftIndent=14, textColor=MID)]
S += [P("Contents", ParagraphStyle("ct", parent=h1)), toc]

# ---------------------------------------------------------------- 1 intro
S += [PageBreak(), P("1  Welcome", h1),
      P("Twisted Breaks MPC is the real Twisted Breaks engine – the same code as the Mac plugin, not a rewrite – "
        "built to run on the MPC. Kits, slicing, SWAP groups, the 32-step sequencer, MUTATE, snapshots, the 16 "
        "performance effects, macros, AKAIZER time-stretching, the vintage character modes and MANGLE (the "
        "bar-mangling sequencer from Twisted Breaks 0.30) all come along. The MPC draws the plugin's pages from a "
        "skin, and the Q-Links and data wheel drive it."),
      P("What you need", h2),
      B("Any standalone MPC: <b>Gen1</b> (One / One+, Live / Live II, X / X SE, Key 37 / 61, Force) or <b>Gen2</b> "
        "(Live III, XL, One G2, Key 37 G2). From 1.8 the plugin comes in both builds."),
      B("<b>MPC OS 3.10.1 or later</b> for the official way (below, no SSH). Older MPC OS 3.x needs SSH and the "
        "community installer."),
      P("Install on MPC OS 3.10.1 or later", h2),
      N(1, "On the MPC: <b>Menu &gt; Preferences &gt; Enhancements</b>. Turn on <b>Enable Enhancements</b>, then "
           "<b>External Plugin Support</b>, and read the warning. Wait a few seconds so the setting is saved."),
      N(2, "On your computer, download <b>Twisted-Breaks-%s-mpc-sdk.zip</b> from the Releases page of "
           "TwistedGenetics/Twisted-MPC-Breaks and unzip it. It holds one folder with both the Gen1 and the Gen2 "
           "binary; the MPC uses the right one." % VERSION),
      N(3, "Copy the folder <b>Twisted Genetics - VST - Twisted Breaks</b> into a folder called <b>VST2 Plugins</b> at "
           "the top of the MPC's internal drive, or of a USB stick or SD card. Make <b>VST2 Plugins</b> if it isn't "
           "there, and don't rename the plugin's folder."),
      N(4, "Restart the MPC. Add <b>Twisted Breaks</b> to a plugin track: it is in the plugin browser under "
           "<b>External Plugins</b>. It opens with the factory jungle kit: a break on pad 01, hits on pads 02–07, "
           "and a starter pattern that plays when you press PLAY."),
      P("Install on older MPC OS (SSH)", h2),
      P("Use the community installer from <b>open-mpc/mpc-vst-plugins</b> with <b>-mpc-armv7.zip</b> on a Gen1 MPC or "
        "<b>-mpc-aarch64.zip</b> on a Gen2. A Gen1 zip doesn't load on a Gen2 MPC (its MPC OS is 64-bit only), and "
        "the other way round."),
      P("Updating keeps your <b>data</b> folder (your kits, MANGLE presets and recent folders), so it is safe to "
        "copy a new build over an old one."),
      P("Status", h2),
      P("This edition builds and passes offline tests that drive it through the same calls the MPC makes. CPU load, "
        "how smoothly the MPC redraws the busier screens, and the skin's readability on the real screen still need "
        "checking on a device. See section 6 for the known limits."),
      ]

# ---------------------------------------------------------------- 2 getting around
S += [PageBreak(), P("2  Getting around", h1),
      P("Pages and tabs", h2),
      P("The plugin has fourteen pages. The MPC shows five page tabs at a time along the top of the plugin screen; "
        "the <b>&gt;</b> tab pages on to the next five."),
      ]
pages = [("KIT", "Load, save and name kits; master level, limiter, scenes, crossfader; what shapes the sound."),
         ("PAD", "Everything about the selected pad: gain, pitch, mode, stretch, character, SWAP, group."),
         ("SAMPLES", "A browser for the MPC's drives: load your own breaks onto pads."),
         ("SLICE", "The green CRT waveform: zoom, nudge and tighten slice markers."),
         ("AUTO SLICE", "Slice by transients or equally, reset, unslice, spread slices onto pads."),
         ("STEPS", "The selected pad's 32 steps, with a running playhead."),
         ("SEQUENCER", "Run, length, step size, fill, MUTATE, snapshots, break banks."),
         ("FX", "The 16 performance effects of an FX group, with lamps showing what is on."),
         ("FX EDIT", "One effect's settings and its key mode (HOLD / LATCH)."),
         ("MANGLE", "The bar as 16 step cells: paint moves, join runs, randomize, mutate."),
         ("MANGLE FX", "TEXTURE grains, SPACE echoes, output, fills, presets, pads-as-steps."),
         ("MACROS", "The 8 one-touch macros, amount, group and latch."),
         ("MIXER", "Group A–D levels and mutes."),
         ("SETUP", "Pad notes, LEARN, MIDI monitor, follow hits, SWAP re-hit behaviour.")]
S += controls(pages, widths=(32 * mm, 142 * mm), head=("Page", "What it's for"))
S += [P("Touching and turning", h2),
      B("<b>Touch a control</b> and it gets an <b>amber frame</b>. The data wheel then turns that control."),
      B("<b>Q-Links</b> follow the page you are on (the table below). On a 4-knob MPC like the One, the <b>Q-Link "
        "button</b> steps through columns of four, and MPC outlines the column the knobs are driving. One-press "
        "buttons (LOAD, HOME, AUTO SLICE…) are kept off the Q-Links so a knob turned by accident can't fire them."),
      B("<b>Keys</b> (the rows of lamp buttons) light their amber lamp when that setting is chosen."),
      B("<b>Readouts</b> (the dark windows) show names, values and status lines. The KIT page's status line says what "
        "the plugin just did (loaded, saved, failed and why)."),
      B("<b>On a Live III or XL</b> one tap on the screen can reach a plugin twice. Twisted Breaks counts it once: "
        "buttons, the naming keyboard, the MANGLE steps, the wave and the sample list. The same key tapped again by "
        "hand still counts."),
      P("The pad you hit is the pad you edit", h2),
      P("Hitting a pad selects it, so PAD, SLICE, STEPS and SAMPLES all switch to that pad. The sequencer playing "
        "a pad doesn't move the selection. Switch this off with <b>FOLLOW HITS</b> on the SETUP page."),
      P("First time on the MPC", h2),
      N(1, "Open the plugin and hit an MPC pad. Look at <b>SETUP</b>: the MIDI IN line says which note arrived and "
           "which pad played. If it's the wrong pad, choose another note layout (try <b>37-52 MPC</b>), or pick a pad, "
           "press <b>LEARN PAD NOTE</b> and hit the MPC pad you want for it."),
      N(2, "Press PLAY on the MPC: the factory pattern runs, locked to the MPC's tempo and song position."),
      N(3, "Load your own break on <b>SAMPLES</b>, chop it on <b>SLICE</b> and <b>AUTO SLICE</b>, program it on "
           "<b>STEPS</b>, then <b>SAVE AS</b> on the KIT page to keep it as a kit."),
      P("Q-Links on each page", h3)]
S += controls([
    ("KIT", "<b>Level, Crossfader, Kit, Scene</b>  ·  Limiter, Curve"),
    ("PAD", "<b>Pad, Gain, Pitch, Pan</b>  ·  Start, End, Velocity, Slice  ·  Mode, Stretch, Tempo Sync, Character  ·  SWAP, SWAP At, Group, Choke"),
    ("SAMPLES", "<b>Pad, Sample (moves the cursor), Slice On Load</b>"),
    ("SLICE", "<b>Pad, Slice, Move Marker, Zoom</b>  ·  Snap, Finger mode"),
    ("AUTO SLICE", "<b>Pad, Sensitivity, Count</b>"),
    ("STEPS", "<b>Pad, Length, Step, Run</b>"),
    ("SEQUENCER", "<b>Mutate Amount, Length, Step, Snapshot</b>  ·  Break Bank, Run, Fill"),
    ("FX", "The 16 effects on / off, four columns of four"),
    ("FX EDIT", "<b>Effect, Setting 1, 2, 3</b>  ·  Key Mode, Effect On, Group"),
    ("MANGLE", "<b>Mix, Amount, Fill Every, Steps</b>  ·  Move, Style, Slot, On  ·  Hold, Join, Chain"),
    ("MANGLE FX", "<b>Texture Amount, Size, Space Time, Space Amount</b>  ·  Density, Scatter, Feedback, Mix  ·  Texture mode, Space mode, Target, Amount  ·  Preset, Fill Every, Fill Length, Pads Paint Steps"),
    ("MACROS", "<b>Amount, Group, Latch</b>  ·  Destroy, Amen Mangle, Rewind, Darken  ·  Tape Stop, Pitch Dive, Jungle Roll, Sci-Fi"),
    ("MIXER", "<b>Levels A–D</b>  ·  Mutes A–D"),
    ("SETUP", "<b>Pad, Note layout, Follow Hits, SWAP re-hit</b>"),
], widths=(30 * mm, 144 * mm), head=("Page", "Column 1 (bold)  ·  then each press of the Q-Link button"))
S += box("Why does pad 01 sound slowed down or pitched?",
         "The factory kit's break is a 170 BPM loop with TEMPO SYNC on, so it is repitched to the MPC's tempo. Set the "
         "MPC to 170 BPM, or switch TEMPO SYNC off on the PAD page. The KIT page's <b>what shapes the sound</b> lines "
         "list everything that is changing the sound right now.")

# ---------------------------------------------------------------- 3 pages
S += [PageBreak(), P("3  The pages", h1),
      P("Each page below has a picture of the skin as the MPC draws it, then its controls. Names are as they "
        "appear on the screen.")]

S += [P("KIT", h2)] + shot("kit") + controls([
    ("KIT knob", "Picks a kit. One click is one kit and it wraps round from the last to the first. The window shows the name."),
    ("◀ PREV / NEXT ▶", "Load the previous / next kit straight away."),
    ("LOAD KIT", "Loads the kit the knob points at."),
    ("SAVE", "Saves over the loaded kit when it is one of yours (\"SAVED OVER …\"). A factory kit can't be overwritten, so SAVE opens the keyboard to name a new one."),
    ("SAVE AS", "Opens a keyboard to name the kit, then saves it as a new kit. SPACE, ⌫, CLEAR, CANCEL and OK are on the keyboard."),
    ("Status", "What just happened: loaded, saved, a failure and its reason."),
    ("What shapes the sound", "Two lines listing everything that changes the sound: pads following the MPC tempo, FX that are on, macros, character modes, reversed pads, MANGLE, the limiter."),
    ("LEVEL, LIMITER", "Master level (−60 to +6 dB) and the master limiter."),
    ("PADS A / B, CROSSFADER, CURVE", "Scene A/B and the crossfader between them, smooth or sharp."),
])
S += [PageBreak(), P("PAD", h2)] + shot("pad") + [
    P("Everything on this page applies to the <b>selected pad</b> (the PAD knob, or the last pad you hit). A line at "
      "the bottom says how the pad plays, e.g. <i>LOOP · FOLLOWS MPC TEMPO: 170 -&gt; 120 BPM, REPITCH</i>.")] + controls([
    ("PAD", "Which pad you are editing (1–32)."),
    ("GAIN, PITCH, PAN", "Level (−48 to +12 dB), pitch (±12 semitones), pan."),
    ("START, END", "Where in the sample the pad starts and stops."),
    ("VELOCITY", "How much hit strength changes the level."),
    ("MODE", "One-shot, Loop, Slice, Seq One, Seq Loop, Loop Cont."),
    ("STRETCH", "Off, Repitch, Beats, Texture, Akai Classic, Akai Revised (the AKAIZER modes)."),
    ("TEMPO SYNC", "Follow the MPC's tempo (with the STRETCH mode chosen)."),
    ("CHARACTER", "Clean, SP-1200, MPC60, S950, Lo-Fi."),
    ("SLICE, REVERSE, CHOKE", "Which slice the pad plays, reverse playback, choke group 1–4."),
    ("SWAP GROUP, SWAP AT", "SWAP group 1–4 and when a hand-off happens: at the slice, 1/16, beat or bar."),
    ("GROUP", "Mixer / FX group A–D."),
])
S += [PageBreak(), P("SAMPLES", h2)] + shot("samples") + [
    P("A touch list that works like the MPC's own browser. It starts at <b>PLACES</b>: the MPC's Samples and "
      "Documents folders, Expansions, the internal drive as the MPC shows it, SD cards and USB sticks, RECENT "
      "folders (where you last loaded from) and the plugin's own folders.")] + controls([
    ("Tap a folder", "Opens it."),
    ("Tap a file", "Plays it (tap again to replay). WAV, AIFF and FLAC are listed."),
    ("PAGE ▲ / ▼, HOME", "Scroll a page at a time; back to the top."),
    ("BACK", "Up one folder. On PLACES, BACK looks again for drives (a stick you just plugged in)."),
    ("LOAD", "Puts the highlighted file on the selected pad."),
    ("SLICE ON LOAD", "When lit, a loaded sample is auto-sliced straight away."),
    ("CLEAR PAD", "Empties the selected pad."),
])
S += box("Tip", "A project remembers where each pad's sample lives. Keep the card or stick in, or SAVE a kit: a kit carries its samples inside it.")

S += [PageBreak(), P("SLICE", h2)] + shot("slice") + [
    P("The selected pad's waveform on the green CRT, with its slice markers as tabs. The marker you are working on is "
      "highlighted.")] + controls([
    ("ZOOM", "ALL (the whole sample), SLICE (the selected slice), or 400 / 100 / 25 ms around the marker. At 25 ms one screen column is 0.4 ms."),
    ("NUDGE ◀◀ ◀ ▶ ▶▶", "Moves the marker one column or eight. Each nudge plays the slice so you hear the change."),
    ("TIGHTEN", "Moves the marker onto the start of its hit, at a zero crossing."),
    ("TIGHTEN ALL", "Does that for every marker."),
    ("MOVE knob", "Moves the marker across whatever the screen is showing."),
    ("SNAP TAPS TO", "Where a finger tap lands: Off, Zero (nearest zero crossing) or Hit (start of the nearest hit)."),
    ("FINGER ON THE WAVE", "What a tap on the wave does: PICK a marker, MOVE it there, ADD one, DELETE one."),
    ("ADD, DELETE, PLAY", "Add a marker, delete the selected one, play the selected slice."),
])
S += box("Getting tight chops", [
    "1. AUTO SLICE, then come back to SLICE and pick a slice.",
    "2. ZOOM to 25 ms and NUDGE until it sits right – every nudge plays the slice.",
    "3. Or press TIGHTEN to put it on the start of its hit; TIGHTEN ALL does every marker.",
    "The MPC's screen sends a drag as small nudges rather than a position, so markers are moved by nudges, "
    "the MOVE knob, or a tap in MOVE mode – not by dragging."])

S += [PageBreak(), P("AUTO SLICE", h2)] + shot("autoslice") + controls([
    ("SENSITIVITY", "How readily a transient becomes a slice."),
    ("COUNT", "4, 8 or 16 slices."),
    ("AUTO SLICE", "Slice at transients."),
    ("EQUAL", "Slice into equal lengths."),
    ("RESET", "Back to the automatic slices."),
    ("UNSLICE", "Remove the slices."),
    ("SPREAD ONTO PADS", "Puts each slice on its own pad."),
])
S += [P("STEPS", h2)] + shot("steps") + controls([
    ("Steps 1–32", "The selected pad's steps as lamp keys. Tap to switch a step on or off. A playhead runs along them."),
    ("LENGTH, STEP", "Pattern length (1–32) and step size (1/8, 1/16, 1/32)."),
    ("RUN", "Sequencer on / off."),
    ("CLEAR STEPS", "Clears this pad's steps."),
])
S += [PageBreak(), P("SEQUENCER", h2)] + shot("sequencer") + controls([
    ("RUN, LENGTH, STEP, FILL", "The sequencer for all pads; FILL plays the fill variation."),
    ("MUTATE + AMOUNT", "Re-writes the pattern by an amount (0–100 %)."),
    ("SNAPSHOT RECALL", "Recall snapshots 1–8 (Off: none)."),
    ("BREAK BANK", "Choose break bank 1–8."),
])
S += [P("FX", h2)] + shot("fx") + [
    P("The 16 performance effects of one FX group: Mute, LPF, HPF, BPF, Crush, Rate, Drive, Tape, Akaizer, Repeat, Gate, "
      "Pitch Drop, Delay, Reverb, Comb and Reverse. A <b>red lamp</b> over each effect lights while it is on – from "
      "a key, a pad held, or latched. MIDI notes 60–75 play the group's effects.")] + controls([
    ("FX GROUP A–D", "Which group's effects the page shows."),
    ("Effect keys", "Switch an effect on and off."),
])
S += [PageBreak(), P("FX EDIT", h2)] + shot("fxedit") + controls([
    ("Effect keys", "Pick the effect to edit."),
    ("Three knobs", "That effect's settings, named under each knob (e.g. LPF: cutoff, resonance, drive)."),
    ("KEY MODE", "HOLD: on while the pad or key is held. LATCH: one hit turns it on, the next turns it off."),
    ("EFFECT ON", "Switch the effect on from here."),
    ("GROUP", "Which FX group you are editing."),
])

S += [PageBreak(), P("MANGLE", h2)] + shot("mangle", "The bar with REPEAT on steps 1–2 (joined), REVERSE on 4, STOP on 7–8, step 5 playing, and an 8-step bar.") + [
    P("MANGLE re-plays what it has just heard through a pattern of <b>moves</b>, one per step, with no added delay. "
      "The 16 cells show the bar: each cell's colour and label is its move, <b>~</b> means the step carries on the "
      "run before it, a <b>white bar</b> marks the step playing, and <b>hatched</b> cells are past the bar's length.")] + controls([
    ("Moves", "THRU: as it is. REPEAT: the run's first slice, cut 2 / 4 / 8. REVERSE: the run before, backwards. HALF: half speed from the run start. STOP: a tape brake over the run. DOUBLE: the run before, twice as fast. SCRATCH: back and forth. FREEZE: holds the run's first moment. SHUFFLE: another step of the last bar in this one's place."),
    ("Tap a step", "Paints the chosen MOVE there. Tap again: back to THRU."),
    ("JOIN", "Then tap the first and last step: one run of the move across them. JOIN switches itself off after."),
    ("CLEAR", "Every step back to THRU."),
    ("RANDOMIZE", "Listens to the last two bars, tries 8 new patterns in the chosen STYLE over them, drops any that click or vanish and keeps the cleanest (it may also change TEXTURE / SPACE unless they are LOCKed). With the MPC stopped there is nothing to listen to, so it writes a pattern without the checks."),
    ("MUTATE", "The same, but changes only a few steps of the pattern you have."),
    ("UNDO", "Goes back to a pattern RANDOMIZE or MUTATE replaced (up to the last three)."),
    ("MANGLE ON", "Mangles every bar."),
    ("HOLD", "Mangles while it is on, from the next step – for playing it like a performance effect."),
    ("FILL NEXT", "Mangles one bar, from the next bar line."),
    ("STEPS", "The bar is 4, 8 or 16 steps."),
    ("RANDOMIZE STYLE", "Gentle, Medium, Wild."),
    ("SLOT A–D, CHAIN SLOTS", "Four variations of the bar; a new slot starts at the next bar. CHAIN plays A, B … up to the last one you filled, a bar each."),
    ("Info line", "On / off, slot, how many steps are mangled, and what RANDOMIZE did."),
])
S += [PageBreak(), P("MANGLE FX", h2)] + shot("manglefx") + controls([
    ("TEXTURE", "Grains over the moves while MANGLE is mangling: OFF, TIGHT, SCATTER, CLOUD, REVERSE, with AMOUNT, SIZE (10–250 ms), DENSITY and SCATTER. LOCK stops RANDOMIZE changing them."),
    ("SPACE", "Synced repeats while MANGLE is mangling, with a tail that rings on after: OFF, ECHO, PING-PONG, DIFFUSE, with TIME (1/32 to 1/2, dotted and triplet), FEEDBACK and AMOUNT. LOCK as above."),
    ("MANGLES", "What MANGLE works on: MASTER, or one group A–D."),
    ("MIX, AMOUNT", "Wet / dry, and AMOUNT: each step's chance of being mangled in a bar (100 %: always)."),
    ("FILL EVERY", "Mangles only the end of each phrase of 2, 4, 8 or 16 bars – the last BAR or HALF bar (OFF: no fills)."),
    ("PRESET, LOAD, SAVE", "The knob picks a preset: the 16 factory ones (shown as GROUP: NAME), then yours (USER: …). SAVE writes a new one, \"MPC MANGLE 001\", 002 …"),
    ("PADS PAINT STEPS", "Pads 01–16 become the bar's steps (below)."),
])
S += box("Painting with the pads", [
    "Switch on <b>PADS PAINT STEPS</b> and pads 01–16 are the bar's steps, as on the Maschine Mikro:",
    "• Hit a pad: paints the selected move on that step. Hit it again: THRU.",
    "• Hold one pad and hit another: joins the steps between them into one run.",
    "The pads don't make sound while this is on. Switch it off to play again."])
S += box("CPU", "TEXTURE (especially CLOUD at high DENSITY) and SPACE add work on top of the break. Watch the MPC's CPU meter before relying on them live.")

S += [PageBreak(), P("MACROS", h2)] + shot("macros") + controls([
    ("Macros", "Destroy, Amen Mangle, Rewind, Darken, Tape Stop, Pitch Dive, Jungle Roll, Sci-Fi: each moves several effects at once."),
    ("AMOUNT", "How far the macros go (0–100 %)."),
    ("GROUP", "Which group they act on, A–D or All."),
    ("LATCH", "Macros stay on after you let go."),
])
S += [P("MIXER", h2)] + shot("mixer") + controls([
    ("LEVEL A–D", "Group levels, −60 to +6 dB."),
    ("MUTE A–D", "Mute a group."),
])
S += [PageBreak(), P("SETUP", h2)] + shot("setup") + controls([
    ("WHICH NOTES PLAY PADS 1–16", "36–51 (C1, default), 37–52 (MPC), 48–63 (C2) or CUSTOM."),
    ("LEARN PAD NOTE", "Pick a pad, press LEARN, hit the MPC pad you want to play it."),
    ("MIDI IN", "The note you just hit and the pad it played."),
    ("FOLLOW HITS", "A hit selects its pad for editing (on by default)."),
    ("HIT A PLAYING SWAP PAD AGAIN", "IGNORE (default): a second hit on a playing SWAP pad does nothing. STOP: it stops the pad."),
])

# ---------------------------------------------------------------- 4 how to
S += [PageBreak(), P("4  How to…", h1),
      P("Load your own break", h2),
      N(1, "Select the pad (hit it, or turn PAD)."),
      N(2, "SAMPLES: open a place (an SD card, the internal drive, a USB stick) and go to your folder."),
      N(3, "Tap files to hear them; with the one you want highlighted, press LOAD. With SLICE ON LOAD lit it arrives sliced."),
      P("Save a kit", h2),
      B("<b>SAVE</b> saves over the kit you loaded, if it is one of yours."),
      B("<b>SAVE AS</b> asks for a name with an on-screen keyboard and saves a new kit. Kits keep their samples inside them."),
      B("A factory kit can't be overwritten: SAVE asks for a name instead."),
      P("SWAP groups", h2),
      P("Pads in the same SWAP group hand over to each other at the SWAP AT point. With the MPC playing, hitting the "
        "pad that is already playing does nothing (IGNORE on SETUP), so a stray double hit won't stop your loop; a hit "
        "while a hand-off is waiting cancels it. With the MPC stopped, loops play only while held."),
      P("Mangle a break", h2),
      N(1, "MANGLE: switch MANGLE ON and choose STEPS (8 is a good start)."),
      N(2, "Pick a move (REPEAT, say) and tap steps to paint it; JOIN for longer runs."),
      N(3, "Or press RANDOMIZE, choose a STYLE, and use MUTATE / UNDO to steer it."),
      N(4, "MANGLE FX: add TEXTURE or SPACE, set FILL EVERY for automatic fills, and SAVE a preset you like."),
      P("Timing", h2),
      P("Everything follows the MPC's transport: the sequencer, synced loops, SWAP hand-offs, quantised FX and MANGLE "
        "lock to the MPC's tempo and song position."),
      P("Projects", h2),
      P("Everything is saved in the MPC project: pads, slices, steps, patterns, snapshots, FX, MANGLE and pad notes."),
      ]

# ---------------------------------------------------------------- 5 files
S += [PageBreak(), P("5  Files on the MPC", h1),
      P("The plugin keeps its own files in a <b>data</b> folder inside its own folder, wherever that is:")]
S += code("VST2 Plugins/Twisted Genetics - VST - Twisted Breaks/data/      (MPC OS 3.10.1)\n"
          "/sdcard/Synths/Twisted Genetics - VST - Twisted Breaks/data/    (older installer, Gen1)")
S += controls([
    ("Presets/Kits/", "Kits (.tbkit). Kits saved on the MPC, and kits copied from the Mac."),
    ("Presets/Mangle/", "MANGLE presets (.tbmangle): ones you SAVE on the MPC and ones copied from the Mac."),
    ("browser-recent.txt", "The RECENT folders in the SAMPLES browser."),
], widths=(46 * mm, 128 * mm), head=("In data/", "What it holds"))
S += [P("From the Mac", h2),
      B("<b>Kits</b>: save a kit in Twisted Breaks' preset browser on the Mac (.tbkit, samples included) and copy it to "
        "<b>data/Presets/Kits/</b>. It appears in the KIT list as soon as you turn the KIT knob."),
      B("<b>MANGLE presets</b>: copy .tbmangle files to <b>data/Presets/Mangle/</b>. They appear after the factory presets on the PRESET knob."),
      P("Copying a new version of the plugin's folder over the old one keeps the data folder.")]

# ---------------------------------------------------------------- 6 limits / troubleshooting
S += [P("6  Troubleshooting and known limits", h1)]
S += controls([
    ("The MPC doesn't list the plugin", "MPC OS 3.10.1 or later? External Plugin Support on? The folder must sit directly in VST2 Plugins at the top of a drive, with its name unchanged. Restart after copying. On older MPC OS, use the zip for your MPC's generation (a Gen1 zip won't load on a Live III)."),
    ("The wrong pad plays", "SETUP: try another note layout, or LEARN PAD NOTE for each pad."),
    ("Pad 01 sounds slow or pitched", "TEMPO SYNC is following the MPC's tempo. Set the MPC to the loop's tempo, or switch TEMPO SYNC off on PAD."),
    ("Something colours the sound", "KIT: the \"what shapes the sound\" lines list every FX, macro, character mode and MANGLE that is on."),
    ("My USB stick doesn't show", "SAMPLES: go to PLACES and press BACK to look for drives again."),
    ("Pads don't make sound", "Check PADS PAINT STEPS (MANGLE FX) is off."),
    ("A sample is missing in a project", "The card or stick it came from isn't in. Keep it in, or save the pads as a kit."),
], widths=(52 * mm, 122 * mm), head=("Problem", "Try this"))
S += [P("Known limits", h2),
      B("The MPC's own pad lights are driven by the MPC; a plugin can't set them. The FX page's lamps show what is on instead."),
      B("Markers on the SLICE screen move by nudges, the MOVE knob, or taps – the MPC sends drags as small nudges."),
      B("The Gen2 build is new in 1.8 and hasn't been tried on a Live III or XL yet."),
      B("Not yet checked on hardware: CPU load (especially MANGLE TEXTURE / SPACE), how smoothly the MPC redraws the "
        "SLICE screen, the STEPS playhead and the MANGLE cells, and how readable the skin is on the device."),
      Spacer(1, 10),
      P("Version history", h2)]
S += controls([
    ("1.8", "Gen2 MPCs (Live III, XL, One G2, Key 37 G2); one folder for MPC OS 3.10.1's VST2 Plugins; a tap the Live III sends twice counts once."),
    ("1.7.1", "Q-Links reordered: continuous controls in the first column, one-press buttons off, the active column outlined."),
    ("1.7", "MANGLE from Twisted Breaks 0.30: MANGLE and MANGLE FX pages, presets, pads paint steps."),
    ("1.6", "Built with MPC OS 3.10's own toolchain (GCC 13.4, glibc 2.39) from inMusic's open source release."),
    ("1.5", "New sample browser (PLACES, drives, recent folders); tight chopping: zoom to 25 ms, nudges, TIGHTEN, snap."),
    ("1.4", "SAVE over the loaded kit / SAVE AS; FX EDIT page; FX lamps."),
    ("1.3", "SWAP re-hit IGNORE; finger slicing; naming kits; browser fixes."),
    ("1.2", "Pages follow the pad you hit; amber frame on the control you touch."),
    ("1.1", "SLICE screen; loading samples on the MPC; what shapes the sound; kit knob on the wheel."),
    ("1.0", "First MPC edition."),
], widths=(22 * mm, 152 * mm), head=("Version", "What changed"))


# ---------------------------------------------------------------- one screen per page in section 3
def regroup(story):
    out, i, in3 = [], 0, False
    while i < len(story):
        f = story[i]
        if isinstance(f, Paragraph) and f.style.name == "h2" and in3:
            grp = [f]; i += 1
            while i < len(story) and not (isinstance(story[i], Paragraph) and story[i].style.name in ("h1", "h2")):
                if not isinstance(story[i], PageBreak): grp.append(story[i])
                i += 1
            if out and not isinstance(out[-1], PageBreak) and not (isinstance(out[-1], Paragraph) and out[-1].style.name == "body"):
                out.append(PageBreak())
            out.append(KeepTogether(grp))
            continue
        if isinstance(f, Paragraph) and f.style.name == "h1":
            if out and not isinstance(out[-1], PageBreak) and f.getPlainText()[:1] in "456": out.append(PageBreak())
            in3 = f.getPlainText().startswith("3")
        out.append(f); i += 1
    return out


S = regroup(S)

doc = Doc(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=20 * mm, bottomMargin=20 * mm,
          title="Twisted Breaks MPC Edition – User Manual", author="Twisted Genetics", subject="Version " + VERSION)
fr = Frame(18 * mm, 18 * mm, A4[0] - 36 * mm, A4[1] - 38 * mm, id="f", leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)
doc.addPageTemplates([PageTemplate("cover", [fr], onPage=on_cover), PageTemplate("main", [fr], onPage=on_page)])
doc.multiBuild(S)
print(OUT)
