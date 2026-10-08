#ifndef LIBRARIES_OPENFONT_H
#define LIBRARIES_OPENFONT_H

#include <exec/types.h>
#include <openfont/openfont.h>

#define OPENFONTLIB_NAME "openfont.library"
#define OPENFONTLIB_VERSION 1
#define OPENFONTLIB_REVISION 0

struct OFFaceOpenRequest {
    OFContext *context;
    CONST_STRPTR path;
};

struct OFShapeRequest {
    OFFace *face;
    CONST_STRPTR utf8;
    ULONG bytes;
    OFGlyph *glyphs;
    ULONG capacity;
};

struct OFMeasureRequest {
    OFFace *face;
    CONST_STRPTR utf8;
    ULONG bytes;
    int64_t *advance_26_6;
};

struct OFRasterRequest {
    OFFace *face;
    ULONG glyph_id;
    OFBitmap *bitmap;
};

struct OFCacheGetRequest {
    OFCache *cache;
    UWORD pixel_size;
    ULONG codepoint;
    ULONG *glyph_id;
    OFBitmap *bitmap;
};

struct OFCacheMatchRequest {
    OFCache *cache;
    OFFace *face;
};

#endif
