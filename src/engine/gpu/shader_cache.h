#pragma once

#include <d3dcommon.h>

#include <cstddef>

namespace engine::gpu {

// D3DCompile with its bytecode kept in shader_cache/ next to the exe: a later run with the same source, defines,
// entry, target, flags and compiler build reads the bytecode instead of compiling it again. The arguments are
// D3DCompile's; with an include handler it compiles without the cache. Errors aren't cached.
HRESULT compileCached(const void* source, size_t size, const char* name, const D3D_SHADER_MACRO* defines, ID3DInclude* include,
                      const char* entry, const char* target, UINT flags1, UINT flags2, ID3DBlob** code, ID3DBlob** errors);

}
