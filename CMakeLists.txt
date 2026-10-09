cmake_minimum_required(VERSION 3.25)
project(engine LANGUAGES CXX)

find_package(SDL3 CONFIG REQUIRED)
find_package(Stb REQUIRED)

# the core has no window or GPU dependencies, so tools can link it alone
add_library(engine_core STATIC
    src/engine/assets/pack.cpp
    src/engine/core/arena.cpp
    src/engine/core/fatal.cpp
    src/engine/core/jobs.cpp
    src/engine/core/png_write.cpp)
target_include_directories(engine_core PUBLIC src)
target_compile_features(engine_core PUBLIC cxx_std_20)
target_compile_definitions(engine_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(engine_core PRIVATE /W4 /permissive-)

add_library(engine STATIC
    src/engine/gpu/buffer.cpp
    src/engine/gpu/constant_ring.cpp
    src/engine/gpu/device.cpp
    src/engine/gpu/frame_clock.cpp
    src/engine/gpu/gpu_profiler.cpp
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

# operator new counting: each exe or DLL that wants its allocations counted links this (a DLL has its own operator new). Off, it only
# defines allocationCount() as 0.
option(ENGINE_COUNT_ALLOCS "count operator new calls (debug and dev builds)" ON)
add_library(engine_alloc_count OBJECT src/engine/core/alloc_count.cpp)
target_link_libraries(engine_alloc_count PUBLIC engine_core)
target_compile_options(engine_alloc_count PRIVATE /W4 /permissive-)
if(ENGINE_COUNT_ALLOCS)
    target_compile_definitions(engine_alloc_count PUBLIC ENGINE_COUNT_ALLOCS)
endif()

add_executable(engine_check checks/engine_check.cpp)
target_include_directories(engine_check PRIVATE ${Stb_INCLUDE_DIR})
target_link_libraries(engine_check PRIVATE engine engine_alloc_count)
target_compile_options(engine_check PRIVATE /W4 /permissive-)
