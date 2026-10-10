#include "engine/render/lines.h"

#include "engine/core/fatal.h"
#include "engine/gpu/shader_cache.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace engine::render {

namespace {

const char SHADER[] = R"(
cbuffer View : register(b0) { row_major float4x4 viewProjection; };
struct Vertex { float3 position : POSITION; float4 color : COLOR; };
struct Pixel { float4 position : SV_Position; float4 color : COLOR; };
Pixel vs(Vertex v) { Pixel p; p.position = mul(float4(v.position, 1), viewProjection); p.color = v.color; return p; }
float4 ps(Pixel p) : SV_Target { return p.color; }
)";

void check(HRESULT hr, const char* what) {
    if (FAILED(hr))
        fatal("%s failed: 0x%08lx", what, static_cast<unsigned long>(hr));
}

ComPtr<ID3DBlob> compile(const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, errors;
    if (FAILED(engine::gpu::compileCached(SHADER, sizeof SHADER - 1, "lines", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code,
                          &errors)))
        fatal("lines shader %s: %s", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
    return code;
}

}

Lines::Lines(gpu::Device& device) : device_(device) {
    auto* d3d = device.d3d();
    const auto vs = compile("vs", "vs_5_0"), ps = compile("ps", "ps_5_0");
    check(d3d->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertexShader_), "CreateVertexShader (lines)");
    check(d3d->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixelShader_), "CreatePixelShader (lines)");
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    check(d3d->CreateInputLayout(elements, 2, vs->GetBufferPointer(), vs->GetBufferSize(), &layout_), "CreateInputLayout (lines)");

    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(DirectX::XMFLOAT4X4);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    check(d3d->CreateBuffer(&desc, nullptr, &constants_), "CreateBuffer (lines constants)");
}

void Lines::add(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, uint32_t color) {
    vertices_.push_back({a, color});
    vertices_.push_back({b, color});
}

void Lines::draw(const DirectX::XMFLOAT4X4& viewProjection) {
    if (vertices_.empty())
        return;
    auto* d3d = device_.d3d();
    auto* context = device_.context();

    if (vertices_.size() > capacity_) {
        capacity_ = std::max(vertices_.size(), capacity_ * 2);
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(capacity_ * sizeof(Vertex));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        check(d3d->CreateBuffer(&desc, nullptr, &buffer_), "CreateBuffer (lines)");
    }
    D3D11_MAPPED_SUBRESOURCE mapped;
    check(context->Map(buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map (lines)");
    std::memcpy(mapped.pData, vertices_.data(), vertices_.size() * sizeof(Vertex));
    context->Unmap(buffer_.Get(), 0);
    check(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map (lines constants)");
    std::memcpy(mapped.pData, &viewProjection, sizeof viewProjection);
    context->Unmap(constants_.Get(), 0);

    const UINT stride = sizeof(Vertex), offset = 0;
    ID3D11Buffer* buffer = buffer_.Get();
    ID3D11Buffer* constants = constants_.Get();
    context->IASetInputLayout(layout_.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
    context->VSSetShader(vertexShader_.Get(), nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &constants);
    context->PSSetShader(pixelShader_.Get(), nullptr, 0);
    context->Draw(static_cast<UINT>(vertices_.size()), 0);
    vertices_.clear();
}

}
