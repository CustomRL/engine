#include "engine/ui2d/font.h"

#include "engine/core/fatal.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>

namespace engine::ui2d {

namespace {

constexpr int ATLAS = 2048;

void check(HRESULT hr, const char* what) {
    if (FAILED(hr))
        fatal("%s failed: 0x%08lx", what, static_cast<unsigned long>(hr));
}

}

struct Font::Info {
    stbtt_fontinfo font;
};

Font::Font(gpu::Device& device, std::vector<uint8_t> ttf) : device_(device), ttf_(std::move(ttf)), info_(std::make_unique<Info>()) {
    if (!stbtt_InitFont(&info_->font, ttf_.data(), stbtt_GetFontOffsetForIndex(ttf_.data(), 0)))
        fatal("not a TrueType font");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = ATLAS;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const std::vector<uint8_t> clear(static_cast<size_t>(ATLAS) * ATLAS, 0);
    const D3D11_SUBRESOURCE_DATA init{clear.data(), ATLAS, 0};
    check(device.d3d()->CreateTexture2D(&desc, &init, &texture_), "CreateTexture2D (glyph atlas)");
    check(device.d3d()->CreateShaderResourceView(texture_.Get(), nullptr, &view_), "CreateShaderResourceView (glyph atlas)");
}

Font::~Font() = default;

float Font::advance(char32_t codepoint, float size) const {
    int advance, bearing;
    stbtt_GetCodepointHMetrics(&info_->font, static_cast<int>(codepoint), &advance, &bearing);
    return advance * stbtt_ScaleForMappingEmToPixels(&info_->font, size);
}

const Font::Glyph& Font::glyph(char32_t codepoint, float size) {
    const auto key = std::make_pair(codepoint, size);
    if (const auto found = glyphs_.find(key); found != glyphs_.end())
        return found->second;
    const float scale = stbtt_ScaleForMappingEmToPixels(&info_->font, size);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&info_->font, static_cast<int>(codepoint), scale, scale, &x0, &y0, &x1, &y1);
    const int w = x1 - x0, h = y1 - y0;
    Glyph g;
    g.advance = advance(codepoint, size);
    if (w > 0 && h > 0) {
        if (shelfX_ + w + 1 > ATLAS) {  // a new shelf
            shelfX_ = 1;
            shelfY_ += shelfHeight_ + 1;
            shelfHeight_ = 0;
        }
        if (shelfY_ + h + 1 > ATLAS)
            fatal("glyph atlas full");  // ponytail: one atlas per font; evict or add pages if a menu ever needs more
        std::vector<uint8_t> bitmap(static_cast<size_t>(w) * h);
        stbtt_MakeCodepointBitmap(&info_->font, bitmap.data(), w, h, w, scale, scale, static_cast<int>(codepoint));
        const D3D11_BOX box{static_cast<UINT>(shelfX_), static_cast<UINT>(shelfY_), 0, static_cast<UINT>(shelfX_ + w),
                            static_cast<UINT>(shelfY_ + h), 1};
        device_.context()->UpdateSubresource(texture_.Get(), 0, &box, bitmap.data(), w, 0);
        g = {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1), static_cast<float>(y1),
             static_cast<float>(shelfX_) / ATLAS, static_cast<float>(shelfY_) / ATLAS, static_cast<float>(shelfX_ + w) / ATLAS,
             static_cast<float>(shelfY_ + h) / ATLAS, g.advance};
        shelfX_ += w + 1;
        shelfHeight_ = std::max(shelfHeight_, h);
    }
    return glyphs_.emplace(key, g).first->second;
}

}
