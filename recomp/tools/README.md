# Tools

## Changes needed in XenonRecomp

The game uses a few instructions XenonRecomp did not handle, and one construct it
translated incorrectly. In `XenonRecomp/recompiler.cpp`:

- `LVEBX` and `LVEHX`: handle like `LVX`.
- `BDNZT`: add (decrement CTR, branch if CTR is non-zero and the condition bit is set).
- `VPKSWSS` (and its 128-bit form): add (pack signed words to signed halfwords with saturation).
- Jump tables: emit `switch (rN.u32)` instead of `switch (rN.u64)`. The upper half
  of the register is not always clean, which sent the game to the wrong case.

When building XenonRecomp with clang, its bundled `fmt` needs
`-DCMAKE_CXX_FLAGS="-include cstdlib -include cstring"`.

## Scripts

| File | Purpose |
|---|---|
| `gen_imports.py` | Generates `runtime/imports.gen.cpp` from XenonRecomp's output |
| `fix_function_bounds.py`, `find_switches.py`, `text_gaps.py` | Found the function bounds and jump tables now listed in `config/` |
| `guest_disasm.py`, `xenos_disasm.py` | Disassemblers for guest code and Xenos shader microcode |
| `xex_dump.cpp`, `xma_test.cpp`, `pad_probe.cpp` | Small test programs |
| `make_icon.ps1` | Draws the program's icon (an original crystal, no game artwork) |
| `package.ps1`, `share.ps1` | Assemble a playable folder / a distributable one without game data |
