#include "openfont/openfont.h"
#include "openfont_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

struct OFContext {
    FT_Library ft;
};

struct OFFace {
    OFContext *ctx;
    FT_Face ft_face;
    hb_font_t *hb_font;
    uint32_t pixel_size;
    char *path;
    uint64_t source_fingerprint;
};

static char *of_strdup(const char *s)
{
    size_t n;
    char *p;
    if (!s)
        return NULL;
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

/* FNV-1a is used only as a stale-cache fingerprint, not as a security hash. */
static uint64_t fingerprint_path(const char *path)
{
    FILE *f;
    uint8_t buf[4096];
    size_t n, i;
    uint64_t h = UINT64_C(14695981039346656037);

    if (!path)
        return 0;
    f = fopen(path, "rb");
    if (!f)
        return 0;
    while ((n = fread(buf, 1, sizeof buf, f)) != 0) {
        for (i = 0; i < n; ++i) {
            h ^= buf[i];
            h *= UINT64_C(1099511628211);
        }
    }
    if (ferror(f))
        h = 0;
    fclose(f);
    return h;
}

OFContext *of_context_create(void)
{
    OFContext *ctx = (OFContext *)calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    if (FT_Init_FreeType(&ctx->ft)) {
        free(ctx);
        return NULL;
    }
    return ctx;
}

void of_context_destroy(OFContext *ctx)
{
    if (!ctx)
        return;
    FT_Done_FreeType(ctx->ft);
    free(ctx);
}

OFFace *of_face_open(OFContext *ctx, const char *path)
{
    OFFace *face;
    if (!ctx || !path)
        return NULL;
    face = (OFFace *)calloc(1, sizeof(*face));
    if (!face)
        return NULL;
    face->ctx = ctx;
    face->path = of_strdup(path);
    if (!face->path) {
        free(face);
        return NULL;
    }
    if (FT_New_Face(ctx->ft, path, 0, &face->ft_face)) {
        free(face->path);
        free(face);
        return NULL;
    }
    face->hb_font = hb_ft_font_create_referenced(face->ft_face);
    if (!face->hb_font) {
        FT_Done_Face(face->ft_face);
        free(face->path);
        free(face);
        return NULL;
    }
    face->source_fingerprint = fingerprint_path(path);
    if (of_face_set_pixel_size(face, 16) != OF_OK) {
        of_face_close(face);
        return NULL;
    }
    return face;
}

void of_face_close(OFFace *face)
{
    if (!face)
        return;
    hb_font_destroy(face->hb_font);
    FT_Done_Face(face->ft_face);
    free(face->path);
    free(face);
}

int of_face_set_pixel_size(OFFace *face, uint32_t pixels)
{
    if (!face || !pixels)
        return OF_ERR_ARGUMENT;
    if (FT_Set_Pixel_Sizes(face->ft_face, 0, pixels))
        return OF_ERR_FONT;
    hb_ft_font_changed(face->hb_font);
    face->pixel_size = pixels;
    return OF_OK;
}

size_t of_shape_utf8(OFFace *face, const char *utf8, size_t bytes,
                     OFGlyph *out, size_t capacity)
{
    hb_buffer_t *buffer;
    hb_glyph_info_t *info;
    hb_glyph_position_t *pos;
    unsigned count = 0, i;
    if (!face || !utf8)
        return 0;
    buffer = hb_buffer_create();
    if (!buffer)
        return 0;
    hb_buffer_add_utf8(buffer, utf8, (int)bytes, 0, (int)bytes);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(face->hb_font, buffer, NULL, 0);
    info = hb_buffer_get_glyph_infos(buffer, &count);
    pos = hb_buffer_get_glyph_positions(buffer, &count);
    if (out && capacity) {
        unsigned n = count < capacity ? count : (unsigned)capacity;
        for (i = 0; i < n; ++i) {
            out[i].glyph_id = info[i].codepoint;
            out[i].cluster = info[i].cluster;
            out[i].x_advance_26_6 = pos[i].x_advance;
            out[i].y_advance_26_6 = pos[i].y_advance;
            out[i].x_offset_26_6 = pos[i].x_offset;
            out[i].y_offset_26_6 = pos[i].y_offset;
        }
    }
    hb_buffer_destroy(buffer);
    return count;
}

int64_t of_measure_utf8(OFFace *face, const char *utf8, size_t bytes)
{
    size_t n, i;
    OFGlyph *glyphs;
    int64_t total = 0;
    n = of_shape_utf8(face, utf8, bytes, NULL, 0);
    if (!n)
        return 0;
    glyphs = (OFGlyph *)malloc(n * sizeof(*glyphs));
    if (!glyphs)
        return 0;
    if (of_shape_utf8(face, utf8, bytes, glyphs, n) != n) {
        free(glyphs);
        return 0;
    }
    for (i = 0; i < n; ++i)
        total += glyphs[i].x_advance_26_6;
    free(glyphs);
    return total;
}

int of_raster_glyph_a8(OFFace *face, uint32_t glyph_id, OFBitmap *out)
{
    FT_GlyphSlot g;
    size_t bytes, y;
    uint8_t *pixels;
    if (!face || !out)
        return OF_ERR_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (FT_Load_Glyph(face->ft_face, glyph_id, FT_LOAD_DEFAULT))
        return OF_ERR_FONT;
    if (FT_Render_Glyph(face->ft_face->glyph, FT_RENDER_MODE_NORMAL))
        return OF_ERR_FONT;
    g = face->ft_face->glyph;
    out->width = g->bitmap.width;
    out->height = g->bitmap.rows;
    out->left = g->bitmap_left;
    out->top = g->bitmap_top;
    out->advance_x_26_6 = (int32_t)g->advance.x;
    out->stride = out->width;
    if (!out->width || !out->height)
        return OF_OK;
    bytes = (size_t)out->width * out->height;
    pixels = (uint8_t *)malloc(bytes);
    if (!pixels)
        return OF_ERR_MEMORY;
    for (y = 0; y < out->height; ++y) {
        const uint8_t *src;
        if (g->bitmap.pitch >= 0)
            src = g->bitmap.buffer + y * (size_t)g->bitmap.pitch;
        else
            src = g->bitmap.buffer + (out->height - 1 - y) * (size_t)(-g->bitmap.pitch);
        memcpy(pixels + y * out->width, src, out->width);
    }
    out->pixels = pixels;
    return OF_OK;
}

uint64_t of_face_source_fingerprint(const OFFace *face)
{
    return face ? face->source_fingerprint : 0;
}

int of_cache_matches_face(const OFCache *cache, const OFFace *face)
{
    uint64_t cache_fp, face_fp;
    if (!cache || !face)
        return 0;
    cache_fp = of_cache_source_fingerprint(cache);
    face_fp = of_face_source_fingerprint(face);
    if (!cache_fp || !face_fp)
        return 0;
    return cache_fp == face_fp;
}

static void free_sizes(OFCacheSizeIn *ins, size_t size_count)
{
    size_t s, i;
    if (!ins)
        return;
    for (s = 0; s < size_count; ++s) {
        if (!ins[s].glyphs)
            continue;
        for (i = 0; i < ins[s].count; ++i)
            free((void *)ins[s].glyphs[i].pixels);
        free((void *)ins[s].glyphs);
    }
    free(ins);
}

int of_compile_cache(OFFace *face, const char *path,
                     const uint16_t *sizes, size_t size_count,
                     uint32_t first_codepoint, uint32_t last_codepoint)
{
    size_t span, s, i;
    OFCacheSizeIn *ins;
    int rc;

    if (!face || !path || !sizes || !size_count || first_codepoint > last_codepoint)
        return OF_ERR_ARGUMENT;
    if (!face->source_fingerprint)
        return OF_ERR_FONT;
    span = (size_t)last_codepoint - first_codepoint + 1;
    ins = (OFCacheSizeIn *)calloc(size_count, sizeof(*ins));
    if (!ins)
        return OF_ERR_MEMORY;

    for (s = 0; s < size_count; ++s) {
        OFCacheGlyphIn *gs = (OFCacheGlyphIn *)calloc(span, sizeof(*gs));
        if (!gs) {
            free_sizes(ins, size_count);
            return OF_ERR_MEMORY;
        }
        ins[s].glyphs = gs;
        ins[s].pixel_size = sizes[s];
        ins[s].count = span;
        if (of_face_set_pixel_size(face, sizes[s]) != OF_OK) {
            free_sizes(ins, size_count);
            return OF_ERR_FONT;
        }
        for (i = 0; i < span; ++i) {
            uint32_t cp = first_codepoint + (uint32_t)i;
            uint32_t gid = FT_Get_Char_Index(face->ft_face, cp);
            OFBitmap b;
            rc = of_raster_glyph_a8(face, gid, &b);
            if (rc != OF_OK) {
                free_sizes(ins, size_count);
                return rc;
            }
            gs[i].codepoint = cp;
            gs[i].glyph_id = gid;
            gs[i].left = b.left;
            gs[i].top = b.top;
            gs[i].advance_x_26_6 = b.advance_x_26_6;
            gs[i].width = b.width;
            gs[i].height = b.height;
            gs[i].pixels = b.pixels;
        }
    }

    rc = of_cache_write(path, face->source_fingerprint, first_codepoint,
                        last_codepoint, ins, size_count);
    free_sizes(ins, size_count);
    return rc;
}

const char *of_error_string(int code)
{
    switch (code) {
    case OF_OK: return "ok";
    case OF_ERR_ARGUMENT: return "bad argument";
    case OF_ERR_MEMORY: return "out of memory";
    case OF_ERR_IO: return "I/O error";
    case OF_ERR_FONT: return "font error";
    case OF_ERR_RANGE: return "range error";
    case OF_ERR_BUFFER: return "buffer too small";
    case OF_ERR_CACHE: return "invalid cache";
    case OF_ERR_EXEC: return "executor failed";
    default: return "unknown error";
    }
}
