#include "openfont/openfont.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t hash_bytes(uint32_t h, const unsigned char *p, size_t n)
{
    while (n--) {
        h ^= *p++;
        h *= 16777619u;
    }
    return h;
}

int main(int argc, char **argv)
{
    static const char text[] = "Office AVWa fi \xC3\xA9";
    OFContext *ctx;
    OFFace *face;
    OFGlyph glyphs[64];
    OFBitmap bitmap;
    size_t n, i;
    int64_t advance;
    uint32_t hash = 2166136261u;
    if (argc != 2) {
        fprintf(stderr, "usage: %s FONT\n", argv[0]);
        return 2;
    }
    ctx = of_context_create();
    face = ctx ? of_face_open(ctx, argv[1]) : NULL;
    if (!face) {
        fprintf(stderr, "open failed\n");
        of_context_destroy(ctx);
        return 1;
    }
    if (of_face_set_pixel_size(face, 16) != OF_OK)
        return 1;
    n = of_shape_utf8(face, text, strlen(text), glyphs, 64);
    if (!n || n > 64)
        return 1;
    advance = of_measure_utf8(face, text, strlen(text));
    for (i = 0; i < n; ++i) {
        hash = hash_bytes(hash, (const unsigned char *)&glyphs[i].glyph_id, sizeof(glyphs[i].glyph_id));
        hash = hash_bytes(hash, (const unsigned char *)&glyphs[i].x_advance_26_6, sizeof(glyphs[i].x_advance_26_6));
    }
    if (of_raster_glyph_a8(face, glyphs[0].glyph_id, &bitmap) != OF_OK)
        return 1;
    if (bitmap.pixels)
        hash = hash_bytes(hash, bitmap.pixels, (size_t)bitmap.width * bitmap.height);
    printf("OPENFONT glyphs=%lu advance=%lld first=%u bitmap=%ux%u hash=%08x\n",
           (unsigned long)n, (long long)advance, glyphs[0].glyph_id,
           bitmap.width, bitmap.height, hash);
    of_bitmap_release(&bitmap);
    of_face_close(face);
    of_context_destroy(ctx);
    return 0;
}
