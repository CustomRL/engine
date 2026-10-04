cmake_minimum_required(VERSION 3.25)
project(engine LANGUAGES CXX)

find_package(SDL3 CONFIG REQUIRED)

add_library(engine STATIC
    src/engine/core/fatal.cpp
    src/engine/platform/window.cpp)
target_include_directories(engine PUBLIC src)
target_compile_features(engine PUBLIC cxx_std_20)
target_compile_options(engine PRIVATE /W4 /permissive-)
target_link_libraries(engine PUBLIC SDL3::SDL3)
