#ifndef OPENFONT_OPENFONT_H
#define OPENFONT_OPENFONT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OF_VERSION_MAJOR 0
#define OF_VERSION_MINOR 2
#define OF_VERSION_PATCH 0

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

enum {
    OF_OK = 0,
    OF_ERR_ARGUMENT = -1,
    OF_ERR_MEMORY = -2,
    OF_ERR_IO = -3,
    OF_ERR_FONT = -4,
    OF_ERR_RANGE = -5,
    OF_ERR_BUFFER = -6,
    OF_ERR_CACHE = -7
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

const char *of_error_string(int code);

#ifdef __cplusplus
}
#endif

#endif
