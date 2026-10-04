#pragma once

#include "engine/gpu/device.h"

#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace engine::ui2d {

// A TrueType font whose glyphs are rasterised on first use, at each pixel size asked for, into one coverage atlas (red channel), which
// Canvas draws as Quad::coverage
class Font {
public:
    Font(gpu::Device& device, std::vector<uint8_t> ttf);
    ~Font();

    struct Glyph {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // the bitmap's box from the pen on the baseline, pixels, y down
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // its place in the atlas
        float advance = 0;                     // pixels
    };
    // codepoint at `size` pixels to the em, as Flash's font size is
    const Glyph& glyph(char32_t codepoint, float size);
    // the advance alone, without rasterising
    float advance(char32_t codepoint, float size) const;
    ID3D11ShaderResourceView* atlas() const { return view_.Get(); }

private:
    struct Info;
    gpu::Device& device_;
    std::vector<uint8_t> ttf_;
    std::unique_ptr<Info> info_;
    std::map<std::pair<char32_t, float>, Glyph> glyphs_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view_;
    int shelfX_ = 1, shelfY_ = 1, shelfHeight_ = 0;  // the next free spot, rows packed left to right
};

}
