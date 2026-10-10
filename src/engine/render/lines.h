#pragma once

#include "engine/gpu/device.h"

#include <DirectXMath.h>

#include <cstdint>
#include <vector>

namespace engine::render {

// Debug line segments in world space, drawn depth-tested in one batch
class Lines {
public:
    explicit Lines(gpu::Device& device);

    // color is 0xAABBGGRR (R8G8B8A8_UNORM in memory)
    void add(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, uint32_t color);
    // Room for that many segments up front, so adding up to them never allocates
    void reserve(size_t segments) { vertices_.reserve(segments * 2); }
    // Draws every segment added since the last draw into the bound target, then forgets them. viewProjection takes row vectors (v * M).
    void draw(const DirectX::XMFLOAT4X4& viewProjection);

private:
    struct Vertex {
        DirectX::XMFLOAT3 position;
        uint32_t color;
    };

    gpu::Device& device_;
    std::vector<Vertex> vertices_;
    size_t capacity_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
};

}
