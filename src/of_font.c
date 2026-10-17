/* OpenFont's font reader: the sfnt container, the metrics and naming tables,
 * cmap, and TrueType outlines (loca/glyf). CFF outlines are in of_cff.c.
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT licence (see LICENSE).
 *
 * From the OpenType specification only. Every read is checked against the
 * file's size: a broken font is OF_ERR_FONT, never a crash. */
#include "openfont/openfont.h"
#include "of_font.h"

#include <stdlib.h>
#include <string.h>

#define TAG(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

/* ---- the path ---- */

void of_path_init(OFPath *p)
{
    memset(p, 0, sizeof(*p));
}

void of_path_free(OFPath *p)
{
    free(p->x);
    free(p->y);
    free(p->tag);
    free(p->ends);
    of_path_init(p);
}

void of_path_reset(OFPath *p)
{
    p->npoints = p->ncontours = 0;
    p->failed = 0;
}

static int path_grow_points(OFPath *p, uint32_t need)
{
    uint32_t cap;
    int32_t *nx, *ny;
    uint8_t *nt;
    if (need <= p->cap_points)
        return 1;
    cap = p->cap_points ? p->cap_points * 2 : 64;
    while (cap < need)
        cap *= 2;
    nx = (int32_t *)realloc(p->x, cap * sizeof(*nx));
    if (!nx)
        return 0;
    p->x = nx;
    ny = (int32_t *)realloc(p->y, cap * sizeof(*ny));
    if (!ny)
        return 0;
    p->y = ny;
    nt = (uint8_t *)realloc(p->tag, cap);
    if (!nt)
        return 0;
    p->tag = nt;
    p->cap_points = cap;
    return 1;
}

void of_path_point(OFPath *p, int32_t x, int32_t y, uint8_t tag)
{
    if (p->failed)
        return;
    if (!path_grow_points(p, p->npoints + 1)) {
        p->failed = 1;
        return;
    }
    p->x[p->npoints] = x;
    p->y[p->npoints] = y;
    p->tag[p->npoints] = tag;
    p->npoints++;
}

void of_path_close(OFPath *p)
{
    uint32_t first;
    if (p->failed)
        return;
    first = p->ncontours ? p->ends[p->ncontours - 1] + 1 : 0;
    if (p->npoints <= first)
        return;                     /* nothing since the last contour */
    if (p->ncontours == p->cap_contours) {
        uint32_t cap = p->cap_contours ? p->cap_contours * 2 : 16;
        uint32_t *ne = (uint32_t *)realloc(p->ends, cap * sizeof(*ne));
        if (!ne) {
            p->failed = 1;
            return;
        }
        p->ends = ne;
        p->cap_contours = cap;
    }
    p->ends[p->ncontours++] = p->npoints - 1;
}

/* The scale: ppem * 64 / units_per_em as a 16.16 number, rounded; a point
 * (16.16 font units) times it, rounded half away from zero. Fixed point the
 * usual way, so a point lands on the 64th of a pixel other renderers put it. */
static int32_t scale_one(int32_t v, int64_t scale)
{
    int64_t n = (int64_t)v * scale;
    if (n >= 0)
        return (int32_t)((n + ((int64_t)1 << 31)) >> 32);
    return (int32_t)(-((-n + ((int64_t)1 << 31)) >> 32));
}

int32_t of_scale_16_16(uint32_t units_per_em, uint32_t ppem)
{
    return (int32_t)((((int64_t)ppem * 64) * 65536 + units_per_em / 2) / units_per_em);
}

void of_path_scale(OFPath *p, uint32_t units_per_em, uint32_t ppem)
{
    uint32_t i;
    int64_t scale = of_scale_16_16(units_per_em, ppem);
    for (i = 0; i < p->npoints; ++i) {
        p->x[i] = scale_one(p->x[i], scale);
        p->y[i] = scale_one(p->y[i], scale);
    }
}

/* ---- the container ---- */

static int in_file(const OFFont *f, uint32_t off, uint32_t len)
{
    return off <= f->size && len <= f->size - off;
}

/* p .. p+len within the table t */
static int in_table(const OFTable *t, uint32_t off, uint32_t len)
{
    return t->len && off <= t->len && len <= t->len - off;
}

uint32_t of_font_count(const uint8_t *data, size_t size)
{
    uint32_t tag;
    if (!data || size < 12)
        return 0;
    tag = of_u32(data);
    if (tag == TAG('t', 't', 'c', 'f')) {
        uint32_t n = of_u32(data + 8);
        if (n == 0 || n > (size - 12) / 4)
            return 0;
        return n;
    }
    if (tag == 0x00010000u || tag == TAG('O', 'T', 'T', 'O') || tag == TAG('t', 'r', 'u', 'e'))
        return 1;
    return 0;
}

