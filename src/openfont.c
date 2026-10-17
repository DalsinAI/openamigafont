#include "openfont/openfont.h"
#include "openfont_internal.h"
#include "openfont_io.h"

#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
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
    uint32_t refs;
    uint8_t *data;                  /* the font file, owned when take was set */
    size_t data_size;
    uint32_t face_index;
    int owns_data;
    int have_fingerprint;           /* worked out the first time a cache asks */
    uint64_t source_fingerprint;
    uint32_t nfallbacks;
    OFFace *fallbacks[OF_MAX_FALLBACKS];
    uint32_t walk;                  /* the last walk of the fallback graph to visit it */
};

/* FNV-1a is used only as a stale-cache fingerprint, not as a security hash. */
static uint64_t fingerprint_bytes(const uint8_t *p, size_t n)
{
    uint64_t h = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0; i < n; ++i) {
        h ^= p[i];
        h *= UINT64_C(1099511628211);
    }
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

static OFFace *open_memory(OFContext *ctx, uint8_t *data, size_t size,
                           uint32_t face_index, int take, int *error)
{
    OFFace *face;
    FT_Error fe;
    if (!ctx || !data || !size) {
        if (take)
            free(data);
        *error = OF_ERR_ARGUMENT;
        return NULL;
    }
    face = (OFFace *)calloc(1, sizeof(*face));
    if (!face) {
        if (take)
            free(data);
        *error = OF_ERR_MEMORY;
        return NULL;
    }
    face->ctx = ctx;
    face->refs = 1;
    face->data = data;
    face->data_size = size;
    face->face_index = face_index;
    face->owns_data = take;
    fe = FT_New_Memory_Face(ctx->ft, data, (FT_Long)size, (FT_Long)face_index, &face->ft_face);
    if (fe) {
        if (take)
            free(data);
        free(face);
        *error = fe == FT_Err_Out_Of_Memory ? OF_ERR_MEMORY : OF_ERR_FONT;
        return NULL;
    }
    face->hb_font = hb_ft_font_create_referenced(face->ft_face);
    if (!face->hb_font) {
        FT_Done_Face(face->ft_face);
        if (take)
            free(data);
        free(face);
        *error = OF_ERR_MEMORY;
        return NULL;
    }
    if ((*error = of_face_set_pixel_size(face, 16)) != OF_OK) {
        of_face_close(face);
        return NULL;
    }
    return face;
}

OFFace *of_face_open_memory(OFContext *ctx, uint8_t *data, size_t size,
                            uint32_t face_index, int take)
{
    int error;
    return open_memory(ctx, data, size, face_index, take, &error);
}

OFFace *of_face_open_file(OFContext *ctx, const char *path, uint32_t face_index, int *error)
{
    uint8_t *data = NULL;
    size_t size = 0;
    int e;
    OFFace *face = NULL;
    if (!ctx || !path)
        e = OF_ERR_ARGUMENT;
    else if ((e = of_io_read_all(path, &data, &size)) == OF_OK)
        face = open_memory(ctx, data, size, face_index, 1, &e);
    if (error)
        *error = e;
    return face;
}

OFFace *of_face_open_index(OFContext *ctx, const char *path, uint32_t face_index)
{
    return of_face_open_file(ctx, path, face_index, NULL);
}

OFFace *of_face_open(OFContext *ctx, const char *path)
{
    return of_face_open_index(ctx, path, 0);
}

void of_face_close(OFFace *face)
{
    uint32_t i;
    if (!face || !face->refs)
        return;
    if (--face->refs)
        return;
    for (i = 0; i < face->nfallbacks; ++i)
        of_face_close(face->fallbacks[i]);
    hb_font_destroy(face->hb_font);
    FT_Done_Face(face->ft_face);
    if (face->owns_data)
        free(face->data);
    free(face);
}

