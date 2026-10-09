#pragma once

#include "engine/gpu/device.h"

#include <array>

namespace engine::gpu {

struct FrameTimes {
    float fps = 0;       // 1000 / frameMs; unchanged by a frame of zero length
    float frameMs = 0;   // loop top to loop top
    float gameMs = 0;    // loop top to drawStart
    float renderMs = 0;  // drawStart to drawEnd, on the CPU
    float gpuMs = 0;     // the GPU's time between drawStart and drawEnd, a few frames late
    long gpuFrames = 0;  // GPU timings read so far
};

// CPU and GPU frame timing for one thread that runs a frame's work, then submits its drawing. The GPU time comes from D3D11 timestamp
// queries read back without waiting.
class FrameClock {
public:
    explicit FrameClock(Device& device);

    // the loop's top, now in seconds (engine::seconds())
    void frameStart(double now);
    // the frame's update is done and its drawing begins
    void drawStart();
    // its drawing is done, before the present
    void drawEnd();
    const FrameTimes& times() const { return times_; }

private:
    void readGpu();

    struct Timer {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint, begin, end;
        bool waiting = false;
    };
    Device& device_;
    std::array<Timer, 4> timers_;  // in turn; one still waiting when its turn comes leaves that frame untimed
    int next_ = 0;
    double frameStart_ = 0, drawStart_ = 0, lastFrame_ = -1;
    FrameTimes times_;
};

}
