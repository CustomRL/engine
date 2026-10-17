#pragma once

#include "engine/gpu/device.h"

#include <cstdint>
#include <vector>

namespace engine::ui2d {

// The points and fan triangles of filled regions, made once. A region's fan, from one point to each segment of the
// closed polylines bounding it, covers every place inside an odd number of times and every place outside an even
// number, which Canvas::fill counts in the stencil.
struct FillMesh {
    Microsoft::WRL::ComPtr<ID3D11Buffer> points, indices;  // float2s; uint32 triangles
};
FillMesh createFillMesh(gpu::Device& device, const float* points, uint32_t pointCount, const uint32_t* indices,
                        uint32_t indexCount);

// Appends to out the fan of the polylines runs[2k] (first point), runs[2k + 1] (point count): a triangle from the
// first run's first point to each segment, but not to those that start or end at that point (they have no area)
void fanTriangles(const uint32_t* runs, uint32_t runCount, std::vector<uint32_t>& out);

// Flash's gradient as 256 straight RGBA8 texels (R in the low byte), from count stops (ratios 0-255 ascending, colours
// 0xAARRGGBB): each texel mixes the stops either side of its ratio linearly, and past the first or last stop takes
// that stop's colour. ours: Scaleform's own ramp code hasn't been traced, so its rounding may differ by one.
void gradientRamp(const uint8_t* ratios, const uint32_t* colors, int count, uint32_t out[256]);

}
