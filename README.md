# PM3 iOS port — Phase 1/2 scaffold

C++17 core (portable, no deps) + SDL2 asset viewer + unsigned-IPA workflow.

```
core/pm3/   bitreader.h  abp.{h,cpp}  fpa.{h,cpp}  image.{h,cpp}   # ABP_, FPA, GDT(8/24bpp/DIB), WPC sheets, WMP
tools/      pm3_dump.cpp          # list entries / export PPMs (for verification against py/)
tests/      test_core.cpp         # data-free unit tests;  compare_with_python.py  # pixel diff vs py/ output
app/        main.cpp (SDL2 viewer) platform_{ios.mm,other.cpp} font5x7.h
ios/        Info.plist.in         # landscape, Files-app visible Documents folder
.github/workflows/ios.yml         # ubuntu core tests + macOS unsigned .ipa
```

## Verified (desktop, real game data)
* `pm3_dump list`: 1505 entries over the 5 FPA archives — identical to `py/fpa.py`.
* `pm3_dump images`: 398 images (SYS0 GDT + WPC sheets + DAT1/2 WMP) — **pixel-identical** to `py/export_images.py`.

```
cmake -S . -B build -DPM3_BUILD_APP=OFF && cmake --build build -j && ctest --test-dir build
./build/pm3_dump images <game_dir> out_cpp
python3 tests/compare_with_python.py <py_png_dir> out_cpp
```

## Viewer
Desktop: `cmake -S . -B build && cmake --build build && ./build/pm3viewer <game_dir>` (needs SDL2 dev package).
Pages: GDT/WMP images, WAV sounds (auto-play), each WPC sheet, then `<NAME>.WPC PLAY` (animation).
Keys: ←/→ ±1, ↑/↓ ±20, Esc. Touch: left/right third = prev/next, top 20% = ±20, centre = first.
iOS: put `PM3SYS0.FPA`, `PM3DAT1.FPA`, `PM3DAT2.FPA` (optionally inside a `PM3/` folder) into the app's
folder in the Files app (On My iPhone → PM3 Viewer). Game data is never bundled.

## IPA
Run the `ios-ipa` workflow; download artifact `pm3viewer-unsigned-ipa`, then sign/sideload (AltStore, Sideloadly, etc.).
Bundle id `com.PM3ios` (set in CMakeLists.txt).

## Notes / porting decisions
* Reference quirks kept on purpose: ABP output is not truncated to the header size (FPA index length depends on it);
  the last FPA entry's end may exceed EOF by 12 bytes in DAT1/DAT2 and is clamped; MTF lists may grow on values >= 256.
* WPC (core/pm3/wpc.*): loader, slot-script VM (ops 01-0F,12-14), movement/anim, group renderer. Verified against
  py/wpc_play.py: per-tick sprite lists + FX logs identical for all 145 WPC (600 ticks, seed 1); frames pixel-identical
  (P000, P003, P142). Particle/overlay ops (11, 15-18, 1b-1d) are logged, not simulated (only P142-P144 use them).
  Viewer: every WPC has an extra "PLAY" page that runs the scene at 30 ticks/s.
* EVX (core/pm3/evx.*): loader, linear-sweep disassembler (u16 args: 01 0c 10 11 12; u8 args: 03 05 07 09 0a; rest 1 byte)
  and stack VM. Engine ops are only logged (except GETV/SETV, see `exe_vars` below). Verified against py/evx_vm.py: trace of all 143 scripts (age=12, seed=1,
  60000 steps) identical (233,243 lines, text payloads excluded) -> `tools/evx_dump <game_dir> all`.
* EVX `exe_vars` (core/pm3/evx_vars.h, off by default): EXE-faithful GETV/SETV from PM3WIN.EXE 0x4249D5 / 0x40D38B —
  vars 256..895 = WORD array (raw s16), every other SETV clamps to 0..9999, 34 special vars map to game-state fields
  (stored = value - add), and SETV returns the old value in R. `tools/evx_dump <dir> all 60000 --exe` (neutral zero init) differs from
  the Python-parity trace in 68/143 scripts; 67 of them only because of the R side effect, 1 (`_Z`) by value mapping (cause open).
* WMP (core/pm3/wmp.*): metadata parser; all 141 scenes identical to py/wmplib.py (`tools/wmp_dump`).
* Scene facts (see py_added/NOTES_EVX_WPC_연결.md): background wrapper arg >=1000 -> (arg-1000)+60; sprite wrapper arg 65535 = none;
  season = op_66(3072) (3 = winter: BG +17, P +32); lesson scripts switch to P+1 for exams, job scripts pick P by result score.
* Scenes (core/pm3/scene.*): `wmp_scene_load` = port of py/wmp_render.py build()/frames() — 141/141 scenes frame-0
  pixel-identical to Python, animated-frame counts equal (7 animated). `evx_scene_info` runs a script and records its
  BACKGROUND/SPRITES ids (`EvxVm::events`; text log unchanged, all 143 traces byte-identical). Viewer: `<SCRIPT>.EVX SCENE`
  pages (WMP background + WPC sprites at screen origin, x2) and WPC PLAY pages with a background; tap centre / key B cycles
  backgrounds (PLAY pages start with the pair seen in the EVX scripts, if any). `pm3_dump scene <dir> out.ppm <SCRIPT|P###:M####>`
  renders one composed frame headlessly.
* WBD portraits (core/pm3/wbd.*): port of py/wbd_render.py — all 404 portraits: default frame RGBA pixel-identical to
  Python, timeline (frame,ticks) lists identical (`pm3_dump wbd`). Viewer: `<NAME>.WBD` pages loop the idle blink (timeline 2, else 1; 33 ms/tick).
* Portrait placement (EXE 0x4311A8/0x43155F/0x40C738): screen = rec.dst + (2*X-256, 2*Y), X/Y per slot set by op 4a (final value is
  always (x,y)); WBD load resets X=Y=0. `EvxVm::events` records Portrait/Move, `EvxSceneInfo::portraits` = slot state at end of run,
  SCENE pages draw them (idle blink loop). Visibility/Z/glide not modelled yet. See docs/NOTES_EVX_장면합성.md.
* WAV (core/pm3/wav.*): FPA WAV entries have no RIFF wrapper (start at the `fmt ` chunk). Two formats in the data: PCM16 mono 8 kHz
  (SYS0, 77 sounds) and IMA ADPCM tag 0x11, mono 22.05 kHz, block 512 (TAK1/TAK2, 518 voices). Decoded to PCM16 using the reference
  (bit-wise) IMA algorithm; the `fact` sample count trims silent padding (dropped samples are all zero). Verified: all 518 ADPCM entries
  bit-exact vs Python `audioop`; ffmpeg differs only by its multiplication rounding `((2d+1)*step)>>3` (reproduced exactly).
  The EXE has no IMA step table (decoding is left to Windows), so the reference algorithm is the right target. `pm3_dump wav <dir> <out>` exports 595 .wav files.
  Viewer: every WAV is a page (`PM3TAK1/0001.WAV`, ...; TAK archives are optional). Opening a page decodes, mixes to mono, resamples to 22.05 kHz
  (`pcm_to_mono_rate`, linear) and queues it on one SDL audio device; waveform + playhead are drawn; Space / tap centre = replay;
  leaving the page stops the sound. iOS: audio category hint `playback` so it is audible with the silent switch on.
  Note: untested on a device/SDL here (only compiled against an SDL signature stub) - check sound on the first real run.
* Not done yet: MIDI audio, AVI, particle/overlay simulation, EVX engine-op semantics, EXE logic (Phase 3).
