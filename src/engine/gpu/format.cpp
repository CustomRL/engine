#include "engine/gpu/format.h"

namespace engine::gpu {

namespace {

// Bytes per texel, or per 4x4 block for the BC formats
uint32_t unitBytes(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS: case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM:
        return 8;
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS: case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS: case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS: case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB:
        return 16;
    case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_SNORM: case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_A8_UNORM:
        return 1;
    case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R8G8_SNORM: case DXGI_FORMAT_R16_FLOAT:
        return 2;
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
        return 4;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32_FLOAT:
        return 8;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return 16;
    default:
        return 0;
    }
}

}

bool blockCompressed(DXGI_FORMAT format) {
    return (format >= DXGI_FORMAT_BC1_TYPELESS && format <= DXGI_FORMAT_BC5_SNORM) ||
           (format >= DXGI_FORMAT_BC6H_TYPELESS && format <= DXGI_FORMAT_BC7_UNORM_SRGB);
}

uint32_t rowBytes(DXGI_FORMAT format, uint32_t width) {
    return unitBytes(format) * (blockCompressed(format) ? (width + 3) / 4 : width);
}

uint32_t rowCount(DXGI_FORMAT format, uint32_t height) {
    return blockCompressed(format) ? (height + 3) / 4 : height;
}

}
