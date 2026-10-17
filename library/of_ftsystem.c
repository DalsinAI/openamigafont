/* FreeType's system layer inside openfont.library, in place of its
 * ftsystem.c: memory from the library's pool (of_runtime.c), and no files.
 * OpenFont gives FreeType every face from memory or through its own
 * stream (src/openfont.c), so FreeType never opens a file by name.
 *
 * Copyright (c) 2026 AmigaChrome Team. OpenFont, MIT licence (LICENSE). */
#include <ft2build.h>
#include FT_CONFIG_CONFIG_H
#include <freetype/internal/ftdebug.h>
#include <freetype/internal/ftstream.h>
#include <freetype/ftsystem.h>
#include <freetype/fterrors.h>
#include <freetype/fttypes.h>

#include <stdlib.h>

static void *of_ft_alloc(FT_Memory memory, long size)
{
    (void)memory;
    return malloc((size_t)size);
}

static void of_ft_free(FT_Memory memory, void *block)
{
    (void)memory;
    free(block);
}

static void *of_ft_realloc(FT_Memory memory, long cur_size, long new_size, void *block)
{
    (void)memory;
    (void)cur_size;
    return realloc(block, (size_t)new_size);
}

static struct FT_MemoryRec_ of_ft_memory = { NULL, of_ft_alloc, of_ft_free, of_ft_realloc };

FT_BASE_DEF( FT_Memory )
FT_New_Memory( void )
{
    return &of_ft_memory;
}

FT_BASE_DEF( void )
FT_Done_Memory( FT_Memory memory )
{
    (void)memory;
}

FT_BASE_DEF( FT_Error )
FT_Stream_Open( FT_Stream stream, const char *filepathname )
{
    (void)filepathname;
    if (stream)
        stream->base = NULL;
    return FT_Err_Cannot_Open_Resource;
}
