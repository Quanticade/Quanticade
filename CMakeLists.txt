cmake_minimum_required(VERSION 3.5)
project(Quanticade VERSION 0.22 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Werror -Wextra -Wpedantic -std=gnu2x)
# Optimizations
add_compile_options(-march=native -flto -O3)

add_executable(${PROJECT_NAME} src/quanticade.c)
