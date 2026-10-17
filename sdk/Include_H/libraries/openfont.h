#ifndef LIBRARIES_OPENFONT_H
#define LIBRARIES_OPENFONT_H
/* openfont.library: fonts, shaping, glyphs and the font index for AmigaOS
 * 3.2 on the 68040, with its own font reader and rasteriser. Programs call
 * it in place of FreeType, HarfBuzz and fontconfig; see
 * Autodocs/openfont.doc.
 *
 * Version 1 is a preview: its calls and structures may still change. They
 * are frozen from version 2, once OpenFont's design tests pass.
 *
 * Copyright (c) 2026 AmigaChrome Team. OpenFont, MIT licence (LICENSE). */

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif

#define OPENFONTNAME "openfont.library"
#define OPENFONT_VERSION 1

/* A face (one font of a file) and a compiled glyph cache: opaque. */
struct OFFace;
struct OFCache;

/* A face's fallbacks: up to this many, tried in order for text the face
 * itself has no glyphs for. */
#define OF_MAX_FALLBACKS 8

/* One shaped glyph. Positions and advances are 26.6 fixed point pixels. */
struct OFGlyph {
    ULONG gl_Glyph;         /* in the face gl_Face names */
    ULONG gl_Cluster;       /* byte offset in the text of its cluster's first byte */
    LONG gl_XAdvance;
    LONG gl_YAdvance;
    LONG gl_XOffset;
    LONG gl_YOffset;
    UWORD gl_Face;          /* 0: the face shaped; n: its n-th fallback (OF_GetFallback) */
    UWORD gl_Flags;         /* OFGF_ below */
};                          /* 28 bytes */

#define OFGF_MISSING        0x0001  /* no face in the chain has it: glyph 0 of face 0 */
#define OFGF_UNSAFE_TO_BREAK 0x0002 /* breaking the run here changes the shaping */

/* One glyph's 8-bit coverage: 0 clear, 255 solid, one byte a pixel. */
struct OFBitmap {
    ULONG bm_Width;
    ULONG bm_Height;
    LONG bm_Left;           /* from the pen to the bitmap's left edge */
    LONG bm_Top;            /* from the baseline up to its top row */
    LONG bm_XAdvance;       /* 26.6 */
    ULONG bm_Stride;        /* bytes from one row to the next */
    UBYTE *bm_Pixels;       /* NULL for an empty glyph; free with OF_FreeBitmap */
};                          /* 28 bytes */

#define OFFF_ITALIC   0x0001
#define OFFF_FIXED    0x0002  /* every glyph the same width */
#define OFFF_SCALABLE 0x0004
#define OFFF_COLOR    0x0008

struct OFFaceInfo {
    char fi_Family[64];
    char fi_Style[64];
    UWORD fi_Weight;        /* 100 to 900: 400 regular, 700 bold */
    UWORD fi_Width;         /* 1 to 9: 5 normal */
    UWORD fi_Flags;         /* OFFF_ above */
    UWORD fi_UnitsPerEm;
    ULONG fi_PixelSize;
    ULONG fi_NumGlyphs;
    ULONG fi_NumFaces;      /* in its file: a collection holds several */
    LONG fi_Ascender;       /* 26.6, at the current size */
    LONG fi_Descender;      /* below the baseline: negative */
    LONG fi_Height;         /* baseline to baseline */
    ULONG fi_UnicodeRanges[4]; /* OS/2 ulUnicodeRange1-4, 0 when absent */
};                          /* 176 bytes */

/* Tags for OF_OpenFaceA and OF_FindFaceA */
#define OFA_Dummy     (TAG_USER + 0x4F460000)   /* 'OF' */
#define OFA_Index     (OFA_Dummy + 1)   /* ULONG: the face in a collection (0) */
#define OFA_Size      (OFA_Dummy + 2)   /* ULONG: pixel size to start at (16) */
#define OFA_Weight    (OFA_Dummy + 3)   /* ULONG: 100 to 900 (400); OF_FindFaceA */
#define OFA_Italic    (OFA_Dummy + 4)   /* BOOL (FALSE); OF_FindFaceA */
#define OFA_Fallbacks (OFA_Dummy + 5)   /* BOOL: add the default fallback chain (TRUE); OF_FindFaceA */
#define OFA_Error     (OFA_Dummy + 6)   /* LONG *: why it failed, an OFERR_ code */

/* Errors */
#define OFERR_OK        0
#define OFERR_ARGUMENT  -1
#define OFERR_MEMORY    -2
#define OFERR_IO        -3
#define OFERR_FONT      -4
#define OFERR_RANGE     -5
#define OFERR_BUFFER    -6
#define OFERR_CACHE     -7
#define OFERR_EXEC      -8
#define OFERR_NOTPROCESS -9   /* the caller is a task, and the call reads files */

#endif /* LIBRARIES_OPENFONT_H */
