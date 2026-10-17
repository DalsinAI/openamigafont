/* OpenFont's shaper: UTF-8 text to positioned glyphs, for left-to-right
 * scripts written letter by letter (Latin, Greek, Cyrillic and the like).
 *
 * Copyright (c) 2026 AmigaChrome Team. MIT licence (see LICENSE).
 *
 * Written from the OpenType specification (its default features and
 * layout processing) and the Unicode Standard (canonical equivalence, the
 * default ignorables, grapheme clusters). No HarfBuzz code.
 *
 * The steps:
 *   1. UTF-8 to characters, each with the byte offset it starts at (its
 *      cluster); a byte that starts no valid sequence is one U+FFFD;
 *   2. marks and joiners join the cluster of the character before them;
 *   3. characters the font lacks are decomposed into ones it has, marks put
 *      in canonical order, and base and mark composed again where the font
 *      has the composed character;
 *   4. characters to glyphs (cmap); a space the font lacks is drawn as its
 *      space, as wide as that space should be;
 *   5. GSUB with the default features (fractions only around U+2044);
 *   6. advances, then GPOS (or 'kern'), marks' advances zeroed and marks
 *      placed on their bases;
 *   7. the default ignorables hidden: the space glyph, no advance. */
#include "openfont/openfont.h"
#include "of_layout.h"
#include "of_unicode.h"

#include <stdlib.h>
#include <string.h>

uint32_t of_script_tag(unsigned script)
{
    static const struct {
        uint16_t script;
        uint32_t tag;
    } tags[] = {
        { OF_SCRIPT_LATIN, OF_TAG('l', 'a', 't', 'n') },
        { OF_SCRIPT_GREEK, OF_TAG('g', 'r', 'e', 'k') },
        { OF_SCRIPT_CYRILLIC, OF_TAG('c', 'y', 'r', 'l') },
        { OF_SCRIPT_ARMENIAN, OF_TAG('a', 'r', 'm', 'n') },
        { OF_SCRIPT_GEORGIAN, OF_TAG('g', 'e', 'o', 'r') },
        { OF_SCRIPT_HEBREW, OF_TAG('h', 'e', 'b', 'r') },
        { OF_SCRIPT_ARABIC, OF_TAG('a', 'r', 'a', 'b') },
        { OF_SCRIPT_THAI, OF_TAG('t', 'h', 'a', 'i') },
        { OF_SCRIPT_HAN, OF_TAG('h', 'a', 'n', 'i') },
        { OF_SCRIPT_HIRAGANA, OF_TAG('k', 'a', 'n', 'a') },
        { OF_SCRIPT_KATAKANA, OF_TAG('k', 'a', 'n', 'a') },
        { OF_SCRIPT_HANGUL, OF_TAG('h', 'a', 'n', 'g') },
        { OF_SCRIPT_DEVANAGARI, OF_TAG('d', 'e', 'v', '2') },
        { OF_SCRIPT_BENGALI, OF_TAG('b', 'n', 'g', '2') },
    };
    size_t i;
    for (i = 0; i < sizeof tags / sizeof tags[0]; ++i)
        if (tags[i].script == script)
            return tags[i].tag;
    return OF_TAG('D', 'F', 'L', 'T');
}

/* ---- 1. UTF-8 ---- */

/* the character at p (n bytes left) and its length; a byte that starts no
 * valid sequence (short, overlong, a surrogate, past U+10FFFF) is U+FFFD,
 * one byte long */
static uint32_t utf8_next(const uint8_t *p, size_t n, size_t *used)
{
    uint32_t c = p[0], min, k, len;
    *used = 1;
    if (c < 0x80)
        return c;
    if (c >= 0xC2 && c <= 0xDF) {
        len = 2;
        c &= 0x1F;
        min = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
        len = 3;
        c &= 0x0F;
        min = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
        len = 4;
        c &= 0x07;
        min = 0x10000;
    } else {
        return 0xFFFD;
    }
    if (n < len)
        return 0xFFFD;
    for (k = 1; k < len; ++k) {
        if ((p[k] & 0xC0) != 0x80)
            return 0xFFFD;
        c = (c << 6) | (p[k] & 0x3F);
    }
    if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
        return 0xFFFD;
    *used = len;
    return c;
}

