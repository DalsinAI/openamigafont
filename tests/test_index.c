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
    of_face_close(face);
    of_index_free(&ix);
    of_index_free(&ix2);
    of_context_destroy(ctx);
    if (fails)
        return 1;
    printf("index: all passed\n");
    return 0;
}
