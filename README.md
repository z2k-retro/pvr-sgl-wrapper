# pvr-sgl-wrapper

A replacement `SGL.DLL` for games written for the PowerVR PCX1/PCX2 (Matrox m3D, VideoLogic Apocalypse 3D/3Dx and similar). The goal is to run native PowerVR SGL games on modern Windows with output that matches a real PCX2, and later at higher resolutions.

The approach is to reuse rather than reimplement:

- **Front end:** Imagination's own SGL library from the [PowerVR Series 1 source release](https://github.com/powervr-graphics/PowerVR-Series1) (MIT), built as-is with small portability fixes. Games get the real SGL API behaviour, including the geometry and tiling work the original driver did on the host CPU.
- **Back end:** in the original driver, SGL reached the card and the screen through `PVROS.DLL`. The wrapper replaces that layer (`src/pvros_backend.c`). Instead of writing to a card, it will hand each frame's parameter data to the hardware-validated PCX2 renderer from the [86Box PCX2 emulation](https://github.com/z2k-retro/86Box) (`vid_powervr_pcx_render.c`), running natively.

## Status

Early scaffold.

- All 68 C files of the original PCX2/Win32 SGL library (`SGLMID5.DLL`) compile with MinGW, with the 69th (`win32/pvrosif.c`, the jump table into `PVROS.DLL`) replaced by the backend. They link into a single `sgl.dll` that exports the 136 SGL functions at their original ordinals.
- Under Wine, the smoke test (`tests/smoke.c`) loads the DLL by ordinal and calls `sgl_get_versions`, which reports `4.1.2.4sjf`. It then calls `sgl_create_screen_device`, which gets as far as the backend and fails cleanly (`-244`), because every backend function is still a stub.

## Building

The build needs the i686 MinGW cross compiler (`gcc-mingw-w64-i686` on Debian/Ubuntu).

```sh
python3 tools/gen_def.py   # regenerate src/sgl.def from ordinals.h (already committed)
tools/build.sh             # -> build/sgl.dll, per-file results in build/summary.txt
i686-w64-mingw32-gcc -O2 tests/smoke.c -o build/smoke.exe
cd build && wine smoke.exe
```

## Layout

| Path | What it is |
| --- | --- |
| `third_party/powervr-series1/` | Imagination's `Source/` tree. The first commit is unmodified, and later changes are listed below. |
| `src/pvros_backend.c` | Stand-in for `PVROS.DLL`: the 43 functions SGL calls for devices, display, texture heap and rendering. All stubs for now. |
| `src/fpu.c` | C versions of `SetupFPU`/`RestoreFPU` from `win32/fast.asm` (x87 single precision, truncating rounding, as the original). |
| `src/sgl.def` | Export table generated from `ordinals.h`. |
| `tools/build.sh` | Cross-compiles the library one file at a time and links `sgl.dll`. |
| `tests/smoke.c` | Loads `sgl.dll` and makes the first calls a game makes. |

## Changes to Imagination's source

These are kept small and commented in place:

1. `metrics.c`: `SglTimeNow` uses `__rdtsc()` under GCC instead of MSVC inline assembly.
2. `dsprite.c`: forward declarations for two static functions that MSVC let the code call before defining them.
3. `pcx/hwregs.c`: its private copies of the bilinear-filtering flags are renamed (`bHw...`). The originals were declared `static` after `hwinterf.h` declared globals of the same name, which MSVC accepted. The globals themselves live in `sgllite.c`.
4. `win32/hwdevice.c`: a cast used as an lvalue (an old MSVC extension).
5. `win32/sglwin.c`: `sgl_use_ddraw_mode` takes `void *hWnd`, matching `sgl.h`.
6. `dtri.h`: `PVR_NO_ASM` keeps the C versions of the routines that the PCX2 build normally took from the MASM files (`dtexml.asm`, `dtriml.asm`). The C versions are Imagination's own.

## Next steps

1. **Devices and texture heap.** The PVROS source is in the release too (`win32/brdsetup.c`, `heap.c`, `texif.c`, `system.c`). Reuse what doesn't touch PCI or the VxD, and back "card memory" with an ordinary allocation.
2. **Display.** Implement `PVROSOpenDisplayDevice`, `PVROSEnumDisplayModes` and `PVROSSelectDisplayMode` on DirectDraw, as the original `win32/display.c` does, and rely on dgVoodoo2's `ddraw.dll` on modern Windows (as RReady does) rather than writing a DirectDraw layer.
3. **Rendering.** `PVROSScheduleRender` gets the frame's ISP/TSP parameters and region data, which is the same data the emulated card reads in 86Box. Run the PCX2 renderer on it and present the result.
4. **Check against games.** Confirm how games import `sgl.dll` (by ordinal through the original `sgl.lib`, or by name; both are exported). Compare frames with 86Box captures.
5. **Later:** the D3D/PowerVR Direct path (`pvrd.c`), a GPU backend for higher resolutions, and DOS SGL titles.

## License

MIT (see `LICENSE`). Imagination's source keeps its own MIT notice (`third_party/powervr-series1/LICENSE`).
