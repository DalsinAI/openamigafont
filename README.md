# OpenFont

OpenFont is the native modern font service for the Open Amiga stack.

It gives Amiga applications one API for font discovery, shaping, measurement,
glyph rasterisation and compiled glyph caches. The production architecture
removes direct application dependencies on FreeType, HarfBuzz and fontconfig:
those projects are reference/implementation technology behind OpenFont while
applications call OpenFont.

Current phase: **the resident library, a preview**. `openfont.library` 1.0 is
OpenFont for AmigaOS 3.2 on the 68040, with FreeType 2.14.3 and HarfBuzz
14.5.1 inside it. It finds fonts by family through its own font index, and a
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

Requirements: CMake, pkg-config, FreeType and HarfBuzz development packages.
The index test needs the DejaVu and Liberation fonts (`fonts-dejavu-core`,
`fonts-liberation2` on Debian and Ubuntu), and is left out without them.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

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
`CC` overrides that. It compiles the same sources three times: `-m68000`,
`-m68020` and `-m68040`, `-O2 -Wall -Wextra -Werror`. The 68000 program is the
fallback. The other two are the same worker.

`src/openfont_omc.c` is not in that build unless `OMC_INC` points at a
directory that contains the NDK and `libraries/openmulticore.h`. The stove has
those. The compiler release tarball does not, so the script does not invent
them. With `OMC_INC` set it also builds `openfontomc-68000` and
`openfontomc-68040`. Both open `openmulticore.library`, check the fallbacks,
and run the same eight glyphs through `OMC_RunRange`. The 68040 binary is the
target. The 68000 binary is the fallback.

On lab `openfont` (14 Oct 2026), that 68000 program built with OpenAmigaGCC
16.2.0-open2 printed `OPENFONT_OMC PASS ver=0 rev=4 where=2`. Library 0.4,
worker on the cores board (`OMCW_BOARD`). A NULL base, a revision-2 library
and a count below the break-even still match the CPU loop.

## openfont.library

The resident library is for AmigaOS 3.2 on a 68040 with its FPU, and only
that: it does not open on another CPU, or on a 68040 without its FPU (an
LC040). Later, AC090 leaves may take shaping,
glyph rasterisation and atlas building, with the same results.

It holds the core, the index, FreeType (TrueType and CFF outlines, their
hinting, both rasterisers) and HarfBuzz, about 1.4 MB. Inside it:

- one lock: calls from several tasks are taken one at a time;
- its own 64 KB stack, so callers need no more than their usual;
- one memory pool for everything FreeType, HarfBuzz and the core allocate,
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

### Building it

[amiga/build-library.sh](amiga/build-library.sh) builds it with OpenAmigaGCC
16.2.0-open2 (the `os32-gcc16` stove's compiler), `-m68040 -m68881`:

```sh
NDK=/path/to/OpenNDK/Include_H sh amiga/build-library.sh
```

- `CC`, `CXX`: the compilers, when not in the stove or under `/opt`;
- `NDK`: AmigaOS headers, when the compiler has none of its own;
- `TARBALLS`: where the FreeType and HarfBuzz releases are kept. Each is
  checked against [amiga/SOURCES](amiga/SOURCES), and fetched when missing.
  Nothing upstream is ever committed;
- `FREETYPE_GIT`: a clone of FreeType's Git mirror at the commit SOURCES pins,
  when the release can't be fetched.

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
plain task. It ends with `OPENFONTLIB PASS`. It has not yet run on a lab.

## Compatibility

FreeType/HarfBuzz compatibility headers will be provided for the subset needed
by existing ports, but they are compatibility front ends. New Open software
uses OpenFont directly.

MIT licensed AmigaChrome Team code. FreeType and HarfBuzz retain their own upstream
licences when used to build the bootstrap/reference implementation, and in
`openfont.library`, which carries FreeType under the FreeType License (FTL)
and HarfBuzz under its MIT licence. The build copies those licences to
`build/amiga/licences`; they go wherever the library is copied.

Portions of openfont.library are copyright © 2026 The FreeType Project
(https://freetype.org). All rights reserved.
