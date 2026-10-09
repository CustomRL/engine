#pragma once

#include "engine/gpu/device.h"

#include <array>

namespace engine::gpu {

// Per-pass GPU timing. mark(name) stamps the GPU timeline and names the pass that ended there, so a pass is the time since the previous
// stamp. Results come back through D3D11 timestamp queries without waiting, a few frames late. Pass names are string literals (compared
// by pointer, then by text); nothing allocates after construction. Every call site may hold a null profiler (the static mark).
class GpuProfiler {
public:
    static constexpr int kStamps = 40;  // per frame, so at most 39 passes
    struct Pass {
        const char* name;
        double ms;    // summed over the frames it ran in
        long frames;
    };

    explicit GpuProfiler(Device& device);

    // before the frame's first pass, and after its last
    void begin();
    void end();
    void mark(const char* pass);
    static void mark(GpuProfiler* profiler, const char* pass) {
        if (profiler)
            profiler->mark(pass);
    }

    // Reads every finished frame; then passes()/frames() are current
    void collect();
    const Pass* passes() const { return passes_.data(); }
    int passCount() const { return passCount_; }
    long frames() const { return frames_; }
    double totalMs() const { return totalMs_; }
    // The mean per pass (over the frames it ran in) and per frame, to stdout
    void print() const;

private:
    struct Set {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
        std::array<Microsoft::WRL::ComPtr<ID3D11Query>, kStamps> stamps;
        std::array<const char*, kStamps - 1> passes;  // passes[i] ended at stamps[i + 1]
        int count = 0;
        bool waiting = false;
    };

    Device& device_;
    std::array<Set, 6> sets_;
    int next_ = 0;
    bool open_ = false;
    int skip_ = 30;  // finished frames left out first: shader and resource warm-up
    std::array<Pass, kStamps - 1> passes_{};  // in first-seen order
    int passCount_ = 0;
    long frames_ = 0;
    double totalMs_ = 0;
};

}
