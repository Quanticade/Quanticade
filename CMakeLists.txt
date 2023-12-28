cmake_minimum_required(VERSION 3.5)
project(Quanticade VERSION 0.4 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Werror -Wextra -Wpedantic -Wno-error=vla)
# Optimizations
add_compile_options(-march=native -Ofast -fomit-frame-pointer -DUSE_SSE41 -msse4.1 -DUSE_SSSE3 -mssse3 -DUSE_SSE2 -msse2 -DUSE_SSE -msse -DUSE_AVX2 -mavx2 -DUSE_AVX512 -mavx512f)

add_subdirectory(Source/nnue)

add_executable(Quanticade "")

target_link_libraries(Quanticade
    PRIVATE
        nnue
        m
)

set(QUANTICADE_SOURCES
    "Source/quanticade.c"
    "Source/utils.c"
    "Source/uci.c"
)

set(QUANTICADE_HEADERS
    "Source/consts.h"
    "Source/structs.h"
    "Source/enums.h"
    "Source/macros.h"
    "Source/utils.h"
    "Source/uci.h"
    "Source/quanticade.h"
    "Source/nnue_consts.h"
)

target_sources(Quanticade
    PRIVATE
        ${QUANTICADE_SOURCES}
    PUBLIC
        ${QUANTICADE_HEADERS}
)
