#include "engine/gpu/sampler.h"

#include "engine/core/fatal.h"

namespace engine::gpu {

namespace {

D3D11_TEXTURE_ADDRESS_MODE mode(Address a) {
    switch (a) {
    case Address::Clamp: return D3D11_TEXTURE_ADDRESS_CLAMP;
    case Address::Mirror: return D3D11_TEXTURE_ADDRESS_MIRROR;
    default: return D3D11_TEXTURE_ADDRESS_WRAP;
    }
}

}

ID3D11SamplerState* Samplers::get(const SamplerDesc& d) {
    auto& made = made_[d];
    if (made)
        return made.Get();
    D3D11_SAMPLER_DESC desc{};
    if (d.nearest)
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    else if (!d.mips)
        desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    else if (d.anisotropy > 1)
        desc.Filter = D3D11_FILTER_ANISOTROPIC;
    else
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.MaxAnisotropy = d.anisotropy > 1 ? d.anisotropy : 1;
    desc.AddressU = mode(d.u);
    desc.AddressV = mode(d.v);
    desc.AddressW = mode(d.w);
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MinLOD = 0;
    // WebGL's NEAREST and LINEAR minification never leave the top level
    desc.MaxLOD = d.nearest || !d.mips ? 0 : D3D11_FLOAT32_MAX;
    if (FAILED(device_.d3d()->CreateSamplerState(&desc, &made)))
        fatal("CreateSamplerState failed");
    return made.Get();
}

}
