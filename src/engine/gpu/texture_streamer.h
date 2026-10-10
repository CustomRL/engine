#pragma once

#include "engine/gpu/texture.h"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace engine::gpu {

// One mip as its source holds it (a mapped pack blob, say), rows of texels or of 4x4 blocks packed tightly; for a cube, of one face
struct SourceMip {
    const void* data;
    uint32_t size, width, height;
};

// Immutable textures made straight from source bytes, with mip streaming under a budget of GPU bytes. The policy:
// - add loads at once the mips from the first one no larger than loadSize on its longer side down to the smallest (moved up while a
//   block-compressed top isn't a multiple of 4, which D3D11 refuses)
// - request(t, top) asks for mips [top, count). A worker thread reads the source bytes (page faults on a mapped file are the async read)
//   and creates the new immutable texture; update() swaps it in and releases the old one. Shrinking works the same way.
// - the budget: a request is refused when the bytes resident plus those in flight plus the request's whole chain would pass it. Old and
//   new chains both count while the new one is in flight, so the textures never hold more than the budget. Nothing is evicted on its
//   own: the caller frees memory by requesting fewer mips.
// - bytes are the mips' sizes as D3D11 lays them out; the driver's padding isn't seen here (Device::videoMemoryUsage measures it)
// Every call but the worker's is the main thread's. No allocation after construction outside the driver.
class TextureStreamer {
public:
    TextureStreamer(Device& device, uint64_t budget, uint32_t capacity, uint32_t loadSize = 64);
    ~TextureStreamer();
    TextureStreamer(const TextureStreamer&) = delete;
    TextureStreamer& operator=(const TextureStreamer&) = delete;

    // A texture from its chain, top first, every mip with data (at most 15), its low mips loaded now. Returns its handle, or ~0u when
    // the device refuses the chain; fatal past capacity. faces 6 makes a cube (+X -X +Y -Y +Z -Z): mips then holds count mips of each
    // face in turn, every face's chain the same sizes, and a request moves the top of all six.
    uint32_t add(DXGI_FORMAT format, const SourceMip* mips, int count, int faces = 1);

    enum class Request { Queued, Resident, Pending, OverBudget, Invalid };
    Request request(uint32_t t, int top);
    void update();  // once a frame
    void finish();  // waits for every request in flight, then update(); for loading and checks

    ID3D11ShaderResourceView* view(uint32_t t) const { return slots_[t].current.view.Get(); }
    ID3D11Texture2D* texture(uint32_t t) const { return slots_[t].current.texture.Get(); }
    int residentTop(uint32_t t) const { return slots_[t].top; }
    int mipCount(uint32_t t) const { return slots_[t].count; }
    uint64_t chainBytes(uint32_t t, int top) const;
    bool validTop(uint32_t t, int top) const;

    struct Stats {
        uint64_t budget = 0, resident = 0, inFlight = 0, peak = 0;  // bytes; peak of resident + in flight
        uint32_t textures = 0, requests = 0, refused = 0, streamed = 0, failed = 0;
        double streamSeconds = 0;  // summed from request to swap
    };
    const Stats& stats() const { return stats_; }

private:
    struct Slot {
        DXGI_FORMAT format;
        int count, faces;
        SourceMip mips[6 * D3D11_REQ_MIP_LEVELS];  // face-major
        Texture current, next;
        int top, nextTop;
        uint64_t bytes, nextBytes;
        bool pending;
        double requestedAt;
    };

    Texture make(const Slot& s, int top) const;
    void work();

    Device& device_;
    uint32_t capacity_, loadSize_;
    std::unique_ptr<Slot[]> slots_;
    std::unique_ptr<uint32_t[]> queue_, done_;  // rings of capacity_: each texture has at most one request in flight
    uint32_t queueHead_ = 0, queueTail_ = 0, doneHead_ = 0, doneTail_ = 0, outstanding_ = 0;
    std::mutex mutex_;
    std::condition_variable wake_, idle_;
    bool quit_ = false;
    Stats stats_;
    std::thread worker_;
};

}
