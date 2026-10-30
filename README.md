# pvr-sgl-wrapper

A replacement `SGL.DLL` for games written for the PowerVR PCX1/PCX2 (Matrox m3D, VideoLogic Apocalypse 3D/3Dx and similar). It runs native PowerVR SGL games on modern Windows, with no PowerVR card, and draws what a real PCX2 would.

It works by rebuilding the original driver rather than imitating it:

- **SGL itself** is Imagination's own library, from the [PowerVR Series 1 source release](https://github.com/powervr-graphics/PowerVR-Series1) (MIT), built with small portability fixes. Games get the real API behaviour, including the geometry, sorting and tiling the driver did on the CPU.
- **PVROS**, the driver's OS layer (DirectDraw display, board set-up, texture memory), is also Imagination's source.
- **The VxD** (`VSGL.VXD`), the one piece that touched the card, is not in the release. `src/vsgl_emu.c` stands in for it and emulates one PCX2. Board memory is ordinary memory. A render request runs the PCX2 renderer from the [86Box emulation](https://github.com/z2k-retro/86Box), which matches a real m3D to within a few pixels per frame in every game compared. The pixels go straight into the DirectDraw surface.

Everything is one 32-bit `sgl.dll` with the same export table as the last `SGL.DLL` Matrox shipped (4.1.1.5).

## Status

The whole path works under Wine:

- **Address mode** (`tests/tri.c`): the application gives SGL a memory buffer. The test draws a Gouraud-shaded triangle with a translucent quad in front.
- **DirectDraw mode** (`tests/ddraw.c`): this is the path games use. The test opens a window, calls `sgl_use_ddraw_mode`, and runs a full-screen 640×480×16 double-buffered device with page flips. It uploads a mip-mapped texture and draws a spinning textured, bilinear-filtered quad over a Gouraud one.

Games tested on Windows 11 (release 0.1):

| Game | Notes |
|---|---|
| Unreal (1998) | Plays at 1024×768. Use with dgVoodoo2's `ddraw.dll`. |
| GLQuake / Quake II | Through Matrox's PowerVR MiniGL (`opengl32.dll`) on top of this `sgl.dll`. |
| POD 2.0 (`podpvrx.exe`) | Needs the `sglhw.ini` handling below (built in). |
| MDK (`MDKPVR.EXE`) | MDK's own `MDK.CFG` must point at the game's data folder, and its CD check applies. |
| Fogtest (PowerVR demo) | Works. |

Starting some games takes a few seconds longer than it should. They run normally once started.

Rendering uses every CPU thread. Set `PVR_RENDER_THREADS` to override the count.

The PCX2 Gouraud-shades in intensity only: one colour per polygon, with brightness interpolated (reference, Appendix B). A triangle with red, green and blue vertices therefore comes out a single hue, as it would on the card.

## Trying it on Windows

1. Copy `sgl.dll` next to the game's executable. For Unreal, that is the `System` folder.
2. Select the game's PowerVR renderer. For Unreal, that is *PowerVR SGL support*, or `GameRenderDevice=SGLDrv.SGLRenderDevice` in `Unreal.ini`.
3. If the game cannot set its 640×480 16-bit mode, or the screen stays black, add dgVoodoo2's `ddraw.dll` to the same folder, as RReady users do.

**Unreal (1998) runs.** On Windows 11 with dgVoodoo2's `ddraw.dll`, it plays at 1024×768 at full speed on a Ryzen 7735HS mini-PC (16 threads, integrated graphics, which only display the frames). That run covered the intro, Vortex Rikers and Nyleve's Falls with saving.

**Games that read `sglhw.ini` themselves.** The PowerVR driver installed `sglhw.ini` in the Windows folder, and some games read it directly. POD (`podpvrx.exe`) reads the tile size from it (`[Default] SW_XRegionSize`, `SW_YRegionSize`) and divides by it. Without the file it crashed with a divide by zero before the first race. The wrapper now answers those reads. It looks first in an `sglhw.ini` next to `sgl.dll`, then in the Windows folder's, and otherwise gives the driver's own default for that setting (32 × 32 for the tile size), for every setting in the driver source whose default is not 0 (`src/compat.c`).

**Screenshots.** Press F12 or Scroll Lock in a game (or set `PVR_SHOT_KEY` to another virtual-key code) to save the frame just drawn as `pvrshot_NNN.bmp` next to `sgl.dll`. The capture is exactly what the emulated chip wrote, after packing and dithering, so it is the frame to compare with 86Box or a real card. Windows capture tools may miss a full-screen DirectDraw game. Setting `PVR_SHOT_FRAME=n` saves frame *n* without a key press.

**Log.** `pvrsgl.log` next to `sgl.dll` records the driver's messages, and for any crash the module, offset, registers and return addresses. `tools/addr2sym.py` (or `i686-w64-mingw32-addr2line -e build/sgl.dll`) turns `sgl.dll` offsets into function and line. Set `PVR_LOG=0` to turn the log off.

`ddraw.exe` from the tests is a quick check that doesn't need a game. It should show a turning checkerboard for a few seconds, then exit and write `ddraw.bmp`.

## Checked against a real m3D

`tests/replay.c` takes a result folder from the PCX2 tools' `pcx2session.sh` (`report.txt`, the 86Box frame dumps it names, and the card's own output for each case). It renders every case through the wrapper's render path: the VxD stand-in's `SCHEDULE_RENDER` (registers, ISP buffer as the TLB maps it, fog table from ROM) and its write-out to the surface. Then it counts the pixels that differ from what the card drew.

