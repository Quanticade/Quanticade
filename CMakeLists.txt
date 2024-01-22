cmake_minimum_required(VERSION 3.16)
project(Quanticade VERSION 0.5 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(CheckIPOSupported)
check_ipo_supported(RESULT supported OUTPUT error)
if( supported )
    message(STATUS "IPO / LTO enabled")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "IPO / LTO not supported: <${error}>")
endif()

add_compile_options(-Wall -Werror -Wextra -Wpedantic -Wno-error=vla)
# Optimizations
add_compile_options(-march=native -Ofast -fomit-frame-pointer -DUSE_SSE41 -msse4.1 -DUSE_SSSE3 -mssse3 -DUSE_SSE2 -msse2 -DUSE_SSE -msse -DUSE_AVX2 -mavx2)
#Not default optimizations. Enable for slight boost on supported systems. Aprox 64ms. From 2828 to 2764 on average.
#add_compile_options(-DUSE_AVX512 -mavx512f)

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
    "Source/attacks.c"
    "Source/evaluate.c"
    "Source/movegen.c"
    "Source/pvtable.c"
    "Source/search.c"
)

set(QUANTICADE_HEADERS
    "Source/attacks.h"
    "Source/evaluate.h"
    "Source/movegen.h"
    "Source/structs.h"
    "Source/enums.h"
    "Source/macros.h"
    "Source/utils.h"
    "Source/uci.h"
    "Source/pvtable.h"
    "Source/search.h"
    "Source/nnue_consts.h"
)

target_sources(Quanticade
    PRIVATE
        ${QUANTICADE_SOURCES}
    PUBLIC
        ${QUANTICADE_HEADERS}
)

configure_file(${PROJECT_SOURCE_DIR}/nnue_binary/nn-eba324f53044.nnue ${CMAKE_BINARY_DIR}/nn-eba324f53044.nnue COPYONLY)