static void find_tables(OFFont *f)
{
    const uint8_t *d = f->data + f->dir;
    uint16_t n = of_u16(d + 4), i;
    for (i = 0; i < n; ++i) {
        const uint8_t *r = d + 12 + 16u * i;
        uint32_t tag = of_u32(r), off = of_u32(r + 8), len = of_u32(r + 12);
        OFTable *t = NULL;
        if (!in_file(f, off, len))
            continue;               /* a table past the end is as good as none */
        switch (tag) {
        case TAG('h', 'e', 'a', 'd'): t = &f->head; break;
        case TAG('h', 'h', 'e', 'a'): t = &f->hhea; break;
        case TAG('h', 'm', 't', 'x'): t = &f->hmtx; break;
        case TAG('m', 'a', 'x', 'p'): t = &f->maxp; break;
        case TAG('c', 'm', 'a', 'p'): t = &f->cmap; break;
        case TAG('l', 'o', 'c', 'a'): t = &f->loca; break;
        case TAG('g', 'l', 'y', 'f'): t = &f->glyf; break;
        case TAG('C', 'F', 'F', ' '): t = &f->cff; break;
        case TAG('C', 'F', 'F', '2'): t = &f->cff2; break;
        case TAG('n', 'a', 'm', 'e'): t = &f->name; break;
        case TAG('O', 'S', '/', '2'): t = &f->os2; break;
        case TAG('p', 'o', 's', 't'): t = &f->post; break;
        case TAG('k', 'e', 'r', 'n'): t = &f->kern; break;
        case TAG('G', 'S', 'U', 'B'): t = &f->gsub; break;
        case TAG('G', 'P', 'O', 'S'): t = &f->gpos; break;
        case TAG('G', 'D', 'E', 'F'): t = &f->gdef; break;
        case TAG('C', 'O', 'L', 'R'): t = &f->colr; break;
        case TAG('C', 'B', 'D', 'T'): t = &f->cbdt; break;
        case TAG('s', 'b', 'i', 'x'): t = &f->sbix; break;
        case TAG('S', 'V', 'G', ' '): t = &f->svg; break;
        default: break;
        }
        if (t) {
            t->off = off;
            t->len = len;
        }
    }
}

/* The cmap subtable to use: a full-Unicode one first, then a BMP one, then
 * Microsoft Symbol, then any Unicode-platform one, then Macintosh Roman. */
static void choose_cmap(OFFont *f)
{
    const OFTable *t = &f->cmap;
    const uint8_t *d = f->data + t->off;
    uint16_t n, i;
    int best = 0;
    if (!in_table(t, 0, 4))
        return;
    n = of_u16(d + 2);
    if (!in_table(t, 4, 8u * n))
        return;
    for (i = 0; i < n; ++i) {
        const uint8_t *r = d + 4 + 8u * i;
        uint16_t pid = of_u16(r), eid = of_u16(r + 2);
        uint32_t off = of_u32(r + 4);
        uint16_t fmt;
        int rank = 0;
        if (!in_table(t, off, 4))
            continue;
        fmt = of_u16(d + off);
        if (fmt != 0 && fmt != 4 && fmt != 6 && fmt != 12 && fmt != 13)
            continue;
        if (pid == 3 && eid == 10)
            rank = 6;
        else if (pid == 0 && (eid == 4 || eid == 6))
            rank = 5;
        else if (pid == 3 && eid == 1)
            rank = 4;
        else if (pid == 0 && eid != 5)
            rank = 3;
        else if (pid == 3 && eid == 0)
            rank = 2;
        else if (pid == 1 && eid == 0)
            rank = 1;
        if (rank > best) {
            best = rank;
            f->cmap_sub = t->off + off;
            f->cmap_format = fmt;
        }
    }
    if (!best)
        f->cmap_sub = 0;
}

