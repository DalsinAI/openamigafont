#ifndef OPENFONT_OPENFONT_H
#define OPENFONT_OPENFONT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OF_VERSION_MAJOR 0
#define OF_VERSION_MINOR 3
#define OF_VERSION_PATCH 0

/* Glyphs in one of_cache_fill_range call before the OpenMulticore path is worth it.
 * Below this, the same worker runs in a loop on this CPU. */
#define OF_RANGE_BREAK_EVEN 8
#define OF_RANGE_PIXELS 4096

typedef struct OFContext OFContext;
typedef struct OFFace OFFace;
typedef struct OFCache OFCache;

typedef struct OFGlyph {
    uint32_t glyph_id;
    uint32_t cluster;
    int32_t x_advance_26_6;
    int32_t y_advance_26_6;
    int32_t x_offset_26_6;
    int32_t y_offset_26_6;
} OFGlyph;

typedef struct OFBitmap {
    uint32_t width;
    uint32_t height;
    int32_t left;
    int32_t top;
    int32_t advance_x_26_6;
    uint32_t stride;
    uint8_t *pixels;
} OFBitmap;

/* One codepoint of a range. sizeof is a multiple of 16 so it can be an
 * OpenMulticore slice: index i writes only slots[i]. */
typedef struct OFRangeSlot {
    int32_t status;
    uint32_t glyph_id;
    int32_t left;
    int32_t top;
    int32_t advance_x_26_6;
    uint32_t width;
    uint32_t height;
    uint32_t npixels;
    uint8_t pixels[OF_RANGE_PIXELS];
} OFRangeSlot;

typedef int (*OFIndexFn)(uint32_t index, void *arg);

/* run_range may call fn for distinct indices at the same time. fn writes
 * only its own index. NULL run_range, or a count below break_even, stays
 * on this CPU. break_even 0 means OF_RANGE_BREAK_EVEN. */
typedef struct OFExec {
    int (*run_range)(OFIndexFn fn, uint32_t count, void *arg, void *user);
    void *user;
    uint32_t break_even;
} OFExec;

enum {
    OF_OK = 0,
    OF_ERR_ARGUMENT = -1,
    OF_ERR_MEMORY = -2,
    OF_ERR_IO = -3,
    OF_ERR_FONT = -4,
    OF_ERR_RANGE = -5,
    OF_ERR_BUFFER = -6,
    OF_ERR_CACHE = -7,
    OF_ERR_EXEC = -8
};

OFContext *of_context_create(void);
void of_context_destroy(OFContext *ctx);

OFFace *of_face_open(OFContext *ctx, const char *path);
void of_face_close(OFFace *face);
int of_face_set_pixel_size(OFFace *face, uint32_t pixels);

size_t of_shape_utf8(OFFace *face, const char *utf8, size_t bytes,
                     OFGlyph *out, size_t capacity);
int64_t of_measure_utf8(OFFace *face, const char *utf8, size_t bytes);

int of_raster_glyph_a8(OFFace *face, uint32_t glyph_id, OFBitmap *out);
void of_bitmap_release(OFBitmap *bitmap);

uint64_t of_face_source_fingerprint(const OFFace *face);

int of_compile_cache(OFFace *face, const char *path,
                     const uint16_t *sizes, size_t size_count,
                     uint32_t first_codepoint, uint32_t last_codepoint);

OFCache *of_cache_open(const char *path);
void of_cache_close(OFCache *cache);
uint64_t of_cache_source_fingerprint(const OFCache *cache);
int of_cache_matches_face(const OFCache *cache, const OFFace *face);

/* Returns a malloc-owned A8 bitmap compatible with of_bitmap_release().
 * glyph_id may be NULL. */
int of_cache_get_a8(OFCache *cache, uint16_t pixel_size, uint32_t codepoint,
                    uint32_t *glyph_id, OFBitmap *out);

/* Copy one cached glyph into slot. No allocation. A bitmap larger than
 * OF_RANGE_PIXELS sets OF_ERR_BUFFER and npixels to the size needed. */
int of_cache_fill_slot(const OFCache *cache, uint16_t pixel_size,
                       uint32_t codepoint, OFRangeSlot *slot);

/* Fill slots[0 .. count) for codepoints first .. first+count-1.
 * exec NULL is the 68k fallback. The same worker runs either way. */
int of_cache_fill_range(const OFCache *cache, uint16_t pixel_size,
                        uint32_t first_codepoint, uint32_t count,
                        OFRangeSlot *slots, const OFExec *exec);

const char *of_error_string(int code);

#ifdef __cplusplus
}
#endif

#endif