/* ---- 2. clusters ---- */

/* a character that belongs with the one before it: a mark, a joiner, an
 * emoji modifier, a variation selector or a tag */
static int continues(uint32_t cp)
{
    if (of_uni_is_mark(cp))
        return 1;
    return cp == 0x200D || (cp >= 0x1F3FB && cp <= 0x1F3FF) ||
           (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0020 && cp <= 0xE007F) ||
           (cp >= 0xE0100 && cp <= 0xE01EF);
}

/* ---- 3. composed and decomposed characters ---- */

static int has(const OFFont *f, uint32_t cp)
{
    return of_font_glyph(f, cp) != 0;
}

/* cp's canonical decomposition into characters the font has, appended to
 * out (room for 32); how many, or 0 when there is none */
static int decompose(const OFFont *f, uint32_t cp, uint32_t *out, int depth)
{
    uint32_t a, b;
    int n, k = of_uni_decompose(cp, &a, &b);
    if (!k || depth > 8)
        return 0;
    if (k == 2 && !has(f, b)) {
        /* b may itself decompose (rarely): then not this way */
        return 0;
    }
    if (has(f, a)) {
        out[0] = a;
        n = 1;
    } else if ((n = decompose(f, a, out, depth + 1)) == 0) {
        return 0;
    }
    if (k == 2)
        out[n++] = b;
    return n;
}

static int insert_at(OFShBuf *b, uint32_t at, uint32_t count)
{
    if (!of_shbuf_reserve(b, count))
        return 0;
    memmove(&b->g[at + count], &b->g[at], (b->n - at) * sizeof(OFShGlyph));
    b->n += count;
    return 1;
}

static void normalise(const OFFont *f, OFShBuf *b)
{
    uint32_t i, j;
    /* decompose what the font lacks */
    for (i = 0; i < b->n; ++i) {
        uint32_t parts[32];
        int n;
        if (has(f, b->g[i].cp) || (n = decompose(f, b->g[i].cp, parts, 0)) == 0)
            continue;
        if (n > 1 && !insert_at(b, i + 1, (uint32_t)n - 1))
            return;
        for (j = 0; j < (uint32_t)n; ++j) {
            b->g[i + j] = b->g[i];
            b->g[i + j].cp = parts[j];
            b->g[i + j].props &= (uint16_t)~OF_SH_CONTINUATION;
        }
        i += (uint32_t)n - 1;
    }
    /* marks in canonical order: each run of non-starters sorted by class,
     * keeping the order of equal ones */
    for (i = 0; i < b->n; ++i) {
        uint32_t end, k;
        if (!of_uni_combining_class(b->g[i].cp))
            continue;
        for (end = i; end < b->n && of_uni_combining_class(b->g[end].cp); ++end)
            ;
        for (k = i + 1; k < end; ++k) {
            OFShGlyph x = b->g[k];
            unsigned cc = of_uni_combining_class(x.cp);
            uint32_t m = k;
            while (m > i && of_uni_combining_class(b->g[m - 1].cp) > cc) {
                b->g[m] = b->g[m - 1];
                m--;
            }
            b->g[m] = x;
        }
        i = end - 1;
    }
    /* compose a base with the marks after it, where the font has the
     * composed character and nothing between blocks it */
    for (i = 0; i < b->n; ++i) {
        unsigned last_cc = 0;
        if (of_uni_combining_class(b->g[i].cp))
            continue;
        for (j = i + 1; j < b->n;) {
            unsigned cc = of_uni_combining_class(b->g[j].cp);
            uint32_t c;
            if (!of_uni_is_mark(b->g[j].cp))
                break;
            if ((last_cc < cc || (last_cc == 0 && j == i + 1)) &&
                (c = of_uni_compose(b->g[i].cp, b->g[j].cp)) != 0 && has(f, c)) {
                b->g[i].cp = c;
                memmove(&b->g[j], &b->g[j + 1], (b->n - j - 1) * sizeof(OFShGlyph));
                b->n--;
                continue;
            }
            if (!cc)
                break;
            last_cc = cc;
            j++;
        }
    }
}

