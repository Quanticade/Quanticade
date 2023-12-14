cmake_minimum_required(VERSION 3.5)
# Project Name Version & main Languages.
project(Quanticade VERSION 0.22 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic -Wno-error=unused-but-set-parameter -Wno-error=pedantic -std=gnu2x -march=native -flto -O3)

add_executable(${PROJECT_NAME} src/quanticade.c)
