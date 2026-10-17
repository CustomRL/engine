#include "engine/ui2d/canvas.h"

#include "engine/core/fatal.h"
#include "engine/gpu/shader_cache.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace engine::ui2d {

namespace {

// A fill's constants: local to clip space, its bounds, its paint's rows and its transform with the colour folded in
const char SHADER[] = R"(
cbuffer Fill : register(b0) {
    row_major float4x4 toClip;
    float4 bounds;
    float4 paintU, paintV;
    float4 mult, offset;
};
Texture2D image : register(t0);
SamplerState linearSampler : register(s0);
float4 stencilVs(float2 p : POSITION) : SV_Position {
    return mul(float4(p, 0, 1), toClip);
}
struct Cover {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
Cover coverVs(uint id : SV_VertexID) {
    float2 p = float2(id & 1 ? bounds.z : bounds.x, id & 2 ? bounds.w : bounds.y);
    Cover c;
    c.position = mul(float4(p, 0, 1), toClip);
    c.uv = float2(dot(paintU.xyz, float3(p, 1)), dot(paintV.xyz, float3(p, 1)));
    return c;
}
float4 paintPs(Cover c) : SV_Target {
    float4 o = saturate(image.Sample(linearSampler, c.uv) * mult + offset);
    return float4(o.rgb * o.a, o.a);
}
)";
constexpr int kFillFloat4s = 9;

void check(HRESULT hr, const char* what) {
    if (FAILED(hr))
        fatal("%s failed: 0x%08lx", what, static_cast<unsigned long>(hr));
}

ComPtr<ID3DBlob> compile(const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, errors;
    if (FAILED(engine::gpu::compileCached(SHADER, sizeof SHADER - 1, "ui2d fill", nullptr, nullptr, entry, target,
                                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors)))
        fatal("ui2d fill shader %s: %s", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
    return code;
}

// No depth; the stencil test (ref & read) func (stencil & read), and on a pass the op through the write mask
D3D11_DEPTH_STENCIL_DESC stencilDesc(D3D11_COMPARISON_FUNC func, UINT8 read, UINT8 write, D3D11_STENCIL_OP pass) {
    D3D11_DEPTH_STENCIL_DESC d{};
    d.DepthEnable = FALSE;
    d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    d.DepthFunc = D3D11_COMPARISON_ALWAYS;
    d.StencilEnable = TRUE;
    d.StencilReadMask = read;
    d.StencilWriteMask = write;
    d.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, pass, func};
    d.BackFace = d.FrontFace;
    return d;
}

}

