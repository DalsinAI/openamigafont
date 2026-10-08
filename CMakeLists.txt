cmake_minimum_required(VERSION 3.16)
project(OpenFont C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)

add_library(openfont STATIC src/openfont.c)
target_include_directories(openfont PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openfont PRIVATE PkgConfig::FREETYPE PkgConfig::HARFBUZZ)

add_executable(openfontcompile tools/openfontcompile.c)
target_link_libraries(openfontcompile PRIVATE openfont)

add_executable(test_openfont tests/test_openfont.c)
target_link_libraries(test_openfont PRIVATE openfont)

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
endif()
