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

// A fill's constants: to clip space, the cover's bounds, its paint's rows and its transform with the colour folded in;
// a stencil's under a grid: local to the grid's space (gridX.w 1) and each axis's knots and where they go
const char SHADER[] = R"(
cbuffer Fill : register(b0) {
    row_major float4x4 toClip;
    float4 bounds;
    float4 paintU, paintV;
    float4 mult, offset;
    float4 gridX, gridY;
    float4 fromX, toX, fromY, toY;
};
Texture2D image : register(t0);
SamplerState linearSampler : register(s0);
float piece(float v, float a, float b, float A, float B) {
    return b > a ? A + (v - a) * (B - A) / (b - a) : A;
}
float axis(float v, float4 f, float4 t) {
    return v < f.y ? piece(v, f.x, f.y, t.x, t.y) : v < f.z ? piece(v, f.y, f.z, t.y, t.z) : piece(v, f.z, f.w, t.z, t.w);
}
float4 stencilVs(float2 p : POSITION) : SV_Position {
    if (gridX.w > 0) {
        float2 q = float2(dot(gridX.xyz, float3(p, 1)), dot(gridY.xyz, float3(p, 1)));
        p = float2(axis(q.x, fromX, toX), axis(q.y, fromY, toY));
    }
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
constexpr int kCoverFloat4s = 9, kStencilFloat4s = 15;

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

void Canvas::cover(const Fill& f, const XMMATRIX& toClip, const std::array<float, 4>& bounds, const std::array<float, 6>& paint) {
    const auto& t = f.transform;
    float c[kCoverFloat4s * 4];
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(c), toClip);
    const float rest[] = {bounds[0], bounds[1], bounds[2], bounds[3], paint[0], paint[2], paint[4], 0, paint[1], paint[3], paint[5], 0,
                          f.color[0] * t.mult[0], f.color[1] * t.mult[1], f.color[2] * t.mult[2], f.color[3] * t.mult[3],
                          t.offset[0], t.offset[1], t.offset[2], t.offset[3]};
    std::memcpy(c + 16, rest, sizeof rest);
    commands_.push_back({Kind::Cover, f.blend, masking_, f.repeat, level_, f.mesh != nullptr, f.texture ? f.texture : white_.view.Get(),
                         nullptr, 0, 0, ring_.push(c, std::size(c), kCoverFloat4s), {}});
    if (masking_) {
        const float x0 = bounds[0], y0 = bounds[1], x1 = bounds[2], y1 = bounds[3];
        for (const auto& [x, y] : {std::array{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}) {  // as a quad's corners
            XMFLOAT4 corner;
            XMStoreFloat4(&corner, XMVector4Transform(XMVectorSet(x, y, 0, 1), toClip));
            maskCorners_.push_back(corner);
        }
    }
}

void Canvas::fill(const Fill& f) {
    if (f.mesh && !stencil_)
        fatal("ui2d: a fill with a mesh needs begin's stencil");
    const XMFLOAT4X4& m = f.matrix;
    const bool grid = f.grid && !m._12 && !m._21 && m._11 && m._22 && !m._13 && !m._23 && !m._14 && !m._24 && m._44 == 1;
    const XMMATRIX toClip = (grid ? XMLoadFloat4x4(&f.grid->outer) : XMLoadFloat4x4(&m)) * XMLoadFloat4x4(&toClip_);
    if (f.mesh) {
        float c[kStencilFloat4s * 4] = {};
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(c), toClip);
        if (grid) {
            const float rows[] = {m._11, 0, m._41, 1, 0, m._22, m._42, 0};
            std::memcpy(c + 36, rows, sizeof rows);
            for (int k = 0; k < 4; k++)
                c[44 + k] = f.grid->fromX[k], c[48 + k] = f.grid->toX[k], c[52 + k] = f.grid->fromY[k], c[56 + k] = f.grid->toY[k];
        }
        commands_.push_back({Kind::Stencil, f.blend, masking_, false, level_, false, nullptr, f.mesh, f.firstIndex, f.indexCount,
                             ring_.push(c, std::size(c), kStencilFloat4s), {}});
    }
    if (!grid)
        return cover(f, toClip, f.bounds, f.paint);
    // the bounds in the grid's space, cut at its knots: each piece mapped on by its own scale and move, its paint taken
    // back through the matrix to local
    const Scale9& g = *f.grid;
    const float a = m._11, d = m._22, tx = m._41, ty = m._42;
    const float qx0 = std::min(a * f.bounds[0], a * f.bounds[2]) + tx, qx1 = std::max(a * f.bounds[0], a * f.bounds[2]) + tx;
    const float qy0 = std::min(d * f.bounds[1], d * f.bounds[3]) + ty, qy1 = std::max(d * f.bounds[1], d * f.bounds[3]) + ty;
    const auto& p = f.paint;
    const std::array<float, 6> paint{p[0] / a, p[1] / a, p[2] / d, p[3] / d, p[4] - p[0] * tx / a - p[2] * ty / d,
                                     p[5] - p[1] * tx / a - p[3] * ty / d};
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            const float x0 = std::max(qx0, g.fromX[i]), x1 = std::min(qx1, g.fromX[i + 1]);
            const float y0 = std::max(qy0, g.fromY[j]), y1 = std::min(qy1, g.fromY[j + 1]);
            if (x0 >= x1 || y0 >= y1)
                continue;
            const float sx = (g.toX[i + 1] - g.toX[i]) / (g.fromX[i + 1] - g.fromX[i]);
            const float sy = (g.toY[j + 1] - g.toY[j]) / (g.fromY[j + 1] - g.fromY[j]);
            const XMMATRIX piece(sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, 1, 0, g.toX[i] - g.fromX[i] * sx, g.toY[j] - g.fromY[j] * sy, 0, 1);
            cover(f, piece * toClip, {x0, y0, x1, y1}, paint);
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
    commands_.push_back({Kind::Pop, Blend::Normal, false, false, level_, false, nullptr, nullptr, first,
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
            context->OMSetBlendState(color ? blends_[int(c.blend)].Get() : noColor_.Get(), nullptr, 0xffffffff);
            context->OMSetDepthStencilState(c.kind == Kind::Pop ? pop_.Get() : c.mask ? maskQuad_.Get() : content_.Get(), c.level);
            for (size_t q = 0; q < c.count; q += MAX_QUADS)
                context->DrawIndexed(UINT(std::min(c.count - q, MAX_QUADS) * 6), 0, INT((c.first + q) * 4));
        } else if (c.kind == Kind::Stencil) {
            ID3D11Buffer* points = c.mesh->points.Get();
            context->IASetInputLayout(pointLayout_.Get());
            context->IASetVertexBuffers(0, 1, &points, &pointStride, &zero);
            context->IASetIndexBuffer(c.mesh->indices.Get(), DXGI_FORMAT_R32_UINT, 0);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(stencilShader_.Get(), nullptr, 0);
            ring_.bind(vertex, 0, c.constants, kStencilFloat4s);
            context->PSSetShader(nullptr, nullptr, 0);
            context->OMSetBlendState(noColor_.Get(), nullptr, 0xffffffff);
            context->OMSetDepthStencilState(count_.Get(), c.level);
            context->DrawIndexed(c.count, c.first, 0);
        } else {
            context->IASetInputLayout(nullptr);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            context->VSSetShader(coverShader_.Get(), nullptr, 0);
            ring_.bind(vertex, 0, c.constants, kCoverFloat4s);
            if (c.mask) {
                context->PSSetShader(nullptr, nullptr, 0);
                context->OMSetBlendState(noColor_.Get(), nullptr, 0xffffffff);
                // a fill's cover writes the next depth where its count is odd; a rectangle steps it up whole
                context->OMSetDepthStencilState(c.counted ? cover_.Get() : maskQuad_.Get(), c.counted ? c.level + 1 : c.level);
            } else {
                ID3D11SamplerState* sampler = c.repeat ? wrap_.Get() : sampler_.Get();
                context->PSSetShader(paintShader_.Get(), nullptr, 0);
                ring_.bind(pixel, 0, c.constants, kCoverFloat4s);
                context->PSSetShaderResources(0, 1, &c.texture);
                context->PSSetSamplers(0, 1, &sampler);
                context->OMSetBlendState(blends_[int(c.blend)].Get(), nullptr, 0xffffffff);
                context->OMSetDepthStencilState(c.counted ? cover_.Get() : content_.Get(), c.level);
            }
            context->Draw(4, 0);
        }
    }
}

}
