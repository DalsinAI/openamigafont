# OpenFont

OpenFont is the native modern font service for the Open Amiga stack.

It gives Amiga applications one API for font discovery, shaping, measurement,
glyph rasterisation and compiled glyph caches. The production architecture
removes direct application dependencies on FreeType, HarfBuzz and fontconfig.
OpenFont reads and draws fonts with its own code, written from the published
specifications; HarfBuzz still shapes inside it until OpenFont's own shaper
replaces it.

Current phase: **the resident library, a preview**. `openfont.library` 1.1 is
OpenFont for AmigaOS 3.2 on the 68040, with its own font reader and
rasteriser and HarfBuzz 14.5.1 inside it. It finds fonts by family through its own font index, and a
face falls back to others for the characters it has no glyphs for. Its calls
may still change until version 2. The same core builds on the x86 and ARM64
cores, where the tests run. Glyph lookup in a compiled cache does not walk the
file, and a range of glyphs can run on extra cores or, with no cores board, as
a plain loop on this CPU.

## Architecture

```text
OpenBrowser / OpenWrite / OpenLayout / native applications
                         |
                     OpenFont
       discovery | shaping | metrics | glyph cache
                         |
                     OpenGfx
                         |
                     OpenGPU
          68040 | AC090 | x86/ARM64 | GPU
```

OpenFont owns text semantics. OpenGfx paints positioned glyph runs. OpenGPU is
only the execution/acceleration layer and does not understand Unicode shaping.

## The core

`include/openfont/openfont.h` is the core the library is built from, and what
the host tests call:

- faces opened from a file or from memory, one face of a collection, counted
  so a face can be shared;
- pixel-size selection, and a face's names and metrics (`of_face_info`);
- UTF-8 shaping to positioned glyph runs, and measurement;
- fallback: up to eight faces after a face, each cluster it has no glyphs for
  shaped by the first that has them, and each glyph marked with the face it
  came from;
- A8 glyph rasterisation;
- a portable compiled bitmap-cache writer and reader.

### The reader and the rasteriser

[src/of_font.h](src/of_font.h) is OpenFont's own font reader and outline
rasteriser, written from the OpenType specification and Adobe's CFF and
Type 2 charstring specifications. No FreeType code is in them, copied or
translated.

- **Fonts:** TrueType and OpenType files and collections (`.ttf`, `.otf`,
  `.ttc`, `.otc`). Tables `cmap` (formats 0, 4, 6, 12, 13), `head`, `hhea`,
  `hmtx`, `maxp`, `loca`/`glyf` (simple and composite glyphs), `CFF `
  (Type 2 charstrings, subroutines, CID-keyed fonts), `name`, `OS/2` and
  `post`. A font without outlines it reads (CFF2, bitmaps only) still opens,
  for its cmap and metrics.
- **Rasteriser** ([src/of_raster.c](src/of_raster.c)): quadratic and cubic
  outlines to A8 coverage with the non-zero rule, exact area coverage at 256
  subpixels a pixel, curves cut into lines no more than 1/16 pixel from them.
  No hinting. Integer arithmetic only, the same to the bit on the host and the
  68040, and a pure function of an outline, so it can move to OpenGPU as it is.
- **Metrics:** advances and the line metrics in 26.6 at a pixel size, not
  rounded to whole pixels; a glyph's bitmap is its outline's control box
  rounded out.

Every read is checked against the file's end, and a damaged font gives
`OF_ERR_FONT`, never a crash: `test_reader` opens and draws fonts cut short
and with bytes changed, under the address and undefined-behaviour sanitisers
on the host.

Glyphs are unhinted: the pixels follow the outline exactly at every size. That
is a change from the hinted FreeType output before.

[src/openfont_index.h](src/openfont_index.h) is the font index: the faces of
a few folders by family, style, weight and slant, kept in a text file and read
again only when a folder's date changes; aliases (`Arial` to Liberation Sans,
`monospace` to DejaVu Sans Mono, the user's own); and the default fallback
chain.

Files are read and folders listed through
[src/openfont_io.h](src/openfont_io.h): stdio and POSIX on the host,
dos.library in the library.

The cache writer is useful before anything else: common font sizes can be
compiled once so the 68k does no font parsing or rasterisation on a cache
hit.

## Build on the x86 or ARM64 cores