```sh
wine build/replay.exe path/to/pcx2session-YYYYMMDD-HHMMSS
bash tests/replay-all.sh FOLDER [results.txt]   # every session archive (.zip/.tar.gz) or folder in FOLDER
```

`replay-all.sh` expects `replay.exe` and `sgl.dll` next to it. Cases that change `PREC_BASE` are listed but not counted.

The 72 pcx2session results from the 86Box work (2026-10-18 to 10-28) were run on the card machine. Most cases match the card exactly. For the 15 sessions that had differences, every case gives the same pixel count as today's 86Box renderer. The differences that remain are the renderer's: single edge pixels in a few game frames, and the hand-built texture and filter probe frames (`frame_9xxxxx`). Cases that change `PREC_BASE` are not counted.

The renderer is compiled with SSE2 floating point, as the 64-bit 86Box build is. Left on GCC's 32-bit x87 default, its extra internal precision put four edge pixels on the other side of a plane from 86Box and the card.

This checks everything from the chip's inputs to the screen. It cannot check how SGL builds a scene, because the dumps come from the real driver in 86Box.

## Building

The build needs the i686 MinGW cross compiler (`gcc-mingw-w64-i686` on Debian/Ubuntu), plus `git` and `make` for JWasm. The build fetches and builds JWasm the first time; it assembles the one assembler file the source has no C version of.

```sh
tools/build.sh             # -> build/sgl.dll; per-file results in build/summary.txt
i686-w64-mingw32-gcc -O2 -Ithird_party/powervr-series1 tests/ddraw.c -o build/ddraw.exe -Lbuild -lsgl -lgdi32
i686-w64-mingw32-gcc -O2 -Ithird_party/powervr-series1 tests/tri.c -o build/tri.exe -Lbuild -lsgl
i686-w64-mingw32-gcc -O2 tests/smoke.c -o build/smoke.exe
```

Under Wine, the DirectDraw test needs a display with 3D support. On a headless machine, use 32-bit Mesa (`libgl1-mesa-dri:i386`) and `xvfb-run -s '-screen 0 1024x768x24 +extension GLX'`. Also configure a Wine virtual desktop, because Xvfb offers only one display mode.

## How it fits together

```
game ─► SGL (Imagination source) ─► PVROS (Imagination source) ─► vsgl_emu.c ─► PCX2 renderer
                                         │                         (the VxD)       (86Box)
                                         └─ DirectDraw: window, mode, flips, locks the back buffer
```

| Path | What it is |
| --- | --- |
| `third_party/powervr-series1/` | Imagination's `Source/` tree. The first commit is unmodified; the changes since are listed below. |
| `src/vsgl_emu.c` | The VxD: the 4.x drivers' `DeviceIoControl` interface (codes from the PCX2 reference, Appendix F) for one emulated PCX2. It handles board enumeration and info, scatter-gather and page allocations, and `SCHEDULE_RENDER`. It also stands in for `pci.asm`. |
| `src/vsgl.h` | The VxD's control codes and `SHAREDBLOCK`. It replaces the missing `vsgl.h` and points `system.c`'s `CreateFile`/`DeviceIoControl` at `vsgl_emu.c`. |
| `src/pvros_backend.c` | What the missing thin `SGL.DLL` did: `DllMain` for both halves; finding the board; creating the texture heap and logical device; `SglSetGlobal`. |
| `src/pcx2/` | The PCX2 renderer and the m3D's ROM tables, from the 86Box emulation (`pcx-perf` 16ef5f7b4), used under MIT with the author's permission. |
| `src/fpu.c` | `SetupFPU`/`RestoreFPU` from `fast.asm` (x87 single precision, truncating rounding). |
| `src/sgl.def`, `src/sgl_exports_4115.txt` | The export table, copied from the shipped 4.1.1.5 `SGL.DLL` by `tools/gen_def.py`. |
| `tools/build.sh` | Cross-compiles one file at a time, assembles `texapiml.asm` with JWasm, and links `sgl.dll`. |
| `tests/` | `smoke.c` (exports and start-up), `tri.c` (address mode to a BMP), `ddraw.c` (full-screen DirectDraw, texture, screen capture; 16 or 24/32-bit) and `replay.c` (86Box dumps through the wrapper, compared with a real m3D). |

