#include "engine/ui2d/canvas.h"

#include "engine/core/fatal.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace engine::ui2d {

namespace {

// The colour transform works on unpremultiplied colour, as Flash's does; the result goes out premultiplied
const char SHADER[] = R"(
Texture2D image : register(t0);
SamplerState linearClamp : register(s0);
struct Vertex {
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
    float2 local : TEXCOORD1;
    float4 mult : TEXCOORD2;
    float4 offset : TEXCOORD3;
    float4 params : TEXCOORD4;
};
struct Pixel {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float2 local : TEXCOORD1;
    float4 mult : TEXCOORD2;
    float4 offset : TEXCOORD3;
    float4 params : TEXCOORD4;
};
Pixel vs(Vertex v) {
    Pixel p;
    p.position = v.position;
    p.uv = v.uv;
    p.local = v.local;
    p.mult = v.mult;
    p.offset = v.offset;
    p.params = v.params;
    return p;
}
float4 ps(Pixel p) : SV_Target {
    if (p.params.y < 360) {
        float2 d = p.local - 0.5;
        float a = degrees(atan2(d.x, -d.y));
        if (fmod(a - p.params.x + 720.0, 360.0) >= p.params.y)
            discard;
    }
    float4 c = image.Sample(linearClamp, p.uv);
    if (p.params.z > 0.5)
        c = c.rrrr;
    float3 rgb = c.a > 0 ? c.rgb / c.a : 0;
    float4 o = saturate(float4(rgb, c.a) * p.mult + p.offset);
    return float4(o.rgb * o.a, o.a);
}
)";

constexpr size_t MAX_QUADS = 16384;  // 16-bit indices

void check(HRESULT hr, const char* what) {
    if (FAILED(hr))
        fatal("%s failed: 0x%08lx", what, static_cast<unsigned long>(hr));
}

ComPtr<ID3DBlob> compile(const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, errors;
    if (FAILED(D3DCompile(SHADER, sizeof SHADER - 1, "ui2d canvas", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code,
                          &errors)))
        fatal("ui2d canvas shader %s: %s", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
    return code;
}

}

ColorTransform ColorTransform::after(const ColorTransform& inner) const {
    ColorTransform out;
    for (int k = 0; k < 4; k++) {
        out.mult[k] = inner.mult[k] * mult[k];
        out.offset[k] = inner.offset[k] * mult[k] + offset[k];
    }
    return out;
}

Canvas::Canvas(gpu::Device& device) : device_(device) {
    auto* d3d = device.d3d();
    const auto vs = compile("vs", "vs_5_0"), ps = compile("ps", "ps_5_0");
    check(d3d->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertexShader_), "CreateVertexShader (ui2d)");
    check(d3d->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixelShader_), "CreatePixelShader (ui2d)");
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, local), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, mult), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, offset), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, params), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    check(d3d->CreateInputLayout(elements, 6, vs->GetBufferPointer(), vs->GetBufferSize(), &layout_), "CreateInputLayout (ui2d)");

    std::vector<uint16_t> indices;
    indices.reserve(MAX_QUADS * 6);
    for (size_t q = 0; q < MAX_QUADS; q++)
        for (const int i : {0, 1, 2, 0, 2, 3})
            indices.push_back(static_cast<uint16_t>(q * 4 + i));
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint16_t));
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    const D3D11_SUBRESOURCE_DATA init{indices.data(), 0, 0};
    check(d3d->CreateBuffer(&desc, &init, &indices_), "CreateBuffer (ui2d indices)");

    for (int b = 0; b < 2; b++) {
        D3D11_BLEND_DESC blend{};
        auto& rt = blend.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt.DestBlend = rt.DestBlendAlpha = b == 0 ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_ONE;
        rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        check(d3d->CreateBlendState(&blend, &blends_[b]), "CreateBlendState (ui2d)");
    }
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = FALSE;
    check(d3d->CreateRasterizerState(&raster, &rasterizer_), "CreateRasterizerState (ui2d)");
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    check(d3d->CreateSamplerState(&sampler, &sampler_), "CreateSamplerState (ui2d)");
    const uint32_t white = 0xffffffff;
    white_ = gpu::createTexture2D(device, 1, 1, gpu::TextureFormat::Rgba8, &white, false);
}

