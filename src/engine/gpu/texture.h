#pragma once

#include "engine/gpu/device.h"

#include <cstdint>
#include <vector>

namespace engine::gpu {

enum class TextureFormat { Rgba8, Rgba8Srgb, Rgba8Snorm, Rgba32Float };

// A texture the GPU samples
struct Texture {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    int width = 0, height = 0;
};

// texels: width x height, top row first, 4 components per texel. With mips the chain down to 1x1 is generated on the GPU from the top
// level, as WebGL's generateMipmap does.
Texture createTexture2D(Device& device, int width, int height, TextureFormat format, const void* texels, bool mips);
// faces: +X -X +Y -Y +Z -Z, each size x size texels
Texture createTextureCube(Device& device, int size, TextureFormat format, const void* const faces[6], bool mips);

// A texture the GPU draws into and then samples. A cube has a target view per face and samples as a cube.
struct RenderTarget {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> views[6];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderView;
    int width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    ID3D11RenderTargetView* view(int face = 0) const { return views[face].Get(); }
};
RenderTarget createRenderTarget(Device& device, int width, int height, DXGI_FORMAT format);
RenderTarget createCubeRenderTarget(Device& device, int size, DXGI_FORMAT format);

// A D32_FLOAT depth buffer the GPU can also sample, as R32_FLOAT
struct DepthTarget {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> view;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderView;
    int width = 0, height = 0;
};
DepthTarget createDepthTarget(Device& device, int width, int height);

// A 2D texture's top level copied back to the CPU, top row first, rows packed tightly
std::vector<uint8_t> readBack(Device& device, ID3D11Texture2D* texture);

}