### Per frame

1. SGL builds the ISP planes and region lists in a scatter-gather buffer, which here is one contiguous allocation. It builds TSP parameters in a page-reserved buffer and a 32-register shadow.
2. PVROS's pre-render callback locks the DirectDraw back buffer. Its address becomes `SOFADDR`.
3. `SCHEDULE_RENDER` does what the VxD did. It copies the TSP parameters to texture-memory offset 0 and takes the register shadow. Then, in place of the chip, it renders the tiles on all threads and writes the pixels the chip would write, packed to `PACKMODE`, into the surface. It reports end of render through `LDEVICE.RenderStatus` and the status word.
4. PVROS's end-of-render callback unlocks the surface and flips.

## What callers expect

- **The shipped driver.** The Matrox m3D driver 4.1.1.5.002 has the layered design the source describes. A thin `SGL.DLL` exports 142 functions by name, at alphabetical ordinals (not the `ordinals.h` numbering). `PVROS.DLL` finds the board and loads `SGLMID5.DLL`, which exports only `GetSGL_IF`. `sgl_register_api` belongs to the thin layer and is a no-op here.
- **Unreal** (1998). `SGLDrv.dll` loads `SGL.DLL` itself and looks up 29 functions by name. These are all from the `sgltri_*` path (PowerSGL Direct) plus device and texture set-up.
- **PowerVR MiniGLs for Quake and Quake II** (Dec. 1997). `opengl32.dll`/`pvrgl.dll` load `SGL.DLL` at run time and look up the same set, plus `sgl_get_free_texture_mem_info` and `sgl_set_texture_extended`.

`tests/smoke.c` checks all 31 names and the 4.1.1.5 ordinals.

## Changes to Imagination's source

Each change is small and commented in place:

1. `metrics.c`: `SglTimeNow` uses `__rdtsc()` under GCC.
2. `dsprite.c`: forward declarations for two static functions that are called before they are defined.
3. `pcx/hwregs.c`: its private copies of the bilinear flags are renamed `bHw...`. The originals were `static` after `hwinterf.h` declared globals of the same name.
4. `win32/hwdevice.c`: removed a cast used as an lvalue.
5. `win32/sglwin.c`: `sgl_use_ddraw_mode` takes `void *hWnd`, matching `sgl.h`.
6. `dtri.h`: `PVR_NO_ASM` keeps Imagination's C versions of the routines the PCX2 build took from `dtexml.asm`/`dtriml.asm`. The assembler versions also build with JWasm, and they draw `tests/tri.c` identically.
7. `win32/display.c`, `win32/system.c`: replaced MSVC's comment-pasting macro (`/##/`). `PVROSQueryPerformance*` use the OS counter instead of reading and resetting the TSC.
8. `win32/brdsetup.c`: `CreateNewPCXBuffer` now returns `PVROS_GROOVY`. The original fell off the end.
9. `profile.c`: the "Couldn't access key SOFTWARE\\PowerVR\\StartUp" message is not logged. Without an installed driver it fires for every setting lookup.
10. `win32/display.c`: `StrictLocks` defaults to on, because today's DirectDraw surface pointers are valid only while locked. The `Sleep(3)` after each frame is left out.

## Next steps

1. **Games.** Unreal and GLQuake/Quake II through the MiniGLs on Windows 11. Compare frames with 86Box captures.
2. **Settings.** The driver reads per-game settings from the registry (`HKLM\SOFTWARE\PowerVR\...`). Shipping defaults like the real driver's may matter for some games.
3. **Later:** the Direct3D/PowerVR Direct path (`pvrd.c`, `PVRMID5.DLL`); a GPU or higher-resolution backend; DOS SGL titles through DOSBox.

## License

MIT (see `LICENSE`). Imagination's source keeps its own MIT notice (`third_party/powervr-series1/LICENSE`).