static int open_font(OFFont *f, const uint8_t *data, size_t size, uint32_t face_index, int info_only)
{
    uint32_t count, tag;
    const uint8_t *p;
    memset(f, 0, sizeof(*f));
    f->data = data;
    f->size = size;
    count = of_font_count(data, size);
    if (!count || face_index >= count)
        return OF_ERR_FONT;
    f->num_faces = count;
    if (of_u32(data) == TAG('t', 't', 'c', 'f'))
        f->dir = of_u32(data + 12 + 4u * face_index);
    if (!in_file(f, f->dir, 12))
        return OF_ERR_FONT;
    tag = of_u32(data + f->dir);
    if (tag != 0x00010000u && tag != TAG('O', 'T', 'T', 'O') && tag != TAG('t', 'r', 'u', 'e'))
        return OF_ERR_FONT;
    if (!in_file(f, f->dir + 12, 16u * of_u16(data + f->dir + 4)))
        return OF_ERR_FONT;
    find_tables(f);

    /* head, hhea and maxp are required; so is an outline table */
    if (!in_table(&f->head, 0, 54) || !in_table(&f->hhea, 0, 36) || !in_table(&f->maxp, 0, 6))
        return OF_ERR_FONT;
    p = data + f->head.off;
    f->units_per_em = of_u16(p + 18);
    if (f->units_per_em < 16 || f->units_per_em > 16384)
        return OF_ERR_FONT;
    f->x_min = of_s16(p + 36);
    f->y_min = of_s16(p + 38);
    f->x_max = of_s16(p + 40);
    f->y_max = of_s16(p + 42);
    f->mac_style = of_u16(p + 44);
    f->index_to_loc = of_s16(p + 50);
    p = data + f->hhea.off;
    f->ascender = of_s16(p + 4);
    f->descender = of_s16(p + 6);
    f->line_gap = of_s16(p + 8);
    f->num_hmetrics = of_u16(p + 34);
    f->num_glyphs = of_u16(data + f->maxp.off + 4);
    if (!f->num_glyphs)
        return OF_ERR_FONT;
    if (f->num_hmetrics > f->num_glyphs)
        f->num_hmetrics = f->num_glyphs;
    if (!info_only && (!f->num_hmetrics || !in_table(&f->hmtx, 0, 4u * f->num_hmetrics)))
        return OF_ERR_FONT;

    f->os2_version = 0xFFFF;
    if (in_table(&f->os2, 0, 78)) {
        p = data + f->os2.off;
        f->os2_version = of_u16(p);
        f->weight = of_u16(p + 4);
        f->width = of_u16(p + 6);
        f->unicode_ranges[0] = of_u32(p + 42);
        f->unicode_ranges[1] = of_u32(p + 46);
        f->unicode_ranges[2] = of_u32(p + 50);
        f->unicode_ranges[3] = of_u32(p + 54);
        f->fs_selection = of_u16(p + 62);
        f->typo_ascender = of_s16(p + 68);
        f->typo_descender = of_s16(p + 70);
        f->typo_line_gap = of_s16(p + 72);
        f->win_ascent = of_u16(p + 74);
        f->win_descent = of_u16(p + 76);
    }
    if (in_table(&f->post, 0, 16))
        f->is_fixed_pitch = of_u32(data + f->post.off + 12);
    choose_cmap(f);

    f->scalable = f->glyf.len || f->cff.len || f->cff2.len;
    if (info_only)
        return OF_OK;
    if (f->glyf.len) {
        if (!f->loca.len || (f->index_to_loc != 0 && f->index_to_loc != 1))
            return OF_ERR_FONT;
    } else if (f->cff.len) {
        int rc = of_cff_open(&f->cff_font, data + f->cff.off, f->cff.len, f->units_per_em);
        if (rc != OF_OK)
            return rc;
    }
    /* else no outlines this reads (CFF2, bitmaps only): the face still
     * opens, for its cmap and metrics, and its outlines are OF_ERR_FONT */
    return OF_OK;
}

int of_font_open(OFFont *f, const uint8_t *data, size_t size, uint32_t face_index)
{
    return open_font(f, data, size, face_index, 0);
}

int of_font_open_info(OFFont *f, const uint8_t *data, size_t size, uint32_t face_index)
{
    return open_font(f, data, size, face_index, 1);
}

void of_font_close(OFFont *f)
{
    if (f->cff_font)
        of_cff_close(f->cff_font);
    f->cff_font = NULL;
}

/* ---- cmap ---- */

