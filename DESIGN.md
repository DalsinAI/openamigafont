# OpenFont design

Version 0.1 — 8 October 2026

## 1. Decision

OpenFont is a first-class Amiga system library. It replaces direct
application-level use of FreeType, HarfBuzz and fontconfig in the Open stack.

The public dependency direction is:

```text
application -> openfont.library -> OpenGfx -> OpenGPU
```

FreeType/HarfBuzz may be used internally while the native implementation is
brought up, but their APIs are not the architecture.

## 2. Responsibilities

OpenFont owns font discovery and substitution, TTF/OTF and Amiga font
integration, Unicode fallback, shaping, metrics, variable-font coordinates,
colour glyph selection, memory caches and persistent compiled caches.

OpenFont does not own drawing state, clipping, layers or compositing. Those
belong to OpenGfx. It does not own GPU policy. OpenGfx/OpenGPU decide execution.

## 3. Compiled font instances

A compiled instance is keyed by face identity, variation coordinates, pixel
size and raster flags. It can contain character-to-glyph mapping, metrics,
position-independent glyph metrics and A8/MONO1/RGBA glyph bitmaps.

Common sizes may be precompiled by `OpenFontCompile`. Uncommon sizes are
created lazily and may be persisted. Cache files are endian-stable and include
a source checksum so stale font data is never reused.

## 4. Acceleration

The reference implementation runs as ordinary 68040 C/C++. AC090 may replace
high-value operations with host leaves while preserving identical results:

- shape a UTF-8 run;
- rasterise A8 glyph;
- rasterise mono glyph;
- rasterise colour glyph;
- build/copy an atlas.

Shaping is not a GPU operation. Positioned glyph runs are handed to OpenGfx,
which may batch their masks through OpenGPU.

## 5. OS 3.2.3 integration

OpenFont patches **nothing** in AmigaOS.

OpenGfx is the sole owner of the four Open-stack `graphics.library` patches: `Text`, `RectFill`, `BltBitMap` and `ScrollRaster`.

When the OpenGfx `Text` patch runs, it calls OpenFont for shaping, metrics and cached MONO1/A8/RGBA glyph data, then OpenGfx performs the actual drawing. OpenFont never installs a `Text`, `TextLength`, `TextExtent` or `TextFit` patch.

Modern applications call OpenFont directly when they need shaped measurement. Classic measurement calls remain OS-owned.

## 6. Testing

The reference gate includes:

- glyph-for-glyph HarfBuzz comparison;
- bitmap comparison with FreeType;
- cache round-trip and corruption tests;
- big-endian cache parsing;
- UTF-8, bidi, combining marks and fallback cases;
- resident-library ABI tests on OS 3.2.3;
- AC090 accelerated/reference differential tests.

Correctness is a prerequisite for acceleration.
