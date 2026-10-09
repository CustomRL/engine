// Checks for the engine's core and GPU utilities: arena, pool, allocation counter, logging and asserts, PNG writer, constant ring, GPU profiler, frame
// clock. Exits 0 when every check passes; a failure names its line and exits 1. Needs a D3D11 GPU (no window).
#include "engine/core/arena.h"
#include "engine/core/fatal.h"
#include "engine/core/png_write.h"
#include "engine/gpu/constant_ring.h"
#include "engine/gpu/frame_clock.h"
#include "engine/gpu/gpu_profiler.h"
#include "engine/platform/clock.h"

#include <stb_image.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <thread>
#include <vector>

#define CHECK(c)                                                                                                                   \
    do {                                                                                                                           \
        if (!(c))                                                                                                                  \
            engine::fatal("engine_check: %s failed (line %d)", #c, __LINE__);                                                     \
    } while (0)

using namespace engine;

[[noreturn]] void assertChild(const char* logPath);
void logAndAssertChecks(const char* self);

static void arenaAndPool() {
    Arena arena(1 << 20);
    CHECK(arena.used() == 0 && arena.committed() == 0);
    char* a = static_cast<char*>(arena.alloc(1, 1));
    double* d = arena.make<double>(3);
    CHECK(reinterpret_cast<uintptr_t>(d) % alignof(double) == 0 && reinterpret_cast<char*>(d) > a);
    void* page = arena.alloc(10, 4096);
    CHECK(reinterpret_cast<uintptr_t>(page) % 4096 == 0);
    const size_t mark = arena.mark();
    arena.alloc(200000);  // past the first commit step
    CHECK(arena.committed() >= arena.used() && arena.used() >= 200000);
    std::memset(arena.alloc(1000), 0xab, 1000);  // committed pages are writable
    arena.reset(mark);
    CHECK(arena.alloc(10, 1) == static_cast<char*>(page) + 10);
    arena.reset();
    CHECK(arena.alloc(1, 1) == a);

    // a steady frame on a warm arena costs no new commits
    size_t committed = 0;
    for (int frame = 0; frame < 100; frame++) {
        arena.reset();
        for (int i = 0; i < 50; i++)
            arena.alloc(4000);
        if (frame == 0)
            committed = arena.committed();
    }
    CHECK(arena.committed() == committed);

    struct Item {
        double x, y;
    };
    static Pool<Item, 64> pool;
    Item* items[64];
    for (auto& it : items)
        CHECK((it = pool.alloc()) != nullptr);
    CHECK(pool.alloc() == nullptr && pool.used() == 64);
    pool.free(items[10]);
    pool.free(items[3]);
    CHECK(pool.alloc() == items[3] && pool.alloc() == items[10] && pool.alloc() == nullptr);
    for (auto* it : items)
        pool.free(it);
    CHECK(pool.used() == 0);
    std::printf("arena, pool: ok\n");
}

static void allocationCounter() {
#ifdef ENGINE_COUNT_ALLOCS
    const uint64_t before = allocationCount();
    int* one = new int(1);
    int* many = new int[3];
    int* quiet = new (std::nothrow) int(2);
    struct alignas(64) Wide {
        float v[16];
    };
    Wide* wide = new Wide;
    const uint64_t counted = allocationCount() - before;
    delete one;
    delete[] many;
    delete quiet;
    delete wide;
    CHECK(counted == 4);
    Arena arena(1 << 16);
    const uint64_t arenaBefore = allocationCount();
    arena.alloc(100);
    CHECK(allocationCount() == arenaBefore);
    std::printf("allocation counter: ok (new, new[], nothrow and aligned new counted; arena not)\n");
#else
    CHECK(allocationCount() == 0);
    std::printf("allocation counter: off in this build\n");
#endif
}

// A bitwise CRC-32 and Adler-32, independent of the writer's table, since stb_image doesn't verify either
static uint32_t crcBitwise(const uint8_t* p, size_t n) {
    uint32_t c = 0xffffffff;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

static uint32_t be(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

static void png() {
    const auto dir = std::filesystem::temp_directory_path();
    std::mt19937 rng(1);
    for (int size : {1, 3, 300}) {  // 300 x 300 x 4 needs several 65535-byte stored blocks
        std::vector<uint8_t> rgba(size_t(size) * size * 4);
        for (auto& v : rgba)
            v = uint8_t(rng());
        const auto path = dir / "engine_check.png";
        CHECK(writePng(path, size, size, rgba.data()));
        int w, h, channels;
        uint8_t* back = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
        CHECK(back && w == size && h == size && channels == 4);
        CHECK(!std::memcmp(back, rgba.data(), rgba.size()));
        stbi_image_free(back);

        std::ifstream in(path, std::ios::binary);
        const std::vector<uint8_t> file{std::istreambuf_iterator<char>(in), {}};
        in.close();
        std::vector<uint8_t> raw;
        for (size_t at = 8; at + 12 <= file.size();) {
            const uint32_t len = be(&file[at]);
            CHECK(at + 12 + len <= file.size());
            CHECK(crcBitwise(&file[at + 4], len + 4) == be(&file[at + 8 + len]));
            if (!std::memcmp(&file[at + 4], "IDAT", 4)) {  // stored blocks: 5-byte headers between the data
                const uint8_t* z = &file[at + 8];
                size_t p = 2;
                for (bool last = false; !last;) {
                    last = z[p] & 1;
                    const size_t n = z[p + 1] | z[p + 2] << 8;
                    raw.insert(raw.end(), z + p + 5, z + p + 5 + n);
                    p += 5 + n;
                }
                uint32_t a = 1, b = 0;
                for (uint8_t v : raw) {
                    a = (a + v) % 65521;
                    b = (b + a) % 65521;
                }
                CHECK(((b << 16) | a) == be(z + p));
            }
            at += 12 + len;
        }
        CHECK(raw.size() == size_t(size) * (size * 4 + 1));
        std::filesystem::remove(path);
    }
    std::printf("png: ok (stb_image decodes it byte for byte; every chunk CRC and the Adler-32 verify)\n");
}

static void constantRing(gpu::Device& device) {
    gpu::ConstantRing ring(device);
    std::vector<float> a(4 * 3), b(4 * 20);
    for (size_t i = 0; i < a.size(); i++)
        a[i] = float(i + 1);
    for (size_t i = 0; i < b.size(); i++)
        b[i] = -float(i + 1);
    for (int frame = 0; frame < 3; frame++) {  // the second and third frames reuse the staging and the buffer
        ring.reset();
        const uint32_t oa = ring.push(a.data(), a.size(), 3);
        const uint32_t ob = ring.push(b.data(), b.size(), 20);
        const uint32_t oc = ring.push(a.data(), 2, 1);  // fewer floats than its float4s: the rest is zero
        CHECK(oa == 0 && ob == 16 && oc == 48);
        ring.upload();
        ring.bind(gpu::ConstantRing::Stage::Vertex, 0, ob, 20);
        ring.bind(gpu::ConstantRing::Stage::Pixel, 1, oc, 1);
        if (!ring.offsetting())
            continue;
        D3D11_BUFFER_DESC desc;
        ring.buffer()->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
        CHECK(SUCCEEDED(device.d3d()->CreateBuffer(&desc, nullptr, &staging)));
        device.context()->CopyResource(staging.Get(), ring.buffer());
        D3D11_MAPPED_SUBRESOURCE mapped;
        CHECK(SUCCEEDED(device.context()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        const float* f = static_cast<const float*>(mapped.pData);
        CHECK(!std::memcmp(f, a.data(), a.size() * 4) && f[a.size()] == 0 && f[16 * 4 - 1] == 0);
        CHECK(!std::memcmp(f + 16 * 4, b.data(), b.size() * 4) && f[16 * 4 + b.size()] == 0);
        CHECK(f[48 * 4] == 1 && f[48 * 4 + 1] == 2 && f[48 * 4 + 2] == 0 && f[64 * 4 - 1] == 0);
        device.context()->Unmap(staging.Get(), 0);
    }
    std::printf("constant ring: ok (%s path; offsets on 16-float4 slices, zero padding, contents read back)\n",
                ring.offsetting() ? "offsetting" : "fallback");
}

// Frames of real GPU work: copies between two 32 MB textures (clears can be metadata-only and take no GPU time)
static void gpuTimers(gpu::Device& device) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 2048;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> a, b;
    CHECK(SUCCEEDED(device.d3d()->CreateTexture2D(&desc, nullptr, &a)) && SUCCEEDED(device.d3d()->CreateTexture2D(&desc, nullptr, &b)));

    gpu::GpuProfiler profiler(device);
    gpu::FrameClock clock(device);
    clock.frameStart(5.0);
    clock.frameStart(5.0);  // two frames on one clock tick: no division by zero
    CHECK(clock.times().frameMs == 0 && std::isfinite(clock.times().fps));
    const int frames = 60;
    for (int frame = 0; frame < frames; frame++) {
        clock.frameStart(seconds());
        clock.drawStart();
        profiler.begin();
        for (int i = 0; i < 4; i++)
            device.context()->CopyResource(i % 2 ? a.Get() : b.Get(), i % 2 ? b.Get() : a.Get());
        profiler.mark("copy a");
        device.context()->CopyResource(a.Get(), b.Get());
        profiler.mark("copy b");
        profiler.end();
        clock.drawEnd();
        device.context()->Flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    for (int i = 0; i < 50 && profiler.frames() < frames - 30; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        profiler.collect();
    }
    clock.frameStart(seconds());
    CHECK(profiler.passCount() == 2 && !std::strcmp(profiler.passes()[0].name, "copy a") && !std::strcmp(profiler.passes()[1].name, "copy b"));
    CHECK(profiler.frames() > 0 && profiler.frames() <= frames - 30);  // the first 30 finished frames are skipped
    CHECK(profiler.passes()[0].ms > 0 && profiler.totalMs() >= profiler.passes()[0].ms);
    CHECK(clock.times().gpuFrames > 0 && clock.times().gpuMs > 0 && std::isfinite(clock.times().fps) && clock.times().fps > 0);
    profiler.print();
    std::printf("gpu profiler, frame clock: ok (%ld profiled frames, last gpu frame %.3f ms)\n", profiler.frames(), clock.times().gpuMs);
}

int main(int argc, char** argv) {
    if (argc == 3 && !std::strcmp(argv[1], "--assert-fail"))
        assertChild(argv[2]);
    arenaAndPool();
    allocationCounter();
    logAndAssertChecks(argv[0]);
    png();
    gpu::Device device;
    constantRing(device);
    gpuTimers(device);
    std::printf("engine_check: ok\n");
    return 0;
}
