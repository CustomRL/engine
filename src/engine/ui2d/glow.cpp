#include "engine/ui2d/glow.h"

#include "engine/core/fatal.h"
#include "engine/gpu/shader_cache.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace engine::ui2d {

namespace {

// Each pass averages a box of taps one texel apart, from -fsize to +fsize along each axis: an even box's taps fall between texels, where
// bilinear filtering takes both halves
const char SHADER[] = R"(
cbuffer Glow : register(b0) {
    float2 texel;
    float2 fsize;     // (box - 1) / 2 per axis
    float2 taps;      // box per axis
    float fromLayer;  // 1: the pass reads the layer's alpha, 0: the blurred alpha in red
    float strength;
    float4 color;     // the glow's rgb and alpha
};
Texture2D a : register(t0);
Texture2D layer : register(t1);
SamplerState border : register(s0);
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}
float box(float2 uv) {
    float sum = 0;
    for (int y = 0; y < (int)taps.y; y++)
        for (int x = 0; x < (int)taps.x; x++) {
            float4 c = a.SampleLevel(border, uv + (float2(x, y) - fsize) * texel, 0);
            sum += fromLayer > 0.5 ? c.a : c.r;
        }
    return sum / (taps.x * taps.y);
}
float4 blur(float4 pos : SV_Position) : SV_Target {
    return box(pos.xy * texel);
}
float4 composite(float4 pos : SV_Position) : SV_Target {
    float2 uv = pos.xy * texel;
    float4 src = layer.SampleLevel(border, uv, 0);
    float glow = saturate(strength * box(uv) * (1 - src.a)) * color.a;
    return src + float4(color.rgb * glow, glow);
}
)";

void check(HRESULT hr, const char* what) {
    if (FAILED(hr))
        fatal("%s failed: 0x%08lx", what, static_cast<unsigned long>(hr));
}

ComPtr<ID3DBlob> compile(const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, errors;
    if (FAILED(engine::gpu::compileCached(SHADER, sizeof SHADER - 1, "ui2d glow", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code,
                          &errors)))
        fatal("ui2d glow shader %s: %s", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
    return code;
}

}

Glow::Glow(gpu::Device& device) : device_(device) {
    auto* d3d = device.d3d();
    const auto vs = compile("vs", "vs_5_0"), blur = compile("blur", "ps_5_0"), composite = compile("composite", "ps_5_0");
    check(d3d->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertexShader_), "CreateVertexShader (glow)");
    check(d3d->CreatePixelShader(blur->GetBufferPointer(), blur->GetBufferSize(), nullptr, &blur_), "CreatePixelShader (glow blur)");
    check(d3d->CreatePixelShader(composite->GetBufferPointer(), composite->GetBufferSize(), nullptr, &composite_),
          "CreatePixelShader (glow composite)");
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = 12 * sizeof(float);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    check(d3d->CreateBuffer(&desc, nullptr, &constants_), "CreateBuffer (glow constants)");
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;  // transparent past the layer
    check(d3d->CreateSamplerState(&sampler, &border_), "CreateSamplerState (glow)");
}

void Glow::pass(ID3D11PixelShader* shader, ID3D11ShaderResourceView* a, ID3D11ShaderResourceView* b, ID3D11RenderTargetView* to,
                const float constants[12]) {
    auto* context = device_.context();
    D3D11_MAPPED_SUBRESOURCE mapped;
    check(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map (glow constants)");
    std::memcpy(mapped.pData, constants, 12 * sizeof(float));
    context->Unmap(constants_.Get(), 0);
    ID3D11ShaderResourceView* none[2] = {};
    context->PSSetShaderResources(0, 2, none);
    context->OMSetRenderTargets(1, &to, nullptr);
    context->PSSetShader(shader, nullptr, 0);
    ID3D11ShaderResourceView* views[2] = {a, b};
    context->PSSetShaderResources(0, 2, views);
    context->Draw(3, 0);
}

void Glow::apply(ID3D11ShaderResourceView* source, ID3D11RenderTargetView* target, int width, int height, const GlowFilter& f) {
    // GetFilterPasses: passes x 2 one-axis boxes, horizontal then vertical, from blurX x blurY of 12800 twips^2 (32 px^2), else `passes`
    // boxes along both axes; the last pass is the composite, and keeps only its vertical box unless it is also the first
    const float bx = std::max(1.0f, std::floor(f.blurX)), by = std::max(1.0f, std::floor(f.blurY));
    std::vector<std::pair<float, float>> boxes;
    for (int p = 0; p < f.passes; p++) {
        if (f.blurX * f.blurY >= 32) {
            boxes.emplace_back(bx, 1.0f);
            boxes.emplace_back(1.0f, by);
        } else {
            boxes.emplace_back(bx, by);
        }
    }
    if (boxes.size() > 1)
        boxes.back().first = 1;

    for (auto& s : scratch_)
        if (s.width != width || s.height != height)
            s = gpu::createRenderTarget(device_, width, height, DXGI_FORMAT_R8_UNORM);
    auto* context = device_.context();
    const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    context->RSSetViewports(1, &viewport);
    context->RSSetState(nullptr);
    context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    context->OMSetDepthStencilState(nullptr, 0);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertexShader_.Get(), nullptr, 0);
    ID3D11Buffer* constants = constants_.Get();
    context->PSSetConstantBuffers(0, 1, &constants);
    ID3D11SamplerState* sampler = border_.Get();
    context->PSSetSamplers(0, 1, &sampler);

    ID3D11ShaderResourceView* from = source;
    for (size_t p = 0; p < boxes.size(); p++) {
        const auto [x, y] = boxes[p];
        const bool last = p + 1 == boxes.size();
        const float c[12] = {1.0f / width, 1.0f / height, (x - 1) / 2, (y - 1) / 2, x, y, p == 0 ? 1.0f : 0.0f, f.strength,
                             f.color[0], f.color[1], f.color[2], f.alpha};
        if (last) {
            pass(composite_.Get(), from, source, target, c);
        } else {
            pass(blur_.Get(), from, nullptr, scratch_[p % 2].view(), c);
            from = scratch_[p % 2].shaderView.Get();
        }
    }
    ID3D11ShaderResourceView* none[2] = {};
    context->PSSetShaderResources(0, 2, none);
}

}
