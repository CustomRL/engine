#include "engine/platform/audio_out.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace engine {

bool AudioOut::open(Render render, void* user, const char** error) {
    close();
    auto fail = [&] {
        if (error)
            *error = SDL_GetError();
        close();
        return false;
    };
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return fail();
    subsystem_ = true;
    SDL_AudioSpec device{};
    if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device, &deviceFrames_))
        return fail();
    render_ = render;
    user_ = user;
    rate_ = device.freq;
    // the device's own rate, so SDL converts only the channel layout (stereo to whatever the device has)
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, device.freq};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, this);
    if (!stream_ || !SDL_ResumeAudioStreamDevice(stream_))
        return fail();
    return true;
}

int AudioOut::defaultRate() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return 0;
    SDL_AudioSpec device{};
    const int rate = SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device, nullptr) ? device.freq : 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return rate;
}

void AudioOut::close() {
    if (stream_)
        SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    if (subsystem_)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    subsystem_ = false;
}

void AudioOut::feed(void* self, SDL_AudioStream* stream, int additional, int) {
    auto& a = *static_cast<AudioOut*>(self);
    for (int frames = additional / int(2 * sizeof(float)); frames > 0;) {
        const int n = std::min(frames, kChunk);
        a.render_(a.user_, a.chunk_, n);
        SDL_PutAudioStreamData(stream, a.chunk_, n * int(2 * sizeof(float)));
        frames -= n;
    }
}

}