static uint32_t cmap_lookup(const OFFont *f, uint32_t c)
{
    const uint8_t *d = f->data;
    uint32_t s = f->cmap_sub;
    if (!s)
        return 0;
    switch (f->cmap_format) {
    case 0:
        if (c < 256 && in_file(f, s + 6, 256))
            return d[s + 6 + c];
        return 0;
    case 6: {
        uint16_t first, count;
        if (!in_file(f, s, 10))
            return 0;
        first = of_u16(d + s + 6);
        count = of_u16(d + s + 8);
        if (c < first || c - first >= count || !in_file(f, s + 10 + 2 * (c - first), 2))
            return 0;
        return of_u16(d + s + 10 + 2 * (c - first));
    }
    case 4: {
        uint16_t segx2, segs, lo, hi;
        uint32_t ends, starts, deltas, ranges;
        if (c > 0xFFFF || !in_file(f, s, 14))
            return 0;
        segx2 = of_u16(d + s + 6);
        segs = segx2 / 2;
        ends = s + 14;
        starts = ends + segx2 + 2;
        deltas = starts + segx2;
        ranges = deltas + segx2;
        if (!segs || !in_file(f, ends, 4u * segx2 + 2))
            return 0;
        /* the first segment whose end is >= c (ends ascend) */
        lo = 0;
        hi = segs;
        while (lo < hi) {
            uint16_t mid = (uint16_t)((lo + hi) / 2);
            if (of_u16(d + ends + 2u * mid) < c)
                lo = (uint16_t)(mid + 1);
            else
                hi = mid;
        }
        if (lo >= segs)
            return 0;
        {
            uint16_t start = of_u16(d + starts + 2u * lo);
            uint16_t delta = of_u16(d + deltas + 2u * lo);
            uint16_t range = of_u16(d + ranges + 2u * lo);
            uint32_t at, g;
            if (c < start)
                return 0;
            if (!range)
                return (c + delta) & 0xFFFF;
            at = ranges + 2u * lo + range + 2u * (c - start);
            if (!in_file(f, at, 2))
                return 0;
            g = of_u16(d + at);
            return g ? ((g + delta) & 0xFFFF) : 0;
        }
    }
    case 12:
    case 13: {
        uint32_t n, lo, hi;
        if (!in_file(f, s, 16))
            return 0;
        n = of_u32(d + s + 12);
        if (n > (f->size - s - 16) / 12)
            return 0;
        lo = 0;
        hi = n;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            const uint8_t *g = d + s + 16 + 12 * mid;
            uint32_t first = of_u32(g), last = of_u32(g + 4);
            if (c < first)
                hi = mid;
            else if (c > last)
                lo = mid + 1;
            else
                return f->cmap_format == 12 ? of_u32(g + 8) + (c - first) : of_u32(g + 8);
        }
        return 0;
    }
    default:
        return 0;
    }
}

uint32_t of_font_glyph(const OFFont *f, uint32_t codepoint)
{
    uint32_t g = cmap_lookup(f, codepoint);
    /* a Microsoft Symbol font maps its characters at U+F0xx */
    if (!g && f->cmap_sub && codepoint < 0x100) {
        const uint8_t *d = f->data + f->cmap.off;
        uint16_t n = of_u16(d + 2), i;
        for (i = 0; i < n; ++i) {
            const uint8_t *r = d + 4 + 8u * i;
            if (of_u16(r) == 3 && of_u16(r + 2) == 0 && f->cmap.off + of_u32(r + 4) == f->cmap_sub) {
                g = cmap_lookup(f, 0xF000 + codepoint);
                break;
            }
        }
    }
    return g < f->num_glyphs ? g : 0;
}

/* ---- metrics ---- */

void of_font_hmetrics(const OFFont *f, uint32_t glyph, uint32_t *advance, int32_t *lsb)
{
    const uint8_t *h = f->data + f->hmtx.off;
    uint32_t n = f->num_hmetrics;
    if (glyph < n) {
        *advance = of_u16(h + 4 * glyph);
        *lsb = of_s16(h + 4 * glyph + 2);
        return;
    }
    *advance = of_u16(h + 4 * (n - 1));
    *lsb = in_table(&f->hmtx, 4 * n + 2 * (glyph - n), 2) ? of_s16(h + 4 * n + 2 * (glyph - n)) : 0;
}

/* ---- names ---- */

/* Macintosh Roman 0x80-0xFF as Unicode */
static const uint16_t mac_roman[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
};

