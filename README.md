# Flintstones/Jetsons Timewarp — CD-i static recompilation

A native Windows build of *Flintstones/Jetsons Timewarp* (Philips Interactive
Media / R/GA, 1993) for the Philips CD-i. The game's 68000 program is
translated ahead of time into C, and a runtime supplies the parts of the CD-i
the code expects: the CD-RTOS (OS-9) kernel, the CD file manager with
real-time play, the video decoder (Direct3D 11 presenter), the ADPCM audio
processor and the Balboa `cdi_bpsys` system extension that real players carry
in ROM.

It is built from the CD-i static recompilation template (from the *Link: The
Faces of Evil* project). The recompiled build is bit-identical, frame for
frame and sample for sample, to a pure-interpreter run of the same session.

Two executables are built from the same recompiled code:

* `timewarp.exe`, the **original edition**: the game as the CD-i showed it.
* `timewarp_hd.exe`, the **HD edition**: Cartoon HD / Cartoon HD+ upscaling,
  run-ahead, sprite interpolation and quicksave (see below).

## Installing

Download the latest release from the
[releases page](https://github.com/tools81/flintstones-jetsons-timewarp-static-recomp/releases).
You need your own copy of the CD-i disc as an image (the `.cue` and its
`.bin`, raw 2352-byte sectors); it is not included.

* **Installer** (`Timewarp-<version>-Setup.exe`): installs for the current
  user, without administrator rights, by default to
  `%USERPROFILE%\Games\Flintstones-Jetsons Timewarp`. One page asks for the
  folder that holds the disc image. Start menu shortcuts are made for both
  editions.
* **Portable** (`Timewarp-<version>-portable.zip`): unzip anywhere (a USB
  stick works) and run `timewarp_hd.exe` or `timewarp.exe`. Put the disc
  image in the `rom` folder, or pick it when the game asks at its first start.

Either way the game is portable: everything it writes stays in its own
folder, in `saves\` (saves, quicksave slots, display and HD settings, the
log) and `timewarp.ini`. Nothing goes to AppData or the registry, and the
graphics shaders are built into the executables. To move the game, move the
folder. Both executables are self-contained (the C runtime is linked in) and
need Windows 10 or later, 64-bit.

`timewarp.ini` (next to the executables):

| Setting | Meaning |
|---------|---------|
| `rom=` | The disc image: its folder, or the `.cue`/`.bin` file |
| `update_check=` | `1`: at start, offer a newer release when there is one (default); `auto`: install it without asking; `0`: never check |

**Updates.** At start the game asks GitHub for the latest release. If it is
newer, the game offers to install it, downloads the portable zip and quits.
A small script copies the new files over the program's folder, keeping
`saves\` and `timewarp.ini`, and starts the game again. `--no-update` skips
the check for one run.

## Building

Requirements: Windows 10+, Visual Studio 2022 (C compiler), CMake 3.19+ and
Python 3.8+. No third-party libraries are needed.

1. Put the disc image in `rom/` (`Flintstones-Jetsons Timewarp (USA).bin` and
   `.cue`, raw 2352-byte sectors). It is not included.
2. Run `build.bat`. It extracts the program modules from the disc, recompiles
   them into `gen/`, builds with MSVC and copies `timewarp.exe` and
   `timewarp_hd.exe` to the project root.
3. To make a release, set the version in `VERSION` and run
   `powershell -ExecutionPolicy Bypass -File tools\package.ps1`. It builds,
   then writes `dist\Timewarp-<version>-portable.zip`,
   `dist\Timewarp-<version>-Setup.exe` (needs Inno Setup 6:
   `winget install JRSoftware.InnoSetup`; script `installer	imewarp.iss`) and
   `dist\SHA256SUMS.txt`. Publish the release with the tag `v<version>` and
   these files attached. The update check looks for the asset ending in
   `-portable.zip` in the latest release of `update_repo` (`game.json`).

## Playing

Run `timewarp.exe` or `timewarp_hd.exe`. Each finds the disc image in a `rom`
folder next to it (or up to four levels above), or takes the path to the
`.cue`/`.bin` as an argument.

| Input | Action |
|-------|--------|
| Mouse | Move the pointer; left button = button 1, right button = button 2 |
| Arrow keys (Shift = faster) | Move the pointer |
| Z / Space / A | Button 1 |
| X / Ctrl / S | Button 2 |
| Enter or C | Both buttons |
| Gamepad | Left stick / d-pad moves, A = button 1, B = button 2 |
| Tab (hold) | Run without frame pacing |
| F9 / F10 | Cycle aspect mode / filter |
| F11 or Alt+Enter | Fullscreen |
| Esc | Quit |

The game is played entirely with the pointer: pick George or Fred, then
point at things in each room and press a button.

## HD edition

| Key | Action |
|-----|--------|
| F2 | Quicksave to the current slot |
| F3 | Next slot (1 to 10; shows the slot's date, or empty) |
| F4 | Load the current slot |
| F5 | Run-ahead: 0, 1, 2 or 3 fields (default 2) |
| F6 | Sprite interpolation on / off (default on) |
| F8 | Upscaler: Cartoon HD+ (default), Cartoon HD, off (original pixels) |

Messages appear at the bottom of the picture. The choices are kept in
`saves/hd_settings.ini`; `--runahead N`, `--interpolation on|off` and
`--upscale hdplus|hd|off` override them for one run.

* **Cartoon HD** enlarges the picture 4x (to 1536x960) with an edge-directed
  2x step applied twice, in the manner of Hyllian's xBR. Flat fills stay flat
  and the staircase outlines of the 384x240 art become smooth curves.
  **Cartoon HD+** adds inking (outlines are darkened a little against their
  surroundings, so they stay bold) and an adaptive sharpen. Only the parts of
  the picture that changed are upscaled again; the result is identical to
  upscaling the whole picture.
* **Run-ahead** hides the game's reaction time. After every field the whole
  machine is snapshotted, the next fields are run with the same input, the
  last of them is shown, and the snapshot is restored. Only the real fields
  are heard, so sound trails the picture by the run-ahead amount. With
  run-ahead on, the sound and the game state are identical to a run without
  it.
* **Sprite interpolation** shows motion at 60 pictures a second. The game
  draws a new picture every 4 to 6 fields. While a picture is shown, the
  emulation looks ahead (from a snapshot) for the next one, a few fields per
  frame. Parts that moved as a whole, such as a sprite sliding or an object
  travelling along a path, are moved along their path field by field.
  Nothing is blended, so the art stays crisp. Parts that were redrawn rather
  than moved (most of this game's hand-drawn animation, zooms) cannot be
  interpolated without ghosting or tearing. They change when they did
  originally. The cursor is drawn on top at its latest position.
* **Quicksave** slots are `saves/quick_01.state` to `quick_10.state` and work
  across sessions. Saving and loading are not available while the Philips
  logo plays (it is a separate program), and a slot only loads into the game
  program it was saved from.

The HD edition needs a multi-core processor; the log ends with the average
and longest work per frame.

## What this game needed beyond the template

* **Disc extraction.** Every file on this disc has file number 1, so
  real-time streams are recognised by their sector submode instead.
* **Separate data module.** `cdi_1anim` carries no initialized data; its
  startup code loads `cdi_1anim_data` and relocates it. That data holds a
  table of 188 `jmp abs.l` stubs called with `jsr N(a6)` (659 call sites).
  The analyzer now reads the data module to resolve those calls
  (`data_module` in `game.json`), which took code coverage from 47% to 90%.
  The generated code calls the target directly while the stub in RAM still
  points to it.
* **Linker symbols.** `cdi_1anim.stb` names all 704 code symbols. They are
  used for function names in listings and backtraces, and as entry points
  (`symbols` in `game.json`).
* **Kernel and CD file manager.** F$Event (all twelve functions), F$Sleep in
  1/256 s, word-aligned modules in one file (`cdi_bumper` + `cdi_bumpdata`),
  SS_Abort ending the play with E$Abort and its signal (without it, skipping
  an animation hangs the game), SS_Pause / SS_Cont, sound map Cncl/Close
  numbering, mono mixing without clipping.
* **cdi_bpsys** (`runtime/bpsys.c`). Balboa programs link this ROM extension
  into the video driver. It moves the cursor, hit-tests the hotspot tree and
  posts hotspot events into the game by calling back into its dispatcher.
  Without it the game plays its intro and then cannot be controlled. Details
  are in `docs/CDI_NOTES.md`.
* **Mouse.** Absolute positioning mapped onto the picture in every aspect
  mode; `--script` gained `Px/y` to place the pointer.
* **Field-by-field frame loop** (for the HD edition). Blocking system calls
  (F$Sleep, Ev$Wait) no longer wait inside the runtime: they return to the
  run loop and execute their trap again, so the emulation stops cleanly
  after every field and the whole machine can be snapshotted there
  (`runtime/state.c`). The original edition's output is unchanged by this.

## game.json

```json
{
  "title": "Flintstones/Jetsons Timewarp",
  "exe_name": "timewarp",
  "modules": [
    {"file": "auto", "base": "0x100000", "extra_entries": [], "a5_table": "auto"},
    {"file": "cdi_bumper", "base": "0xE0000", "extra_entries": [], "a5_table": "auto"}
  ]
}
```

* The first module is the boot program; `"auto"` reads its name
  (`cdi_1anim`) from the disc label.
* `cdi_bumper` is the Philips logo "bumper" the game forks at start-up. It is
  recompiled for the address the runtime loads it at.
* `data_module` and `symbols` default to `"auto"`: `extracted/<name>_data`
  and `extracted/<name>.stb` are used when they exist; `null` disables them.
* Run `python tools/analyze.py [module index]` to write a disassembly with
  function names to `build/listing_<module>.asm`.

## Layout

| Path | Contents |
|------|----------|
| `tools/cdi_extract.py` | Green Book filesystem reader. Writes program files and `_app.txt` (`--all` also dumps the real-time streams). |
| `tools/m68k.py` | MC68000 decoder |
| `tools/os9mod.py` | OS-9 module header, idata and irefs |
| `tools/analyze.py` | Code discovery (recursive descent, switch tables, A5 and a6 jump tables, code pointers, linker symbols) |
| `tools/gen.py` | 68000→C generator, flag liveness, dispatch tables, `gen/game_config.h` |
| `tools/config.py` | `game.json` loader |
| `tools/gen_tests.py` | Per-instruction differential tests |
| `tools/bmp2png.py` | Converts frame dumps to PNG |
| `runtime/cpu.c` | Dispatcher, run loop and synchronous calls into 68k code |
| `runtime/frontend.c` | Frame loop, presentation; HD edition run-ahead, interpolation lookahead, quicksave keys, messages |
| `runtime/state.c` | Machine snapshots (every module provides its part), quicksave slot files |
| `runtime/cartoon.c` | Cartoon HD / HD+ upscalers, worker threads |
| `runtime/motion.c` | Sprite interpolation (block motion search, in-between pictures) |
| `runtime/edition_orig.c`, `edition_hd.c` | Which edition an executable is |
| `runtime/update.c` | Update check and installer for new releases |
| `runtime/find_image.inc` | Finding the disc image, `timewarp.ini`, the disc picker |
| `installer/timewarp.iss` | Inno Setup script (the disc page, `timewarp.ini`) |
| `tools/package.ps1` | Builds the release: portable zip, installer, checksums |
| `VERSION` | The version (compiled into the executables and their file properties) |
| `runtime/interp.c` | 68000 interpreter, for RAM code and the `--interp` reference mode |
| `runtime/alu.c` | Shifts, division and BCD with exact flags |
| `runtime/os9.c` | OS-9 kernel: memory, processes (fork/wait/exit), signals, alarms, events, paths, NVRAM, CSD |
| `runtime/bpsys.c` | Balboa `cdi_bpsys` extension: pointer, cursor, hotspot events, idle functions |
| `runtime/cdfm.c` | CD file manager: reads, and real-time SS_Play with PCB/PCL/CIL |
| `runtime/video.c` | UCM display control (FCT/LCT), MCD212-style renderer, cursor, pointer |
| `runtime/audio.c` | ADPCM levels A/B/C, sound maps with gapless SM_Out, attenuation |
| `runtime/render_d3d11.c` | GPU presenter: aspect modes and sharp/smooth/CRT filters |
| `runtime/host_win32.c` | Window, input (keyboard and XInput), waveOut, pacing, scripted and fuzz input |
| `test/` | Instruction differential tester and ALU reference vectors |
| `docs/CDI_NOTES.md` | CD-i facts and API codes worked out during the Link and Timewarp ports |

## Debugging workflow

* **Find what fails.** Unimplemented OS calls and SetStat/GetStat codes are
  logged to `saves/recomp.log`. Fatal errors also show a 68k register dump and a
  stack backtrace with function names.
* **Debug options:**
  * `--trace` / `--trace3` log syscalls with callers, signals and disc activity.
  * `--headless --frames N --dump K` runs fast without a window and writes every
    Kth frame as a BMP.
  * `--script "field:key:dur,..."` scripts input (keys `1 2 L R U D`, and
    `Px/y` to put the pointer at x,y in 768x480 screen units). For example
    `--script "7000:P243/240:2,7020:1:5"` picks George.
  * `--fuzz SEED` drives the game with random input. Random clicks soon find
    the game's quit option, so fuzz runs often end early with `F$Exit(0)`.
  * `--interp --calls 101870,10096a` logs every call to those functions
    (hex addresses from the listing) with d0/d1 and the caller.
  * A headless run that reaches `--frames` writes `saves/ramdump.bin`.
  * `--dump-start N` starts the frame dumps at field N.
  * HD edition: `--script` keys `S`, `N` and `O` press F2, F3 and F4.
    `TW_UPSCALE_FULL=1` upscales whole pictures (to compare with the
    incremental upscaler), `TW_INTERP_LOG=1` logs each interpolated motion,
    and `TW_BENCH_UPSCALE=frame.bmp` times the upscalers on a frame dump.
* **Check run-ahead and snapshots.** A headless HD run with `--runahead 2`
  must give the same `--wav` and final `ramdump.bin` as one with
  `--runahead 0 --interpolation off`; anything a snapshot misses shows up
  as a difference.
  * `--wav out.wav` records the audio.
* **Check the translation.** Run the same headless session twice, once with
  `--interp`, which executes everything in the interpreter, and compare the
  frame dumps and WAVs. Headless runs are deterministic: emulated time is
  counted in instructions, and the clock uses a fixed epoch. Any difference
  points to a translation bug.
* **Unit-level checks.** `python tools/gen_tests.py` and
  `python test/gen_alu_vectors.py` generate test sources. Then build the
  `insn_test` and `alu_test` targets:

  ```
  cmake --build build\cmake --config Release --target insn_test alu_test
  build\cmake\Release\insn_test.exe rom\<image>.bin 200
  build\cmake\Release\alu_test.exe
  ```

* Code that jumps into RAM is not an error. It runs in the interpreter. Jumps to
  *undiscovered* addresses inside a recompiled module are fatal. Add those
  addresses to `extra_entries`, or teach `analyze.py` the pattern.

## Known limitations

* NTSC only (the disc is the US release).
* The `cdi_bpsys` extension is reimplemented from the program side of the
  Balboa library; events other than button presses, enter and leave
  (masks 0x0010 / 0x0020) are not generated.
* Full Motion Video is not supported (the game does not use it; it only
  checks for an `mv` module and carries on without one).
