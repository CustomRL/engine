#include "engine/gpu/texture.h"

#include "engine/core/fatal.h"
#include "engine/gpu/format.h"

#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace engine::gpu {

namespace {

void check(HRESULT hr, const char* what) {
    if (FAILED(hr))
        fatal("%s failed: 0x%08lx", what, static_cast<unsigned long>(hr));
}

DXGI_FORMAT dxgi(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba8: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::Rgba8Srgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case TextureFormat::Rgba8Snorm: return DXGI_FORMAT_R8G8B8A8_SNORM;
    case TextureFormat::Rgba32Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
    return DXGI_FORMAT_UNKNOWN;
}

UINT texelBytes(TextureFormat format) {
    return format == TextureFormat::Rgba32Float ? 16 : 4;
}

// faces texels into a texture of `count` array slices, its top level filled and, with mips, the rest generated from it
Texture create(Device& device, int width, int height, TextureFormat format, const void* const* slices, UINT count, bool cube, bool mips) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = mips ? 0 : 1;
    desc.ArraySize = count;
    desc.Format = dxgi(format);
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (mips ? D3D11_BIND_RENDER_TARGET : 0);
    desc.MiscFlags = (mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0) | (cube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0);

    Texture out;
    out.width = width;
    out.height = height;
    check(device.d3d()->CreateTexture2D(&desc, nullptr, &out.texture), "CreateTexture2D");
    check(device.d3d()->CreateShaderResourceView(out.texture.Get(), nullptr, &out.view), "CreateShaderResourceView");
    out.texture->GetDesc(&desc);
    for (UINT slice = 0; slice < count; slice++)
        device.context()->UpdateSubresource(out.texture.Get(), D3D11CalcSubresource(0, slice, desc.MipLevels), nullptr, slices[slice],
                                            width * texelBytes(format), 0);
    if (mips)
        device.context()->GenerateMips(out.view.Get());
    return out;
}

}

Texture createTexture2D(Device& device, int width, int height, TextureFormat format, const void* texels, bool mips) {
    return create(device, width, height, format, &texels, 1, false, mips);
}

Texture createTextureCube(Device& device, int size, TextureFormat format, const void* const faces[6], bool mips) {
    return create(device, size, size, format, faces, 6, true, mips);
}

Texture createTexture2D(Device& device, int width, int height, DXGI_FORMAT format, const std::vector<MipLevel>& mips) {
    Texture out = createTexture2D(device, width, height, format, mips.data(), static_cast<int>(mips.size()));
    if (!out.texture)
        fatal("CreateTexture2D (mips) failed: %dx%d, format %d, %zu mips", width, height, static_cast<int>(format), mips.size());
    return out;
}

Texture createTexture2D(Device& device, int width, int height, DXGI_FORMAT format, const MipLevel* mips, int count) {
    return createTexture(device, width, height, format, mips, count, 1);
}

Texture createTexture(Device& device, int width, int height, DXGI_FORMAT format, const MipLevel* mips, int count, int faces) {
    if (count < 1 || count > D3D11_REQ_MIP_LEVELS || (faces != 1 && faces != 6))
        return {};
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = static_cast<UINT>(count);
    desc.ArraySize = static_cast<UINT>(faces);
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = faces == 6 ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;
    D3D11_SUBRESOURCE_DATA data[6 * D3D11_REQ_MIP_LEVELS];  // subresource order: face-major, as mips is
    for (int i = 0; i < count * faces; i++)
        data[i] = {mips[i].data, mips[i].rowPitch, 0};

    Texture out;
    if (FAILED(device.d3d()->CreateTexture2D(&desc, data, &out.texture)))
        return {};
    check(device.d3d()->CreateShaderResourceView(out.texture.Get(), nullptr, &out.view), "CreateShaderResourceView (mips)");
    out.width = width;
    out.height = height;
    return out;
}

RenderTarget createRenderTarget(Device& device, int width, int height, DXGI_FORMAT format) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    RenderTarget out;
    out.width = width;
    out.height = height;
    out.format = format;
    check(device.d3d()->CreateTexture2D(&desc, nullptr, &out.texture), "CreateTexture2D (target)");
    check(device.d3d()->CreateRenderTargetView(out.texture.Get(), nullptr, &out.views[0]), "CreateRenderTargetView");
    check(device.d3d()->CreateShaderResourceView(out.texture.Get(), nullptr, &out.shaderView), "CreateShaderResourceView (target)");
    return out;
}

