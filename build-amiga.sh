#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmigaChrome/stoves/os32-gcc16"}
P="$STOVE/prefix"
CC=${CC:-"$P/bin/m68k-amigaos-gcc"}
OUT=${OUT:-"$HERE/build-amiga"}
FONT_PREFIX=${FONT_PREFIX:-"$HOME/AmigaChrome-dev/webkit-os32-040/m68k-amigaos"}

test -f "$FONT_PREFIX/lib/libfreetype.a" || {
  echo "missing $FONT_PREFIX/lib/libfreetype.a" >&2
  exit 2
}
test -f "$FONT_PREFIX/lib/libharfbuzz.a" || {
  echo "missing $FONT_PREFIX/lib/libharfbuzz.a" >&2
  exit 2
}

mkdir -p "$OUT/include/openfont" "$OUT/include/libraries" \
         "$OUT/include/inline" "$OUT/include/proto" "$OUT/lib" "$OUT/tests"

cp "$HERE/include/openfont/openfont.h" "$OUT/include/openfont/"
cp "$HERE/include/libraries/openfont.h" "$OUT/include/libraries/"
cp "$HERE/include/inline/openfont.h" "$OUT/include/inline/"
cp "$HERE/include/proto/openfont.h" "$OUT/include/proto/"

INCLUDES="-I$HERE/include -I$FONT_PREFIX/include/freetype2 -I$FONT_PREFIX/include/harfbuzz"
LIBS="-L$FONT_PREFIX/lib -lharfbuzz -lfreetype -lpng -lz -lstdc++ -lpthread -lm"

# The first resident build deliberately keeps the proven FreeType/HarfBuzz
# engines inside the library while applications see only the OpenFont ABI.
"$CC" -m68040 -m68881 -mcrt=nix20 -O2 -fomit-frame-pointer \
  -fno-toplevel-reorder -Wall -Wextra -Werror -Wno-unused-parameter \
  -nostartfiles $INCLUDES \
  -o "$OUT/lib/openfont.library" \
  "$HERE/library/openfont_lib.c" "$HERE/src/openfont.c" \
  $LIBS -lamiga -lgcc -Wl,-Map,"$OUT/lib/openfont.library.map"

echo "$OUT/lib/openfont.library ($(wc -c < "$OUT/lib/openfont.library") bytes)"
"$P/bin/m68k-amigaos-nm" -u "$OUT/lib/openfont.library"

"$CC" -m68040 -m68881 -mcrt=nix20 -O2 -Wall -Wextra -Werror \
  -noixemul -I"$HERE/include" \
  -o "$OUT/tests/OpenFontLibTest" "$HERE/tests/test_library.c"

echo "$OUT/tests/OpenFontLibTest ($(wc -c < "$OUT/tests/OpenFontLibTest") bytes)"
sha256sum "$OUT/lib/openfont.library" "$OUT/tests/OpenFontLibTest"