Requirements: CMake, and HarfBuzz: its development package (pkg-config), or
its release's sources (`-DOPENFONT_HARFBUZZ_SOURCE=/path/to/harfbuzz-14.5.1`,
built in). FreeType is not needed.
The index test needs the DejaVu and Liberation fonts (`fonts-dejavu-core`,
`fonts-liberation2` on Debian and Ubuntu), and is left out without them.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The reference gate compares the reader and rasteriser with FreeType 2.14.3,
unhinted, when given a FreeType tree built in its own `build/` folder (only
for this test; nothing of FreeType's is linked into OpenFont):

```sh
cmake -S . -B build -DOPENFONT_FREETYPE_REF=/path/to/freetype-2.14.3
build/compare_freetype FONT...
```

For each font, face and size (9, 12, 16, 24 and 48 pixels) it must match
FreeType exactly in the cmap (every codepoint to U+2FFFF), the family and
style names, the bold, italic, fixed-pitch and colour flags, the line metrics
and every glyph's advance, bitmap size and place; and each pixel within 64 of
255, the mean difference within 2.5. The pixels differ because the two cut
curves into lines differently: measured against a near-exact raster (1/256
pixel), OpenFont is the closer, FreeType's own error reaching 57 in a pixel
and a mean of 2.6 on CFF fonts at 9 pixels.

On 17 October 2026 it passed on 63 font files of this PC: DejaVu and
Liberation (TrueType), the URW base 35 fonts (CFF), and the Noto CJK
collections (CID-keyed CFF, two faces of each), 335 font sizes.

Compile common sizes:

```sh
./build/openfontcompile /path/to/font.ttf font.ofc 9 10 11 12 14 16 18 24 32
```

The file format is big-endian. Version 3 (what `openfontcompile` writes now)
carries a size directory, so a glyph is a couple of loads. Version 1 and 2
caches still open.

## Fast path: 68k fallback and OpenMulticore

`of_cache_fill_range` fills one `OFRangeSlot` per codepoint. The slot is a
multiple of 16 bytes, so it is a legal OpenMulticore slice: index `i` writes
only `slots[i]`. The worker does not call the OS and does not allocate.

- No `OFExec`, or fewer glyphs than the break-even (8): the worker runs in a
  loop on this CPU. That is the 68k fallback, and it is the reference.
- An `OFExec` runs the same worker. On Amiga, [src/openfont_omc.c](src/openfont_omc.c)
  passes it to `OMC_RunRange` (`openmulticore.library` 0.3 or later). A cores
  board spreads the indices. No board, an older library, or a refused job falls
  back to the loop. The call does not set `OMCF_BOARD`, so a plain A1200 still
  gets the glyphs.

A cache hit does no rasterisation on either path. A glyph bigger than
`OF_RANGE_PIXELS` (4096 bytes) stays a per-slot `OF_ERR_BUFFER`;
`of_cache_get_a8` on this CPU still returns it. Shaping of one run stays on
this CPU. Independent ranges are what the extra cores are for.

The cache bytes and its index are one Fast RAM block (`of_cache_memory` in
the internal header). Grant that block read-only and the slots write. Leave
the job permissive.

## 68k build

[amiga/build.sh](amiga/build.sh) builds the range test with OpenAmigaGCC
**16.2.0-open2**, the compiler in the `os32-gcc16` stove. Nothing in the
OpenAmigaGCC releases is named 16.2.9-open2. The script looks in
`~/AmigaChrome/stoves/os32-gcc16`, then `/opt/openamigagcc-16.2.0-open2-x86_64-linux`.
`CC` overrides that. It compiles the sources for the 68040 only: `-m68040`,
`-O2 -Wall -Wextra -Werror`, soft float through `toolchain/m68k-soft-m68040.specs`
(libnix's soft maths, not libm881), so it also runs on an LC or EC040.

It also builds `openfontreader-68040`, the reader and rasteriser's test
(`tests/test_reader.c`). It prints a fingerprint of every glyph's bitmap, place
and advance at 12 and 16 pixels for each font. On lab t4-a (OS 3.2.3, 17 Oct
2026) it printed the host's fingerprints for DejaVu Sans, Liberation Serif
Italic and Nimbus Sans (CFF), and `test_reader: ok`.

`src/openfont_omc.c` is not in that build unless `OMC_INC` points at a
directory that contains the NDK and `libraries/openmulticore.h`. The stove has
those. The compiler release tarball does not, so the script does not invent
them. With `OMC_INC` set it also builds `openfontomc-68040`. It opens
`openmulticore.library`, checks the fallbacks, and runs the same eight glyphs
through `OMC_RunRange`.

On lab `openfont` (14 Oct 2026), a 68000 build of that program (before builds
became 68040 only, 17 Oct 2026), built with OpenAmigaGCC
16.2.0-open2 printed `OPENFONT_OMC PASS ver=0 rev=4 where=2`. Library 0.4,
worker on the cores board (`OMCW_BOARD`). A NULL base, a revision-2 library
and a count below the break-even still match the CPU loop.

## openfont.library

The resident library is for AmigaOS 3.2 on a 68040 with its FPU, and only
that: it does not open on another CPU, or on a 68040 without its FPU (an
LC040). Later, AC090 leaves may take shaping,
glyph rasterisation and atlas building, with the same results.

It holds the core, the index, OpenFont's reader and rasteriser, and HarfBuzz,
about 1.0 MB. Inside it:

- one lock: calls from several tasks are taken one at a time;
- its own 64 KB stack, so callers need no more than their usual;
- one memory pool for everything HarfBuzz and the core allocate,
  freed whole when the library is expunged;
- no C startup and no stdio; files through dos.library, only for processes
  (a plain task gets `OFERR_NOTPROCESS`);
- no patches to the system.

### Fonts

`OF_FindFace` looks in `FONTS:`, `LIBS:WebBrowser/Fonts`, each folder listed
one a line in `ENVARC:OpenFont/FontDirs`, and folders a program adds. It keeps
what it found in `ENVARC:OpenFont/FontIndex`.

`ENVARC:OpenFont/Aliases` holds the user's aliases, one a line
(`Name=Family,Family`), before OpenFont's own. `ENVARC:OpenFont/Fallbacks` is
the default fallback chain (`DejaVu Sans,Noto Sans,Liberation Sans,DejaVu Sans
Mono` without it).

### Using it

```c
#include <proto/openfont.h>

struct Library *OpenFontBase = OpenLibrary("openfont.library", 1);
struct OFFace *face = OF_FindFace("sans-serif", OFA_Size, 16, TAG_END);
struct OFGlyph glyphs[64];
ULONG n = OF_ShapeText(face, text, strlen(text), glyphs, 64);
/* glyphs[i].gl_Face is the face of the chain each glyph came from:
   OF_GetFallback(face, glyphs[i].gl_Face) draws it */
OF_CloseFace(face);
CloseLibrary(OpenFontBase);
```

[sdk](sdk) is what a program needs: the SFD, the C and assembler headers
(`Include_H`, `Include_I`), the FD file, and the autodoc
([sdk/Autodocs/openfont.doc](sdk/Autodocs/openfont.doc)). The varargs calls
`OF_OpenFace` and `OF_FindFace` are in `libopenfont.a`, which the build makes
from [sdk/stubs](sdk/stubs). Every call does its work on the library's stack.

Revision 1 (1.1) adds two calls for engines that make their own transforms,
such as our bullet.library (amigachrome-guest `libraries/openbullet`), which
draws diskfont.library's outline fonts through OpenFont:
`OF_RasterGlyphMatrix` draws a glyph through a 2 x 2 matrix (a size, a
stretch, a shear, a rotation) and `OF_GlyphAdvance` gives its advance in font
units. Check `lib_Revision` before calling them.

### Building it

[amiga/build-library.sh](amiga/build-library.sh) builds it with OpenAmigaGCC
16.2.0-open2 (the `os32-gcc16` stove's compiler), `-m68040 -m68881`:

```sh
NDK=/path/to/OpenNDK/Include_H sh amiga/build-library.sh
```

- `CC`, `CXX`: the compilers, when not in the stove or under `/opt`;
- `NDK`: AmigaOS headers, when the compiler has none of its own;
- `TARBALLS`: where the HarfBuzz release is kept. It is checked against
  [amiga/SOURCES](amiga/SOURCES), and fetched when missing. Nothing upstream
  is ever committed.

It writes `build/amiga/openfont.library`, its link map, `libopenfont.a` and
`OpenFontLibTest`. It fails when the library leaves a symbol undefined (libnix
gives it only stateless functions: memory, strings, `qsort`, `setjmp`), or
when its code does not start with `start()`. [build-amiga.sh](build-amiga.sh)
is the same build for a Cradle stove, which sets `STOVE` and `OUT`.

### Checking it

Copy `openfont.library` to `LIBS:`, then, from a shell with a 4 KB stack:

```text
OpenFontLibTest FONT/A,FONTDIR/A,CACHE
```

`FONT` is `DejaVuSans.ttf`, `FONTDIR` a folder with the DejaVu and Liberation
fonts, and `CACHE` an `.ofc` made from `FONT` on the host. It shapes and
rasterises `FONT` at 16 pixels and expects the host's glyphs, advance and
pixels byte for byte (the host's `test_openfont` prints the same hash), then checks the
index, the aliases, the snowman from a fallback and `OFERR_NOTPROCESS` from a
plain task. It ends with `OPENFONTLIB PASS`. It has not yet passed on a lab:
on lab t4-a (17 Oct 2026) it stops before its first line of output, with this
library and with the FreeType one before it alike.

## Compatibility

FreeType/HarfBuzz compatibility headers will be provided for the subset needed
by existing ports, but they are compatibility front ends. New Open software
uses OpenFont directly.

MIT licensed AmigaChrome Team code. HarfBuzz keeps its own upstream licence
(MIT) in `openfont.library`, until OpenFont's own shaper replaces it; the
build copies it to `build/amiga/licences`, and it goes wherever the library
is copied. FreeType is no longer in OpenFont; the reference test builds
against a separate FreeType tree.
