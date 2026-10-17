#pragma once

#include "engine/gpu/constant_ring.h"
#include "engine/gpu/device.h"
#include "engine/gpu/texture.h"
#include "engine/ui2d/fill.h"

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

// Flash's scale9Grid: positions in the grid's space (a Fill's matrix takes it there) mapped per axis, piece by piece,
// through four knots (the content's edge, the grid's two lines, the content's other edge) to where they go, then by outer
// to stage units. The fan's points are mapped each; the cover is cut at the knots, so a texture stretches piece by piece.
struct Scale9 {
    std::array<float, 4> fromX, toX, fromY, toY;
    DirectX::XMFLOAT4X4 outer{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

// A filled region: the mesh's fan (fill.h) counted even-odd into the stencil, then its bounds covered where the count
// is odd, painted by the texture. Without a mesh the bounds are covered whole (a bitmap's rectangle). The paint is
// straight alpha, as the game's textures are: texture times colour, then the transform, premultiplied after.
struct Fill {
    const FillMesh* mesh = nullptr;
    uint32_t firstIndex = 0, indexCount = 0;
    std::array<float, 4> bounds{};  // x0, y0, x1, y1, local: around every point of the fan (its count is cleared within them)
    DirectX::XMFLOAT4X4 matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // local to stage, as Quad's
    ID3D11ShaderResourceView* texture = nullptr;  // straight RGBA; null is opaque white
    bool repeat = false;                          // texture coordinates wrap rather than clamp
    std::array<float, 6> paint{1, 0, 0, 1, 0, 0};  // local to texture coordinates: u = a x + c y + tx, v = b x + d y + ty
    std::array<float, 4> color{1, 1, 1, 1};        // straight RGBA (a solid fill's colour)
    ColorTransform transform;
    Blend blend = Blend::Normal;
    const Scale9* grid = nullptr;  // the matrix then only scales and moves (else the grid isn't applied, as Flash
                                   // drops it under a rotation)
};

// Records quads, fills and masks in order and draws them at end, with one upload of vertices and one of constants.
// Fills and masks need begin's stencil: its low 7 bits hold the mask depth, its top bit a fill's count.
class Canvas {
public:
    explicit Canvas(gpu::Device& device);

    // A pass into target, width x height pixels, mapping stage units to pixels by stageToTarget (row vectors). clear: to transparent.
    // stencil: a buffer with 8 stencil bits the size of target (and its sample count), cleared at end before drawing.
    void begin(ID3D11RenderTargetView* target, int width, int height, const DirectX::XMFLOAT4X4& stageToTarget, bool clear = false,
               ID3D11DepthStencilView* stencil = nullptr);
    void draw(const Quad& quad);
    void fill(const Fill& fill);
    // What is drawn from beginMask to endMask isn't drawn but forms a mask (the union of its shapes; a quad's whole rectangle) that
    // clips what is drawn after it, within the masks already in force, until popMask lifts it
    void beginMask();
    void endMask();
    void popMask();
    // what is drawn from here on lands only inside the target's pixel rectangle (x0, y0, x1, y1); null lifts it
    void clip(const std::array<int, 4>* rect);
    void end();

private:
    struct Vertex {
        float position[4];  // clip space
        float uv[2], local[2];
        float mult[4], offset[4];
        float params[4];  // sweepStart, sweep, coverage
    };
    static constexpr size_t MAX_QUADS = 16384;  // per draw: 16-bit indices
    enum class Kind : uint8_t { Quads, Stencil, Cover, Pop, Clip, NoClip };
    struct Command {
        Kind kind;
        Blend blend;
        bool mask, repeat;
        uint8_t level;  // the mask depth it draws within (Pop: the depth it lifts)
        bool counted;   // Cover: where a Stencil counted, else the whole rectangle
        ID3D11ShaderResourceView* texture;
        const FillMesh* mesh;
        uint32_t first, count;  // Quads, Pop: quads; Stencil: indices
        uint32_t constants;     // Stencil, Cover: their float4s in the ring
        std::array<int, 4> rect;
    };
    void createFillPipeline();
    void cover(const Fill& f, const DirectX::XMMATRIX& toClip, const std::array<float, 4>& bounds, const std::array<float, 6>& paint);
    void replay();

    gpu::Device& device_;
    std::vector<Vertex> vertices_;
    std::vector<Command> commands_;
    std::vector<DirectX::XMFLOAT4> maskCorners_;  // the clip-space corners of the masks' pieces, innermost last
    std::vector<uint32_t> maskStarts_;            // each mask's first corner
    uint8_t level_ = 0;
    bool masking_ = false;
    size_t capacity_ = 0;
    DirectX::XMFLOAT4X4 toClip_{};
    ID3D11RenderTargetView* target_ = nullptr;
    ID3D11DepthStencilView* stencil_ = nullptr;
    int width_ = 0, height_ = 0;
    bool clear_ = false;
    gpu::Texture white_;
    gpu::ConstantRing ring_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer_, indices_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_, stencilShader_, coverShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_, paintShader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_, pointLayout_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blends_[2], noColor_;
    // content: inside the masks; count: a fill's count; cover: where it's odd, back to the depth; maskQuad: a quad's rectangle onto
    // the depth; pop: a mask's pieces off it
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> content_, count_, cover_, maskQuad_, pop_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_, scissor_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_, wrap_;
};

}
