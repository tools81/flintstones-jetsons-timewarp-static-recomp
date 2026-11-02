# CD-i / CD-RTOS notes for static recompilation

These are facts worked out while porting *Link: The Faces of Evil* and then
*Flintstones/Jetsons Timewarp*. The Green Book (CD-i Full Functional
Specification, May 1994) describes the APIs but mostly omits numeric codes.
The codes below were inferred from the games' library wrappers and the
parameter lists in the spec. An entry marked *(seen)* was confirmed by actual
use in one of the games.

## Disc and boot

* Sectors are raw 2352 bytes, Mode 2: sync (12), header (4), subheader
  (file, channel, submode, coding, ×2), then data. Form 1 carries 2048 bytes
  and Form 2 carries 2324 (audio uses 2304).
* The disc label is at LSN 16 (`"CD-I "` at data offset 1). The application
  identifier at label offset 574 names the boot module, for example `cdi_link`.
* Real-time files (`.rtr`, `.rtf`) have file number ≥ 1. Their directory size
  counts only that file's sectors, and padding sectors use file number 0.
  The file number does **not** identify real-time files: Timewarp gives every
  file, programs included, file number 1. Real-time sectors carry the RT
  (0x40) or Form 2 (0x20) submode bit, so the extractor tests those.
* A file can hold several modules, word aligned (`cdi_bumper` is followed by
  `cdi_bumpdata` at offset 0x2D06).
  Empty sectors (submode with no V/A/D bit) can still carry EOR, trigger or EOF
  and **must be processed** for play-control purposes.

## OS-9 process start (what the runtime emulates)

* The kernel copies idata into static storage and applies irefs. The first
  table is code-relative (+module base) and the second data-relative (+data
  base).
* On entry, **a6 = static storage + 0x8000**, a5 = a7 = parameter area,
  a3 = module, d5 = parameter size and d6 = total memory.
* MPW-built programs (Link): idata begins with a jump table header
  `[size, a5 offset]` followed by `dc.w seg; jmp abs.l` stubs copied to
  `a5+0x20`. `jsr N(a5)` jumps to a stub in RAM, which costs one extra
  instruction. Count it, or cycle-exact comparisons with the interpreter
  drift. Function names are present as MacsBug symbols after each function.
* Microware C programs (the Philips "bumper"): a6-relative data and calls of
  the form `movea.l #off,aN / jsr d8(pc,aN.l)`.
* Programs linked with separate data (Timewarp): the program module has no
  idata. Its startup code F$Loads `<name>_data`, F$Links itself for its base
  address, copies the data module's idata (same layout as program idata,
  found through M$Exec) and applies both iref tables itself. The idata
  contains a table of `jmp abs.l` stubs, code-relative, called with
  `jsr N(a6)`; set `data_module` in game.json so the analyzer resolves them
  (Timewarp: 188 stubs, 659 call sites). The linker symbol table module
  `<name>.stb` (10-byte entries: value.l, type.w, name offset.l; type bit 2
  marks code) supplies function names and entry points.

## Kernel behaviour the games rely on

* **Signals** are delivered asynchronously. Games busy-wait on flags that
  their intercept handlers set, so translated code must yield at backward
  branches.
* **F$Sleep and F$Wait clear the signal mask.** The standard pattern is
  "mask, start an operation, sleep".
* While an intercept routine runs, signals are masked. F$RTE restores the
  context and unmasks.
* F$Alarm `A$Cycle` (d1=2) with time bit 31 set means the time is in 1/256 s.
  Link's 16 Hz game tick is `0x80000010`.
* F$Fork + F$Wait: a forked child (for example the bumper) runs to completion
  while the parent waits. Asynchronous signal sources must target the process
  that requested them.
