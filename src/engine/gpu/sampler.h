#pragma once

#include "engine/gpu/device.h"

#include <map>
#include <tuple>

namespace engine::gpu {

enum class Address { Wrap, Clamp, Mirror };

// How a texture is filtered, in WebGL's terms: nearest (no blending, no mips), linear without mips (the top level only), trilinear, or
// anisotropic past 1
struct SamplerDesc {
    bool nearest = false;
    bool mips = true;
    int anisotropy = 1;
    Address u = Address::Wrap, v = Address::Wrap, w = Address::Wrap;

    auto key() const { return std::tie(nearest, mips, anisotropy, u, v, w); }
    bool operator<(const SamplerDesc& other) const { return key() < other.key(); }
};

// Sampler states made once per description
class Samplers {
public:
    explicit Samplers(Device& device) : device_(device) {}
    ID3D11SamplerState* get(const SamplerDesc& desc);

private:
    Device& device_;
    std::map<SamplerDesc, Microsoft::WRL::ComPtr<ID3D11SamplerState>> made_;
};

}
