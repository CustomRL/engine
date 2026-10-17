// Checks for the 2D canvas's fills and masks, drawn into a multisampled target and read back resolved: an even-odd hole, a half-covered
// edge, nested masks lifted again, a repeating and a clamped bitmap, a gradient's ramp, straight alpha, a colour transform and additive
// blending; and no allocation in a frame once the first has sized the canvas.
#include "engine/core/arena.h"
#include "engine/core/fatal.h"
#include "engine/ui2d/canvas.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(c)                                                                                                                   \
    do {                                                                                                                           \
        if (!(c))                                                                                                                  \
            engine::fatal("engine_check: %s failed (line %d)", #c, __LINE__);                                                     \
    } while (0)

using namespace engine;
using Microsoft::WRL::ComPtr;

namespace {

constexpr int kSize = 64;

struct Target {
    ComPtr<ID3D11Texture2D> color, stencil, resolved;
    ComPtr<ID3D11RenderTargetView> view;
    ComPtr<ID3D11DepthStencilView> stencilView;
    UINT samples = 0;
};

Target makeTarget(gpu::Device& device) {
    auto* d3d = device.d3d();
    Target t;
    for (const UINT n : {8u, 4u}) {
        UINT a = 0, b = 0;
        d3d->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, n, &a);
        d3d->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, n, &b);
        if (a && b) {
            t.samples = n;
            break;
        }
    }
    CHECK(t.samples);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = kSize;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = t.samples;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    CHECK(SUCCEEDED(d3d->CreateTexture2D(&desc, nullptr, &t.color)) && SUCCEEDED(d3d->CreateRenderTargetView(t.color.Get(), nullptr, &t.view)));
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    CHECK(SUCCEEDED(d3d->CreateTexture2D(&desc, nullptr, &t.stencil)) &&
          SUCCEEDED(d3d->CreateDepthStencilView(t.stencil.Get(), nullptr, &t.stencilView)));
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = 0;
    CHECK(SUCCEEDED(d3d->CreateTexture2D(&desc, nullptr, &t.resolved)));
    return t;
}

std::vector<uint8_t> resolve(gpu::Device& device, const Target& t) {
    device.context()->ResolveSubresource(t.resolved.Get(), 0, t.color.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    return gpu::readBack(device, t.resolved.Get());
}

// The pixel's RGBA within tolerance of the expected bytes
bool pixelIs(const std::vector<uint8_t>& px, int x, int y, int r, int g, int b, int a, int tolerance = 1) {
    const uint8_t* p = &px[(size_t(y) * kSize + x) * 4];
    return std::abs(p[0] - r) <= tolerance && std::abs(p[1] - g) <= tolerance && std::abs(p[2] - b) <= tolerance &&
           std::abs(p[3] - a) <= tolerance;
}

ui2d::Fill solid(float x0, float y0, float x1, float y1, std::array<float, 4> color) {
    ui2d::Fill f;
    f.bounds = {x0, y0, x1, y1};
    f.color = color;
    return f;
}

void ramps() {
    uint32_t out[256];
    const uint8_t two[] = {0, 255}, three[] = {0, 128, 255}, inside[] = {64, 192};
    const uint32_t greys[] = {0xff000000, 0xffffffff}, rgb[] = {0xffff0000, 0xff00ff00, 0xff0000ff};
    ui2d::gradientRamp(two, greys, 2, out);
    CHECK(out[0] == 0xff000000 && out[255] == 0xffffffff && out[128] == 0xff808080);
    ui2d::gradientRamp(three, rgb, 3, out);
    CHECK(out[0] == 0xff0000ff && out[64] == 0xff008080 && out[128] == 0xff00ff00 && out[255] == 0xffff0000);  // RGBA, R low
    ui2d::gradientRamp(inside, rgb, 2, out);
    CHECK(out[0] == 0xff0000ff && out[64] == 0xff0000ff && out[192] == 0xff00ff00 && out[255] == 0xff00ff00);
}

}