/* ---- 4. spaces the font lacks ---- */

enum { SP_NONE, SP_SPACE, SP_EM, SP_EM_2, SP_EM_3, SP_EM_4, SP_EM_5, SP_EM_6, SP_EM_16, SP_4_EM_18, SP_FIGURE, SP_PUNCTUATION, SP_NARROW };

static int space_kind(uint32_t cp)
{
    switch (cp) {
    case 0x0020: case 0x00A0: return SP_SPACE;
    case 0x2000: case 0x2002: return SP_EM_2;
    case 0x2001: case 0x2003: case 0x3000: return SP_EM;
    case 0x2004: return SP_EM_3;
    case 0x2005: return SP_EM_4;
    case 0x2006: return SP_EM_6;
    case 0x2007: return SP_FIGURE;
    case 0x2008: return SP_PUNCTUATION;
    case 0x2009: return SP_EM_5;
    case 0x200A: return SP_EM_16;
    case 0x202F: return SP_NARROW;
    case 0x205F: return SP_4_EM_18;
    default: return of_uni_category(cp) == OF_GC_ZS ? SP_SPACE : SP_NONE;
    }
}

static int32_t space_width(const OFFont *f, uint32_t ppem, int kind, uint32_t space_gid)
{
    int32_t em = (int32_t)ppem * 64;
    uint32_t g;
    switch (kind) {
    case SP_EM: return em;
    /* the fractions of an em to the nearest 64th, but the medium
     * mathematical space's 4/18 and the narrow space's half rounded down,
     * as other shapers draw them */
    case SP_EM_2: return (em + 1) / 2;
    case SP_EM_3: return (em + 1) / 3;
    case SP_EM_4: return (em + 2) / 4;
    case SP_EM_5: return (em + 2) / 5;
    case SP_EM_6: return (em + 3) / 6;
    case SP_EM_16: return (em + 8) / 16;
    case SP_4_EM_18: return em * 4 / 18;
    case SP_FIGURE:
        g = of_font_glyph(f, '0');
        return g ? of_font_advance_px(f, g, ppem) : em / 2;
    case SP_PUNCTUATION:
        g = of_font_glyph(f, '.');
        return g ? of_font_advance_px(f, g, ppem) : em / 5;
    case SP_NARROW:
        return of_font_advance_px(f, space_gid, ppem) / 2;
    default:
        return of_font_advance_px(f, space_gid, ppem);
    }
}

/* ---- 5. fractions ---- */

static void fractions(OFShBuf *b)
{
    uint32_t i;
    for (i = 0; i < b->n; ++i) {
        uint32_t start, end, k;
        if (b->g[i].cp != 0x2044)
            continue;
        for (start = i; start > 0 && of_uni_category(b->g[start - 1].cp) == OF_GC_ND; --start)
            ;
        for (end = i + 1; end < b->n && of_uni_category(b->g[end].cp) == OF_GC_ND; ++end)
            ;
        if (start == i || end == i + 1)
            continue;
        for (k = start; k < end; ++k)
            b->g[k].mask |= OF_MASK_FRAC | (k < i ? OF_MASK_NUMR : (k > i ? OF_MASK_DNOM : 0));
        of_layout_unsafe(b, (long)start, (long)end - 1);   /* one fraction */
        i = end - 1;
    }
}

/* ---- the whole ---- */