void Canvas::begin(ID3D11RenderTargetView* target, int width, int height, const XMFLOAT4X4& stageToTarget, bool clear) {
    auto* context = device_.context();
    if (clear) {
        const float none[4] = {0, 0, 0, 0};
        context->ClearRenderTargetView(target, none);
    }
    // target pixels to clip space for row vectors: x' = 2x / width - w, y' = w - 2y / height
    const XMMATRIX toClip(2.0f / width, 0, 0, 0, 0, -2.0f / height, 0, 0, 0, 0, 1, 0, -1, 1, 0, 1);
    XMStoreFloat4x4(&toClip_, XMLoadFloat4x4(&stageToTarget) * toClip);
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    context->OMSetRenderTargets(1, &target, nullptr);
    context->OMSetDepthStencilState(nullptr, 0);
    context->RSSetViewports(1, &viewport);
    context->RSSetState(rasterizer_.Get());
    context->IASetInputLayout(layout_.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetIndexBuffer(indices_.Get(), DXGI_FORMAT_R16_UINT, 0);
    context->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context->PSSetShader(pixelShader_.Get(), nullptr, 0);
    ID3D11SamplerState* sampler = sampler_.Get();
    context->PSSetSamplers(0, 1, &sampler);
}

void Canvas::draw(const Quad& q) {
    ID3D11ShaderResourceView* texture = q.texture ? q.texture : white_.view.Get();
    if (!vertices_.empty() && (texture != texture_ || q.blend != blend_ || vertices_.size() >= MAX_QUADS * 4))
        flush();
    texture_ = texture;
    blend_ = q.blend;
    const XMMATRIX m = XMLoadFloat4x4(&q.matrix) * XMLoadFloat4x4(&toClip_);
    const float x0 = q.rect[0], y0 = q.rect[1], x1 = x0 + q.rect[2], y1 = y0 + q.rect[3];
    const float corners[4][4] = {{x0, y0, 0, 0}, {x1, y0, 1, 0}, {x1, y1, 1, 1}, {x0, y1, 0, 1}};
    for (const auto& c : corners) {
        XMFLOAT4 p;
        XMStoreFloat4(&p, XMVector4Transform(XMVectorSet(c[0], c[1], 0, 1), m));
        Vertex v{{p.x, p.y, 0, p.w},
                 {c[2] ? q.uv[2] : q.uv[0], c[3] ? q.uv[3] : q.uv[1]},
                 {c[2], c[3]},
                 {q.color.mult[0], q.color.mult[1], q.color.mult[2], q.color.mult[3]},
                 {q.color.offset[0], q.color.offset[1], q.color.offset[2], q.color.offset[3]},
                 {q.sweepStart, q.sweep, q.coverage ? 1.0f : 0.0f, 0}};
        vertices_.push_back(v);
    }
}

void Canvas::flush() {
    if (vertices_.empty())
        return;
    auto* context = device_.context();
    if (vertices_.size() > capacity_) {
        capacity_ = std::max(vertices_.size(), capacity_ * 2);
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(capacity_ * sizeof(Vertex));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        check(device_.d3d()->CreateBuffer(&desc, nullptr, &buffer_), "CreateBuffer (ui2d vertices)");
    }
    D3D11_MAPPED_SUBRESOURCE mapped;
    check(context->Map(buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map (ui2d vertices)");
    std::memcpy(mapped.pData, vertices_.data(), vertices_.size() * sizeof(Vertex));
    context->Unmap(buffer_.Get(), 0);
    ID3D11Buffer* buffer = buffer_.Get();
    const UINT stride = sizeof(Vertex), offset = 0;
    context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
    context->PSSetShaderResources(0, 1, &texture_);
    context->OMSetBlendState(blends_[blend_ == Blend::Add ? 1 : 0].Get(), nullptr, 0xffffffff);
    context->DrawIndexed(static_cast<UINT>(vertices_.size() / 4 * 6), 0, 0);
    vertices_.clear();
}

void Canvas::end() {
    flush();
    ID3D11ShaderResourceView* none = nullptr;
    device_.context()->PSSetShaderResources(0, 1, &none);
}

}
