#include "engine/ui2d/fill.h"

#include "engine/gpu/buffer.h"

#include <cmath>

namespace engine::ui2d {

FillMesh createFillMesh(gpu::Device& device, const float* points, uint32_t pointCount, const uint32_t* indices,
                        uint32_t indexCount) {
    return {gpu::createBuffer(device, D3D11_BIND_VERTEX_BUFFER, points, pointCount * 2 * sizeof(float)),
            gpu::createBuffer(device, D3D11_BIND_INDEX_BUFFER, indices, indexCount * sizeof(uint32_t))};
}

void fanTriangles(const uint32_t* runs, uint32_t runCount, GrowArray<uint32_t>& out) {
    if (!runCount)
        return;
    const uint32_t apex = runs[0];
    for (uint32_t r = 0; r < runCount; r++)
        for (uint32_t p = runs[2 * r], end = p + runs[2 * r + 1] - 1; p < end; p++)
            if (p != apex && p + 1 != apex)
                out.push_back(apex), out.push_back(p), out.push_back(p + 1);
}

void gradientRamp(const uint8_t* ratios, const uint32_t* colors, int count, uint32_t out[256]) {
    for (int t = 0, k = 0; t < 256; t++) {
        while (k + 1 < count && ratios[k + 1] < t)
            k++;
        const int b = k + 1 < count && t > ratios[k] ? k + 1 : k;
        const float f = b == k ? 0.f : float(t - ratios[k]) / float(ratios[b] - ratios[k]);
        uint32_t texel = 0;
        for (int c = 0; c < 4; c++) {  // ARGB in, RGBA out
            const int shift = c == 3 ? 24 : 16 - 8 * c;
            const float x = float(colors[k] >> shift & 255), y = float(colors[b] >> shift & 255);
            texel |= uint32_t(std::lround(x + (y - x) * f)) << (8 * c);
        }
        out[t] = texel;
    }
}

}
