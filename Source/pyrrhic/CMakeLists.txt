cmake_minimum_required(VERSION 3.16)

project(pyrrhic C)

add_library(pyrrhic STATIC)

target_sources(pyrrhic
    PRIVATE
        "tbprobe.c"
)

target_include_directories(pyrrhic
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)