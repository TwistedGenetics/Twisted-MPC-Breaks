# TWISTED BREAKS — MPC EDITION

[Twisted Breaks](https://github.com/TwistedGenetics/Twisted-Breaks), the jungle / drum & bass break sampler,
sequencer and performance-FX instrument, running **inside the MPC's own software** as a native plugin on
Gen1 standalone MPCs (One, Live, Live II, X, Key 61) and the Force.

It is the real Twisted Breaks engine, the same code as the Mac plugin (pinned as a submodule), not a
rewrite: the kits, slicing, SWAP groups, the 32-step sequencer, MUTATE, snapshots, the 16 performance
effects, macros, AKAIZER and the vintage character modes all come along. MPC draws its page from a skin and
the Q-Links drive it.

> **Status: untested on hardware.** It builds, and passes offline tests that drive it through the same calls
> MPC makes. The first device test is next.

## What you need
- A **Gen1** MPC or Force with **SSH enabled** (an SSH-modded MPC OS image), on **MPC OS 3.x**.
  Gen2 devices (Live III and later) are locked down and are not supported.
- The community MPC plugin installer from
  [sd88me/mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (Mac / Windows / Linux), or SSH.

## Install
1. Download `TwistedBreaks-…-mpc-armv7.zip` from **Releases**.
2. Drop the zip into the MPC plugin installer and install, or unzip it and follow its `INSTALL.md`.
3. On the MPC: add **Twisted Breaks** to a plugin track. It starts with the factory jungle kit loaded
   (a break on pad 01, hits on 02–07 and a starter pattern that plays when you press PLAY).

## On the MPC

| Page | What's on it |
|---|---|
| KIT | Kit knob (one click = one kit, wraps round), ◀ PREV / NEXT ▶ (load straight away), LOAD KIT, SAVE (over the loaded kit when it is one of yours; a factory kit asks for a name), SAVE AS (a keyboard to name it), what's playing; status; **what shapes the sound** (pads following the MPC tempo, FX, macros, character, limiter); master level, limiter, scene A/B, crossfader |
| PAD | The selected pad: gain, pitch, pan, start/end, velocity, mode, stretch, **tempo sync**, character, slice, reverse, choke, SWAP, group, and a line saying how it plays (e.g. "LOOP · FOLLOWS MPC TEMPO: 170 -> 120 BPM, REPITCH") |
| SAMPLES | A touch list: PLACES first (MPC Samples, MPC Documents, Expansions, the internal drive as MPC shows it, SD and USB drives, RECENT folders, the plugin's folders). Tap a folder to open it, tap a file to hear it (again to replay), PAGE ▲ ▼, BACK; LOAD puts it on the selected pad (sliced if SLICE ON LOAD is lit) |
| SLICE | Chopping on the green CRT: ZOOM (all, slice, 400 / 100 / 25 ms around the marker: at 25 ms one column is 0.4 ms), NUDGE ◀◀ ◀ ▶ ▶▶ (one or eight columns), TIGHTEN (the marker onto the start of its hit, at a zero crossing) and TIGHTEN ALL, MOVE knob (spans what the screen shows), SNAP TAPS TO off / zero / hit, FINGER ON THE WAVE pick / move / add / delete, ADD, DELETE, PLAY |
| AUTO SLICE | Sensitivity, count, AUTO SLICE, EQUAL, RESET, UNSLICE, SPREAD onto pads |
| STEPS | The selected pad's 32 steps as lamp keys, with a running playhead; length, step size, run, CLEAR |
| SEQUENCER | Run, length, step size, fill, MUTATE + amount, snapshot recall, break bank |
| FX | FX group A–D and its 16 performance effects; a red lamp over each lights while it is on (from a key, a pad held, or latched) |
| FX EDIT | Pick an effect: its settings (e.g. LPF cutoff / res / drive) on three knobs, KEY MODE (HOLD: on while the pad is held; LATCH: a hit turns it on, the next off), EFFECT ON, the group |
| MACROS | The 8 macros (Destroy, Amen Mangle, Rewind, Darken, Tape Stop, Pitch Dive, Jungle Roll, Sci-Fi), amount, group, latch |
| MIXER | Group A–D levels and mutes |
| SETUP | FOLLOW HITS (a hit selects its pad); hitting a playing SWAP pad again: IGNORE (default) or STOP; which MIDI notes play pads 1–16 (36–51, 37–52, 48–63 or custom), LEARN a note for one pad, and a MIDI IN line showing the note you just hit and the pad it played |

(The MPC shows five page tabs at a time; the `>` tab pages to the rest.)

### First time on the MPC
1. Open the plugin and hit an MPC pad. Look at **SETUP**: the MIDI IN line says which note arrived and which
   pad played. If it's the wrong pad, press another note layout (try **37-52 MPC**), or pick a pad, press
   **LEARN PAD NOTE** and hit the MPC pad you want for it.
2. Your own breaks: on **SAMPLES**, open a drive (the MPC's internal drive, an SD card, a USB stick) and go to
   the folder with your WAV / AIFF / FLAC files, just like the MPC's own browser. LOAD puts one on the selected pad.
3. Program it on **STEPS**, shape it on **SLICE** and **PAD**, then **SAVE KIT** on the KIT page. Kits saved on
   the MPC appear as "MPC Kit 001", "MPC Kit 002"… and keep their samples with them.

### More
- **The pad you hit is the pad you edit**: hitting a pad selects it, so PAD, SLICE, STEPS and SAMPLES all switch to
  it (the sequencer playing doesn't move the selection). Turn it off with FOLLOW HITS on the SETUP page.
- **SWAP groups**: with the MPC playing, hitting the pad that is playing again does nothing (IGNORE); a hit while
  a hand-off to another pad is waiting cancels the hand-off. With the MPC stopped, loops play only while held.
- **Tight chops**: AUTO SLICE, then on SLICE pick a slice, ZOOM 25 ms, NUDGE until it sits right (each nudge plays the
  slice), or TIGHTEN to put it on the start of its hit. TIGHTEN ALL does every marker. The MPC's screen sends a drag
  as small nudges, not a position, so a marker is moved by nudges, the MOVE knob, or a tap in MOVE mode.
- **Which control am I on?** The control you touch gets an amber frame; the data wheel then turns that one.
- **Pads**: notes 36–51 play pads 01–16 by default (change it on SETUP); notes 60–75 play the FX group's effects.
- **Why does pad 01 sound slowed down / pitched?** The factory kit's break is a 170 BPM loop with TEMPO SYNC on,
  so it is repitched to the MPC's tempo. Set the MPC to 170, or switch TEMPO SYNC off on the PAD page. The KIT
  page's "what shapes the sound" lines list everything that changes the sound.
- **Timing** follows the MPC's transport: the sequencer, synced loops, SWAP hand-offs and quantised FX lock to
  the MPC's tempo and song position.
- **Kits from the Mac**: save a kit in Twisted Breaks' preset browser (`.tbkit`, samples included) and copy it
  to `/sdcard/Synths/Twisted Genetics - VST - Twisted Breaks/data/Presets/Kits/` on the MPC. It shows in the KIT
  list as soon as you turn the KIT knob. The installer keeps the `data` folder when you update the plugin.
- **Projects**: everything is saved in the MPC project (pads, slices, steps, patterns, snapshots, FX, pad notes).

## How it works
- `src/MpcHost.cpp` hosts the Twisted Breaks processor behind the engine interface of the community toolkit
  (`external/mpc-vst-plugins`, `wrapper/engine.h`); the toolkit's VST2 wrapper is what MPC loads. The plugin
  runs its own JUCE message thread (MPC's own JUCE is a different, older copy inside `/usr/bin/MPC`).
- `CMakeLists.txt` compiles JUCE 8.0.4's modules directly, with FreeType linked in statically and no ALSA,
  fontconfig or X11 libraries, so the `.so` needs nothing but libc. It exports only `VSTPluginMain`.
- `tools/make_params.py` writes `vst/params.json`, the controls the MPC sees. **Append only**: MPC projects store
  parameters by number (`vst/params.lock` fails the build if one moves).
- `skin/make_skin.py` draws the skin in the Mac plugin's control-room look (enamel, rust, bakelite plates,
  amber lamp keys, VFD windows) and writes `vst/art/` and `vst/layout.conf` from the same coordinates. Edit the
  script, not the layout. Its own previews are in `skin/preview/`; CI renders the real skin with the toolkit's
  browser renderer and attaches those previews to each release.

## Build
GitHub Actions builds everything on every push (`.github/workflows/build.yml`):
- **Offline tests**: the plugin for x86 under ASan/UBSan, driven by the toolkit's host test and
  `tests/tbm_test.c` (kits, pads, slicing, the sample browser, steps, pad notes + LEARN, SAVE KIT, the pattern from the song position, state save/restore, several instances).
- **MPC plugin**: cross-compiled with the toolchain that matches MPC OS 3.10 (below), checked (exports, glibc no newer
  than 2.39, no dynamic libstdc++, libraries), skin built, packaged as the installer zip. Builds of `main` are
  published under Releases.

### The MPC G1 toolchain (from inMusic's open source release)
inMusic publishes the Linux side of MPC OS: [inmusic-software/az0x-open-source](https://github.com/inmusic-software/az0x-open-source),
branch `mpc-g1/3.10` for the G1 MPCs (One / One+, Live / Live II, X / X SE, Key 37 / 61, Force). `toolchain/` pins it:

| | MPC OS 3.10 (G1) |
|---|---|
| Kernel | linux-az01 6.18.44, PREEMPT_RT, RK3288 (MPC One = board ACVA; its audio: 2 ch, 44.1 kHz) |
| C library | glibc 2.39 (release/2.39/master `ce65d944` + Yocto patches), built against Linux 6.6 headers |
| Compiler | GCC 13.4.0 (+ Yocto patches); binutils 2.42 (Yocto scarthgap) |
| Also on the device | alsa-lib 1.2.11, FreeType 2.13.2, libusb 1.0.27, systemd 255, ... |

`toolchain/build.sh` builds `arm-az01-linux-gnueabihf` from those exact sources (each archive sha256-checked
against `toolchain/az01-g1-3.10.lock`): binutils, GCC stage 1, glibc into a sysroot, GCC with C++ (libstdc++ and
libgcc built `-fPIC`, so they link statically into a plugin), alsa-lib in the sysroot, then checks the result
(hard-float loader, VFP arguments, GLIBC versions, runs under qemu-arm). `.github/workflows/toolchain.yml` runs it
and publishes release [`toolchain-az01-g1-3.10`](../../releases/tag/toolchain-az01-g1-3.10); the plugin build uses that.
`cmake -DCMAKE_TOOLCHAIN_FILE=cmake/az01-g1.cmake -DTBM_AZ01=<extracted az01-g1-3.10>` builds with it locally.
(`cmake/armv7-mpc.cmake`, the earlier glibc 2.27 toolchain, still works for MPC OS 2.x.)

Locally (Linux): `git submodule update --init`, then
`cmake -B build -G Ninja && cmake --build build && build/tbm_test && build/toolkit_host_test`.

## Known limits (1.6)
- The MPC's own pad lights can't be set by a plugin (MPC drives them); see the FX page's lamps instead.
- Untested on hardware: CPU load, how smoothly the MPC redraws the slice screen and the STEPS playhead, and the
  skin's readability need a device check.
- A project remembers where each pad's sample lives (a drive, a folder): keep the card or stick in, or SAVE KIT
  (a kit carries its samples inside it).
