#pragma once

#include "engine/gpu/device.h"

#include <cstddef>

namespace engine::gpu {

// An immutable vertex or index buffer (bind: D3D11_BIND_VERTEX_BUFFER or D3D11_BIND_INDEX_BUFFER)
Microsoft::WRL::ComPtr<ID3D11Buffer> createBuffer(Device& device, UINT bind, const void* data, size_t bytes);

// A constant buffer of float4s, rewritten whole on each update
class ConstantBuffer {
public:
    ConstantBuffer(Device& device, int float4s);
    void update(Device& device, const float* values);
    ID3D11Buffer* get() const { return buffer_.Get(); }
    int float4s() const { return float4s_; }

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer_;
    int float4s_;
};

}