void ui2dChecks(gpu::Device& device) {
    ramps();
    const Target t = makeTarget(device);
    ui2d::Canvas canvas(device);
    const DirectX::XMFLOAT4X4 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    // a square ring (two closed runs, even-odd), a rectangle whose left edge halves a column, the left half as a mask
    const float points[][2] = {{8, 8},     {40, 8},  {40, 40}, {8, 40},  {8, 8},    {16, 16}, {32, 16}, {32, 32}, {16, 32},
                               {16, 16},   {44.5f, 8}, {56, 8}, {56, 20}, {44.5f, 20}, {44.5f, 8}, {0, 0},  {32, 0},  {32, 64},
                               {0, 64},    {0, 0}};
    const uint32_t ring[] = {0, 5, 5, 5}, edge[] = {10, 5}, half[] = {15, 5};
    std::vector<uint32_t> indices;
    ui2d::fanTriangles(ring, 2, indices);
    const uint32_t ringCount = uint32_t(indices.size());
    ui2d::fanTriangles(edge, 1, indices);
    const uint32_t edgeCount = uint32_t(indices.size()) - ringCount;
    ui2d::fanTriangles(half, 1, indices);
    const uint32_t halfCount = uint32_t(indices.size()) - ringCount - edgeCount;
    const ui2d::FillMesh mesh = ui2d::createFillMesh(device, &points[0][0], uint32_t(std::size(points)), indices.data(), uint32_t(indices.size()));

    auto shapes = [&] {
        canvas.begin(t.view.Get(), kSize, kSize, identity, true, t.stencilView.Get());
        ui2d::Fill f = solid(8, 8, 40, 40, {1, 0, 0, 1});
        f.mesh = &mesh, f.indexCount = ringCount;
        canvas.fill(f);
        f = solid(44.5f, 8, 56, 20, {1, 1, 1, 1});
        f.mesh = &mesh, f.firstIndex = ringCount, f.indexCount = edgeCount;
        canvas.fill(f);
        canvas.end();
    };
    shapes();
    auto px = resolve(device, t);
    CHECK(pixelIs(px, 12, 12, 255, 0, 0, 255) && pixelIs(px, 24, 24, 0, 0, 0, 0) && pixelIs(px, 4, 4, 0, 0, 0, 0));
    CHECK(pixelIs(px, 44, 12, 128, 128, 128, 128, 8) && pixelIs(px, 45, 12, 255, 255, 255, 255) && pixelIs(px, 43, 12, 0, 0, 0, 0));
    CHECK(pixelIs(px, 40, 12, 0, 0, 0, 0) && pixelIs(px, 39, 12, 255, 0, 0, 255));  // the ring's edge on a pixel boundary: none of 40
    const uint64_t before = allocationCount();
    shapes();
    CHECK(allocationCount() == before);

    // mask A (the left half, a fill) then within it mask B (the top half, a quad): blue within A, red within both, then after B is
    // lifted white within A only, and after A green anywhere
    canvas.begin(t.view.Get(), kSize, kSize, identity, true, t.stencilView.Get());
    ui2d::Fill a = solid(0, 0, 32, 64, {1, 1, 1, 1});
    a.mesh = &mesh, a.firstIndex = ringCount + edgeCount, a.indexCount = halfCount;
    canvas.beginMask();
    canvas.fill(a);
    canvas.endMask();
    canvas.fill(solid(0, 0, 64, 64, {0, 0, 1, 1}));
    ui2d::Quad top;
    top.rect = {0, 0, 64, 32};
    canvas.beginMask();
    canvas.draw(top);
    canvas.endMask();
    canvas.fill(solid(0, 0, 64, 64, {1, 0, 0, 1}));
    canvas.popMask();
    canvas.fill(solid(16, 40, 24, 48, {1, 1, 1, 1}));
    canvas.fill(solid(48, 40, 56, 48, {1, 1, 1, 1}));
    canvas.popMask();
    canvas.fill(solid(8, 8, 16, 16, {0, 1, 0, 1}));
    canvas.end();
    px = resolve(device, t);
    CHECK(pixelIs(px, 20, 20, 255, 0, 0, 255) && pixelIs(px, 16, 52, 0, 0, 255, 255) && pixelIs(px, 48, 16, 0, 0, 0, 0));
    CHECK(pixelIs(px, 20, 44, 255, 255, 255, 255) && pixelIs(px, 52, 44, 0, 0, 0, 0) && pixelIs(px, 12, 12, 0, 255, 0, 255));

    // a 2x2 bitmap (red, green / blue, white), 8 pixels a texel, its centres on pixel centres: repeating, then clamped
    const uint32_t texels[] = {0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff};
    const gpu::Texture bitmap = gpu::createTexture2D(device, 2, 2, gpu::TextureFormat::Rgba8, texels, false);
    for (const bool repeat : {true, false}) {
        canvas.begin(t.view.Get(), kSize, kSize, identity, true, t.stencilView.Get());
        ui2d::Fill f = solid(0, 0, 64, 64, {1, 1, 1, 1});
        f.texture = bitmap.view.Get(), f.repeat = repeat;
        f.paint = {1 / 16.f, 0, 0, 1 / 16.f, -0.5f / 16, -0.5f / 16};
        canvas.fill(f);
        canvas.end();
        px = resolve(device, t);
        CHECK(pixelIs(px, 4, 4, 255, 0, 0, 255) && pixelIs(px, 12, 4, 0, 255, 0, 255) && pixelIs(px, 4, 12, 0, 0, 255, 255) &&
              pixelIs(px, 12, 12, 255, 255, 255, 255));
        CHECK(repeat ? pixelIs(px, 20, 4, 255, 0, 0, 255) : pixelIs(px, 20, 4, 0, 255, 0, 255));
    }
    {  // sheared, clamped: u = (x + y - 5) / 16, v = (y - 0.5) / 16
        canvas.begin(t.view.Get(), kSize, kSize, identity, true, t.stencilView.Get());
        ui2d::Fill f = solid(0, 0, 64, 64, {1, 1, 1, 1});
        f.texture = bitmap.view.Get();
        f.paint = {1 / 16.f, 0, 1 / 16.f, 1 / 16.f, -5 / 16.f, -0.5f / 16};
        canvas.fill(f);
        canvas.end();
        px = resolve(device, t);
        CHECK(pixelIs(px, 4, 4, 255, 0, 0, 255) && pixelIs(px, 12, 4, 0, 255, 0, 255) && pixelIs(px, 4, 12, 255, 255, 255, 255));
    }

    // a black-to-white ramp across the target; straight alpha premultiplied on the way out; a transform; red plus green added
    uint32_t greys[256];
    const uint8_t ends[] = {0, 255};
    const uint32_t colors[] = {0xff000000, 0xffffffff};
    ui2d::gradientRamp(ends, colors, 2, greys);
    const gpu::Texture ramp = gpu::createTexture2D(device, 256, 1, gpu::TextureFormat::Rgba8, greys, false);
    canvas.begin(t.view.Get(), kSize, kSize, identity, true, t.stencilView.Get());
    ui2d::Fill g = solid(0, 0, 64, 8, {1, 1, 1, 1});
    g.texture = ramp.view.Get();
    g.paint = {255.f / 256 / 64, 0, 0, 0, 0.5f / 256, 0.5f};  // t = x / 64 onto the texels' centres
    canvas.fill(g);
    canvas.fill(solid(0, 16, 64, 24, {0.5f, 0, 0, 1}));
    ui2d::Fill add = solid(0, 16, 64, 24, {0, 0.5f, 0, 1});
    add.blend = ui2d::Blend::Add;
    canvas.fill(add);
    ui2d::Fill tinted = solid(0, 32, 64, 40, {1, 1, 1, 1});
    tinted.transform = {{0.5f, 1, 1, 1}, {0, 0, 0.25f, 0}};
    canvas.fill(tinted);
    canvas.fill(solid(0, 48, 64, 56, {1, 1, 1, 0.5f}));
    canvas.end();
    px = resolve(device, t);
    CHECK(pixelIs(px, 32, 4, 130, 130, 130, 255, 3) && pixelIs(px, 0, 4, 2, 2, 2, 255, 2) && pixelIs(px, 63, 4, 253, 253, 253, 255, 2));
    CHECK(pixelIs(px, 32, 20, 128, 128, 0, 255) && pixelIs(px, 32, 36, 128, 255, 255, 255) && pixelIs(px, 32, 52, 128, 128, 128, 128));
    std::printf("ui2d fills: ok (%ux MSAA; even-odd hole, half-covered edge, nested masks lifted, bitmap repeat and clamp, ramp, "
                "straight alpha, transform, add; no allocation in a repeated frame)\n",
                t.samples);
}
