#!/bin/sh
# Build SGL.DLL (the PCX2 SGL library, originally SGLMID5.DLL) from Imagination's source with the
# i686 MinGW cross compiler, linking it against the wrapper's own backend
# (src/) in place of PVROS.DLL. Each file is compiled on its own so one
# failure does not hide the rest; build/summary.txt lists the results.
#
# Usage: tools/build.sh [outdir]      (default: build/)
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=$ROOT/third_party/powervr-series1
OUT=${1:-$ROOT/build}
CC=${CC:-i686-w64-mingw32-gcc}
rm -rf "$OUT/obj" "$OUT/log" "$OUT/sgl.dll" "$OUT/undefined.txt"
mkdir -p "$OUT/obj" "$OUT/log"

# Source lists from pcmake/Makefile and pcmake.mak for HARDWARE=PCX2, OS=WIN32.
SGL_STD="dlconvex dldelete dlcamera dllists dlglobal dllod dlmater dlmesh dlpoint dltransf
 rnlights rnmater rnrender rnstate rntransf rntrav rnlod rnpoint rnqualit adjacenc rnconvex
 dlobject rncamera rnconvpr rnfshade array rnreject rnmesh dllights rnshadow rnshadrj dlnewtrn
 dlqualit rntex rnshade sgl_init"
SGL_COMMON="error rnglobal txmops ldbmp nm_imp sgl_math singmath dvdevice metrics parmbuff
 list dregion pkisp pktsp debug"
SGL_LITE="dsprite dlines dpoint dtex dtexnp disp dtsp dshade dtri sgltri sgllite"
PCX="pcx/hwregs pcx/texas"
# win32/pvrosif.c (the jump table into PVROS.DLL) is replaced by
# src/pvros_backend.c. The MASM files (win32/*.asm) are replaced by the C
# versions already in the source (PVR_NO_ASM) and by src/fpu.c.
WIN32="win32/w32dll win32/hwdevice win32/sglwin win32/sglheap"
# PVROS.DLL's own source (the OS layer: display, board setup, texture heap),
# built into the same DLL. Its VxD calls go to src/vsgl_emu.c (see vsgl.h).
# win32/heap.c is left out: SGL's own win32/sglheap.c provides the same
# functions (a private rather than a shared heap, which is all one process
# needs).
PVROS="win32/system win32/display win32/brdcfg win32/brdsetup win32/texif
 texapi tmalloc profile callback"
# Assembler files with no C equivalent in the source, assembled with JWasm.
ASM="win32/texapiml"
OURS="pvros_backend fpu vsgl_emu pcx2/vid_powervr_pcx_render"

# Defines from pcmake.mak (release build). The original compiler was Intel's
# icl with -Zp4 (4-byte structure packing) and x87 floating point.
CFLAGS="-D_BUILDING_SGL_=1 -DLIBNAME=\"SGLMID5\" -DWIN32=1 -DPCX2=1
 -DWIN32_LEAN_AND_MEAN -DFULL_ON -DICL=1 -DBUILD=\"Release\" -DDEBUG=0
 -DPVR_NO_ASM=1 -O2 -fpack-struct=4 -fgnu89-inline -fcommon -w
 -I$SRC -I$SRC/win32 -I$SRC/pcx -I$SRC/pcmake -I$ROOT/src"

ok=0; bad=0; : > "$OUT/summary.txt"
compile() { # $1 = source path, $2 = object name
  if $CC -c $CFLAGS ${EXTRA:-} "$1" -o "$OUT/obj/$2.o" > "$OUT/log/$2.log" 2>&1; then
    ok=$((ok+1)); echo "ok   $2" >> "$OUT/summary.txt"
  else
    bad=$((bad+1)); echo "FAIL $2 ($(grep -c 'error' "$OUT/log/$2.log") errors)" >> "$OUT/summary.txt"
  fi
}
for f in $SGL_STD $SGL_COMMON $SGL_LITE $PCX $WIN32 $PVROS; do
  EXTRA=""
  [ "$f" = win32/system ] && EXTRA="-DVSGL_REDIRECT"
  [ "$f" = win32/w32dll ] && EXTRA="-DDllMain=SGL_DllMain"
  compile "$SRC/$f.c" "$(echo "$f" | tr / _)"
done
EXTRA=""
for f in $OURS; do
  compile "$ROOT/src/$f.c" "src_$(echo "$f" | tr / _)"
done
# JWasm (MASM-compatible) is fetched and built on first use.
JWASM=${JWASM:-$OUT/tools/jwasm}
if [ ! -x "$JWASM" ]; then
  echo "building JWasm into $OUT/tools"
  mkdir -p "$OUT/tools"
  rm -rf "$OUT/tools/jwasm-src"
  git clone -q --depth 1 https://github.com/Baron-von-Riedesel/JWasm "$OUT/tools/jwasm-src" &&
    make -s -C "$OUT/tools/jwasm-src" -f GccUnix.mak > "$OUT/log/jwasm-build.log" 2>&1 &&
    cp "$OUT/tools/jwasm-src/build/GccUnixR/jwasm" "$JWASM"
fi
for f in $ASM; do
  n=$(echo "$f" | tr / _)
  if "$JWASM" -q -coff -Zm -Cx -DBLD_COFF -DIS_32 -DMASM6 -I"$SRC/win32" \
       -Fo"$OUT/obj/$n.o" "$SRC/$f.asm" > "$OUT/log/$n.log" 2>&1; then
    ok=$((ok+1)); echo "ok   $n" >> "$OUT/summary.txt"
  else
    bad=$((bad+1)); echo "FAIL $n" >> "$OUT/summary.txt"
  fi
done
echo "compiled $ok, failed $bad" | tee -a "$OUT/summary.txt"

# Link straight into SGL.DLL. The original shipped a thin SGL.DLL that
# forwarded each call through a jump table into SGLMID5.DLL; its source
# (new_thin.c) is not in the release, and one DLL is simpler anyway. The
# exports, with the original ordinals, come from src/sgl.def.
if $CC -shared -static-libgcc -o "$OUT/sgl.dll" "$OUT"/obj/*.o "$ROOT/src/sgl.def" \
     -luser32 -lkernel32 -lversion -lgdi32 -ladvapi32 -lddraw -lwinmm -Wl,--out-implib,"$OUT/libsgl.a" \
     > "$OUT/log/link.log" 2>&1; then
  echo "linked $OUT/sgl.dll" | tee -a "$OUT/summary.txt"
else
  grep -o "undefined reference to \`[^']*'" "$OUT/log/link.log" | sort -u > "$OUT/undefined.txt"
  echo "link failed; $(wc -l < "$OUT/undefined.txt") undefined symbols (see $OUT/log/link.log)" \
    | tee -a "$OUT/summary.txt"
  exit 1
fi
