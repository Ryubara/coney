// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
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
///
/// Devices change under a running game: when the system's default output changes, or the default device is unplugged
/// and another takes over, SDL moves the stream to the new default by itself, converting to its format. Only when SDL
/// gives the device up (an SDL_EVENT_AUDIO_DEVICE_REMOVED for it) does lost() turn true; reopen() then opens the
/// default device afresh.
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
    /// SDL's id of the device the stream plays on (a logical device; 0 while none is open).
    [[nodiscard]] std::uint32_t deviceId() const { return m_deviceId.load(std::memory_order_acquire); }
    /// Whether the device is gone: SDL reported it removed, or the last reopen() failed.
    [[nodiscard]] bool lost() const { return m_stream == nullptr || m_lost.load(std::memory_order_acquire); }
    /// Closes the stream (no callback runs after that) and opens the system's default playback device again, on the
    /// same mixer. Call on the thread that opened the device. Fails as open() does; the device then stays lost().
    [[nodiscard]] std::expected<void, Error> reopen();

    /// The SDL event watch: notes a removal of this device. Public only so the C watch can reach it; any thread.
    void noteRemoved(std::uint32_t deviceId);

    /// SDL's callback: mixes `bytes` more bytes into the stream. Public only so the C callback can reach it.
    void feed(void* stream, int bytes);

  private:
    explicit SdlAudioDevice(audio::Mixer& mixer) : m_mixer(mixer) {}

    // Opens the default playback device's stream, feeding from the mixer, and starts it.
    [[nodiscard]] std::expected<void, Error> openStream();
    // Stops and closes the stream, if any; SDL waits for a callback in progress, so none runs after this.
    void closeStream();

    audio::Mixer& m_mixer;
    void* m_stream = nullptr;                 // SDL_AudioStream*
    std::atomic<std::uint32_t> m_deviceId{0}; // the stream's logical device, read by the event watch
    std::atomic<bool> m_lost{false};          // set by the event watch
    bool m_watching = false;                  // the event watch is installed
    std::string m_name;
    // The block mixed per pass: fixed, so the audio thread never allocates.
    std::array<std::int16_t, std::size_t{1024} * audio::kOutputChannels> m_block{};
};

} // namespace coney::platform
