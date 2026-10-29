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
rm -rf "$OUT/obj" "$OUT/log"
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
OURS="pvros_backend fpu"

# Defines from pcmake.mak (release build). The original compiler was Intel's
# icl with -Zp4 (4-byte structure packing) and x87 floating point.
CFLAGS="-D_BUILDING_SGL_=1 -DLIBNAME=\"SGLMID5\" -DWIN32=1 -DPCX2=1
 -DWIN32_LEAN_AND_MEAN -DFULL_ON -DICL=1 -DBUILD=\"Release\" -DDEBUG=0
 -DPVR_NO_ASM=1 -O2 -fpack-struct=4 -fgnu89-inline -fcommon -w
 -I$SRC -I$SRC/win32 -I$SRC/pcx -I$SRC/pcmake -I$ROOT/src"

ok=0; bad=0; : > "$OUT/summary.txt"
compile() { # $1 = source path, $2 = object name
  if $CC -c $CFLAGS "$1" -o "$OUT/obj/$2.o" > "$OUT/log/$2.log" 2>&1; then
    ok=$((ok+1)); echo "ok   $2" >> "$OUT/summary.txt"
  else
    bad=$((bad+1)); echo "FAIL $2 ($(grep -c 'error' "$OUT/log/$2.log") errors)" >> "$OUT/summary.txt"
  fi
}
for f in $SGL_STD $SGL_COMMON $SGL_LITE $PCX $WIN32; do
  compile "$SRC/$f.c" "$(echo "$f" | tr / _)"
done
for f in $OURS; do
  compile "$ROOT/src/$f.c" "src_$f"
done
echo "compiled $ok, failed $bad" | tee -a "$OUT/summary.txt"

# Link straight into SGL.DLL. The original shipped a thin SGL.DLL that
# forwarded each call through a jump table into SGLMID5.DLL; its source
# (new_thin.c) is not in the release, and one DLL is simpler anyway. The
# exports, with the original ordinals, come from src/sgl.def.
if $CC -shared -static-libgcc -o "$OUT/sgl.dll" "$OUT"/obj/*.o "$ROOT/src/sgl.def" \
     -luser32 -lkernel32 -lversion -Wl,--out-implib,"$OUT/libsgl.a" \
     > "$OUT/log/link.log" 2>&1; then
  echo "linked $OUT/sgl.dll" | tee -a "$OUT/summary.txt"
else
  grep -o "undefined reference to \`[^']*'" "$OUT/log/link.log" | sort -u > "$OUT/undefined.txt"
  echo "link failed; $(wc -l < "$OUT/undefined.txt") undefined symbols (see $OUT/log/link.log)" \
    | tee -a "$OUT/summary.txt"
  exit 1
fi