int of_face_set_pixel_size(OFFace *face, uint32_t pixels)
{
    if (!face || !pixels)
        return OF_ERR_ARGUMENT;
    if (face->pixel_size == pixels)
        return OF_OK;
    if (FT_Set_Pixel_Sizes(face->ft_face, 0, pixels))
        return OF_ERR_FONT;
    hb_ft_font_changed(face->hb_font);
    face->pixel_size = pixels;
    return OF_OK;
}

static void copy_name(char *out, size_t size, const char *in)
{
    size_t n = in ? strlen(in) : 0;
    if (n >= size)
        n = size - 1;
    if (n)
        memcpy(out, in, n);
    out[n] = 0;
}

int of_face_info(OFFace *face, OFFaceInfo *info)
{
    FT_Face f;
    TT_OS2 *os2;
    if (!face || !info)
        return OF_ERR_ARGUMENT;
    f = face->ft_face;
    memset(info, 0, sizeof(*info));
    copy_name(info->family, sizeof info->family, f->family_name);
    copy_name(info->style, sizeof info->style, f->style_name);
    os2 = (TT_OS2 *)FT_Get_Sfnt_Table(f, FT_SFNT_OS2);
    if (os2 && os2->version != 0xFFFFu) {
        info->weight = os2->usWeightClass;
        info->width = os2->usWidthClass;
        info->unicode_ranges[0] = (uint32_t)os2->ulUnicodeRange1;
        info->unicode_ranges[1] = (uint32_t)os2->ulUnicodeRange2;
        info->unicode_ranges[2] = (uint32_t)os2->ulUnicodeRange3;
        info->unicode_ranges[3] = (uint32_t)os2->ulUnicodeRange4;
    }
    if (!info->weight)
        info->weight = (f->style_flags & FT_STYLE_FLAG_BOLD) ? 700 : 400;
    if (!info->width)
        info->width = 5;
    if (f->style_flags & FT_STYLE_FLAG_ITALIC)
        info->flags |= OF_FACE_ITALIC;
    if (FT_IS_FIXED_WIDTH(f))
        info->flags |= OF_FACE_FIXED;
    if (FT_IS_SCALABLE(f))
        info->flags |= OF_FACE_SCALABLE;
    if (FT_HAS_COLOR(f))
        info->flags |= OF_FACE_COLOR;
    info->units_per_em = f->units_per_EM;
    info->pixel_size = face->pixel_size;
    info->num_glyphs = (uint32_t)f->num_glyphs;
    info->num_faces = (uint32_t)f->num_faces;
    if (f->size) {
        info->ascender_26_6 = (int32_t)f->size->metrics.ascender;
        info->descender_26_6 = (int32_t)f->size->metrics.descender;
        info->height_26_6 = (int32_t)f->size->metrics.height;
    }
    return OF_OK;
}

uint32_t of_face_char_index(OFFace *face, uint32_t codepoint)
{
    return face ? (uint32_t)FT_Get_Char_Index(face->ft_face, codepoint) : 0;
}

/* One walk at a time: the faces are not shared between threads (the
 * library takes its lock around every call). */
static uint32_t walk_count;

/* Whether from's fallbacks, or theirs, lead to to. The graph has no loops
 * (of_face_add_fallback keeps it so), and each face is visited once. */
static int leads_to(OFFace *from, const OFFace *to, uint32_t walk)
{
    uint32_t i;
    if (from == to)
        return 1;
    if (from->walk == walk)
        return 0;
    from->walk = walk;
    for (i = 0; i < from->nfallbacks; ++i)
        if (leads_to(from->fallbacks[i], to, walk))
            return 1;
    return 0;
}

int of_face_add_fallback(OFFace *face, OFFace *fallback)
{
    if (!face || !fallback)
        return OF_ERR_ARGUMENT;
    if (++walk_count == 0)
        walk_count = 1;
    /* No loops, however long: each face holds its fallbacks, so a loop
     * would keep every face in it open after its owners close them. */
    if (leads_to(fallback, face, walk_count))
        return OF_ERR_ARGUMENT;
    if (face->nfallbacks >= OF_MAX_FALLBACKS)
        return OF_ERR_RANGE;
    fallback->refs++;
    face->fallbacks[face->nfallbacks++] = fallback;
    return OF_OK;
}

