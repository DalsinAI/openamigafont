#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/openfont.h>

#include <stdio.h>
#include <string.h>

struct Library *OpenFontBase;

#define CHECK(x) do { if (!(x)) {     printf("OPENFONTLIB FAIL line=%d expr=%s\n", __LINE__, #x);     rc = 20; goto done; } } while (0)

int main(int argc, char **argv)
{
    int rc = 0;
    OFContext *ctx = NULL;
    OFFace *face = NULL;
    OFGlyph glyphs[64];
    OFBitmap bitmap;
    struct OFFaceOpenRequest open_req;
    struct OFShapeRequest shape;
    struct OFMeasureRequest measure;
    struct OFRasterRequest raster;
    int64_t advance = 0;
    ULONG count;

    if (argc != 2) {
        puts("usage: OpenFontLibTest FONT.ttf");
        return 10;
    }

    OpenFontBase = OpenLibrary((CONST_STRPTR)OPENFONTLIB_NAME, OPENFONTLIB_VERSION);
    CHECK(OpenFontBase != NULL);
    CHECK((OF_LibVersion() >> 16) == OPENFONTLIB_VERSION);

    ctx = OF_ContextCreate();
    CHECK(ctx != NULL);

    open_req.context = ctx;
    open_req.path = (CONST_STRPTR)argv[1];
    face = OF_FaceOpen(&open_req);
    CHECK(face != NULL);
    CHECK(OF_FaceSetPixelSize(face, 16) == OF_OK);

    shape.face = face;
    shape.utf8 = (CONST_STRPTR)"Office AVWa fi";
    shape.bytes = (ULONG)strlen((const char *)shape.utf8);
    shape.glyphs = glyphs;
    shape.capacity = 64;
    count = OF_ShapeUTF8(&shape);
    CHECK(count > 0 && count <= 64);

    measure.face = face;
    measure.utf8 = shape.utf8;
    measure.bytes = shape.bytes;
    measure.advance_26_6 = &advance;
    CHECK(OF_MeasureUTF8(&measure) == OF_OK);
    CHECK(advance > 0);

    memset(&bitmap, 0, sizeof bitmap);
    raster.face = face;
    raster.glyph_id = glyphs[0].glyph_id;
    raster.bitmap = &bitmap;
    CHECK(OF_RasterGlyphA8(&raster) == OF_OK);
    CHECK(bitmap.width > 0 && bitmap.height > 0);

    printf("OPENFONTLIB PASS glyphs=%lu advance=%lld first=%lu bitmap=%lux%lu\n",
           (unsigned long)count, (long long)advance,
           (unsigned long)glyphs[0].glyph_id,
           (unsigned long)bitmap.width, (unsigned long)bitmap.height);

done:
    OF_BitmapRelease(&bitmap);
    if (face) OF_FaceClose(face);
    if (ctx) OF_ContextDestroy(ctx);
    if (OpenFontBase) CloseLibrary(OpenFontBase);
    return rc;
}
