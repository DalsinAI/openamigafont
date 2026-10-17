# OpenFont

OpenFont is the native modern font service for the Open Amiga stack.

It gives Amiga applications one API for font discovery, shaping, measurement,
glyph rasterisation and compiled glyph caches. The production architecture
removes direct application dependencies on FreeType, HarfBuzz and fontconfig:
those projects are reference/implementation technology behind OpenFont while
applications call OpenFont.

Current phase: **bootstrap core, with a fast cache**. The x86/ARM64 build still
uses FreeType and HarfBuzz inside OpenFont so the public API and the cache can
be tested. Glyph lookup no longer walks the file, and a range of glyphs can run
on extra cores or, with no cores board, as a plain loop on this CPU. The
resident `openfont.library` is still to come.

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

## Bootstrap API

`include/openfont/openfont.h` exposes:

- face open/close;
- pixel-size selection;
- UTF-8 shaping to positioned glyph runs;
- UTF-8 measurement;
- A8 glyph rasterisation;
- a portable compiled bitmap-cache writer.

The cache writer is intentionally useful before the full resident library:
common font sizes can be compiled once so the 68k does no font parsing or
rasterisation on a cache hit.

## Build on the x86 or ARM64 cores

Requirements: CMake, pkg-config, FreeType and HarfBuzz development packages.

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
`CC` overrides that. It compiles the sources for the 68040 only: `-m68040`,
`-O2 -Wall -Wextra -Werror`, soft float through `toolchain/m68k-soft-m68040.specs`
(libnix's soft maths, not libm881), so it also runs on an LC or EC040.

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

## Amiga target

The resident `openfont.library` will be added after this bootstrap core is
qualified. The target platform is AmigaOS 3.2.3 on a 68040 + FPU, with clean
fallbacks for ordinary 68k execution and accelerated AC090 leaves for shaping,
glyph rasterisation and atlas building.

## Compatibility

FreeType/HarfBuzz compatibility headers will be provided for the subset needed
by existing ports, but they are compatibility front ends. New Open software
uses OpenFont directly.

MIT licensed AmigaChrome Team code. FreeType and HarfBuzz retain their own upstream
licences when used to build the bootstrap/reference implementation.