uint32_t of_face_fallback_count(const OFFace *face)
{
    return face ? face->nfallbacks : 0;
}

OFFace *of_face_fallback(OFFace *face, uint32_t n)
{
    if (!face)
        return NULL;
    if (n == 0)
        return face;
    if (n > face->nfallbacks)
        return NULL;
    /* shaping sets a fallback to the size of the face it serves; another
     * face it serves may have shaped since */
    of_face_set_pixel_size(face->fallbacks[n - 1], face->pixel_size);
    return face->fallbacks[n - 1];
}

/* ---- reading a face's tables through a stream (the index) ---- */

static unsigned long stream_read(FT_Stream stream, unsigned long offset,
                                 unsigned char *buffer, unsigned long count)
{
    OFFile *f = (OFFile *)stream->descriptor.pointer;
    if (!count)
        return offset <= stream->size ? 0 : 1;  /* a seek: 0 is success */
    return of_io_read_at(f, (uint32_t)offset, buffer, (uint32_t)count);
}

static void stream_close(FT_Stream stream)
{
    of_io_close((OFFile *)stream->descriptor.pointer);
    stream->descriptor.pointer = NULL;
}

int of_probe_face(OFContext *ctx, const char *path, uint32_t face_index, OFFaceInfo *info)
{
    FT_StreamRec *stream;
    FT_Open_Args args;
    OFFace face;
    uint32_t size = 0;
    OFFile *f;
    int rc;

    if (!ctx || !path || !info)
        return OF_ERR_ARGUMENT;
    f = of_io_open(path, &size);
    if (!f)
        return OF_ERR_IO;
    /* FreeType keeps the stream until the face is done, and closes it */
    stream = (FT_StreamRec *)calloc(1, sizeof(*stream));
    if (!stream) {
        of_io_close(f);
        return OF_ERR_MEMORY;
    }
    stream->size = size;
    stream->descriptor.pointer = f;
    stream->read = stream_read;
    stream->close = stream_close;
    memset(&args, 0, sizeof args);
    args.flags = FT_OPEN_STREAM;
    args.stream = stream;
    memset(&face, 0, sizeof face);
    if ((rc = FT_Open_Face(ctx->ft, &args, (FT_Long)face_index, &face.ft_face)) != 0) {
        /* FT_Open_Face closes a stream it was given, even on failure */
        free(stream);
        return rc == FT_Err_Out_Of_Memory ? OF_ERR_MEMORY : OF_ERR_FONT;
    }
    rc = of_face_info(&face, info);
    FT_Done_Face(face.ft_face);
    free(stream);
    return rc;
}

/* ---- shaping, with fallback by cluster ---- */

typedef struct GlyphVec {
    OFGlyph *g;
    size_t n, cap;
    int failed;
} GlyphVec;

static void vec_push(GlyphVec *v, const OFGlyph *g)
{
    if (v->failed)
        return;
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 64;
        OFGlyph *ng = (OFGlyph *)realloc(v->g, cap * sizeof(*ng));
        if (!ng) {
            v->failed = 1;
            return;
        }
        v->g = ng;
        v->cap = cap;
    }
    v->g[v->n++] = *g;
}

/* Shapes text[off, off + len) with face, the whole text as its context, and
 * appends the glyphs, each marked as from face_no. The run's script and
 * direction are guessed the first time (props->direction invalid) and kept
 * in props; the fallbacks' items reuse them, since a piece of a run (an
 * emoji, a mark in Arabic) can't tell its own. */
