// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's SDL3 audio output (no @orig): docs/research/sound.md#coneys-implementation.
#include "platform/sdl_audio_device.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
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

// The SDL event watch: passes device removals to the device. SDL calls it from whichever thread pushes the event, so
// it only records.
bool SDLCALL watchRemovals(void* userdata, SDL_Event* event) {
    if (event->type == SDL_EVENT_AUDIO_DEVICE_REMOVED && !event->adevice.recording) {
        static_cast<SdlAudioDevice*>(userdata)->noteRemoved(event->adevice.which);
    }
    return true;
}

} // namespace

std::expected<std::unique_ptr<SdlAudioDevice>, Error> SdlAudioDevice::open(audio::Mixer& mixer) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return sdlFailure("SDL_InitSubSystem(SDL_INIT_AUDIO)");
    }
    // The constructor is private, so make_unique cannot reach it. From here the destructor stops SDL's audio.
    std::unique_ptr<SdlAudioDevice> device(new SdlAudioDevice(mixer));
    if (auto opened = device->openStream(); !opened) {
        return std::unexpected(std::move(opened.error()));
    }
    device->m_watching = SDL_AddEventWatch(watchRemovals, device.get());
    return device;
}

SdlAudioDevice::~SdlAudioDevice() {
    if (m_watching) {
        SDL_RemoveEventWatch(watchRemovals, this);
    }
    closeStream();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

std::expected<void, Error> SdlAudioDevice::openStream() {
    const SDL_AudioSpec spec{.format = SDL_AUDIO_S16, .channels = audio::kOutputChannels, .freq = audio::kOutputRate};
    // The default device, not a named one: SDL then follows the system's default as it changes.
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feedStream, this);
    if (stream == nullptr) {
        return sdlFailure("SDL_OpenAudioDeviceStream");
    }
    m_stream = stream;
    const SDL_AudioDeviceID id = SDL_GetAudioStreamDevice(stream);
    m_lost.store(false, std::memory_order_release);
    m_deviceId.store(id, std::memory_order_release);
    const char* name = SDL_GetAudioDeviceName(id);
    m_name = name != nullptr ? name : "unknown device";
    // A device stream opens paused; from here SDL's thread calls feedStream.
    if (!SDL_ResumeAudioStreamDevice(stream)) {
        auto failure = sdlFailure("SDL_ResumeAudioStreamDevice");
        closeStream();
        return failure;
    }
    return {};
}

void SdlAudioDevice::closeStream() {
    if (m_stream != nullptr) {
        // Closes the device too; SDL waits for a callback in progress, so none runs after this.
        SDL_DestroyAudioStream(static_cast<SDL_AudioStream*>(m_stream));
        m_stream = nullptr;
    }
    m_deviceId.store(0, std::memory_order_release);
}

std::expected<void, Error> SdlAudioDevice::reopen() {
    // The old stream goes first: the mixer has one mix side, so two streams must never pull from it at once.
    closeStream();
    return openStream();
}

void SdlAudioDevice::noteRemoved(std::uint32_t deviceId) {
    const std::uint32_t ours = m_deviceId.load(std::memory_order_acquire);
    if (ours != 0 && deviceId == ours) {
        m_lost.store(true, std::memory_order_release);
    }
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
