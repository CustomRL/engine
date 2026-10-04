#pragma once

#include "engine/gpu/device.h"
#include "engine/gpu/texture.h"

namespace engine::ui2d {

// Flash's GlowFilter (outer, not knocked out) as Scaleform's filter passes draw it (rl3dclone docs/loading-screen.md, filter passes;
// GetFilterPasses 0x140cb9f20, SetFilterFill 0x140cced10): the layer's alpha averaged over boxes of max(1, floor(blur)) pixels, taps every
// texel from -(box - 1) / 2 to +(box - 1) / 2; then the glow's alpha, saturate(strength x blur x (1 - layer alpha)) x alpha, in its colour
// under the layer (FBox2Shadow)
struct GlowFilter {
    float color[3] = {0, 0, 0};
    float alpha = 1;
    float blurX = 6, blurY = 6;  // pixels at the layer's own scale
    float strength = 1;
    int passes = 1;              // BitmapFilterQuality: LOW 1, MEDIUM 2, HIGH 3
};

class Glow {
public:
    explicit Glow(gpu::Device& device);

    // source: a premultiplied layer, width x height; target: the layer with its glow, the same size
    void apply(ID3D11ShaderResourceView* source, ID3D11RenderTargetView* target, int width, int height, const GlowFilter& filter);

private:
    void pass(ID3D11PixelShader* shader, ID3D11ShaderResourceView* a, ID3D11ShaderResourceView* b, ID3D11RenderTargetView* to,
              const float constants[12]);

    gpu::Device& device_;
    gpu::RenderTarget scratch_[2];  // the blurred alpha, ping-ponged
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> blur_, composite_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> border_;
};

}