static int shape_item(OFFace *face, uint16_t face_no, const char *utf8, size_t bytes,
                      size_t off, size_t len, hb_segment_properties_t *props, GlyphVec *out)
{
    hb_buffer_t *buffer;
    hb_glyph_info_t *info;
    hb_glyph_position_t *pos;
    unsigned count = 0, i;

    buffer = hb_buffer_create();
    if (!hb_buffer_allocation_successful(buffer)) {
        hb_buffer_destroy(buffer);
        return OF_ERR_MEMORY;
    }
    hb_buffer_add_utf8(buffer, utf8, (int)bytes, (unsigned)off, (int)len);
    if (props->direction == HB_DIRECTION_INVALID) {
        hb_buffer_guess_segment_properties(buffer);
        hb_buffer_get_segment_properties(buffer, props);
    } else {
        hb_buffer_set_segment_properties(buffer, props);
    }
    hb_shape(face->hb_font, buffer, NULL, 0);
    if (!hb_buffer_allocation_successful(buffer)) {
        hb_buffer_destroy(buffer);
        return OF_ERR_MEMORY;
    }
    info = hb_buffer_get_glyph_infos(buffer, &count);
    pos = hb_buffer_get_glyph_positions(buffer, &count);
    for (i = 0; i < count; ++i) {
        OFGlyph g;
        g.glyph_id = info[i].codepoint;
        g.cluster = info[i].cluster;
        g.x_advance_26_6 = pos[i].x_advance;
        g.y_advance_26_6 = pos[i].y_advance;
        g.x_offset_26_6 = pos[i].x_offset;
        g.y_offset_26_6 = pos[i].y_offset;
        g.face = face_no;
        g.flags = (hb_glyph_info_get_glyph_flags(&info[i]) & HB_GLYPH_FLAG_UNSAFE_TO_BREAK)
                  ? OF_GLYPH_UNSAFE_TO_BREAK : 0;
        vec_push(out, &g);
    }
    hb_buffer_destroy(buffer);
    return out->failed ? OF_ERR_MEMORY : OF_OK;
}

/* Appends glyphs, glyph 0 marked missing. */
static void push_marked(GlyphVec *out, const OFGlyph *g, size_t n)
{
    size_t k;
    for (k = 0; k < n; ++k) {
        OFGlyph x = g[k];
        if (!x.glyph_id)
            x.flags |= OF_GLYPH_MISSING;
        vec_push(out, &x);
    }
}

/* Shapes text[off, off + len) with the chain's level-th face. Every whole
 * cluster that face has no glyph for is shaped again with the next one;
 * what the last face can't show is the first face's own shaping of it (its
 * glyph 0, marked missing). */
