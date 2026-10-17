/* Host test: the font index (finding, aliases, the file, staleness) and
 * fallback by cluster. Copyright (c) 2026 AmigaChrome Team. MIT. */
#include "openfont/openfont.h"
#include "openfont_index.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL %s\n", what);
        fails++;
    }
}

static const char *family_of(const OFIndexEntry *e)
{
    return e ? e->family : "(none)";
}

int main(int argc, char **argv)
{
    OFContext *ctx = of_context_create();
    OFIndex ix, ix2;
    const OFIndexEntry *e;
    OFFace *face;
    OFGlyph glyphs[64];
    size_t n, i;
    int found_fallback = 0;
    /* "snow" and a snowman: Liberation Sans has no U+2603, DejaVu Sans has */
    static const char text[] = "snow \xE2\x98\x83!";

    if (argc < 3) {
        printf("usage: %s INDEXFILE DIR...\n", argv[0]);
        return 2;
    }
    of_index_init(&ix);
    for (i = 2; i < (size_t)argc; ++i)
        expect(of_index_add_dir(&ix, argv[i]) == OF_OK, "add dir");
    expect(of_index_stale(&ix), "a new index is stale");
    expect(of_index_scan(&ix, ctx) == OF_OK, "scan");
    expect(ix.count > 4, "fonts found");
    expect(!of_index_stale(&ix), "fresh after a scan");

    e = of_index_find(&ix, "Arial", 400, 0);
    expect(e && !strcmp(e->family, "Liberation Sans"), "Arial is Liberation Sans");
    printf("Arial -> %s %s\n", family_of(e), e ? e->style : "");
    e = of_index_find(&ix, "monospace", 700, 0);
    expect(e && !strcmp(e->family, "DejaVu Sans Mono") && e->weight == 700, "monospace bold");
    printf("monospace 700 -> %s %s\n", family_of(e), e ? e->style : "");
    e = of_index_find(&ix, "DejaVu Serif", 400, 1);
    expect(e && (e->flags & OF_FACE_ITALIC), "serif italic");
    e = of_index_find(&ix, "No Such Family", 400, 0);
    expect(e && !strcmp(e->family, "DejaVu Sans"), "unknown goes to sans-serif");
    of_index_configure(&ix, "Comic=Liberation Mono\n", NULL);
    e = of_index_find(&ix, "Comic", 400, 0);
    expect(e && !strcmp(e->family, "Liberation Mono"), "the user's alias");

    expect(of_index_save(&ix, argv[1]) == OF_OK, "save");
    of_index_init(&ix2);
    for (i = 2; i < (size_t)argc; ++i)
        of_index_add_dir(&ix2, argv[i]);
    expect(of_index_load(&ix2, argv[1]) == OF_OK, "load");
    expect(ix2.count == ix.count, "same entries after load");
    expect(!of_index_stale(&ix2), "loaded index is fresh");
    for (i = 0; i < ix.count && i < ix2.count; ++i)
        if (memcmp(&ix.entries[i], &ix2.entries[i], sizeof ix.entries[i]))
            break;
    expect(i == ix.count, "entries round-trip");

    face = of_index_open(&ix, ctx, "Liberation Sans", 400, 0, 1);
    expect(face != NULL, "open with fallbacks");
    expect(of_face_fallback_count(face) >= 1, "a fallback chain");
    of_face_set_pixel_size(face, 16);
    n = of_shape_utf8(face, text, strlen(text), glyphs, 64);
    expect(n == 7, "seven glyphs");
    for (i = 0; i < n && i < 64; ++i) {
        printf("  g=%u cluster=%u face=%u flags=%u adv=%d\n", glyphs[i].glyph_id,
               glyphs[i].cluster, glyphs[i].face, glyphs[i].flags, glyphs[i].x_advance_26_6);
        if (glyphs[i].cluster == 5) {
            found_fallback = glyphs[i].face != 0 && glyphs[i].glyph_id != 0;
        } else {
            expect(glyphs[i].face == 0, "the rest from the face itself");
        }
    }
    expect(found_fallback, "the snowman from a fallback");
    {
        OFBitmap b;
        OFFace *fb = of_face_fallback(face, glyphs[5].face);
        expect(fb && of_raster_glyph_a8(fb, glyphs[5].glyph_id, &b) == OF_OK && b.width > 0,
               "the fallback face draws it");
        of_bitmap_release(&b);
    }
    /* a codepoint no face of the chain has: the first face's own glyph 0,
     * with its own advance, not the last fallback's */
    {
        static const char none[] = "\xF4\x8F\xBF\xBD";     /* U+10FFFD */
        OFFace *alone = of_index_open(&ix, ctx, "Liberation Sans", 400, 0, 0);
        OFGlyph a[4], c[4];
        expect(alone && of_face_fallback_count(alone) == 0, "a face with no fallbacks");
        if (alone) {
            of_face_set_pixel_size(alone, 16);
            expect(of_shape_utf8(alone, none, 4, a, 4) == 1 && of_shape_utf8(face, none, 4, c, 4) == 1,
                   "one glyph for an unknown codepoint");
            expect(c[0].glyph_id == 0 && c[0].face == 0 && (c[0].flags & OF_GLYPH_MISSING), "marked missing");
            expect(c[0].x_advance_26_6 == a[0].x_advance_26_6, "the first face's own advance for it");
            printf("  missing: adv=%d (the face alone %d)\n", c[0].x_advance_26_6, a[0].x_advance_26_6);
            of_face_close(alone);
        }
    }
    of_face_close(face);

    /* no loops in the fallback graph, however long */
    {
        OFFace *fa = of_index_open(&ix, ctx, "DejaVu Sans", 400, 0, 0);
        OFFace *fb = of_index_open(&ix, ctx, "DejaVu Serif", 400, 0, 0);
        OFFace *fc = of_index_open(&ix, ctx, "Liberation Mono", 400, 0, 0);
        expect(fa && fb && fc, "three faces");
        if (fa && fb && fc) {
            expect(of_face_add_fallback(fa, fa) == OF_ERR_ARGUMENT, "not to itself");
            expect(of_face_add_fallback(fa, fb) == OF_OK, "A to B");
            expect(of_face_add_fallback(fb, fa) == OF_ERR_ARGUMENT, "not B back to A");
            expect(of_face_add_fallback(fb, fc) == OF_OK, "B to C");
            expect(of_face_add_fallback(fc, fa) == OF_ERR_ARGUMENT, "not C back to A");
            expect(of_face_add_fallback(fa, fc) == OF_OK, "A to C as well");
        }
        of_face_close(fa);
        of_face_close(fb);
        of_face_close(fc);
    }

    /* a folder that is there but can't be listed stays stale */
    {
        OFIndex ix3;
        char file[1024];
        snprintf(file, sizeof file, "%s/DejaVuSans.ttf", argv[2]);
        of_index_init(&ix3);
        of_index_add_dir(&ix3, argv[2]);
        of_index_add_dir(&ix3, file);
        expect(of_index_scan(&ix3, ctx) == OF_OK && ix3.count > 0, "scan with a folder that isn't one");
        expect(of_index_stale(&ix3), "it is read again next time");
        of_index_free(&ix3);
    }

    of_index_free(&ix);
    of_index_free(&ix2);
    of_context_destroy(ctx);
    if (fails)
        return 1;
    printf("index: all passed\n");
    return 0;
}