void Canvas::createFillPipeline() {
    auto* d3d = device_.d3d();
    const auto stencil = compile("stencilVs", "vs_5_0"), cover = compile("coverVs", "vs_5_0"), paint = compile("paintPs", "ps_5_0");
    check(d3d->CreateVertexShader(stencil->GetBufferPointer(), stencil->GetBufferSize(), nullptr, &stencilShader_), "CreateVertexShader (ui2d stencil)");
    check(d3d->CreateVertexShader(cover->GetBufferPointer(), cover->GetBufferSize(), nullptr, &coverShader_), "CreateVertexShader (ui2d cover)");
    check(d3d->CreatePixelShader(paint->GetBufferPointer(), paint->GetBufferSize(), nullptr, &paintShader_), "CreatePixelShader (ui2d paint)");
    const D3D11_INPUT_ELEMENT_DESC point{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    check(d3d->CreateInputLayout(&point, 1, stencil->GetBufferPointer(), stencil->GetBufferSize(), &pointLayout_), "CreateInputLayout (ui2d fill)");
    D3D11_BLEND_DESC none{};
    none.RenderTarget[0].RenderTargetWriteMask = 0;
    check(d3d->CreateBlendState(&none, &noColor_), "CreateBlendState (ui2d no colour)");
    // the low 7 bits are the mask depth, the top bit a fill's count: content draws where the depth is the ref; a fill's fan flips the
    // top bit within it, and its cover passes where that bit is set (a ref without it, not equal), writing the ref back
    const struct {
        ComPtr<ID3D11DepthStencilState>& state;
        D3D11_DEPTH_STENCIL_DESC desc;
    } states[] = {
        {content_, stencilDesc(D3D11_COMPARISON_EQUAL, 0x7f, 0, D3D11_STENCIL_OP_KEEP)},
        {count_, stencilDesc(D3D11_COMPARISON_EQUAL, 0x7f, 0x80, D3D11_STENCIL_OP_INVERT)},
        {cover_, stencilDesc(D3D11_COMPARISON_NOT_EQUAL, 0x80, 0xff, D3D11_STENCIL_OP_REPLACE)},
        {maskQuad_, stencilDesc(D3D11_COMPARISON_EQUAL, 0x7f, 0x7f, D3D11_STENCIL_OP_INCR_SAT)},
        {pop_, stencilDesc(D3D11_COMPARISON_EQUAL, 0x7f, 0x7f, D3D11_STENCIL_OP_DECR_SAT)},
    };
    for (const auto& s : states)
        check(d3d->CreateDepthStencilState(&s.desc, &s.state), "CreateDepthStencilState (ui2d)");
}

void Canvas::fill(const Fill& f) {
    if (f.mesh && !stencil_)
        fatal("ui2d: a fill with a mesh needs begin's stencil");
    XMFLOAT4X4 m;
    const XMMATRIX toClip = XMLoadFloat4x4(&f.matrix) * XMLoadFloat4x4(&toClip_);
    XMStoreFloat4x4(&m, toClip);
    const auto& p = f.paint;
    const auto& t = f.transform;
    float c[kFillFloat4s * 4];
    std::memcpy(c, &m, sizeof m);
    const float rest[] = {f.bounds[0], f.bounds[1], f.bounds[2], f.bounds[3], p[0], p[2], p[4], 0, p[1], p[3], p[5], 0,
                          f.color[0] * t.mult[0], f.color[1] * t.mult[1], f.color[2] * t.mult[2], f.color[3] * t.mult[3],
                          t.offset[0], t.offset[1], t.offset[2], t.offset[3]};
    std::memcpy(c + 16, rest, sizeof rest);
    const uint32_t at = ring_.push(c, std::size(c), kFillFloat4s);
    commands_.push_back({Kind::Fill, f.blend, masking_, f.repeat, level_, f.texture ? f.texture : white_.view.Get(), f.mesh,
                         f.firstIndex, f.indexCount, at, {}});
    if (masking_) {
        const float x0 = f.bounds[0], y0 = f.bounds[1], x1 = f.bounds[2], y1 = f.bounds[3];
        for (const auto& [x, y] : {std::array{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}) {  // as a quad's corners
            XMFLOAT4 corner;
            XMStoreFloat4(&corner, XMVector4Transform(XMVectorSet(x, y, 0, 1), toClip));
            maskCorners_.push_back(corner);
        }
    }
}

void Canvas::beginMask() {
    if (!stencil_ || masking_ || level_ == 0x7f)
        fatal("ui2d: beginMask without begin's stencil, inside a mask, or 127 deep");
    masking_ = true;
    maskStarts_.push_back(uint32_t(maskCorners_.size()));
}

void Canvas::endMask() {
    masking_ = false;
    level_++;
}

void Canvas::popMask() {
    if (maskStarts_.empty() || masking_)
        fatal("ui2d: popMask with no mask, or inside one");
    const uint32_t start = maskStarts_.back(), first = uint32_t(vertices_.size() / 4);
    maskStarts_.pop_back();
    // each piece's rectangle again, where the mask raised the depth
    for (uint32_t k = start; k < maskCorners_.size(); k++) {
        const XMFLOAT4& p = maskCorners_[k];
        vertices_.push_back({{p.x, p.y, 0, p.w}, {}, {}, {}, {}, {}});
    }
    commands_.push_back({Kind::Pop, Blend::Normal, false, false, level_, nullptr, nullptr, first,
                         uint32_t(maskCorners_.size() - start) / 4, 0, {}});
    maskCorners_.resize(start);
    level_--;
}

void Canvas::replay() {
    auto* context = device_.context();
    ID3D11Buffer* quads = buffer_.Get();
    const UINT quadStride = sizeof(Vertex), pointStride = 2 * sizeof(float), zero = 0;
    const auto vertex = gpu::ConstantRing::Stage::Vertex, pixel = gpu::ConstantRing::Stage::Pixel;
    for (const Command& c : commands_) {
        if (c.kind == Kind::Clip) {
            const D3D11_RECT r{c.rect[0], c.rect[1], c.rect[2], c.rect[3]};
            context->RSSetScissorRects(1, &r);
            context->RSSetState(scissor_.Get());
        } else if (c.kind == Kind::NoClip) {
            context->RSSetState(rasterizer_.Get());
        } else if (c.kind == Kind::Quads || c.kind == Kind::Pop) {
            const bool color = c.kind == Kind::Quads && !c.mask;
            context->IASetInputLayout(layout_.Get());
            context->IASetVertexBuffers(0, 1, &quads, &quadStride, &zero);
            context->IASetIndexBuffer(indices_.Get(), DXGI_FORMAT_R16_UINT, 0);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(vertexShader_.Get(), nullptr, 0);
            context->PSSetShader(color ? pixelShader_.Get() : nullptr, nullptr, 0);
            if (color) {
                ID3D11SamplerState* sampler = sampler_.Get();
                context->PSSetShaderResources(0, 1, &c.texture);
                context->PSSetSamplers(0, 1, &sampler);
            }
            context->OMSetBlendState(color ? blends_[c.blend == Blend::Add].Get() : noColor_.Get(), nullptr, 0xffffffff);
            context->OMSetDepthStencilState(c.kind == Kind::Pop ? pop_.Get() : c.mask ? maskQuad_.Get() : content_.Get(), c.level);
            for (size_t q = 0; q < c.count; q += MAX_QUADS)
                context->DrawIndexed(UINT(std::min(c.count - q, MAX_QUADS) * 6), 0, INT((c.first + q) * 4));
        } else {
            if (c.mesh) {
                ID3D11Buffer* points = c.mesh->points.Get();
                context->IASetInputLayout(pointLayout_.Get());
                context->IASetVertexBuffers(0, 1, &points, &pointStride, &zero);
                context->IASetIndexBuffer(c.mesh->indices.Get(), DXGI_FORMAT_R32_UINT, 0);
                context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                context->VSSetShader(stencilShader_.Get(), nullptr, 0);
                ring_.bind(vertex, 0, c.constants, kFillFloat4s);
                context->PSSetShader(nullptr, nullptr, 0);
                context->OMSetBlendState(noColor_.Get(), nullptr, 0xffffffff);
                context->OMSetDepthStencilState(count_.Get(), c.level);
                context->DrawIndexed(c.count, c.first, 0);
            }
            context->IASetInputLayout(nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            context->VSSetShader(coverShader_.Get(), nullptr, 0);
            ring_.bind(vertex, 0, c.constants, kFillFloat4s);
            if (c.mask) {
                context->PSSetShader(nullptr, nullptr, 0);
                context->OMSetBlendState(noColor_.Get(), nullptr, 0xffffffff);
                // a fill's cover writes the next depth where its count is odd; a rectangle steps it up whole
                context->OMSetDepthStencilState(c.mesh ? cover_.Get() : maskQuad_.Get(), c.mesh ? c.level + 1 : c.level);
            } else {
                ID3D11SamplerState* sampler = c.repeat ? wrap_.Get() : sampler_.Get();
                context->PSSetShader(paintShader_.Get(), nullptr, 0);
                ring_.bind(pixel, 0, c.constants, kFillFloat4s);
                context->PSSetShaderResources(0, 1, &c.texture);
                context->PSSetSamplers(0, 1, &sampler);
                context->OMSetBlendState(blends_[c.blend == Blend::Add].Get(), nullptr, 0xffffffff);
                context->OMSetDepthStencilState(c.mesh ? cover_.Get() : content_.Get(), c.level);
            }
            context->Draw(4, 0);
        }
    }
}

}
