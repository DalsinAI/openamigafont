/* OpenFontLibTest: openfont.library's calls on AmigaOS 3.2, for a lab.
 *
 *   OpenFontLibTest FONT/A,FONTDIR/A,CACHE
 *
 * FONT is DejaVuSans.ttf: the shaping and raster hash must be the one the
 * host's tests/test_openfont.c prints for it at 16 pixels (EXPECT below),
 * since both run FreeType 2.14.3 and HarfBuzz 14.5.1. FONTDIR holds DejaVu
 * and Liberation, for the index, the aliases and fallback. CACHE is an
 * .ofc file openfontcompile made on the host from FONT (sizes 9 12 16 24).
 *
 * Start it with a 4 KB stack: the library works on its own.
 *
 * Prints one line per check and "OPENFONTLIB PASS" or "OPENFONTLIB FAIL n".
 *
 * Copyright (c) 2026 AmigaChrome Team. OpenFont, MIT licence (LICENSE). */
#include <exec/types.h>
#include <exec/tasks.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <libraries/openfont.h>
#include <proto/openfont.h>

#include <stdio.h>
#include <string.h>

/* tests/test_openfont.c on the host, DejaVuSans.ttf at 16 pixels */
#define EXPECT_GLYPHS 13
#define EXPECT_ADVANCE 8155
#define EXPECT_HASH 0x79cfa8e6UL

struct Library *OpenFontBase;
static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
}

static ULONG hash_bytes(ULONG h, const UBYTE *p, ULONG n)
{
    while (n--) {
        h ^= *p++;
        h *= 16777619UL;
    }
    return h;
}

/* a plain task, not a process: the library must refuse to read files */
static volatile LONG task_result = 1, task_error = 0;
static struct Task *parent;
static void plain_task(void)
{
    LONG err = 0;
    struct OFFace *f = OF_FindFace((CONST_STRPTR)"DejaVu Sans", OFA_Error, (ULONG)&err, TAG_END);
    task_result = f != NULL;
    task_error = err;
    if (f)
        OF_CloseFace(f);
    Signal(parent, SIGBREAKF_CTRL_F);
}

