#pragma once

#include "engine/core/arena.h"
#include "engine/gpu/device.h"

#include <d3d11_1.h>

#include <cstdint>

namespace engine::gpu {

// The per-draw constants of one pass: every draw's values are staged on the CPU, written to one dynamic buffer with a single
// Map(WRITE_DISCARD), and bound by offset (ID3D11DeviceContext1::*SetConstantBuffers1, in 256-byte slices). Without constant buffer
// offsetting, a shared 64 KB buffer per stage is rewritten before each draw instead. The staging grows in an arena to the largest
// frame seen, never through operator new.
class ConstantRing {
public:
    enum class Stage { Vertex, Pixel };

    explicit ConstantRing(Device& device);

    void reset() { staged_.clear(); }
    // Stages count floats as float4s float4s (zero padded to a slice) and returns where they start, in float4s
    uint32_t push(const float* values, size_t count, int float4s);
    void upload();
    void bind(Stage stage, UINT slot, uint32_t offset, int float4s);

    bool offsetting() const { return context1_ != nullptr; }
    ID3D11Buffer* buffer() const { return buffer_.Get(); }  // the offsetting path's buffer, for checks

private:
    Device& device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer_, fallback_[2];
    size_t capacity_ = 0;
    Arena staging_{size_t(256) << 20};
    GrowArray<float> staged_{staging_};
};

}
