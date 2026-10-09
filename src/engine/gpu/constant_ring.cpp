#include "engine/gpu/constant_ring.h"

#include "engine/core/fatal.h"

#include <algorithm>
#include <cstring>

namespace engine::gpu {

namespace {

constexpr int SLICE = 16;  // float4s: the offset and count granularity of the *SetConstantBuffers1 calls
constexpr UINT FALLBACK_BYTES = 4096 * 16;  // D3D11's largest constant buffer

Microsoft::WRL::ComPtr<ID3D11Buffer> dynamicConstants(ID3D11Device* device, size_t bytes) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = static_cast<UINT>(bytes);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    if (FAILED(device->CreateBuffer(&desc, nullptr, &buffer)))
        fatal("CreateBuffer failed (constant ring, %zu bytes)", bytes);
    return buffer;
}

void write(ID3D11DeviceContext* context, ID3D11Buffer* buffer, const void* data, size_t bytes) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        fatal("Map failed (constant ring)");
    std::memcpy(mapped.pData, data, bytes);
    context->Unmap(buffer, 0);
}

}

ConstantRing::ConstantRing(Device& device) : device_(device) {
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    if (SUCCEEDED(device.context()->QueryInterface(IID_PPV_ARGS(&context1_))) &&
        SUCCEEDED(device.d3d()->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof options)) && options.ConstantBufferOffsetting)
        return;
    context1_.Reset();
    for (auto& b : fallback_)
        b = dynamicConstants(device.d3d(), FALLBACK_BYTES);
}

uint32_t ConstantRing::push(const float* values, size_t count, int float4s) {
    const uint32_t offset = static_cast<uint32_t>(staged_.size() / 4);
    const size_t slice = static_cast<size_t>((std::max(float4s, 1) + SLICE - 1) / SLICE * SLICE) * 4;
    staged_.resize(staged_.size() + slice, 0.0f);
    std::memcpy(&staged_[offset * 4], values, std::min(count, slice) * sizeof(float));
    return offset;
}

void ConstantRing::upload() {
    if (!context1_ || staged_.empty())
        return;
    const size_t bytes = staged_.size() * sizeof(float);
    if (bytes > capacity_) {
        capacity_ = std::max(bytes, capacity_ * 2);
        buffer_ = dynamicConstants(device_.d3d(), capacity_);
    }
    write(device_.context(), buffer_.Get(), staged_.data(), bytes);
}

void ConstantRing::bind(Stage stage, UINT slot, uint32_t offset, int float4s) {
    const bool vertex = stage == Stage::Vertex;
    if (context1_) {
        ID3D11Buffer* buffer = buffer_.Get();
        const UINT count = static_cast<UINT>((std::max(float4s, 1) + SLICE - 1) / SLICE * SLICE);
        if (vertex)
            context1_->VSSetConstantBuffers1(slot, 1, &buffer, &offset, &count);
        else
            context1_->PSSetConstantBuffers1(slot, 1, &buffer, &offset, &count);
        return;
    }
    ID3D11Buffer* buffer = fallback_[vertex ? 0 : 1].Get();
    write(device_.context(), buffer, &staged_[offset * 4], static_cast<size_t>(std::max(float4s, 1)) * 16);
    if (vertex)
        device_.context()->VSSetConstantBuffers(slot, 1, &buffer);
    else
        device_.context()->PSSetConstantBuffers(slot, 1, &buffer);
}

}
