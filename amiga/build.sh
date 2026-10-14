#!/bin/sh
# Copyright (c) 2026 Dalsin Limited. OpenFont, MIT licence (LICENSE).
# The 68k range test, built with OpenAmigaGCC 16.2.0-open2.
# That is the compiler in the os32-gcc16 stove. There is no 16.2.9-open2.
#
#   amiga/build.sh [OUT_DIR]
#
# CC overrides the compiler. Otherwise the script looks for the stove, then
# the unpacked release under /opt.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
if [ -z "${CC:-}" ]; then
    if [ -x "$HOME/AmigaChrome/stoves/os32-gcc16/bin/m68k-amigaos-gcc" ]; then
        CC=$HOME/AmigaChrome/stoves/os32-gcc16/bin/m68k-amigaos-gcc
    elif [ -x "$HOME/AmigaChrome/stoves/os32-gcc16/prefix/bin/m68k-amigaos-gcc" ]; then
        CC=$HOME/AmigaChrome/stoves/os32-gcc16/prefix/bin/m68k-amigaos-gcc
    elif [ -x /opt/openamigagcc-16.2.0-open2-x86_64-linux/bin/m68k-amigaos-gcc ]; then
        CC=/opt/openamigagcc-16.2.0-open2-x86_64-linux/bin/m68k-amigaos-gcc
    else
        echo "m68k-amigaos-gcc not found. Set CC, or install the os32-gcc16 stove." >&2
        exit 1
    fi
fi
OUT=${1:-$ROOT/build}
mkdir -p "$OUT"
"$CC" --version | head -1
for cpu in 68000 68020 68040; do
    "$CC" -m"$cpu" -O2 -Wall -Wextra -Werror \
        -I "$ROOT/include" -I "$ROOT/src" \
        -o "$OUT/openfontrange-$cpu" \
        "$ROOT/tests/test_range.c" "$ROOT/src/openfont_cache.c"
    echo "$OUT/openfontrange-$cpu ($(wc -c < "$OUT/openfontrange-$cpu") bytes)"
done

# The OpenMulticore path needs the NDK and openamigamulticore/include.
# OMC_INC is that include directory. Without it, only the range test is built.
if [ -n "${OMC_INC:-}" ]; then
    "$CC" -m68000 -O2 -Wall -Wextra -Werror \
        -I "$ROOT/include" -I "$ROOT/src" -I "$OMC_INC" \
        -o "$OUT/openfontomc-68000" \
        "$ROOT/tests/test_omc.c" "$ROOT/src/openfont_omc.c" "$ROOT/src/openfont_cache.c"
    echo "$OUT/openfontomc-68000 ($(wc -c < "$OUT/openfontomc-68000") bytes)"
fi
