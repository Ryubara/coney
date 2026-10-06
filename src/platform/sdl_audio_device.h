// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include "audio/mixer.h"
#include "core/error.h"

// No SDL type appears in this header, so code that holds a device stays platform-neutral and never includes SDL.

namespace coney::platform {

/// The system's default playback device through SDL3's audio, pulling from a Mixer: signed 16-bit stereo at the
/// mixer's rate (SDL converts to whatever the device takes). SDL calls the mixer on its own audio thread whenever the
/// device needs more, so the mixer's mix side runs there; the game thread only sends it commands.
class SdlAudioDevice {
  public:
    /// Starts SDL's audio and opens the default playback device on `mixer`, which must outlive the device. Fails with
    /// ErrorCode::PlatformFailure, with SDL's reason, when there is no device or SDL refuses.
    [[nodiscard]] static std::expected<std::unique_ptr<SdlAudioDevice>, Error> open(audio::Mixer& mixer);

    /// Stops the device (no callback runs after this returns) and SDL's audio.
    ~SdlAudioDevice();
    SdlAudioDevice(const SdlAudioDevice&) = delete;
    SdlAudioDevice& operator=(const SdlAudioDevice&) = delete;
    SdlAudioDevice(SdlAudioDevice&&) = delete;
    SdlAudioDevice& operator=(SdlAudioDevice&&) = delete;

    /// The device's name as SDL gives it, for the log.
    [[nodiscard]] const std::string& name() const { return m_name; }

    /// SDL's callback: mixes `bytes` more bytes into the stream. Public only so the C callback can reach it.
    void feed(void* stream, int bytes);

  private:
    explicit SdlAudioDevice(audio::Mixer& mixer) : m_mixer(mixer) {}

    audio::Mixer& m_mixer;
    void* m_stream = nullptr; // SDL_AudioStream*
    std::string m_name;
    // The block mixed per pass: fixed, so the audio thread never allocates.
    std::array<std::int16_t, std::size_t{1024} * audio::kOutputChannels> m_block{};
};

} // namespace coney::platform
