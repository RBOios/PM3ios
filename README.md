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
Keys: ←/→ ±1, ↑/↓ ±20, Esc. Touch: left/right third = prev/next, top 20% = ±20, centre = first.
iOS: put `PM3SYS0.FPA`, `PM3DAT1.FPA`, `PM3DAT2.FPA` (optionally inside a `PM3/` folder) into the app's
folder in the Files app (On My iPhone → PM3 Viewer). Game data is never bundled.

## IPA
Run the `ios-ipa` workflow; download artifact `pm3viewer-unsigned-ipa`, then sign/sideload (AltStore, Sideloadly, etc.).
Bundle id `com.PM3ios` (set in CMakeLists.txt).

## Notes / porting decisions
* Reference quirks kept on purpose: ABP output is not truncated to the header size (FPA index length depends on it);
  the last FPA entry's end may exceed EOF by 12 bytes in DAT1/DAT2 and is clamped; MTF lists may grow on values >= 256.
* Not done yet: TFN font, WAV/MIDI audio, AVI, WPC script VM / particle opcodes, EVX VM, EXE logic (Phase 3).
