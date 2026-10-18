cmake_minimum_required(VERSION 3.16)
project(OpenFont C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Fonts are read, shaped and drawn by OpenFont's own code (src/of_*.c); it
# needs nothing but a C compiler.
add_library(openfont STATIC src/openfont.c src/of_font.c src/of_cff.c src/of_raster.c
            src/of_layout.c src/of_shape.c src/of_unicode_data.c
            src/openfont_cache.c src/openfont_io_stdio.c src/openfont_io_join.c src/openfont_io_posixdir.c src/openfont_index.c)
target_include_directories(openfont PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(openfontcompile tools/openfontcompile.c)
target_link_libraries(openfontcompile PRIVATE openfont)

add_executable(test_openfont tests/test_openfont.c)
target_link_libraries(test_openfont PRIVATE openfont)

add_executable(test_cache tests/test_cache.c)
target_link_libraries(test_cache PRIVATE openfont)

add_executable(test_range tests/test_range.c)
target_include_directories(test_range PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_range PRIVATE openfont)

enable_testing()
find_file(OPENFONT_TEST_FONT
    NAMES DejaVuSans.ttf LiberationSans-Regular.ttf
    PATHS /usr/share/fonts/truetype/dejavu
          /usr/share/fonts/truetype/liberation2
          /usr/share/fonts/truetype/liberation)
if(OPENFONT_TEST_FONT)
    add_test(NAME openfont_core COMMAND test_openfont ${OPENFONT_TEST_FONT})
    add_test(NAME openfont_compile
             COMMAND openfontcompile ${OPENFONT_TEST_FONT}
                     ${CMAKE_CURRENT_BINARY_DIR}/test.ofc 9 12 16 24)
    add_test(NAME openfont_cache_roundtrip
             COMMAND test_cache ${OPENFONT_TEST_FONT}
                     ${CMAKE_CURRENT_BINARY_DIR}/roundtrip.ofc)
endif()
add_test(NAME openfont_range COMMAND test_range)

add_executable(test_index tests/test_index.c)
target_include_directories(test_index PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_index PRIVATE openfont)
find_path(OPENFONT_DEJAVU_DIR DejaVuSans.ttf PATHS /usr/share/fonts/truetype/dejavu)
find_path(OPENFONT_LIBERATION_DIR LiberationSans-Regular.ttf
    PATHS /usr/share/fonts/truetype/liberation2 /usr/share/fonts/truetype/liberation)
if(OPENFONT_DEJAVU_DIR AND OPENFONT_LIBERATION_DIR)
    add_test(NAME openfont_index COMMAND test_index ${CMAKE_CURRENT_BINARY_DIR}/fonts.index
             ${OPENFONT_DEJAVU_DIR} ${OPENFONT_LIBERATION_DIR})
endif()

# The Unicode properties and the shaper; with DejaVu Sans, shaping too.
add_executable(test_shape tests/test_shape.c)
target_include_directories(test_shape PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_shape PRIVATE openfont)
add_test(NAME openfont_shape COMMAND test_shape)
find_file(OPENFONT_DEJAVU_SANS NAMES DejaVuSans.ttf PATHS /usr/share/fonts/truetype/dejavu)
if(OPENFONT_DEJAVU_SANS)
    add_test(NAME openfont_shape_dejavu COMMAND test_shape ${OPENFONT_DEJAVU_SANS}
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/shape/latin.txt)
endif()

# The reader and rasteriser alone, on any fonts at hand (no reference).
add_executable(test_reader tests/test_reader.c)
target_include_directories(test_reader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_reader PRIVATE openfont)
add_test(NAME openfont_reader COMMAND test_reader)
find_file(OPENFONT_TEST_OTF NAMES NimbusSans-Regular.otf PATHS /usr/share/fonts/opentype/urw-base35)
find_file(OPENFONT_TEST_TTC NAMES NotoSansCJK-Regular.ttc PATHS /usr/share/fonts/opentype/noto)
if(OPENFONT_TEST_FONT)
    add_test(NAME openfont_reader_fonts COMMAND test_reader ${OPENFONT_TEST_FONT} ${OPENFONT_TEST_OTF} ${OPENFONT_TEST_TTC})
endif()

# The reference gate: OpenFont's reader and rasteriser against FreeType
# (unhinted), given a FreeType build (its source folder, with the library
# built in build/). Only for testing; nothing of FreeType's is in OpenFont.
set(OPENFONT_FREETYPE_REF "" CACHE PATH "a FreeType source tree with build/libfreetype.a, for compare_freetype")
if(OPENFONT_FREETYPE_REF)
    add_executable(compare_freetype tests/ref/compare_freetype.c src/of_font.c src/of_cff.c src/of_raster.c)
    target_include_directories(compare_freetype PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src
                               ${OPENFONT_FREETYPE_REF}/include ${OPENFONT_FREETYPE_REF}/build/include)
    target_link_libraries(compare_freetype PRIVATE ${OPENFONT_FREETYPE_REF}/build/libfreetype.a m)
    if(OPENFONT_TEST_FONT)
        add_test(NAME openfont_compare_freetype COMMAND compare_freetype ${OPENFONT_TEST_FONT} ${OPENFONT_TEST_OTF})
    endif()
endif()

# The reference gate for the shaper: OpenFont against HarfBuzz, given
# HarfBuzz's release sources (the folder harfbuzz-14.5.1.tar.xz unpacks to),
# built here for this test only; nothing of HarfBuzz's is in OpenFont.
set(OPENFONT_HARFBUZZ_REF "" CACHE PATH "HarfBuzz sources, for compare_harfbuzz")
if(OPENFONT_HARFBUZZ_REF)
    enable_language(CXX)
    add_library(openfont_ref_harfbuzz STATIC ${OPENFONT_HARFBUZZ_REF}/src/harfbuzz.cc)
    target_compile_features(openfont_ref_harfbuzz PRIVATE cxx_std_17)
    target_compile_options(openfont_ref_harfbuzz PRIVATE -fno-exceptions -fno-rtti)
    target_compile_definitions(openfont_ref_harfbuzz PRIVATE HB_NO_MT)
    target_include_directories(openfont_ref_harfbuzz PUBLIC ${OPENFONT_HARFBUZZ_REF}/src)
    add_executable(compare_harfbuzz tests/ref/compare_harfbuzz.c)
    target_include_directories(compare_harfbuzz PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(compare_harfbuzz PRIVATE openfont openfont_ref_harfbuzz)
    set_target_properties(compare_harfbuzz PROPERTIES LINKER_LANGUAGE CXX)
    if(OPENFONT_TEST_FONT)
        add_test(NAME openfont_compare_harfbuzz COMMAND compare_harfbuzz
                 ${CMAKE_CURRENT_SOURCE_DIR}/tests/shape/latin.txt ${OPENFONT_TEST_FONT} ${OPENFONT_TEST_OTF})
    endif()
endif()
