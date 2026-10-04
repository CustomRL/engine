#pragma once

#include "engine/gpu/device.h"
#include "engine/gpu/texture.h"

#include <DirectXMath.h>

#include <array>
#include <vector>

namespace engine::ui2d {

// Flash's ColorTransform: each channel of the unpremultiplied colour times its multiplier plus its offset, clamped to 0..1. Offsets are
// in 0..1 (Flash's over 255).
struct ColorTransform {
    std::array<float, 4> mult{1, 1, 1, 1};
    std::array<float, 4> offset{0, 0, 0, 0};

    // inner applied first, then this, as Flash concatenates a child's transform with its parent's (no clamp between)
    ColorTransform after(const ColorTransform& inner) const;
    static ColorTransform flash(float rm, float gm, float bm, float am, float ro, float go, float bo, float ao) {
        return {{rm, gm, bm, am}, {ro / 255, go / 255, bo / 255, ao / 255}};
    }
};

// Normal: source over destination; Add: source added (Flash's BlendMode.ADD). Colours are premultiplied throughout.
enum class Blend { Normal, Add };

// One textured rectangle. Its local corners (x, y) to (x + width, y + height), as row vectors (x, y, 0, 1) times matrix, give stage
// units in homogeneous form, so a Flash perspective projection keeps its perspective.
struct Quad {
    ID3D11ShaderResourceView* texture = nullptr;  // premultiplied RGBA; null is opaque white
    bool coverage = false;                        // the texture's red is coverage (a glyph atlas): white at that alpha
    std::array<float, 4> rect{};                  // x, y, width, height
    std::array<float, 4> uv{0, 0, 1, 1};          // u0, v0, u1, v1
    DirectX::XMFLOAT4X4 matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    ColorTransform color;
    Blend blend = Blend::Normal;
    // a pie of the rectangle: what lies within `sweep` degrees clockwise from `sweepStart` (0 up, y down) about its centre
    float sweepStart = 0, sweep = 360;
};

// Draws quads in order into a target, batched while texture and blend stay the same
class Canvas {
public:
    explicit Canvas(gpu::Device& device);

    // A pass into target, width x height pixels, mapping stage units to pixels by stageToTarget (row vectors). clear: to transparent.
    void begin(ID3D11RenderTargetView* target, int width, int height, const DirectX::XMFLOAT4X4& stageToTarget, bool clear = false);
    void draw(const Quad& quad);
    void end();

private:
    struct Vertex {
        float position[4];  // clip space
        float uv[2], local[2];
        float mult[4], offset[4];
        float params[4];  // sweepStart, sweep, coverage
    };
    void flush();

    gpu::Device& device_;
    std::vector<Vertex> vertices_;
    size_t capacity_ = 0;
    ID3D11ShaderResourceView* texture_ = nullptr;
    Blend blend_ = Blend::Normal;
    DirectX::XMFLOAT4X4 toClip_{};
    gpu::Texture white_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer_, indices_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blends_[2];
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
};

}
