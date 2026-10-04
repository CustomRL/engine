#include "engine/gpu/states.h"

#include "engine/core/fatal.h"

namespace engine::gpu {

namespace {

void check(HRESULT hr, const char* what) {
    if (FAILED(hr))
        fatal("%s failed: 0x%08lx", what, static_cast<unsigned long>(hr));
}

}

States::States(Device& device) {
    auto* d3d = device.d3d();
    for (int b = 0; b < 4; b++) {
        D3D11_BLEND_DESC desc{};
        auto& target = desc.RenderTarget[0];
        target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (b == static_cast<int>(Blend::Modulate)) {  // TStaticBlendState<BO_Add, BF_DestColor, BF_Zero>, its alpha One and Zero
            target.BlendEnable = TRUE;
            target.SrcBlend = D3D11_BLEND_DEST_COLOR;
            target.DestBlend = D3D11_BLEND_ZERO;
            target.BlendOp = D3D11_BLEND_OP_ADD;
            target.SrcBlendAlpha = D3D11_BLEND_ONE;
            target.DestBlendAlpha = D3D11_BLEND_ZERO;
            target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        } else if (b != static_cast<int>(Blend::Opaque)) {
            const bool additive = b == static_cast<int>(Blend::Additive);
            target.BlendEnable = TRUE;
            target.SrcBlend = additive ? D3D11_BLEND_ONE : D3D11_BLEND_SRC_ALPHA;
            target.DestBlend = additive ? D3D11_BLEND_ONE : D3D11_BLEND_INV_SRC_ALPHA;
            target.BlendOp = D3D11_BLEND_OP_ADD;
            target.SrcBlendAlpha = D3D11_BLEND_ONE;
            target.DestBlendAlpha = additive ? D3D11_BLEND_ONE : D3D11_BLEND_INV_SRC_ALPHA;
            target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        }
        check(d3d->CreateBlendState(&desc, &blend_[b]), "CreateBlendState");
    }
    for (int d = 0; d < 3; d++) {
        D3D11_DEPTH_STENCIL_DESC desc{};
        desc.DepthEnable = d != static_cast<int>(Depth::Off);
        desc.DepthWriteMask = d == static_cast<int>(Depth::TestWrite) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        check(d3d->CreateDepthStencilState(&desc, &depth_[d]), "CreateDepthStencilState");
    }
    const D3D11_CULL_MODE modes[] = {D3D11_CULL_BACK, D3D11_CULL_FRONT, D3D11_CULL_NONE};
    for (int c = 0; c < 3; c++) {
        D3D11_RASTERIZER_DESC desc{};
        desc.FillMode = D3D11_FILL_SOLID;
        desc.CullMode = modes[c];
        desc.DepthClipEnable = TRUE;
        check(d3d->CreateRasterizerState(&desc, &cull_[c]), "CreateRasterizerState");
    }
}

}
