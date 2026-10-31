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
| KIT | Kit picker + LOAD, status line, master level, limiter, scene A/B, crossfader |
| PAD | The selected pad: gain, pitch, pan, mode, slice, reverse, choke, start/end, stretch, character, SWAP, group, velocity |
| SLICE | The selected pad's slicer: sensitivity, count (4 / 8 / 16), AUTO SLICE, EQUAL, RESET, UNSLICE, SPREAD (each slice onto its own pad), and 16 keys to pick the slice the pad plays |
| SEQUENCER | Run, length, step size, fill, MUTATE + amount, snapshot recall, break bank |
| FX | FX group A–D and its 16 performance effects |
| MACROS | The 8 macros (Destroy, Amen Mangle, Rewind, Darken, Tape Stop, Pitch Dive, Jungle Roll, Sci-Fi), amount, group, latch |
| MIXER | Group A–D levels and mutes |

- **Pads**: MIDI notes 36–51 play pads 01–16 (Twisted Breaks' Drum Rack layout); notes 60–75 play the FX
  group's effects. Which notes the MPC's own pads send on a plugin track still needs checking on a device.
- **Timing** follows the MPC's transport: the sequencer, synced loops, SWAP hand-offs and quantised FX lock to
  the MPC's tempo and song position.
- **Your own kits**: save a kit on the Mac (Twisted Breaks' preset browser, `.tbkit`, samples included) and copy
  it to `/sdcard/Synths/Twisted Genetics - VST - Twisted Breaks/data/Presets/Kits/` on the MPC. It shows in the
  KIT list as soon as you turn the KIT selector. The installer keeps the `data` folder when you update the plugin.
- **Projects**: everything is saved in the MPC project (pads, slices, patterns, snapshots, FX).

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
  `tests/tbm_test.c` (kits, pads, the pattern from the song position, state save/restore, several instances).
- **MPC plugin**: cross-compiled for armv7 (glibc 2.27, GCC 14), checked (exports, glibc, libraries), skin built,
  packaged as the installer zip. Builds of `main` are published under Releases.

Locally (Linux): `git submodule update --init`, then
`cmake -B build -G Ninja && cmake --build build && build/tbm_test && build/toolkit_host_test`.

## Known limits (first version)
- Untested on hardware: pad note mapping, CPU load and the skin need a device check.
- Breaks load from kits only (no per-pad sample browser yet).
- Sample paths in a saved project point into the plugin's `data` folder, so a project moved to another MPC
  needs that MPC to have the same kits installed.