int of_shape_text(const OFFont *f, uint32_t ppem, const char *text, size_t bytes,
                  size_t off, size_t len, unsigned *script, OFShBuf *b)
{
    const uint8_t *p = (const uint8_t *)text;
    size_t at = off, end = off + len;
    uint32_t i, space;
    uint32_t tag;
    int rc;

    b->n = 0;
    b->failed = 0;
    if (end > bytes)
        end = bytes;
    /* 1, 2: characters and clusters */
    while (at < end) {
        size_t used;
        uint32_t cp = utf8_next(p + at, end - at, &used);
        OFShGlyph *x;
        if (!of_shbuf_reserve(b, 1))
            return OF_ERR_MEMORY;
        x = &b->g[b->n];
        memset(x, 0, sizeof(*x));
        x->cp = cp;
        x->cluster = (uint32_t)at;
        x->mask = OF_MASK_GLOBAL;
        x->attach_to = -1;
        if (b->n && continues(cp)) {
            x->cluster = b->g[b->n - 1].cluster;
            x->props |= OF_SH_CONTINUATION;
        }
        if (of_uni_default_ignorable(cp))
            x->props |= OF_SH_IGNORABLE;
        if (cp == 0x200D)
            x->props |= OF_SH_ZWJ;
        if (cp == 0x200C)
            x->props |= OF_SH_ZWNJ;
        b->n++;
        at += used;
    }
    /* the run's script: its first letter's that belongs to one */
    if (*script == OF_SCRIPT_UNKNOWN) {
        *script = OF_SCRIPT_COMMON;
        for (i = 0; i < b->n; ++i) {
            unsigned s = of_uni_script(b->g[i].cp);
            if (s != OF_SCRIPT_COMMON && s != OF_SCRIPT_INHERITED && s != OF_SCRIPT_UNKNOWN) {
                *script = s;
                break;
            }
        }
    }
    tag = of_script_tag(*script);

    /* 3 */
    normalise(f, b);
    if (b->failed)
        return OF_ERR_MEMORY;

    /* 4: glyphs */
    space = of_font_glyph(f, ' ');
    for (i = 0; i < b->n; ++i) {
        OFShGlyph *x = &b->g[i];
        x->gid = of_font_glyph(f, x->cp);
        if (!x->gid && space) {
            int kind = space_kind(x->cp);
            if (kind != SP_NONE) {
                x->gid = space;
                x->props |= OF_SH_SPACE_FALLBACK;
                x->space_kind = (uint8_t)kind;
            }
        }
        if (!x->gid && x->cp == 0x2011)
            x->gid = of_font_glyph(f, 0x2010);  /* a non-breaking hyphen looks like a hyphen */
    }
    /* fractions, when the font has them */
    if (of_layout_has_gsub_feature(f, tag, OF_TAG('f', 'r', 'a', 'c')) ||
        of_layout_has_gsub_feature(f, tag, OF_TAG('n', 'u', 'm', 'r')) ||
        of_layout_has_gsub_feature(f, tag, OF_TAG('d', 'n', 'o', 'm')))
        fractions(b);

    /* 5 */
    of_layout_classes(f, b);
    if ((rc = of_layout_substitute(f, b, tag)) != OF_OK)
        return rc;

    /* 6 */
    for (i = 0; i < b->n; ++i) {
        OFShGlyph *x = &b->g[i];
        x->xa = (x->props & OF_SH_SPACE_FALLBACK) ? space_width(f, ppem, x->space_kind, space)
                                                  : of_font_advance_px(f, x->gid, ppem);
        x->ya = x->xo = x->yo = 0;
    }
    if ((rc = of_layout_position(f, b, tag, ppem)) != OF_OK)
        return rc;

    /* 7 */
    for (i = 0; i < b->n; ++i) {
        OFShGlyph *x = &b->g[i];
        if (!(x->props & OF_SH_IGNORABLE))
            continue;
        /* with no GPOS, a hidden character of its own is marked unsafe to
         * break before, as other shapers mark it */
        if (!f->gpos.len && i > 0)
            of_layout_unsafe(b, (long)i - 1, (long)i);
        if (space) {
            x->gid = space;
            x->xa = x->ya = 0;
        } else {
            memmove(x, x + 1, (b->n - i - 1) * sizeof(OFShGlyph));
            b->n--;
            i--;
        }
    }
    return OF_OK;
}
