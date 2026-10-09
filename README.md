# engine

A C++20 game engine for Windows on D3D11. It holds only general parts: window, input, GPU, 2D UI, text, audio, networking,
assets and the check harness. Anything specific to one game lives in that game's repo.

## Build

Needs Visual Studio 18 with the C++ workload, which bundles CMake, Ninja and vcpkg. From an x64 Native Tools prompt or a
Developer PowerShell:

```
set VCPKG_ROOT=<Visual Studio>\VC\vcpkg    (or any vcpkg clone)
cmake --preset debug
cmake --build --preset debug
```

The presets are `debug` and `release` (RelWithDebInfo). vcpkg installs the ports in `vcpkg.json` on the first configure.

## Using it from a game

Add the engine as a submodule and call `add_subdirectory` on it. vcpkg reads only the top-level manifest, so the game's
`vcpkg.json` must also list the engine's ports (today `sdl3`). The engine's public headers include `windows.h` through the D3D11 headers;
it defines `NOMINMAX` and `WIN32_LEAN_AND_MEAN` for everything that links it.

## Layout

```
src/engine/core/      fatal errors
src/engine/platform/  the window (SDL3), its input (keys with their press times, mouse motion, the first gamepad) and the clock
src/engine/gpu/       the D3D11 device, swap chain and depth buffer
src/engine/render/    debug lines
```
