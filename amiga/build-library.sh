#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenFont, MIT licence (LICENSE).
# openfont.library for AmigaOS 3.2 on the 68040, built with OpenAmigaGCC
# 16.2.0-open2 (the os32-gcc16 stove's compiler), with FreeType 2.14.3 and
# HarfBuzz 14.5.1 inside it.
#
#   amiga/build-library.sh [OUT_DIR]          (default build/amiga)
#
#   CC, CXX    the compilers (default: the os32-gcc16 stove, then the
#              release unpacked under /opt)
#   NDK        a folder of AmigaOS headers, when the compiler has none of
#              its own: OpenNDK's Include_H (openamigasdk/OpenNDK/Include_H)
#   TARBALLS   where the upstream sources are kept (default amiga/tarballs);
#              each is checked against amiga/SOURCES, and fetched when
#              missing
#   FREETYPE_GIT  a checkout of FreeType's Git mirror at the commit
#              amiga/SOURCES pins, used when the release tarball can't be had
#
# Nothing upstream is ever committed: the sources are fetched and checked
# here, and built in OUT_DIR/work.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
OUT=${1:-$ROOT/build/amiga}
WORK=$OUT/work
TARBALLS=${TARBALLS:-$HERE/tarballs}
mkdir -p "$OUT" "$WORK" "$TARBALLS"

find_cc() {
    for d in "$HOME/AmigaChrome/stoves/os32-gcc16/bin" "$HOME/AmigaChrome/stoves/os32-gcc16/prefix/bin" \
             /opt/openamigagcc-16.2.0-open2-x86_64-linux/bin; do
        if [ -x "$d/m68k-amigaos-gcc" ]; then echo "$d"; return; fi
    done
}
if [ -z "${CC:-}" ]; then
    BIN=$(find_cc)
    [ -n "$BIN" ] || { echo "m68k-amigaos-gcc not found: set CC and CXX, or install the os32-gcc16 stove" >&2; exit 1; }
    CC=$BIN/m68k-amigaos-gcc
    CXX=$BIN/m68k-amigaos-g++
fi
CXX=${CXX:-$(dirname "$CC")/m68k-amigaos-g++}
NM=$(dirname "$CC")/m68k-amigaos-nm
"$CC" --version | head -1

NDKFLAGS=
[ -n "${NDK:-}" ] && NDKFLAGS="-I$NDK"

# ---- the sources, checked against SOURCES ----
src_line() { grep "^$1 " "$HERE/SOURCES"; }
fetch() {   # NAME: the tarball named in SOURCES, fetched if missing, then checked
    set -- $(src_line "$1")
    t="$TARBALLS/$2"
    if [ ! -f "$t" ]; then
        echo "fetching $4"
        curl -sSfL -o "$t.part" "$4" && mv "$t.part" "$t" || { rm -f "$t.part"; return 1; }
    fi
    echo "$3  $t" | sha256sum -c - >/dev/null || { echo "SHA-256 mismatch: $t" >&2; exit 2; }
}

rm -rf "$WORK/freetype" "$WORK/harfbuzz"
if fetch freetype 2>/dev/null; then
    set -- $(src_line freetype)
    mkdir -p "$WORK/freetype" && tar -xJf "$TARBALLS/$2" -C "$WORK/freetype" --strip-components=1
    echo "FreeType from $2 (SHA-256 checked)"
else
    set -- $(src_line freetype-git)
    G=${FREETYPE_GIT:-}
    [ -n "$G" ] || { echo "FreeType: no tarball, and FREETYPE_GIT not set (a clone of $4 at $2)" >&2; exit 2; }
    [ "$(git -C "$G" rev-parse HEAD)" = "$3" ] || { echo "FreeType: $G is not at $3" >&2; exit 2; }
    mkdir -p "$WORK/freetype" && git -C "$G" archive HEAD | tar -x -C "$WORK/freetype"
    echo "FreeType from Git at $3 (commit checked)"
fi
fetch harfbuzz
set -- $(src_line harfbuzz)
mkdir -p "$WORK/harfbuzz" && tar -xJf "$TARBALLS/$2" -C "$WORK/harfbuzz" --strip-components=1

CPU="-m68040 -m68881"   # the 68040 and its own FPU (-m68040 alone gives soft float)
COMMON="$CPU -O2 -fno-delete-null-pointer-checks -fno-common -ffunction-sections -fdata-sections -DNDEBUG"

# ---- FreeType: the modules OpenFont needs, its memory from the library ----
FT=$WORK/freetype
HB=$WORK/harfbuzz
CONF=$WORK/ft-config/freetype/config
mkdir -p "$CONF"
sed -e 's|^#define FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES|/* no environment: a library has none */|' \
    -e 's|^#define FT_CONFIG_OPTION_USE_LZW|/* no LZW */|' \
    -e 's|^#define FT_CONFIG_OPTION_USE_ZLIB|/* no zlib */|' \
    -e 's|^#define FT_CONFIG_OPTION_MAC_FONTS|/* no Mac resource forks */|' \
    -e 's|^#define FT_CONFIG_OPTION_GUESSING_EMBEDDED_RFORK|/* no rforks */|' \
    -e 's|^#define FT_CONFIG_OPTION_SVG|/* no OT-SVG yet */|' \
    -e 's|^#define TT_CONFIG_OPTION_BDF|/* no BDF */|' \
    "$FT/include/freetype/config/ftoption.h" > "$CONF/ftoption.h"
cat > "$CONF/ftmodule.h" <<'EOF'
/* openfont.library's FreeType modules: TrueType and CFF outlines (TTF,
 * OTF, collections), their hinting, and the two rasterisers. */
FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
FT_USE_MODULE( FT_Renderer_Class, ft_raster1_renderer_class )
EOF
FTFLAGS="-I$WORK/ft-config -I$FT/include -DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H=<freetype/config/ftmodule.h> -DFT_CONFIG_OPTIONS_H=<freetype/config/ftoption.h>"
OBJ=$WORK/obj
mkdir -p "$OBJ"
# SKIP_ENGINES=1 keeps FreeType's and HarfBuzz's objects from the last
# build (they take most of the time) and rebuilds only OpenFont's own
if [ "${SKIP_ENGINES:-0}" != 1 ] || [ ! -f "$OBJ/harfbuzz.o" ]; then
rm -f "$OBJ"/*.o
for f in base/ftbase base/ftinit base/ftbitmap base/ftglyph base/ftmm base/ftsynth base/ftdebug \
         truetype/truetype cff/cff sfnt/sfnt smooth/smooth raster/raster \
         psaux/psaux psnames/psnames pshinter/pshinter autofit/autofit; do
    o=$OBJ/ft_$(echo "$f" | tr '/' '_').o
    "$CC" $COMMON $NDKFLAGS $FTFLAGS -c "$FT/src/$f.c" -o "$o"
done
"$CC" $COMMON $NDKFLAGS $FTFLAGS -c "$ROOT/library/of_ftsystem.c" -o "$OBJ/of_ftsystem.o"

# ---- HarfBuzz: one file, single-threaded under the library's lock ----
HBFLAGS="-std=c++17 -Wno-attributes -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
 -DHAVE_FREETYPE=1 -DHB_NO_MT -DHB_NO_ATEXIT -DHB_NO_GETENV -DHB_NO_SETLOCALE \
 -DHB_NO_BUFFER_SERIALIZE -DHB_NO_BUFFER_MESSAGE -DHB_NO_BUFFER_VERIFY -DHB_NO_CAIRO \
 -DHB_NO_OPEN -DHB_NO_MMAP -DHB_NO_ERRNO \
 -I$FT/include -I$WORK/ft-config"
"$CXX" $COMMON $NDKFLAGS $HBFLAGS -c "$HB/src/harfbuzz.cc" -o "$OBJ/harfbuzz.o"
fi

# ---- OpenFont: the core, the index, and the library around them ----
OFFLAGS="-std=gnu99 -Wall -Wextra -Werror -I$ROOT/include -I$ROOT/src -I$ROOT/library -I$FT/include -I$WORK/ft-config -I$HB/src"
for f in src/openfont src/openfont_cache src/openfont_index src/openfont_io_join; do
    "$CC" $COMMON $NDKFLAGS $OFFLAGS -c "$ROOT/$f.c" -o "$OBJ/of_$(basename "$f").o"
done
# The library's own files include the OS headers, whose struct timeval is
# timer.device's: libnix's own is then left out.
LIBFLAGS="$OFFLAGS -D_TIMEVAL_DEFINED"
for f in library/of_runtime library/of_stack library/of_io_dos; do
    "$CC" $COMMON $NDKFLAGS $LIBFLAGS -c "$ROOT/$f.c" -o "$OBJ/of_$(basename "$f").o"
done
# the library's file first, its top level in order: the code starts with start()
"$CC" $COMMON $NDKFLAGS $LIBFLAGS -fno-toplevel-reorder -fno-function-sections -fno-data-sections -c "$ROOT/library/openfont_lib.c" -o "$OBJ/aa_openfont_lib.o"

# ---- linking: no C startup; libnix only for its stateless functions ----
"$CC" $CPU -nostartfiles -Wl,--gc-sections -Wl,-Map,"$OUT/openfont.library.map" \
    -o "$OUT/openfont.library" \
    "$OBJ/aa_openfont_lib.o" "$OBJ"/of_*.o "$OBJ"/ft_*.o "$OBJ/harfbuzz.o" \
    -lc -lm -lgcc
"$NM" -u "$OUT/openfont.library" > "$OUT/undefined.txt" || true
# the first code in the file must be start(), which returns at once when
# the library is run as a program
if [ "$("$NM" "$OUT/openfont.library" | awk '$3 == "_start" {print $1}')" != 00000000 ]; then
    echo "start() is not at the beginning of the code" >&2
    exit 1
fi
if [ -s "$OUT/undefined.txt" ]; then
    echo "undefined symbols:" >&2
    cat "$OUT/undefined.txt" >&2
    exit 1
fi
echo "$OUT/openfont.library ($(wc -c < "$OUT/openfont.library") bytes)"
sha256sum "$OUT/openfont.library"

# ---- the SDK's link library (the varargs calls), and the lab test ----
SDKFLAGS="$CPU -O2 -Wall -Wextra -Werror -fno-delete-null-pointer-checks $NDKFLAGS -I$ROOT/sdk/Include_H -D_TIMEVAL_DEFINED"
STUBS=$WORK/stubs
rm -rf "$STUBS"; mkdir -p "$STUBS"
for f in "$ROOT"/sdk/stubs/*.c; do
    "$CC" $SDKFLAGS -c "$f" -o "$STUBS/$(basename "$f" .c).o"
done
rm -f "$OUT/libopenfont.a"
"$(dirname "$CC")/m68k-amigaos-ar" rcs "$OUT/libopenfont.a" "$STUBS"/*.o
"$CC" $SDKFLAGS -o "$OUT/OpenFontLibTest" "$ROOT/tests/amiga/OpenFontLibTest.c" -L"$OUT" -lopenfont
echo "$OUT/OpenFontLibTest ($(wc -c < "$OUT/OpenFontLibTest") bytes)"
