cmake_minimum_required(VERSION 3.25)
project(engine LANGUAGES CXX)

find_package(SDL3 CONFIG REQUIRED)
find_package(Stb REQUIRED)

# the core has no window or GPU dependencies, so tools can link it alone
add_library(engine_core STATIC
    src/engine/assets/pack.cpp
    src/engine/core/fatal.cpp
    src/engine/core/jobs.cpp)
target_include_directories(engine_core PUBLIC src)
target_compile_features(engine_core PUBLIC cxx_std_20)
target_compile_definitions(engine_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(engine_core PRIVATE /W4 /permissive-)

add_library(engine STATIC
    src/engine/gpu/buffer.cpp
    src/engine/gpu/device.cpp
    src/engine/gpu/sampler.cpp
    src/engine/gpu/shader.cpp
    src/engine/gpu/states.cpp
    src/engine/gpu/texture.cpp
    src/engine/platform/clock.cpp
    src/engine/platform/input.cpp
    src/engine/platform/window.cpp
    src/engine/render/lines.cpp
    src/engine/ui2d/canvas.cpp
    src/engine/ui2d/font.cpp
    src/engine/ui2d/glow.cpp
    src/engine/ui2d/image.cpp)
target_include_directories(engine PUBLIC src)
target_include_directories(engine PRIVATE ${Stb_INCLUDE_DIR})
target_compile_features(engine PUBLIC cxx_std_20)
# the GPU headers pull in windows.h for every file that includes them
target_compile_definitions(engine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(engine PRIVATE /W4 /permissive-)
target_link_libraries(engine PUBLIC engine_core SDL3::SDL3 d3d11 dxgi d3dcompiler)