static size_t put_utf8(char *out, size_t at, size_t size, uint32_t c)
{
    char b[4];
    size_t n, i;
    if (c < 0x80) {
        b[0] = (char)c;
        n = 1;
    } else if (c < 0x800) {
        b[0] = (char)(0xC0 | (c >> 6));
        b[1] = (char)(0x80 | (c & 0x3F));
        n = 2;
    } else if (c < 0x10000) {
        b[0] = (char)(0xE0 | (c >> 12));
        b[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        b[2] = (char)(0x80 | (c & 0x3F));
        n = 3;
    } else {
        b[0] = (char)(0xF0 | (c >> 18));
        b[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        b[3] = (char)(0x80 | (c & 0x3F));
        n = 4;
    }
    if (at + n >= size)
        return at;                  /* no room: stop at a whole character */
    for (i = 0; i < n; ++i)
        out[at + i] = b[i];
    return at + n;
}

static int decode_name(const uint8_t *s, uint16_t len, int utf16, char *out, size_t size)
{
    size_t at = 0, i;
    if (utf16) {
        for (i = 0; i + 1 < len; i += 2) {
            uint32_t c = of_u16(s + i);
            if (c >= 0xD800 && c < 0xDC00 && i + 3 < len) {
                uint32_t lo = of_u16(s + i + 2);
                if (lo >= 0xDC00 && lo < 0xE000) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }
            }
            if (!c)
                break;
            at = put_utf8(out, at, size, c);
        }
    } else {
        for (i = 0; i < len; ++i) {
            uint32_t c = s[i];
            if (!c)
                break;
            at = put_utf8(out, at, size, c < 0x80 ? c : mac_roman[c - 0x80]);
        }
    }
    out[at] = 0;
    return at > 0;
}

int of_font_name(const OFFont *f, uint16_t name_id, char *out, size_t size)
{
    const OFTable *t = &f->name;
    const uint8_t *d = f->data + t->off;
    uint16_t n, strings, i;
    int best = 0;
    const uint8_t *best_s = NULL;
    uint16_t best_len = 0;
    int best_utf16 = 0;
    if (!size)
        return 0;
    out[0] = 0;
    if (!in_table(t, 0, 6))
        return 0;
    n = of_u16(d + 2);
    strings = of_u16(d + 4);
    if (!in_table(t, 6, 12u * n))
        return 0;
    for (i = 0; i < n; ++i) {
        const uint8_t *r = d + 6 + 12u * i;
        uint16_t pid = of_u16(r), eid = of_u16(r + 2), lang = of_u16(r + 4), nid = of_u16(r + 6);
        uint16_t len = of_u16(r + 8), off = of_u16(r + 10);
        int rank = 0, utf16 = 1;
        if (nid != name_id || !len || !in_table(t, (uint32_t)strings + off, len))
            continue;
        if (pid == 3 && (eid == 1 || eid == 10))
            rank = lang == 0x0409 ? 6 : ((lang & 0xFF) == 0x09 ? 5 : 3);
        else if (pid == 0)
            rank = 4;
        else if (pid == 1 && eid == 0) {
            rank = lang == 0 ? 2 : 1;
            utf16 = 0;
        }
        if (rank > best) {
            best = rank;
            best_s = d + strings + off;
            best_len = len;
            best_utf16 = utf16;
        }
    }
    return best ? decode_name(best_s, best_len, best_utf16, out, size) : 0;
}

/* ---- TrueType outlines ---- */

#define ON_CURVE        0x01
#define X_SHORT         0x02
#define Y_SHORT         0x04
#define REPEAT          0x08
#define X_SAME          0x10
#define Y_SAME          0x20

#define ARG_WORDS       0x0001
#define ARGS_XY         0x0002
#define HAVE_SCALE      0x0008
#define MORE_COMPONENTS 0x0020
#define HAVE_XY_SCALE   0x0040
#define HAVE_2X2        0x0080
#define USE_MY_METRICS  0x0200
#define SCALED_OFFSET   0x0800
#define UNSCALED_OFFSET 0x1000

#define MAX_DEPTH 8

static int glyph_range(const OFFont *f, uint32_t g, uint32_t *off, uint32_t *len)
{
    const uint8_t *l = f->data + f->loca.off;
    uint32_t a, b;
    if (g >= f->num_glyphs)
        return 0;
    if (f->index_to_loc == 0) {
        if (!in_table(&f->loca, 2 * g, 4))
            return 0;
        a = 2u * of_u16(l + 2 * g);
        b = 2u * of_u16(l + 2 * g + 2);
    } else {
        if (!in_table(&f->loca, 4 * g, 8))
            return 0;
        a = of_u32(l + 4 * g);
        b = of_u32(l + 4 * g + 4);
    }
    if (b < a || !in_table(&f->glyf, a, b - a))
        return 0;
    *off = f->glyf.off + a;
    *len = b - a;
    return 1;
}

/* scale 0: points in 16.16 font units; else 26.6 at that 16.16 scale */
static int simple_glyph(const uint8_t *g, uint32_t len, int16_t ncont, OFPath *path, int64_t scale)
{
    uint32_t i, npts, at, flags_at, xs_len = 0, ys_len = 0, first = path->npoints;
    uint16_t ninstr;
    uint8_t *flags;
    const uint8_t *px, *py;
    int32_t x = 0, y = 0;
    uint32_t c;

    if (len < 10u + 2u * ncont + 2u)
        return OF_ERR_FONT;
    npts = (uint32_t)of_u16(g + 10 + 2 * (ncont - 1)) + 1;
    ninstr = of_u16(g + 10 + 2 * ncont);
    flags_at = 10u + 2u * ncont + 2u + ninstr;
    if (flags_at > len)
        return OF_ERR_FONT;
    flags = (uint8_t *)malloc(npts);
    if (!flags)
        return OF_ERR_MEMORY;
    /* the flags, with their repeats, and how long the coordinates are */
    at = flags_at;
    for (i = 0; i < npts;) {
        uint8_t fl, rep = 0;
        if (at >= len) {
            free(flags);
            return OF_ERR_FONT;
        }
        fl = g[at++];
        if (fl & REPEAT) {
            if (at >= len) {
                free(flags);
                return OF_ERR_FONT;
            }
            rep = g[at++];
        }
        do {
            if (i >= npts)
                break;
            flags[i++] = fl;
            xs_len += (fl & X_SHORT) ? 1 : ((fl & X_SAME) ? 0 : 2);
            ys_len += (fl & Y_SHORT) ? 1 : ((fl & Y_SAME) ? 0 : 2);
        } while (rep--);
    }
    if (at + xs_len + ys_len > len) {
        free(flags);
        return OF_ERR_FONT;
    }
    px = g + at;
    py = g + at + xs_len;
    for (i = 0; i < npts; ++i) {
        uint8_t fl = flags[i];
        if (fl & X_SHORT) {
            x += (fl & X_SAME) ? *px : -(int32_t)*px;
            px += 1;
        } else if (!(fl & X_SAME)) {
            x += of_s16(px);
            px += 2;
        }
        if (fl & Y_SHORT) {
            y += (fl & Y_SAME) ? *py : -(int32_t)*py;
            py += 1;
        } else if (!(fl & Y_SAME)) {
            y += of_s16(py);
            py += 2;
        }
        if (x < -32768 || x > 32767 || y < -32768 || y > 32767) {
            free(flags);
            return OF_ERR_FONT;     /* past what a glyph's coordinates can be */
        }
        if (scale)
            of_path_point(path, scale_one(x * 65536, scale), scale_one(y * 65536, scale),
                          (fl & ON_CURVE) ? OF_PT_ON : OF_PT_QUAD);
        else
            of_path_point(path, x * 65536, y * 65536, (fl & ON_CURVE) ? OF_PT_ON : OF_PT_QUAD);
    }
    /* the contours, checking each end follows the last */
    for (c = 0; c < (uint32_t)ncont; ++c) {
        uint32_t end = of_u16(g + 10 + 2 * c);
        uint32_t prev = c ? of_u16(g + 10 + 2 * (c - 1)) : 0;
        if (end >= npts || (c && end < prev)) {
            free(flags);
            path->npoints = first;
            return OF_ERR_FONT;
        }
        if (path->failed)
            break;
        /* of_path_close ends the contour at the last point added: add the
         * ends by moving npoints back and forth */
        {
            uint32_t keep = path->npoints;
            path->npoints = first + end + 1;
            of_path_close(path);
            path->npoints = keep;
        }
    }
    free(flags);
    return path->failed ? OF_ERR_MEMORY : OF_OK;
}

/* a composite's arithmetic kept in range however a damaged font nests and
 * scales its parts: 2^29 is 8192 em at 16.16, far past any real glyph */
static int32_t sat(int64_t v)
{
    const int64_t lim = (int64_t)1 << 29;
    return (int32_t)(v > lim ? lim : (v < -lim ? -lim : v));
}

static int32_t mul_2_14(int32_t v, int32_t m)       /* v (16.16) * m (2.14) */
{
    int64_t n = (int64_t)v * m;
    return (int32_t)(n >= 0 ? (n + 8192) >> 14 : -((-n + 8192) >> 14));
}

static int load_glyph(const OFFont *f, uint32_t gid, OFPath *path, int depth, int64_t scale)
{
    uint32_t off, len;
    const uint8_t *g;
    int16_t ncont;
    uint32_t at, base = path->npoints;     /* the composite's first point */
    uint16_t flags;

    if (depth > MAX_DEPTH)
        return OF_ERR_FONT;
    if (!glyph_range(f, gid, &off, &len))
        return OF_ERR_FONT;
    if (!len)
        return OF_OK;               /* an empty glyph */
    g = f->data + off;
    if (len < 10)
        return OF_ERR_FONT;
    ncont = of_s16(g);
    if (ncont > 0)
        return simple_glyph(g, len, ncont, path, scale);
    if (ncont == 0)
        return OF_OK;

    /* a composite */
    at = 10;
    do {
        uint32_t child, start = path->npoints, i;
        int32_t a1, a2, dx, dy;
        int32_t xx = 0x4000, xy = 0, yx = 0, yy = 0x4000;
        int transformed = 0, rc;
        if (at + 4 > len)
            return OF_ERR_FONT;
        flags = of_u16(g + at);
        child = of_u16(g + at + 2);
        at += 4;
        if (flags & ARG_WORDS) {
            if (at + 4 > len)
                return OF_ERR_FONT;
            if (flags & ARGS_XY) {
                a1 = of_s16(g + at);
                a2 = of_s16(g + at + 2);
            } else {
                a1 = of_u16(g + at);
                a2 = of_u16(g + at + 2);
            }
            at += 4;
        } else {
            if (at + 2 > len)
                return OF_ERR_FONT;
            if (flags & ARGS_XY) {
                a1 = (int8_t)g[at];
                a2 = (int8_t)g[at + 1];
            } else {
                a1 = g[at];
                a2 = g[at + 1];
            }
            at += 2;
        }
        if (flags & HAVE_SCALE) {
            if (at + 2 > len)
                return OF_ERR_FONT;
            xx = yy = of_s16(g + at);
            at += 2;
            transformed = 1;
        } else if (flags & HAVE_XY_SCALE) {
            if (at + 4 > len)
                return OF_ERR_FONT;
            xx = of_s16(g + at);
            yy = of_s16(g + at + 2);
            at += 4;
            transformed = 1;
        } else if (flags & HAVE_2X2) {
            if (at + 8 > len)
                return OF_ERR_FONT;
            xx = of_s16(g + at);
            xy = of_s16(g + at + 2);
            yx = of_s16(g + at + 4);
            yy = of_s16(g + at + 6);
            at += 8;
            transformed = 1;
        }
        rc = load_glyph(f, child, path, depth + 1, scale);
        if (rc != OF_OK)
            return rc;
        if (transformed) {
            for (i = start; i < path->npoints; ++i) {
                int32_t x = path->x[i], y = path->y[i];
                path->x[i] = sat((int64_t)mul_2_14(x, xx) + mul_2_14(y, yx));
                path->y[i] = sat((int64_t)mul_2_14(x, xy) + mul_2_14(y, yy));
            }
        }
        if (flags & ARGS_XY) {
            dx = a1 * 65536;
            dy = a2 * 65536;
            /* the offset is in the parent's space unless the component asks
             * for it scaled (as Apple's fonts do) */
            if (transformed && (flags & SCALED_OFFSET) && !(flags & UNSCALED_OFFSET)) {
                int32_t tx = sat((int64_t)mul_2_14(dx, xx) + mul_2_14(dy, yx));
                int32_t ty = sat((int64_t)mul_2_14(dx, xy) + mul_2_14(dy, yy));
                dx = tx;
                dy = ty;
            }
            if (scale) {
                dx = scale_one(dx, scale);
                dy = scale_one(dy, scale);
            }
        } else {
            /* point matching: the parent's point a1 (numbered from the
             * composite's first) meets the child's a2 */
            uint32_t pa = base + (uint32_t)a1, pc = start + (uint32_t)a2;
            if (pa >= start || pc >= path->npoints)
                return OF_ERR_FONT;
            dx = sat((int64_t)path->x[pa] - path->x[pc]);
            dy = sat((int64_t)path->y[pa] - path->y[pc]);
        }
        if (dx || dy) {
            for (i = start; i < path->npoints; ++i) {
                path->x[i] = sat((int64_t)path->x[i] + dx);
                path->y[i] = sat((int64_t)path->y[i] + dy);
            }
        }
    } while (flags & MORE_COMPONENTS);
    return path->failed ? OF_ERR_MEMORY : OF_OK;
}

/* The glyph whose metrics a glyph uses: a composite takes a component's
 * advance and side bearing when that component says USE_MY_METRICS. */
static uint32_t metrics_glyph(const OFFont *f, uint32_t gid)
{
    int depth;
    if (f->cff_font)
        return gid;
    for (depth = 0; depth <= MAX_DEPTH; ++depth) {
        uint32_t off, len, at = 10, next = gid;
        const uint8_t *g;
        uint16_t flags;
        if (!glyph_range(f, gid, &off, &len) || len < 10)
            return gid;
        g = f->data + off;
        if (of_s16(g) >= 0)
            return gid;
        do {
            if (at + 4 > len)
                return gid;
            flags = of_u16(g + at);
            if (flags & USE_MY_METRICS) {
                next = of_u16(g + at + 2);
                break;
            }
            at += 4 + ((flags & ARG_WORDS) ? 4 : 2);
            if (flags & HAVE_SCALE)
                at += 2;
            else if (flags & HAVE_XY_SCALE)
                at += 4;
            else if (flags & HAVE_2X2)
                at += 8;
        } while (flags & MORE_COMPONENTS);
        if (next == gid || next >= f->num_glyphs)
            return gid;
        gid = next;
    }
    return gid;
}

static int outline(const OFFont *f, uint32_t glyph, uint32_t ppem, OFPath *path)
{
    int rc;
    uint32_t keep_points = path->npoints, keep_contours = path->ncontours, i;
    int64_t scale = ppem ? of_scale_16_16(f->units_per_em, ppem) : 0;
    if (glyph >= f->num_glyphs)
        return OF_ERR_FONT;
    if (f->cff_font) {
        rc = of_cff_outline(f->cff_font, glyph, path);
        if (rc == OF_OK && scale)
            for (i = keep_points; i < path->npoints; ++i) {
                path->x[i] = scale_one(path->x[i], scale);
                path->y[i] = scale_one(path->y[i], scale);
            }
    } else {
        rc = load_glyph(f, glyph, path, 0, scale);
        /* The horizontal origin is where hmtx puts it: the OpenType spec
         * sets phantom point 1 at xMin - lsb, so a glyph whose header xMin
         * and hmtx lsb disagree moves by the difference */
        if (rc == OF_OK && path->npoints > keep_points) {
            uint32_t off, len, adv;
            int32_t lsb, shift;
            if (glyph_range(f, glyph, &off, &len) && len >= 10) {
                of_font_hmetrics(f, glyph, &adv, &lsb);
                shift = lsb - of_s16(f->data + off + 2);
                if (shift) {
                    shift = sat((int64_t)shift * 65536);
                    if (scale)
                        shift = scale_one(shift, scale);
                    for (i = keep_points; i < path->npoints; ++i)
                        path->x[i] += shift;
                }
            }
        }
    }
    if (rc == OF_OK && path->failed)
        rc = OF_ERR_MEMORY;
    if (rc != OF_OK) {
        path->npoints = keep_points;
        path->ncontours = keep_contours;
        path->failed = 0;
    }
    return rc;
}

int of_font_outline(const OFFont *f, uint32_t glyph, OFPath *path)
{
    return outline(f, glyph, 0, path);
}

int of_font_outline_px(const OFFont *f, uint32_t glyph, uint32_t ppem, OFPath *path)
{
    return ppem ? outline(f, glyph, ppem, path) : OF_ERR_ARGUMENT;
}

/* ---- metrics ---- */

int32_t of_font_advance_px(const OFFont *f, uint32_t glyph, uint32_t ppem)
{
    uint32_t adv;
    int32_t lsb;
    int64_t scale = of_scale_16_16(f->units_per_em, ppem);
    int32_t origin = 0;
    uint32_t off, len;
    of_font_hmetrics(f, metrics_glyph(f, glyph), &adv, &lsb);
    /* TrueType: from the origin to the advance point, each placed in font
     * units (phantom points 1 and 2: xMin - lsb, and that plus the
     * advance) and scaled */
    if (!f->cff_font && glyph_range(f, glyph, &off, &len) && len >= 10) {
        uint32_t a2;
        int32_t l2;
        of_font_hmetrics(f, glyph, &a2, &l2);
        origin = of_s16(f->data + off + 2) - l2;
    }
    return scale_one(sat(((int64_t)origin + adv) * 65536), scale) - scale_one(sat((int64_t)origin * 65536), scale);
}

void of_font_line_units(const OFFont *f, int32_t *ascender, int32_t *descender, int32_t *height)
{
    int32_t a = f->ascender, d = f->descender, gap = f->line_gap;
    /* OS/2 fsSelection bit 7, USE_TYPO_METRICS: the font asks for its
     * typographic metrics */
    if (f->os2_version != 0xFFFF && (f->fs_selection & 0x80) &&
        (f->typo_ascender || f->typo_descender)) {
        a = f->typo_ascender;
        d = f->typo_descender;
        gap = f->typo_line_gap;
    } else if (!a && !d && f->os2_version != 0xFFFF) {
        a = f->typo_ascender;
        d = f->typo_descender;
        gap = f->typo_line_gap;
        if (!a && !d) {
            a = f->win_ascent;
            d = -(int32_t)f->win_descent;
            gap = 0;
        }
    }
    *ascender = a;
    *descender = d;
    *height = a - d + gap;
    if (*height <= 0)
        *height = f->units_per_em * 12 / 10;
}

void of_font_line_px(const OFFont *f, uint32_t ppem, int32_t *ascender, int32_t *descender, int32_t *height)
{
    int32_t a, d, h;
    int64_t scale = of_scale_16_16(f->units_per_em, ppem);
    of_font_line_units(f, &a, &d, &h);
    a = scale_one(a * 65536, scale);
    d = scale_one(d * 65536, scale);
    h = scale_one(h * 65536, scale);
    *ascender = (a + 63) & ~63;
    *descender = d & ~63;
    *height = (h + 32) & ~63;
}

unsigned of_font_style(const OFFont *f)
{
    unsigned s = 0;
    if (f->os2_version != 0xFFFF) {
        if (f->fs_selection & 0x20)
            s |= OF_STYLE_BOLD;
        if (f->fs_selection & 0x01)
            s |= OF_STYLE_ITALIC;
    } else {
        if (f->mac_style & 1)
            s |= OF_STYLE_BOLD;
        if (f->mac_style & 2)
            s |= OF_STYLE_ITALIC;
    }
    return s;
}

int of_font_has_color(const OFFont *f)
{
    return f->colr.len || f->cbdt.len || f->sbix.len || f->svg.len;
}