RenderTarget createCubeRenderTarget(Device& device, int size, DXGI_FORMAT format) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = size;
    desc.Height = size;
    desc.MipLevels = 1;
    desc.ArraySize = 6;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    RenderTarget out;
    out.width = out.height = size;
    out.format = format;
    check(device.d3d()->CreateTexture2D(&desc, nullptr, &out.texture), "CreateTexture2D (cube target)");
    for (UINT face = 0; face < 6; face++) {
        D3D11_RENDER_TARGET_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray.FirstArraySlice = face;
        view.Texture2DArray.ArraySize = 1;
        check(device.d3d()->CreateRenderTargetView(out.texture.Get(), &view, &out.views[face]), "CreateRenderTargetView (cube face)");
    }
    check(device.d3d()->CreateShaderResourceView(out.texture.Get(), nullptr, &out.shaderView), "CreateShaderResourceView (cube target)");
    return out;
}

DepthTarget createDepthTarget(Device& device, int width, int height) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    DepthTarget out;
    out.width = width;
    out.height = height;
    check(device.d3d()->CreateTexture2D(&desc, nullptr, &out.texture), "CreateTexture2D (depth)");
    D3D11_DEPTH_STENCIL_VIEW_DESC depth{};
    depth.Format = DXGI_FORMAT_D32_FLOAT;
    depth.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    check(device.d3d()->CreateDepthStencilView(out.texture.Get(), &depth, &out.view), "CreateDepthStencilView");
    D3D11_SHADER_RESOURCE_VIEW_DESC read{};
    read.Format = DXGI_FORMAT_R32_FLOAT;
    read.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    read.Texture2D.MipLevels = 1;
    check(device.d3d()->CreateShaderResourceView(out.texture.Get(), &read, &out.shaderView), "CreateShaderResourceView (depth)");
    return out;
}

std::vector<uint8_t> readBack(Device& device, ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    const UINT texel = desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : desc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 16 : 4;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    check(device.d3d()->CreateTexture2D(&desc, nullptr, &staging), "CreateTexture2D (staging)");
    device.context()->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture, 0, nullptr);

    D3D11_MAPPED_SUBRESOURCE mapped;
    check(device.context()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map (staging)");
    std::vector<uint8_t> out(static_cast<size_t>(desc.Width) * desc.Height * texel);
    for (UINT row = 0; row < desc.Height; row++)
        std::memcpy(out.data() + static_cast<size_t>(row) * desc.Width * texel, static_cast<const uint8_t*>(mapped.pData) + row * mapped.RowPitch,
                    desc.Width * texel);
    device.context()->Unmap(staging.Get(), 0);
    return out;
}

std::vector<uint8_t> readBackMip(Device& device, ID3D11Texture2D* texture, int mip, int face) {
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    const UINT width = std::max(1u, desc.Width >> mip), height = std::max(1u, desc.Height >> mip);
    // a staging copy of the whole chain: a staging texture of the mip's own size would be refused for a block-compressed mip under 4x4
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    check(device.d3d()->CreateTexture2D(&desc, nullptr, &staging), "CreateTexture2D (mip staging)");
    device.context()->CopyResource(staging.Get(), texture);

    const size_t row = rowBytes(desc.Format, width), rows = rowCount(desc.Format, height);
    const UINT sub = D3D11CalcSubresource(mip, face, desc.MipLevels);
    D3D11_MAPPED_SUBRESOURCE mapped;
    check(device.context()->Map(staging.Get(), sub, D3D11_MAP_READ, 0, &mapped), "Map (mip staging)");
    std::vector<uint8_t> out(row * rows);
    for (size_t r = 0; r < rows; r++)
        std::memcpy(out.data() + r * row, static_cast<const uint8_t*>(mapped.pData) + r * mapped.RowPitch, row);
    device.context()->Unmap(staging.Get(), sub);
    return out;
}

}
