#pragma once

#include <dxgiformat.h>

#include <cstdint>

namespace engine::gpu {

// Sizes of texture data as D3D11 lays it out with rows tightly packed: a row is one row of texels, or of 4x4 blocks for a
// block-compressed format. 0 for a format not listed (the BC, 8-bit-per-channel, 16-bit-float and 32-bit-float families).
bool blockCompressed(DXGI_FORMAT format);
uint32_t rowBytes(DXGI_FORMAT format, uint32_t width);
uint32_t rowCount(DXGI_FORMAT format, uint32_t height);
inline uint64_t mipBytes(DXGI_FORMAT format, uint32_t width, uint32_t height) {
    return uint64_t(rowBytes(format, width)) * rowCount(format, height);
}

}
