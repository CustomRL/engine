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

// A texture with its mips given, top first, none generated, in any format the device samples (block-compressed ones too). rowPitch: the
// bytes of one row of texels, or of one row of 4x4 blocks
struct MipLevel {
    const void* data;
    unsigned rowPitch;
};
Texture createTexture2D(Device& device, int width, int height, DXGI_FORMAT format, const std::vector<MipLevel>& mips);
// The same from count levels (at most D3D11's 15), with no allocation outside the driver. Null texture and view when the device refuses
// the description (a block-compressed top level whose size isn't a multiple of 4, say)
Texture createTexture2D(Device& device, int width, int height, DXGI_FORMAT format, const MipLevel* mips, int count);
// The same with faces 1 (2D) or 6 (a cube: +X -X +Y -Y +Z -Z, square), mips face-major: count levels of face 0, then of face 1, ...
Texture createTexture(Device& device, int width, int height, DXGI_FORMAT format, const MipLevel* mips, int count, int faces);

// A 2D texture's top level copied back to the CPU, top row first, rows packed tightly
std::vector<uint8_t> readBack(Device& device, ID3D11Texture2D* texture);
// One mip of a 2D texture (or of a cube's face) whose format format.h sizes, copied back to the CPU with rows (of texels, or of 4x4
// blocks) packed tightly: for a texture made from bytes laid out that way, those bytes
std::vector<uint8_t> readBackMip(Device& device, ID3D11Texture2D* texture, int mip, int face = 0);

}
