#include "engine/gpu/gpu_profiler.h"

#include "engine/core/fatal.h"

#include <cstdio>
#include <cstring>

namespace engine::gpu {

GpuProfiler::GpuProfiler(Device& device) : device_(device) {
    const D3D11_QUERY_DESC disjoint{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, stamp{D3D11_QUERY_TIMESTAMP, 0};
    for (Set& s : sets_) {
        if (FAILED(device.d3d()->CreateQuery(&disjoint, &s.disjoint)))
            fatal("CreateQuery (gpu profiler) failed");
        for (auto& q : s.stamps)
            if (FAILED(device.d3d()->CreateQuery(&stamp, &q)))
                fatal("CreateQuery (gpu profiler) failed");
    }
}

void GpuProfiler::print() const {
    if (!frames_)
        return;
    std::printf("gpu passes, mean ms over %ld frames (a pass is averaged over the frames it ran in)\n", frames_);
    for (int i = 0; i < passCount_; i++)
        std::printf("  %-22s %7.3f  (%ld)\n", passes_[i].name, passes_[i].ms / static_cast<double>(passes_[i].frames), passes_[i].frames);
    std::printf("  %-22s %7.3f\n", "total", totalMs_ / static_cast<double>(frames_));
}

void GpuProfiler::begin() {
    collect();
    Set& s = sets_[next_];
    open_ = !s.waiting;
    if (!open_)
        return;
    s.count = 0;
    device_.context()->Begin(s.disjoint.Get());
    device_.context()->End(s.stamps[0].Get());
}

void GpuProfiler::mark(const char* pass) {
    Set& s = sets_[next_];
    if (!open_ || s.count + 1 >= kStamps)
        return;
    device_.context()->End(s.stamps[s.count + 1].Get());
    s.passes[s.count++] = pass;
}

void GpuProfiler::end() {
    if (!open_)
        return;
    Set& s = sets_[next_];
    device_.context()->End(s.disjoint.Get());
    s.waiting = true;
    next_ = (next_ + 1) % static_cast<int>(sets_.size());
    open_ = false;
}

// every set the GPU has finished, oldest first
void GpuProfiler::collect() {
    auto* context = device_.context();
    for (size_t k = 0; k < sets_.size(); k++) {
        Set& s = sets_[(next_ + k) % sets_.size()];
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
        if (!s.waiting || context->GetData(s.disjoint.Get(), &disjoint, sizeof disjoint, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            continue;
        s.waiting = false;
        if (disjoint.Disjoint || skip_ > 0) {
            skip_ -= !disjoint.Disjoint;
            continue;
        }
        UINT64 previous = 0, now = 0;
        double total = 0;
        bool ok = context->GetData(s.stamps[0].Get(), &previous, sizeof previous, 0) == S_OK;
        for (int i = 0; ok && i < s.count; i++) {
            ok = context->GetData(s.stamps[i + 1].Get(), &now, sizeof now, 0) == S_OK;
            if (!ok)
                break;
            const double ms = static_cast<double>(now - previous) * 1000 / static_cast<double>(disjoint.Frequency);
            previous = now;
            total += ms;
            int p = 0;
            while (p < passCount_ && passes_[p].name != s.passes[i] && std::strcmp(passes_[p].name, s.passes[i]))
                p++;
            if (p == passCount_) {
                if (passCount_ == static_cast<int>(passes_.size()))
                    continue;  // more distinct names than one frame can hold: the extra ones go untimed
                passes_[passCount_++] = {s.passes[i], 0, 0};
            }
            passes_[p].ms += ms;
            passes_[p].frames++;
        }
        frames_++;
        totalMs_ += total;
    }
}

}
