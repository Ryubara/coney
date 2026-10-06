// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's SDL3 audio output (no @orig): docs/research/sound.md#coneys-implementation.
#include "platform/sdl_audio_device.h"

#include <algorithm>
#include <span>
#include <string>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

namespace coney::platform {

namespace {

// SDL's stream callback, on SDL's audio thread: hands the request to the device.
void SDLCALL feedStream(void* userdata, SDL_AudioStream* stream, int additionalBytes, int /*totalBytes*/) {
    static_cast<SdlAudioDevice*>(userdata)->feed(stream, additionalBytes);
}

// A PlatformFailure naming the SDL call that failed and SDL's reason.
std::unexpected<Error> sdlFailure(const char* call) {
    return fail(ErrorCode::PlatformFailure,
                std::string("could not start audio: ") + call + " failed: " + SDL_GetError());
}

} // namespace

std::expected<std::unique_ptr<SdlAudioDevice>, Error> SdlAudioDevice::open(audio::Mixer& mixer) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return sdlFailure("SDL_InitSubSystem(SDL_INIT_AUDIO)");
    }
    // The constructor is private, so make_unique cannot reach it.
    std::unique_ptr<SdlAudioDevice> device(new SdlAudioDevice(mixer));
    const SDL_AudioSpec spec{.format = SDL_AUDIO_S16, .channels = audio::kOutputChannels, .freq = audio::kOutputRate};
    SDL_AudioStream* stream =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feedStream, device.get());
    if (stream == nullptr) {
        return sdlFailure("SDL_OpenAudioDeviceStream"); // the destructor stops SDL's audio
    }
    device->m_stream = stream;
    const char* name = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(stream));
    device->m_name = name != nullptr ? name : "unknown device";
    // A device stream opens paused; from here SDL's thread calls feedStream.
    if (!SDL_ResumeAudioStreamDevice(stream)) {
        return sdlFailure("SDL_ResumeAudioStreamDevice"); // the destructor closes the stream and SDL's audio
    }
    return device;
}

SdlAudioDevice::~SdlAudioDevice() {
    if (m_stream != nullptr) {
        // Closes the device too; SDL waits for a callback in progress, so none runs after this.
        SDL_DestroyAudioStream(static_cast<SDL_AudioStream*>(m_stream));
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SdlAudioDevice::feed(void* stream, int bytes) {
    constexpr int kFrameBytes = static_cast<int>(sizeof(std::int16_t)) * audio::kOutputChannels;
    const std::size_t blockFrames = m_block.size() / audio::kOutputChannels;
    // In blocks of the fixed buffer until SDL has what it asked for (rounded up to a whole frame).
    for (int left = (bytes + kFrameBytes - 1) / kFrameBytes; left > 0;) {
        const std::size_t frames = std::min(blockFrames, static_cast<std::size_t>(left));
        const std::span<std::int16_t> block(m_block.data(), frames * audio::kOutputChannels);
        m_mixer.mix(block);
        SDL_PutAudioStreamData(static_cast<SDL_AudioStream*>(stream), block.data(),
                               static_cast<int>(block.size_bytes()));
        left -= static_cast<int>(frames);
    }
}

} // namespace coney::platform
