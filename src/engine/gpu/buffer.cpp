#include "engine/gpu/buffer.h"

#include "engine/core/fatal.h"

#include <cstring>

namespace engine::gpu {

Microsoft::WRL::ComPtr<ID3D11Buffer> createBuffer(Device& device, UINT bind, const void* data, size_t bytes) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(bytes);
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = bind;
    const D3D11_SUBRESOURCE_DATA init{data, 0, 0};
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    if (FAILED(device.d3d()->CreateBuffer(&desc, &init, &buffer)))
        fatal("CreateBuffer failed (%zu bytes)", bytes);
    return buffer;
}

ConstantBuffer::ConstantBuffer(Device& device, int float4s) : float4s_(float4s) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(float4s * 16);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device.d3d()->CreateBuffer(&desc, nullptr, &buffer_)))
        fatal("CreateBuffer failed (constants, %d float4s)", float4s);
}

void ConstantBuffer::update(Device& device, const float* values) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(device.context()->Map(buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        fatal("Map failed (constants)");
    std::memcpy(mapped.pData, values, static_cast<size_t>(float4s_) * 16);
    device.context()->Unmap(buffer_.Get(), 0);
}

}
