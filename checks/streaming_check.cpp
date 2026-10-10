// Checks for the asset and GPU streaming parts: format sizes, a pack set pinned to one build, immutable textures from bytes read back
// mip by mip, and the texture streamer's policy and budget.
#include "engine/assets/pack_set.h"
#include "engine/core/arena.h"
#include "engine/core/fatal.h"
#include "engine/gpu/format.h"
#include "engine/gpu/texture_streamer.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

#define CHECK(c)                                                                                                                   \
    do {                                                                                                                           \
        if (!(c))                                                                                                                  \
            engine::fatal("engine_check: %s failed (line %d)", #c, __LINE__);                                                     \
    } while (0)

using namespace engine;

static void formats() {
    using namespace gpu;
    CHECK(blockCompressed(DXGI_FORMAT_BC1_UNORM_SRGB) && blockCompressed(DXGI_FORMAT_BC7_UNORM) && !blockCompressed(DXGI_FORMAT_R8_UNORM));
    CHECK(rowBytes(DXGI_FORMAT_BC1_UNORM, 1) == 8 && rowBytes(DXGI_FORMAT_BC1_UNORM, 5) == 16 && rowCount(DXGI_FORMAT_BC3_UNORM, 2) == 1);
    CHECK(mipBytes(DXGI_FORMAT_BC3_UNORM, 256, 256) == 65536 && mipBytes(DXGI_FORMAT_BC5_UNORM, 1, 1) == 16);
    CHECK(mipBytes(DXGI_FORMAT_B8G8R8A8_UNORM, 3, 5) == 60 && mipBytes(DXGI_FORMAT_R8G8_SNORM, 3, 5) == 30);
    CHECK(rowBytes(DXGI_FORMAT_UNKNOWN, 16) == 0);
    std::printf("formats: ok\n");
}

// A one-asset pack as the 4.1 spec lays it out
static std::filesystem::path writePack(const char* name, const char* build, uint64_t id, uint32_t value) {
    std::vector<char> file(2 * assets::kPackAlign, 0);
    assets::PackHeader h{assets::kPackMagic, assets::kPackVersion, {}, 1, 0, file.size(), assets::kPackAlign};
    std::memcpy(h.build, build, std::strlen(build));
    const assets::PackEntry e{assets::kPackAlign, sizeof value, 7};
    std::memcpy(file.data(), &h, sizeof h);
    std::memcpy(file.data() + sizeof h, &id, sizeof id);
    std::memcpy(file.data() + sizeof h + sizeof id, &e, sizeof e);
    std::memcpy(file.data() + assets::kPackAlign, &value, sizeof value);
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path, std::ios::binary).write(file.data(), file.size());
    return path;
}

static void packSet() {
    const auto a = writePack("engine_check_a.pack", "build-1", 10, 0xaaaa), b = writePack("engine_check_b.pack", "build-1", 20, 0xbbbb),
               c = writePack("engine_check_c.pack", "build-2", 30, 0xcccc);
    {
        assets::PackSet set;
        const char* why = nullptr;
        CHECK(set.open(a.c_str()) && set.open(b.c_str()) && !std::strcmp(set.build(), "build-1"));
        CHECK(!set.open(c.c_str(), &why) && why && set.count() == 2);  // another cook: refused
        const assets::Blob x = set.find(20);
        CHECK(x.data && x.type == 7 && *x.at<uint32_t>(0) == 0xbbbb && *set.find(10).at<uint32_t>(0) == 0xaaaa && !set.find(30).data);
    }
    for (const auto& p : {a, b, c})
        std::filesystem::remove(p);
    std::printf("pack set: ok\n");
}

