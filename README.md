# OpenFont

OpenFont is the native modern font service for the Open Amiga stack.

It gives Amiga applications one API for font discovery, shaping, measurement,
glyph rasterisation and compiled glyph caches. The production architecture
removes direct application dependencies on FreeType, HarfBuzz and fontconfig:
those projects are reference/implementation technology behind OpenFont while
applications call OpenFont.

Current phase: **bootstrap core**. The host build deliberately uses upstream
FreeType and HarfBuzz internally so the public OpenFont API and cache format can
be tested before the Amiga resident library and AC090 leaves are wired in.

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
               68040 | AC090 | GPU
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

## Build on the host

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

The file format is big-endian and fixed-width so the same cache is consumable
by a 68k Amiga and by the host-side test/reference tools.

## Amiga target

The resident `openfont.library` will be added after this bootstrap core is
qualified. The target platform is AmigaOS 3.2.3 on a 68040 + FPU, with clean
fallbacks for ordinary 68k execution and accelerated AC090 leaves for shaping,
glyph rasterisation and atlas building.

## Compatibility

FreeType/HarfBuzz compatibility headers will be provided for the subset needed
by existing ports, but they are compatibility front ends. New Open software
uses OpenFont directly.

MIT licensed Dalsin code. FreeType and HarfBuzz retain their own upstream
licences when used to build the bootstrap/reference implementation.
