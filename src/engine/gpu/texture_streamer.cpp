#include "engine/gpu/texture_streamer.h"

#include "engine/core/fatal.h"
#include "engine/gpu/format.h"
#include "engine/platform/clock.h"

#include <algorithm>

namespace engine::gpu {

TextureStreamer::TextureStreamer(Device& device, uint64_t budget, uint32_t capacity, uint32_t loadSize)
    : device_(device), capacity_(capacity), loadSize_(loadSize), slots_(new Slot[capacity]), queue_(new uint32_t[capacity]),
      done_(new uint32_t[capacity]) {
    stats_.budget = budget;
    worker_ = std::thread([this] { work(); });
}

TextureStreamer::~TextureStreamer() {
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
    }
    wake_.notify_one();
    worker_.join();
}

uint64_t TextureStreamer::chainBytes(uint32_t t, int top) const {
    const Slot& s = slots_[t];
    uint64_t bytes = 0;
    for (int i = top; i < s.count; i++)
        bytes += mipBytes(s.format, s.mips[i].width, s.mips[i].height);
    return bytes * s.faces;
}

bool TextureStreamer::validTop(uint32_t t, int top) const {
    const Slot& s = slots_[t];
    return top >= 0 && top < s.count && (!blockCompressed(s.format) || (s.mips[top].width % 4 == 0 && s.mips[top].height % 4 == 0));
}

Texture TextureStreamer::make(const Slot& s, int top) const {
    MipLevel levels[6 * D3D11_REQ_MIP_LEVELS];
    const int n = s.count - top;
    for (int f = 0; f < s.faces; f++)
        for (int i = top; i < s.count; i++) {
            const SourceMip& m = s.mips[f * s.count + i];
            levels[f * n + i - top] = {m.data, rowBytes(s.format, m.width)};
        }
    return createTexture(device_, s.mips[top].width, s.mips[top].height, s.format, levels, n, s.faces);
}

uint32_t TextureStreamer::add(DXGI_FORMAT format, const SourceMip* mips, int count, int faces) {
    const uint32_t t = reserve(format, mips, count, faces);
    if (t == ~0u || load(t))
        return t;
    stats_.textures--;  // the device refused it: the slot is free again
    return ~0u;
}

uint32_t TextureStreamer::reserve(DXGI_FORMAT format, const SourceMip* mips, int count, int faces) {
    if (stats_.textures == capacity_)
        fatal("TextureStreamer: more than %u textures", capacity_);
    if (count < 1 || count > D3D11_REQ_MIP_LEVELS || (faces != 1 && faces != 6))
        return ~0u;
    const uint32_t t = stats_.textures;
    Slot& s = slots_[t];
    s.format = format;
    s.count = count;
    s.faces = faces;
    std::copy(mips, mips + count * faces, s.mips);
    for (int i = 0; i < count * faces; i++)
        if (rowBytes(format, mips[i].width) == 0 || mips[i].size != mipBytes(format, mips[i].width, mips[i].height) ||
            mips[i].width != mips[i % count].width || mips[i].height != mips[i % count].height)
            return ~0u;
    s.current = {};
    s.top = count;  // nothing resident
    s.bytes = 0;
    s.pending = false;
    stats_.textures++;
    return t;
}

bool TextureStreamer::load(uint32_t t) {
    Slot& s = slots_[t];
    if (s.current.texture || s.pending)
        return true;
    int top = s.count - 1;
    while (top > 0 && std::max(s.mips[top - 1].width, s.mips[top - 1].height) <= loadSize_)
        top--;
    while (top > 0 && !validTop(t, top))
        top--;
    s.current = make(s, top);
    if (!s.current.texture)
        return false;
    s.top = top;
    s.bytes = chainBytes(t, top);
    stats_.resident += s.bytes;
    stats_.peak = std::max(stats_.peak, stats_.resident + stats_.inFlight);
    return true;
}

TextureStreamer::Request TextureStreamer::request(uint32_t t, int top) {
    Slot& s = slots_[t];
    if (!validTop(t, top))
        return Request::Invalid;
    if (s.pending)
        return Request::Pending;
    if (top == s.top)
        return Request::Resident;
    stats_.requests++;
    const uint64_t need = chainBytes(t, top);
    if (stats_.resident + stats_.inFlight + need > stats_.budget) {
        stats_.refused++;
        return Request::OverBudget;
    }
    s.nextTop = top;
    s.nextBytes = need;
    s.pending = true;
    s.requestedAt = seconds();
    stats_.inFlight += need;
    stats_.peak = std::max(stats_.peak, stats_.resident + stats_.inFlight);
    outstanding_++;
    {
        std::lock_guard lock(mutex_);
        queue_[queueTail_++ % capacity_] = t;
    }
    wake_.notify_one();
    return Request::Queued;
}

void TextureStreamer::work() {
    for (;;) {
        uint32_t t;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return quit_ || queueHead_ != queueTail_; });
            if (quit_)
                return;
            t = queue_[queueHead_++ % capacity_];
        }
        Slot& s = slots_[t];
        Texture made = make(s, s.nextTop);  // the main thread leaves a pending slot alone until update() takes it back
        {
            std::lock_guard lock(mutex_);
            s.next = std::move(made);
            done_[doneTail_++ % capacity_] = t;
        }
        idle_.notify_one();
    }
}

void TextureStreamer::update() {
    std::lock_guard lock(mutex_);
    const double now = seconds();
    for (; doneHead_ != doneTail_; doneHead_++) {
        Slot& s = slots_[done_[doneHead_ % capacity_]];
        stats_.inFlight -= s.nextBytes;
        s.pending = false;
        outstanding_--;
        if (!s.next.texture) {
            stats_.failed++;
            continue;
        }
        s.current = std::move(s.next);  // releases the old chain
        stats_.resident += s.nextBytes;
        stats_.resident -= s.bytes;
        s.bytes = s.nextBytes;
        s.top = s.nextTop;
        stats_.streamed++;
        stats_.streamSeconds += now - s.requestedAt;
    }
}

void TextureStreamer::finish() {
    {
        std::unique_lock lock(mutex_);
        idle_.wait(lock, [this] { return doneTail_ - doneHead_ == outstanding_; });
    }
    update();
}

}