static void streamer(gpu::Device& device) {
    using gpu::TextureStreamer;
    // two 256x256 BC1 chains of random blocks, 9 mips each
    std::mt19937 rng(7);
    std::vector<uint8_t> bytes[2];
    gpu::SourceMip mips[2][9];
    for (int t = 0; t < 2; t++) {
        size_t total = 0;
        for (int i = 0; i < 9; i++)
            total += gpu::mipBytes(DXGI_FORMAT_BC1_UNORM, 256 >> i, 256 >> i);
        bytes[t].resize(total);
        for (auto& v : bytes[t])
            v = uint8_t(rng());
        size_t at = 0;
        for (int i = 0; i < 9; i++) {
            const uint32_t size = uint32_t(gpu::mipBytes(DXGI_FORMAT_BC1_UNORM, 256 >> i, 256 >> i));
            mips[t][i] = {bytes[t].data() + at, size, 256u >> i, 256u >> i};
            at += size;
        }
    }
    auto equalsSource = [&](TextureStreamer& s, uint32_t t, int top) {
        for (int i = top; i < 9; i++) {
            const auto back = gpu::readBackMip(device, s.texture(t), i - top);
            if (back.size() != mips[t][i].size || std::memcmp(back.data(), mips[t][i].data, back.size()))
                return false;
        }
        return true;
    };

    // budget: one whole chain plus two low ones (sizes from a probe streamer)
    uint64_t full, low;
    {
        TextureStreamer probe(device, ~0ull, 1);
        CHECK(probe.add(DXGI_FORMAT_BC1_UNORM, mips[0], 9) == 0 && probe.residentTop(0) == 2);  // 64x64 and down at once
        full = probe.chainBytes(0, 0);
        low = probe.chainBytes(0, 2);
        CHECK(probe.stats().resident == low && low == 2048 + 512 + 128 + 32 + 8 + 8 + 8);
    }
    TextureStreamer s(device, full + 2 * low, 4);
    CHECK(s.add(DXGI_FORMAT_BC1_UNORM, mips[0], 9) == 0 && equalsSource(s, 0, 2));
    CHECK(s.request(0, 7) == TextureStreamer::Request::Invalid && s.request(0, 9) == TextureStreamer::Request::Invalid);  // 2x2 BC top
    CHECK(s.request(0, 2) == TextureStreamer::Request::Resident);
    CHECK(s.request(0, 0) == TextureStreamer::Request::Queued && s.request(0, 1) == TextureStreamer::Request::Pending);
    s.finish();
    CHECK(s.residentTop(0) == 0 && s.stats().resident == full && s.stats().inFlight == 0 && equalsSource(s, 0, 0));

    CHECK(s.add(DXGI_FORMAT_BC1_UNORM, mips[1], 9) == 1 && s.stats().resident == full + low);
    CHECK(s.request(1, 0) == TextureStreamer::Request::OverBudget && s.stats().refused == 1);
    const uint64_t before = allocationCount();
    CHECK(s.request(0, 2) == TextureStreamer::Request::Queued);  // shrink the first to make room
    s.finish();
    CHECK(s.residentTop(0) == 2 && s.stats().resident == 2 * low);
    CHECK(s.request(1, 0) == TextureStreamer::Request::Queued);
    for (int spin = 0; s.residentTop(1) != 0; spin++) {  // as a frame loop sees it: update until it lands
        CHECK(spin < 100000);
        s.update();
    }
    CHECK(allocationCount() == before);  // requests, the worker and update allocate nothing
    CHECK(s.stats().resident == low + full && equalsSource(s, 1, 0) && equalsSource(s, 0, 2));
    CHECK(s.stats().peak <= s.stats().budget && s.stats().streamed == 3 && s.stats().requests == 4 && s.stats().failed == 0);
    std::printf("texture streamer: ok (peak %llu of %llu bytes, %.3f ms a stream)\n", static_cast<unsigned long long>(s.stats().peak),
                static_cast<unsigned long long>(s.stats().budget), s.stats().streamSeconds * 1e3 / s.stats().streamed);

    // a cube: six faces of 128x128 BC1, 8 mips each, from slices of the random bytes above; 64x64 and down at once, then all
    gpu::SourceMip faces[6 * 8];
    for (int f = 0; f < 6; f++)
        for (int i = 0; i < 8; i++)
            faces[f * 8 + i] = {mips[f & 1][i + 1].data, mips[f & 1][i + 1].size, 128u >> i, 128u >> i};
    TextureStreamer cube(device, ~0ull, 1);
    CHECK(cube.add(DXGI_FORMAT_BC1_UNORM, faces, 8, 6) == 0 && cube.residentTop(0) == 1);
    CHECK(cube.stats().resident == cube.chainBytes(0, 1) && cube.chainBytes(0, 1) == 6 * low);
    CHECK(cube.request(0, 0) == TextureStreamer::Request::Queued);
    cube.finish();
    bool same = cube.residentTop(0) == 0;
    for (int f = 0; same && f < 6; f++)
        for (int i = 0; same && i < 8; i++) {
            const auto back = gpu::readBackMip(device, cube.texture(0), i, f);
            same = back.size() == faces[f * 8 + i].size && !std::memcmp(back.data(), faces[f * 8 + i].data, back.size());
        }
    CHECK(same);
    std::printf("texture streamer cube: ok\n");

    // reserved textures take nothing until loaded or requested
    TextureStreamer lazy(device, ~0ull, 2);
    CHECK(lazy.reserve(DXGI_FORMAT_BC1_UNORM, mips[0], 9) == 0 && lazy.reserve(DXGI_FORMAT_BC1_UNORM, mips[1], 9) == 1);
    CHECK(!lazy.loaded(0) && !lazy.view(0) && lazy.stats().resident == 0 && lazy.stats().textures == 2);
    CHECK(lazy.load(0) && lazy.residentTop(0) == 2 && lazy.stats().resident == low && equalsSource(lazy, 0, 2));
    CHECK(lazy.request(1, 0) == TextureStreamer::Request::Queued);
    lazy.finish();
    CHECK(lazy.residentTop(1) == 0 && lazy.stats().resident == low + full && equalsSource(lazy, 1, 0));
    std::printf("texture streamer reserve: ok\n");
}

void streamingChecks(gpu::Device& device) {
    formats();
    packSet();
    streamer(device);
}
