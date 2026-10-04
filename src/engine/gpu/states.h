#pragma once

#include "engine/gpu/device.h"

namespace engine::gpu {

// Colour = src x SrcAlpha + dst x (1 - SrcAlpha) for Translucent, src + dst for Additive, as WebGL's normal and one-one blending
enum class Blend { Opaque, Translucent, Additive };
// LessEqual tests, as WebGL's default
enum class Depth { TestWrite, Test, Off };
// Which side is culled for triangles listed clockwise on screen as the front
enum class Cull { Back, Front, None };

// The fixed-function states in use, made once
class States {
public:
    explicit States(Device& device);
    ID3D11BlendState* blend(Blend b) const { return blend_[static_cast<int>(b)].Get(); }
    ID3D11DepthStencilState* depth(Depth d) const { return depth_[static_cast<int>(d)].Get(); }
    ID3D11RasterizerState* cull(Cull c) const { return cull_[static_cast<int>(c)].Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_[3];
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_[3];
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> cull_[3];
};

}
