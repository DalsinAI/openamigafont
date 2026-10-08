/* openfont.library 1.0 bootstrap
 * Copyright (c) 2026 Dalsin Limited. MIT.
 *
 * OpenFont owns font/shaping/raster/cache services. It patches no OS vector.
 */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>

#include "../include/libraries/openfont.h"

#define REG(r, decl) register decl __asm(#r)
#define LIB_VERSION OPENFONTLIB_VERSION
#define LIB_REVISION OPENFONTLIB_REVISION

struct OpenFontLibBase {
    struct Library lib;
    BPTR seglist;
};

struct ExecBase *SysBase;

int start(void) { return -1; }

static const char lib_name[] = OPENFONTLIB_NAME;
static const char lib_id[] =
    "openfont.library 1.0 (8.10.2026) OpenFont 0.2, Dalsin Limited\r\n";

static struct Library *lib_init(REG(d0, struct OpenFontLibBase *base),
                                REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct OpenFontLibBase *base));
static BPTR lib_close(REG(a6, struct OpenFontLibBase *base));
static BPTR lib_expunge(REG(a6, struct OpenFontLibBase *base));
static ULONG lib_null(void);

static ULONG OF_LibVersion(REG(a6, struct OpenFontLibBase *base));
static OFContext *OF_ContextCreate(REG(a6, struct OpenFontLibBase *base));
static void OF_ContextDestroy(REG(a0, OFContext *ctx), REG(a6, struct OpenFontLibBase *base));
static OFFace *OF_FaceOpen(REG(a0, struct OFFaceOpenRequest *r), REG(a6, struct OpenFontLibBase *base));
static void OF_FaceClose(REG(a0, OFFace *face), REG(a6, struct OpenFontLibBase *base));
static LONG OF_FaceSetPixelSize(REG(a0, OFFace *face), REG(d0, ULONG pixels), REG(a6, struct OpenFontLibBase *base));
static ULONG OF_ShapeUTF8(REG(a0, struct OFShapeRequest *r), REG(a6, struct OpenFontLibBase *base));
static LONG OF_MeasureUTF8(REG(a0, struct OFMeasureRequest *r), REG(a6, struct OpenFontLibBase *base));
static LONG OF_RasterGlyphA8(REG(a0, struct OFRasterRequest *r), REG(a6, struct OpenFontLibBase *base));
static void OF_BitmapRelease(REG(a0, OFBitmap *bitmap), REG(a6, struct OpenFontLibBase *base));
static OFCache *OF_CacheOpen(REG(a0, CONST_STRPTR path), REG(a6, struct OpenFontLibBase *base));
static void OF_CacheClose(REG(a0, OFCache *cache), REG(a6, struct OpenFontLibBase *base));
static LONG OF_CacheMatchesFace(REG(a0, struct OFCacheMatchRequest *r), REG(a6, struct OpenFontLibBase *base));
static LONG OF_CacheGetA8(REG(a0, struct OFCacheGetRequest *r), REG(a6, struct OpenFontLibBase *base));

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)OF_LibVersion,
    (APTR)OF_ContextCreate, (APTR)OF_ContextDestroy,
    (APTR)OF_FaceOpen, (APTR)OF_FaceClose, (APTR)OF_FaceSetPixelSize,
    (APTR)OF_ShapeUTF8, (APTR)OF_MeasureUTF8,
    (APTR)OF_RasterGlyphA8, (APTR)OF_BitmapRelease,
    (APTR)OF_CacheOpen, (APTR)OF_CacheClose,
    (APTR)OF_CacheMatchesFace, (APTR)OF_CacheGetA8,
    (APTR)-1
};

static const struct {
    ULONG size;
    const APTR *vectors;
    APTR data;
    APTR init;
} lib_inittable = {
    sizeof(struct OpenFontLibBase), lib_vectors, NULL, (APTR)lib_init
};

const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0,
    (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable
};

static struct Library *lib_init(REG(d0, struct OpenFontLibBase *base),
                                REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Revision = LIB_REVISION;
    return &base->lib;
}

static struct Library *lib_open(REG(a6, struct OpenFontLibBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}

static BPTR lib_close(REG(a6, struct OpenFontLibBase *base))
{
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct OpenFontLibBase *base))
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize,
            base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

static ULONG lib_null(void) { return 0; }

static ULONG OF_LibVersion(REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    return (OPENFONTLIB_VERSION << 16) | OPENFONTLIB_REVISION;
}

static OFContext *OF_ContextCreate(REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    return of_context_create();
}

static void OF_ContextDestroy(REG(a0, OFContext *ctx), REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    of_context_destroy(ctx);
}

static OFFace *OF_FaceOpen(REG(a0, struct OFFaceOpenRequest *r), REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    if (!r || !r->context || !r->path)
        return NULL;
    return of_face_open(r->context, (const char *)r->path);
}

static void OF_FaceClose(REG(a0, OFFace *face), REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    of_face_close(face);
}

static LONG OF_FaceSetPixelSize(REG(a0, OFFace *face), REG(d0, ULONG pixels), REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    return (LONG)of_face_set_pixel_size(face, pixels);
}

static ULONG OF_ShapeUTF8(REG(a0, struct OFShapeRequest *r), REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    if (!r || !r->face || !r->utf8)
        return 0;
    return (ULONG)of_shape_utf8(r->face, (const char *)r->utf8, r->bytes,
                                r->glyphs, r->capacity);
}

static LONG OF_MeasureUTF8(REG(a0, struct OFMeasureRequest *r), REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    if (!r || !r->face || !r->utf8 || !r->advance_26_6)
        return OF_ERR_ARGUMENT;
    *r->advance_26_6 = of_measure_utf8(r->face, (const char *)r->utf8, r->bytes);
    return OF_OK;
}

static LONG OF_RasterGlyphA8(REG(a0, struct OFRasterRequest *r), REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    if (!r || !r->face || !r->bitmap)
        return OF_ERR_ARGUMENT;
    return (LONG)of_raster_glyph_a8(r->face, r->glyph_id, r->bitmap);
}

static void OF_BitmapRelease(REG(a0, OFBitmap *bitmap), REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    of_bitmap_release(bitmap);
}

static OFCache *OF_CacheOpen(REG(a0, CONST_STRPTR path), REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    return path ? of_cache_open((const char *)path) : NULL;
}

static void OF_CacheClose(REG(a0, OFCache *cache), REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    of_cache_close(cache);
}

static LONG OF_CacheMatchesFace(REG(a0, struct OFCacheMatchRequest *r), REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    if (!r)
        return 0;
    return of_cache_matches_face(r->cache, r->face) ? 1 : 0;
}

static LONG OF_CacheGetA8(REG(a0, struct OFCacheGetRequest *r), REG(a6, struct OpenFontLibBase *base))
{
    (void)base;
    if (!r || !r->cache || !r->bitmap)
        return OF_ERR_ARGUMENT;
    return (LONG)of_cache_get_a8(r->cache, r->pixel_size, r->codepoint,
                                 r->glyph_id, r->bitmap);
}