int main(void)
{
    static const char text[] = "Office AVWa fi \xC3\xA9";
    static const char snow[] = "snow \xE2\x98\x83!";
    LONG args[3] = { 0, 0, 0 };
    struct RDArgs *rda;
    struct OFFace *face, *found;
    struct OFFaceInfo info;
    struct OFGlyph glyphs[64];
    struct OFBitmap bm;
    ULONG n, i, hash = 2166136261UL;
    LONG advance, err;

    rda = ReadArgs((CONST_STRPTR)"FONT/A,FONTDIR/A,CACHE", args, NULL);
    if (!rda) {
        printf("usage: OpenFontLibTest FONT/A,FONTDIR/A,CACHE\n");
        return 20;
    }
    OpenFontBase = OpenLibrary((CONST_STRPTR)"openfont.library", 1);
    check(OpenFontBase != NULL, "openfont.library 1 opens");
    if (!OpenFontBase) {
        FreeArgs(rda);
        printf("OPENFONTLIB FAIL 1\n");
        return 20;
    }
    printf("     openfont.library %u.%u, OpenFont %lu.%lu\n", OpenFontBase->lib_Version,
           OpenFontBase->lib_Revision, OF_Version() >> 16, OF_Version() & 0xFFFF);
    check(sizeof(struct OFGlyph) == 28 && sizeof(struct OFBitmap) == 28 && sizeof(struct OFFaceInfo) == 176,
          "structure sizes match the library's");

    err = 99;
    face = OF_OpenFace((CONST_STRPTR)args[0], OFA_Size, 16, OFA_Error, (ULONG)&err, TAG_END);
    check(face != NULL && err == OFERR_OK, "OF_OpenFace on the font");
    if (face) {
        check(OF_GetFaceInfo(face, &info) == OFERR_OK, "OF_GetFaceInfo");
        printf("     %s %s, weight %u, %lu glyphs, ascender %ld\n", info.fi_Family, info.fi_Style,
               info.fi_Weight, info.fi_NumGlyphs, info.fi_Ascender);
        check(!strcmp(info.fi_Family, "DejaVu Sans") && info.fi_Weight == 400 &&
              (info.fi_Flags & OFFF_SCALABLE) && info.fi_PixelSize == 16, "the face's information");

        n = OF_ShapeText(face, (CONST_STRPTR)text, strlen(text), NULL, 0);
        check(n == EXPECT_GLYPHS, "OF_ShapeText counts the glyphs");
        n = OF_ShapeText(face, (CONST_STRPTR)text, strlen(text), glyphs, 64);
        advance = OF_MeasureText(face, (CONST_STRPTR)text, strlen(text));
        for (i = 0; i < n && i < 64; ++i) {
            hash = hash_bytes(hash, (const UBYTE *)&glyphs[i].gl_Glyph, 4);
            hash = hash_bytes(hash, (const UBYTE *)&glyphs[i].gl_XAdvance, 4);
        }
        check(advance == EXPECT_ADVANCE, "OF_MeasureText gives the host's advance");
        memset(&bm, 0, sizeof bm);
        check(OF_RasterGlyph(face, glyphs[0].gl_Glyph, &bm) == OFERR_OK && bm.bm_Pixels, "OF_RasterGlyph");
        if (bm.bm_Pixels)
            hash = hash_bytes(hash, bm.bm_Pixels, bm.bm_Width * bm.bm_Height);
        printf("     glyphs=%lu advance=%ld first=%lu bitmap=%lux%lu hash=%08lx\n", n, advance,
               glyphs[0].gl_Glyph, bm.bm_Width, bm.bm_Height, hash);
        check(hash == EXPECT_HASH, "shaping and pixels match the host byte for byte");
        OF_FreeBitmap(&bm);
        check(OF_CharIndex(face, 'A') != 0 && OF_CharIndex(face, 0x10FFFD) == 0, "OF_CharIndex");

        if (args[2]) {
            struct OFCache *c = OF_OpenCache((CONST_STRPTR)args[2]);
            ULONG gid = 0;
            check(c != NULL, "OF_OpenCache on the host's .ofc");
            if (c) {
                check(OF_CacheMatchesFace(c, face), "the cache was compiled from this face");
                memset(&bm, 0, sizeof bm);
                check(OF_CacheGetGlyph(c, 16, 'O', &bm, &gid) == OFERR_OK && gid == glyphs[0].gl_Glyph &&
                      bm.bm_Pixels, "OF_CacheGetGlyph: the glyph, with no rasterising");
                OF_FreeBitmap(&bm);
                OF_CloseCache(c);
            }
        }
        OF_CloseFace(face);
    }

    /* the index over FONTDIR, and fallback */
    check(OF_AddFontDir((CONST_STRPTR)args[1]) == OFERR_OK, "OF_AddFontDir");
    check(OF_RescanFonts() == OFERR_OK, "OF_RescanFonts");
    found = OF_FindFace((CONST_STRPTR)"Arial", OFA_Size, 16, TAG_END);
    check(found != NULL, "OF_FindFace(\"Arial\")");
    if (found) {
        OF_GetFaceInfo(found, &info);
        printf("     Arial -> %s %s\n", info.fi_Family, info.fi_Style);
        check(!strcmp(info.fi_Family, "Liberation Sans"), "Arial is Liberation Sans");
        check(OF_GetFallback(found, 1) != NULL, "a default fallback chain");
        n = OF_ShapeText(found, (CONST_STRPTR)snow, strlen(snow), glyphs, 64);
        for (i = 0; i < n && i < 64; ++i)
            if (glyphs[i].gl_Cluster == 5)
                break;
        check(n == 7 && i < n && glyphs[i].gl_Face != 0 && glyphs[i].gl_Glyph != 0 &&
              !(glyphs[i].gl_Flags & OFGF_MISSING), "the snowman comes from a fallback");
        if (i < n) {
            struct OFFace *fb = OF_GetFallback(found, glyphs[i].gl_Face);
            memset(&bm, 0, sizeof bm);
            check(fb && OF_RasterGlyph(fb, glyphs[i].gl_Glyph, &bm) == OFERR_OK && bm.bm_Width,
                  "the fallback draws it");
            OF_FreeBitmap(&bm);
        }
        OF_CloseFace(found);
    }
    found = OF_FindFace((CONST_STRPTR)"monospace", OFA_Weight, 700, TAG_END);
    if (found) {
        OF_GetFaceInfo(found, &info);
        printf("     monospace 700 -> %s %s\n", info.fi_Family, info.fi_Style);
        check(!strcmp(info.fi_Family, "DejaVu Sans Mono") && info.fi_Weight == 700, "monospace bold");
        OF_CloseFace(found);
    } else {
        check(0, "OF_FindFace(\"monospace\")");
    }

    /* a plain task gets OFERR_NOTPROCESS, and the library stays usable */
    {
        struct Task *t;
        APTR stack = AllocMem(4096, MEMF_CLEAR);
        parent = FindTask(NULL);
        SetSignal(0, SIGBREAKF_CTRL_F);
        t = stack ? (struct Task *)AllocMem(sizeof(struct Task), MEMF_PUBLIC | MEMF_CLEAR) : NULL;
        if (t) {
            t->tc_Node.ln_Type = NT_TASK;
            t->tc_Node.ln_Name = (char *)"OpenFontLibTest task";
            t->tc_SPLower = stack;
            t->tc_SPUpper = (UBYTE *)stack + 4096;
            t->tc_SPReg = t->tc_SPUpper;
            AddTask(t, (APTR)plain_task, NULL);
            Wait(SIGBREAKF_CTRL_F);
            Delay(5);           /* let it return before its memory goes */
            check(task_result == 0 && task_error == -9, "a plain task gets OFERR_NOTPROCESS");
            FreeMem(t, sizeof(struct Task));
        } else {
            check(0, "a plain task");
        }
        if (stack)
            FreeMem(stack, 4096);
    }
    check(!strcmp((const char *)OF_ErrorString(OFERR_FONT), "font error"), "OF_ErrorString");

    CloseLibrary(OpenFontBase);
    FreeArgs(rda);
    if (fails) {
        printf("OPENFONTLIB FAIL %d\n", fails);
        return 10;
    }
    printf("OPENFONTLIB PASS\n");
    return 0;
}