static int shape_chain(OFFace *root, uint32_t level, const char *utf8, size_t bytes,
                       size_t off, size_t len, hb_segment_properties_t *props, GlyphVec *out)
{
    OFFace *face = level ? root->fallbacks[level - 1] : root;
    GlyphVec tmp;
    unsigned char *missing;
    size_t i, k;
    int rc;

    memset(&tmp, 0, sizeof tmp);
    if (level && of_face_set_pixel_size(face, root->pixel_size) != OF_OK)
        return OF_ERR_FONT;
    rc = shape_item(face, (uint16_t)level, utf8, bytes, off, len, props, &tmp);
    if (rc != OF_OK) {
        free(tmp.g);
        return rc;
    }
    if (!tmp.n) {
        free(tmp.g);
        return OF_OK;
    }
    missing = (unsigned char *)calloc(tmp.n, 1);
    if (!missing) {
        free(tmp.g);
        return OF_ERR_MEMORY;
    }
    /* a cluster is missing when any glyph of it is glyph 0; a cluster's
     * glyphs are next to each other in the run */
    for (i = 0; i < tmp.n; ++i) {
        if (tmp.g[i].glyph_id)
            continue;
        missing[i] = 1;
        for (k = i; k > 0 && tmp.g[k - 1].cluster == tmp.g[i].cluster; --k)
            missing[k - 1] = 1;
        for (k = i + 1; k < tmp.n && tmp.g[k].cluster == tmp.g[i].cluster; ++k)
            missing[k] = 1;
    }
    for (i = 0; i < tmp.n;) {
        size_t j;
        uint32_t cmin, cmax, end;
        if (!missing[i]) {
            vec_push(out, &tmp.g[i]);
            ++i;
            continue;
        }
        for (j = i; j < tmp.n && missing[j]; ++j)
            ;
        cmin = cmax = tmp.g[i].cluster;
        for (k = i; k < j; ++k) {
            if (tmp.g[k].cluster < cmin)
                cmin = tmp.g[k].cluster;
            if (tmp.g[k].cluster > cmax)
                cmax = tmp.g[k].cluster;
        }
        /* the run ends where the next cluster after it begins */
        end = (uint32_t)(off + len);
        for (k = 0; k < tmp.n; ++k)
            if (tmp.g[k].cluster > cmax && tmp.g[k].cluster < end)
                end = tmp.g[k].cluster;
        if (level < root->nfallbacks) {
            rc = shape_chain(root, level + 1, utf8, bytes, cmin, end - cmin, props, out);
            if (rc != OF_OK)
                break;
        } else if (level) {
            /* none of the chain has it: the first face's own glyphs for it,
             * so its glyph numbers, advances and pixels agree */
            GlyphVec first;
            memset(&first, 0, sizeof first);
            rc = shape_item(root, 0, utf8, bytes, cmin, end - cmin, props, &first);
            if (rc == OF_OK)
                push_marked(out, first.g, first.n);
            free(first.g);
            if (rc != OF_OK)
                break;
        } else {
            push_marked(out, tmp.g + i, j - i);
        }
        i = j;
    }
    free(missing);
    free(tmp.g);
    if (rc == OF_OK && out->failed)
        rc = OF_ERR_MEMORY;
    return rc;
}

size_t of_shape_utf8(OFFace *face, const char *utf8, size_t bytes,
                     OFGlyph *out, size_t capacity)
{
    GlyphVec v;
    hb_segment_properties_t props = HB_SEGMENT_PROPERTIES_DEFAULT;
    size_t n;
    if (!face || !utf8 || !bytes)
        return 0;
    memset(&v, 0, sizeof v);
    if (shape_chain(face, 0, utf8, bytes, 0, bytes, &props, &v) != OF_OK) {
        free(v.g);
        return 0;
    }
    n = v.n;
    if (out && capacity)
        memcpy(out, v.g, (n < capacity ? n : capacity) * sizeof(*out));
    free(v.g);
    return n;
}

int64_t of_measure_utf8(OFFace *face, const char *utf8, size_t bytes)
{
    GlyphVec v;
    hb_segment_properties_t props = HB_SEGMENT_PROPERTIES_DEFAULT;
    size_t i;
    int64_t total = 0;
    if (!face || !utf8 || !bytes)
        return 0;
    memset(&v, 0, sizeof v);
    if (shape_chain(face, 0, utf8, bytes, 0, bytes, &props, &v) != OF_OK) {
        free(v.g);
        return 0;
    }
    for (i = 0; i < v.n; ++i)
        total += v.g[i].x_advance_26_6;
    free(v.g);
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

/* The file and the face in it: two faces of one collection differ. Worked
 * out when a cache first asks, since it reads the whole file: a second on a
 * real 68040 for a large font, and most programs never need it. */
uint64_t of_face_source_fingerprint(const OFFace *face)
{
    OFFace *f = (OFFace *)face;
    if (!f || !f->data)
        return 0;
    if (!f->have_fingerprint) {
        f->source_fingerprint = fingerprint_bytes(f->data, f->data_size) ^
                                ((uint64_t)f->face_index * UINT64_C(0x9E3779B97F4A7C15));
        f->have_fingerprint = 1;
    }
    return f->source_fingerprint;
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
    if (!of_face_source_fingerprint(face))
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

    rc = of_cache_write(path, of_face_source_fingerprint(face), first_codepoint,
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
