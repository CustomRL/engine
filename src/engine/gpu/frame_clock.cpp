#include "engine/gpu/frame_clock.h"

#include "engine/core/fatal.h"
#include "engine/platform/clock.h"

namespace engine::gpu {

FrameClock::FrameClock(Device& device) : device_(device) {
    for (Timer& t : timers_) {
        const D3D11_QUERY_DESC disjoint{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, stamp{D3D11_QUERY_TIMESTAMP, 0};
        if (FAILED(device.d3d()->CreateQuery(&disjoint, &t.disjoint)) || FAILED(device.d3d()->CreateQuery(&stamp, &t.begin)) ||
            FAILED(device.d3d()->CreateQuery(&stamp, &t.end)))
            fatal("CreateQuery (frame clock) failed");
    }
}

void FrameClock::frameStart(double now) {
    if (lastFrame_ >= 0) {
        times_.frameMs = static_cast<float>((now - lastFrame_) * 1000);
        if (times_.frameMs > 0)  // two frames on one clock tick would divide by zero
            times_.fps = 1000 / times_.frameMs;
    }
    lastFrame_ = frameStart_ = now;
    readGpu();
}

void FrameClock::drawStart() {
    drawStart_ = seconds();
    times_.gameMs = static_cast<float>((drawStart_ - frameStart_) * 1000);
    Timer& t = timers_[next_];
    if (t.waiting)
        return;
    device_.context()->Begin(t.disjoint.Get());
    device_.context()->End(t.begin.Get());
}

void FrameClock::drawEnd() {
    times_.renderMs = static_cast<float>((seconds() - drawStart_) * 1000);
    Timer& t = timers_[next_];
    if (t.waiting)
        return;
    device_.context()->End(t.end.Get());
    device_.context()->End(t.disjoint.Get());
    t.waiting = true;
    next_ = (next_ + 1) % static_cast<int>(timers_.size());
}

// the timers the GPU has finished, oldest first, so the newest result stays
void FrameClock::readGpu() {
    for (size_t k = 0; k < timers_.size(); k++) {
        Timer& t = timers_[(next_ + k) % timers_.size()];
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
        UINT64 begin, end;
        auto* context = device_.context();
        if (!t.waiting || context->GetData(t.disjoint.Get(), &disjoint, sizeof disjoint, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            context->GetData(t.begin.Get(), &begin, sizeof begin, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            context->GetData(t.end.Get(), &end, sizeof end, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            continue;
        t.waiting = false;
        if (!disjoint.Disjoint) {
            times_.gpuMs = static_cast<float>(static_cast<double>(end - begin) * 1000 / static_cast<double>(disjoint.Frequency));
            times_.gpuFrames++;
        }
    }
}

}
