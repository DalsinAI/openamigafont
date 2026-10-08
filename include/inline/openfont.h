#ifndef INLINE_OPENFONT_H
#define INLINE_OPENFONT_H

#ifndef __INLINE_MACROS_H
#include <inline/macros.h>
#endif

#ifndef OPENFONT_BASE_NAME
#define OPENFONT_BASE_NAME OpenFontBase
#endif

#define OF_LibVersion() LP0(0x1e, ULONG, OF_LibVersion, , OPENFONT_BASE_NAME)
#define OF_ContextCreate() LP0(0x24, OFContext *, OF_ContextCreate, , OPENFONT_BASE_NAME)
#define OF_ContextDestroy(ctx) LP1NR(0x2a, OF_ContextDestroy, OFContext *, ctx, a0, , OPENFONT_BASE_NAME)
#define OF_FaceOpen(req) LP1(0x30, OFFace *, OF_FaceOpen, struct OFFaceOpenRequest *, req, a0, , OPENFONT_BASE_NAME)
#define OF_FaceClose(face) LP1NR(0x36, OF_FaceClose, OFFace *, face, a0, , OPENFONT_BASE_NAME)
#define OF_FaceSetPixelSize(face,pixels) LP2(0x3c, LONG, OF_FaceSetPixelSize, OFFace *, face, a0, ULONG, pixels, d0, , OPENFONT_BASE_NAME)
#define OF_ShapeUTF8(req) LP1(0x42, ULONG, OF_ShapeUTF8, struct OFShapeRequest *, req, a0, , OPENFONT_BASE_NAME)
#define OF_MeasureUTF8(req) LP1(0x48, LONG, OF_MeasureUTF8, struct OFMeasureRequest *, req, a0, , OPENFONT_BASE_NAME)
#define OF_RasterGlyphA8(req) LP1(0x4e, LONG, OF_RasterGlyphA8, struct OFRasterRequest *, req, a0, , OPENFONT_BASE_NAME)
#define OF_BitmapRelease(bitmap) LP1NR(0x54, OF_BitmapRelease, OFBitmap *, bitmap, a0, , OPENFONT_BASE_NAME)
#define OF_CacheOpen(path) LP1(0x5a, OFCache *, OF_CacheOpen, CONST_STRPTR, path, a0, , OPENFONT_BASE_NAME)
#define OF_CacheClose(cache) LP1NR(0x60, OF_CacheClose, OFCache *, cache, a0, , OPENFONT_BASE_NAME)
#define OF_CacheMatchesFace(req) LP1(0x66, LONG, OF_CacheMatchesFace, struct OFCacheMatchRequest *, req, a0, , OPENFONT_BASE_NAME)
#define OF_CacheGetA8(req) LP1(0x6c, LONG, OF_CacheGetA8, struct OFCacheGetRequest *, req, a0, , OPENFONT_BASE_NAME)

#endif
