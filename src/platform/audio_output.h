// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "audio/mixer.h"
#include "audio/offline_device.h"
#include "audio/pcm_sound.h"
#include "audio/sound_player.h"
#include "core/error.h"
#include "debug/audio_controls.h"
#include "platform/sdl_audio_device.h"

namespace coney::platform {

/// Where an AudioOutput's sound goes.
enum class AudioSink : std::uint8_t {
    Device,  ///< The system's default playback device, through SDL (SdlAudioDevice).
    Offline, ///< Nowhere: the main loop pulls a fixed step of frames per frame (audio::OfflineDevice). Test mode.
};

/// Coney's sound output for one run: the mixer, the game-facing SoundPlayer over it, and the device that pulls it
/// (SDL's, or the offline one of test mode, which never opens a device and never reads a clock). It also implements
/// the debug menus' AudioControls (volumes and the test tone).
class AudioOutput final : public debug::AudioControls {
  public:
    /// Starts the output on `sink`. Fails with ErrorCode::PlatformFailure when the device cannot be opened (no
    /// device, or SDL refuses); the offline sink cannot fail.
    [[nodiscard]] static std::expected<std::unique_ptr<AudioOutput>, Error> start(AudioSink sink);

    ~AudioOutput() override;
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;
    AudioOutput(AudioOutput&&) = delete;
    AudioOutput& operator=(AudioOutput&&) = delete;

    /// The game-facing sound calls.
    [[nodiscard]] audio::SoundPlayer& sounds() { return m_sounds; }
    /// The mixer underneath.
    [[nodiscard]] audio::Mixer& mixer() { return *m_mixer; }

    /// Call once at the end of every frame, on the game thread: offline, mixes one fixed step's frames (each frame of
    /// test mode is one step); either way lets go of the sounds of voices that ended.
    void endFrame();
    /// One line on where the sound goes, for the log at start-up.
    [[nodiscard]] std::string startLine() const;
    /// One line on what was mixed (offline: frames, peak and hash), for the log at the end. Counts only.
    [[nodiscard]] std::string summary() const;

    // debug::AudioControls
    [[nodiscard]] std::size_t volumeCount() const override { return audio::kBusCount + 1; }
    [[nodiscard]] std::string_view volumeName(std::size_t index) const override;
    [[nodiscard]] float volume(std::size_t index) const override;
    void setVolume(std::size_t index, float volume) override;
    [[nodiscard]] bool testTone() const override;
    void setTestTone(bool on) override;
    [[nodiscard]] debug::AudioStatus status() const override;

  private:
    AudioOutput();

    // The mixer first: the device that calls it must go before it does.
    std::unique_ptr<audio::Mixer> m_mixer;
    audio::SoundPlayer m_sounds;
    std::optional<audio::OfflineDevice> m_offline;
    std::unique_ptr<SdlAudioDevice> m_device;
    std::shared_ptr<const audio::PcmSound> m_tone;
    audio::VoiceHandle m_toneVoice;
};

} // namespace coney::platform
