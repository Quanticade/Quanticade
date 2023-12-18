cmake_minimum_required(VERSION 3.16)

project(nnue CXX)

add_library(nnue STATIC)

target_sources(nnue
    PRIVATE
        "nnue.h"
        "nnue.cpp"
		"misc.h"
		"misc.cpp"
)

target_include_directories(nnue
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)