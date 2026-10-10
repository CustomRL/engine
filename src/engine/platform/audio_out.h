#pragma once

struct SDL_AudioStream;

namespace engine {

// The default playback device through SDL3, at the device's own rate and in float stereo. SDL's audio thread pulls
// samples through render, which must not block or allocate; nothing here allocates once open returns.
class AudioOut {
public:
    using Render = void (*)(void* user, float* out, int frames);  // out: frames of interleaved stereo

    AudioOut() = default;
    ~AudioOut() { close(); }
    AudioOut(const AudioOut&) = delete;
    AudioOut& operator=(const AudioOut&) = delete;

    // False (with SDL's message in error, when given) when there is no playback device
    bool open(Render render, void* user, const char** error = nullptr);
    void close();
    int rate() const { return rate_; }        // frames a second, the device's
    int deviceFrames() const { return deviceFrames_; }  // the device's buffer, frames
    // The default playback device's rate without opening it (for rendering offline at it), 0 without a device
    static int defaultRate();

private:
    static constexpr int kChunk = 1024;  // frames rendered per call into render
    static void feed(void* self, SDL_AudioStream* stream, int additional, int total);

    SDL_AudioStream* stream_ = nullptr;
    Render render_ = nullptr;
    void* user_ = nullptr;
    int rate_ = 0, deviceFrames_ = 0;
    bool subsystem_ = false;
    float chunk_[kChunk * 2];
};

}