* F$Sleep with bit 31 set: the time is in 1/256 s *(seen: 0x80000400)*.
* F$Event 53, function in d1: Ev$Link 0, Ev$UnLnk 1, Ev$Creat 2, Ev$Delet 3,
  Ev$Wait 4, Ev$WaitR 5, Ev$Read 6, Ev$Info 7, Ev$Signl 8, Ev$Pulse 9,
  Ev$Set 10, Ev$SetR 11 *(seen)*. Ev$Creat: d0 value, d2 wait increment,
  d3 signal increment, a0 name. Ev$Wait: d2/d3 range; it returns in d1 the
  value that satisfied the wait, **before** the wait increment is applied
  (Balboa's dispatcher loops forever otherwise). A signal ends a wait.
* Common system call numbers: F$Link 00, F$Load 01, F$UnLink 02, F$Fork 03,
  F$Wait 04, F$Exit 06, F$Send 08, F$Icpt 09, F$Sleep 0A, F$ID 0C, F$Time 15,
  F$UnLoad 1D, F$RTE 1E, F$TLink 21, F$SetSys 27, F$SRqMem 28, F$SRtMem 29,
  F$Alarm 56, F$SigMask 57, F$SRqCMem 5C, I$Create 83, I$Open 84, I$Delete 87,
  I$Seek 88, I$Read 89, I$Write 8A, I$ReadLn 8B, I$WritLn 8C, I$GetStt 8D,
  I$SetStt 8E, I$Close 8F.
* Error codes: E$BMode CB, E$MemFul CF, E$UnkSvc D0, E$EOF D3, E$PNNF D8,
  E$MNF DD, E$DevBsy FA.

## Devices and the CSD

* Games read `/nvr/csd`. It holds text Device Status Descriptors of the form
  `type:name:params` ending in CR. Types:
  * 1 is the CD control unit (`/cd`);
  * 2 is the audio processor (`/ap`);
  * 3 is video (`/vid`, with `LI="525"` or `"625"`);
  * 4 is NVRAM (`/nvr`, `SZ#8`);
  * 5 is the pointer (`/ptr`, `CL="d"` for a manoeuvring device).
* GetStat 0x43 on `/nvr` returns free NVRAM bytes in d2 *(seen)*.

## CDFM (`/cd` files)

| SetStat | Code | Notes |
|---------|------|-------|
| SS_Play | 0x2D | a0 = PCB *(seen)* |
| SS_Abort | 0x31 | *(seen)*; the play ends: PCB_Stat gets 0x8100 and PCB_Sig the error E$Abort 0x060A, then the signal is sent (Balboa's `util_abort` waits for that) |
| SS_Seek | 0x30 | |
| SS_Pause / SS_Cont | 0x33 / 0x37 | *(seen in Timewarp's pause)* |
| SS_Enable / SS_Disable / SS_Stop | 0x39 / 0x38 / 0x36 | probable numbering |

* **PCB:** Stat.w, Sig.w, Rec.l, Chan.l, AChan.w, then CIL pointers for video,
  audio and data.
  * PCB_Sig is sent on EOR, trigger and finish.
  * Rec is decremented on each EOR, and the play ends when it reaches 0.
* **PCL:** Ctrl.b, res, SMode.b, Type.b, Sig.w, Nxt.l, Buf.l, BufSz.l (in
  sectors), Err.l, res.w, Cnt.l.
  * When the buffer fills, Ctrl bit 0 is set, the CIL entry advances to Nxt,
    and PCL_Sig is sent.
  * A full PCL that comes round again is an overrun. That ends the play with
    an error.
* Plays run at 75 sectors per second (1×). Audio sectors whose channel is in
  AChan go to the audio processor. Otherwise they go to the audio CIL.

## Audio (`/ap`)

* SS_SM = 0x3B. These are SetStat unless marked GetStat:
  * Creat 0 (coding, groups) returns the id and a0 = data;
  * Out 1 (id, a0 = asynchronous status block);
  * Off 2;
  * Cncl 3 (stop the soundmap's output);
  * Close 4 (Philips library names `sm_cncl`/`sm_close`, *seen*);
  * Info 0 is GetStat and returns a0 = descriptor; Stat 1 is GetStat.
* **SM_Out on a busy processor queues the new soundmap** to start when the
  current sector finishes. Games double-buffer music this way and pace
  themselves on the completion signal (ASY_Sig), so completions must be
  sample-exact.
* SS_SC = 0x3D, SC_Atten d3: bytes 2 and 0 are the direct paths (L→L, R→R),
  bytes 3 and 1 the cross paths. 0xFF is off.
* ADPCM coding byte:
  * bits 0-1 select stereo (01 = stereo);
  * bits 2-3 select the rate (00 = 37.8 kHz, 01 = 18.9 kHz);
  * bits 4-5 select the sample size (00 = 4-bit, 01 = 8-bit).

  A sound group is 128 bytes: 16 parameter bytes (copies at 4..11) followed by
  112 data bytes. The filter coefficients are the XA ones.

## Video (`/vid`)

* SS_DC = 0x56 (d2 = function):

  | Code | Function | Code | Function |
  |------|----------|------|----------|
  | 0 | CrFCT | 0xA | WrLI |
  | 1 | RdFCT | 0xB | DlLCT |
  | 2 | WrFCT | 0xC | FLnk |
  | 3 | RdFI | 0xD | LLnk |
  | 4 | WrFI | 0xE | Exec |
  | 5 | DlFCT | 0x11 | SSig |
  | 6 | CrLCT | 0x12 | Relea |
  | 7 | RdLCT | 0x1B | PRdLCT |
  | 8 | WrLCT | 0x1C | PWrLCT |
  | 9 | RdLI | | |

  Seen: 0, 2, 4, 6, 7, 0xA, 0xC, 0xD, 0xE, 0x11 and 0x1C. 0xF and 0x13 are
  probably Intl and SetCmp; they're ignored.
* LCT line numbers are in UCM coordinates: normal-resolution tables hold half
  the lines. "P" variants use physical lines.
* SS_GC = 0x52: Pos 0, Show 1, Hide 2, Ptn 3, Col 4, Org 5, Blnk 6.
  Coordinates are high resolution (768×480).
* SS_PT = 0x59: Coord 0 (GetStat) returns d0 = buttons (bit 15 = active) and
  d1 = H:V. SSig 1, Relea 2, **Pos 3, Org 4** (note this order).
* Video interrupts come only from DCP instruction `$60`. DC_SSig counts them.
* DCP opcodes:

  | Opcode | Meaning |
  |--------|---------|
  | 10 | NOP |
  | 20 | link (driver-internal) |
  | 40 | display line start |
  | 60 | interrupt |
  | 78 | display parameters (RMS bits 0-1, 10 = run-length) |
  | 80-BF | CLUT entry within the current bank |
  | C0 | coding: CM0 bits 0-3, CM1 bits 8-11, NM bit 19, CS bit 22 |
  | C1 | transparency: TA bits 0-3, TB bits 8-11, bit 23 = no mix |
  | C2 | plane order |
  | C3 | bank |
  | C4 / C6 | transparent colour |
  | C7 / C9 | mask colour |
  | CA / CB | DYUV start values |
  | D0-D7 | mattes |
  | D8 | backdrop |
  | D9 / DA | mosaic |
  | DB / DC | ICF (0-63) |

* RL7: a byte with bit 7 set starts a run of colour b&0x7F whose length is the
  next byte (0 = to the end of the line). Otherwise the byte is a single pixel.
* DYUV deltas: 0,1,4,9,16,27,44,79,128,177,212,229,240,247,252,255.
* Colours are in studio range (black 16, white 235).
* Mono audio with all four SC_Atten paths open (`0x00000000`, Timewarp) must
  not be summed twice: identical left and right inputs take the larger of the
  direct and cross gains.
* Link draws sprites with **compiled sprite routines loaded from disc into
  RAM**. They run in the interpreter.

## Timing model used by the runtime

* Emulated time counts executed 68k instructions (`CPU_IPS` = 4M per second).
* Video fields (60 Hz), CD sectors (75 Hz), alarms and soundmap completions are
  scheduled on that clock. Pacing to the wall clock happens once per field.
* Translated code yields only at backward branches (`BUDGET`), and the
  interpreter yields by the same rule. That keeps `--interp` runs comparable
  frame for frame.

## Balboa and the cdi_bpsys system extension

Balboa is Philips' authoring library (play manager `pml_`/`pmm_`, video
environments `ve_`, control panel `ve_cp_`, dispatcher, signal manager
`sgm_`, timers `tmr_`). Its program side is linked into the game, but the
pointer handling lives in **cdi_bpsys**, an extension module in player ROM
that the program links into the video driver. `runtime/bpsys.c` emulates it;
the structure layouts are in the comment at the top of that file.

* `SS_SLink` = SetStat 0x5A on `/vid`, a0 = module name (`"cdi_bpsys"`).
* Then SetStat functions in d2 (d1 = code):

  | Code / function | Meaning |
  |-----------------|---------|
  | 0x51 / 0x4200 | version: high word of d0 (Timewarp expects 0x2000) |
  | 0x52 / 0x4204, 0x59 / 0x4206 | install the cursor and pointer handlers |
  | 0x51 / 0x420C | video environment to the front (a0 = VE) |
  | 0x51 / 0x4210 | remove VE |
  | 0x51 / 0x4214 | make VE current |

* The extension moves the control panel cursor, hit-tests the hotspot trees
  (deepest unlocked child wins) and reports events (0x0004 / 0x0008 button 1
  / 2 pressed, 0x0100 enter, 0x0200 leave) by calling, with the program's a6,
  the packet allocator stored in the control panel and the
  `dispatch_function_sys` stored in the VE. It must not do so while the
  dispatcher's busy flag (VE+56) is set or signals are masked.
* No hotspot events while the control panel cursor is hidden (`ve_cp_hide`).
  Timewarp hides it during scene changes and relies on that: a click on a
  map location while the map is still streaming makes `go_new_env` call
  `pml_play`, which fails (0x2C0C, still playing), and the game's busy flag
  is never cleared again.
* Pointer positions are in high-resolution screen units. A program on an
  NTSC player (CSD `LI="525"`, which sets Timewarp's flag `-32730(a6)`) uses
  768x480 directly. On PAL it adds 40 to its hotspot rectangles and limits
  the cursor to a taller area.
* Idle functions (`ve_cp_idle_function`): the period is in 1/10 s, the
  extension calls the "sleep" function after that much inactivity and the
  "wake" function on the next input. Timewarp uses them as a screen saver.
