// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "audio/game_sound.h"
#include "audio/mixer.h"
#include "audio/offline_device.h"
#include "audio/output_status.h"
#include "audio/pcm_sound.h"
#include "audio/sound_player.h"
#include "core/error.h"
#include "platform/sdl_audio_device.h"

namespace coney::io {
class Wad;
}

namespace coney::platform {

/// Where an AudioOutput's sound goes.
enum class AudioSink : std::uint8_t {
    Device,  ///< The system's default playback device, through SDL (SdlAudioDevice).
    Offline, ///< Nowhere: the main loop pulls a fixed step of frames per frame (audio::OfflineDevice). Test mode.
};

/// Coney's sound output for one run: the mixer, the game-facing SoundPlayer over it, and the device that pulls it
/// (SDL's, or the offline one of test mode, which never opens a device and never reads a clock). The volumes, the test
/// tone and the status are its own API; the debug menus' Audio page reaches them through AudioOutputControls in the
/// debug tools (src/platform/audio_output_controls.h).
class AudioOutput final {
  public:
    /// Starts the output on `sink`. Fails with ErrorCode::PlatformFailure when the device cannot be opened (no
    /// device, or SDL refuses); the offline sink cannot fail.
    [[nodiscard]] static std::expected<std::unique_ptr<AudioOutput>, Error> start(AudioSink sink);

    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;
    AudioOutput(AudioOutput&&) = delete;
    AudioOutput& operator=(AudioOutput&&) = delete;

    /// The game-facing sound calls.
    [[nodiscard]] audio::SoundPlayer& sounds() { return m_sounds; }
    /// The game's sound over them: what the bindings, gameplay and the front end drive (audio/game_sound.h).
    [[nodiscard]] audio::GameSound& game() { return m_game; }
    /// The mixer underneath.
    [[nodiscard]] audio::Mixer& mixer() { return *m_mixer; }

    /// Loads the game's sound data from `wad` (the sound tables of `warriors.glr`, the disc's IOP/BFW.SND and
    /// IOP/MUSIC.SND) and gives sounds() a SoundEngine over it, which loads banks from `wad`. `wad` must outlive the
    /// output. Fails as the reads do; the output then plays only Coney's own sounds.
    [[nodiscard]] std::expected<void, Error> startEngine(const io::Wad& wad);
    /// Call once at the end of every frame, on the game thread, with the fixed steps the frame ran: runs the sound
    /// engine for their game time; offline, mixes one fixed step's frames (each frame of test mode is one step);
    /// either way lets go of the sounds of voices that ended. The game's sound (game()) runs first. When SDL gave the
    /// device up, it opens the system's default device again, trying once a second (30 frames) until one opens; the
    /// mixer and its voices carry on, so the sound resumes where it was.
    void endFrame(std::uint32_t steps);
    /// Hands each offline step's mixed frames (interleaved 16-bit stereo at audio::kOutputRate) to `tap`, after
    /// endFrame() mixes them; empty for none. For `--record` (ClipRecorder), Coney's own debug tooling. The device
    /// sink mixes on SDL's thread and calls nothing.
    void setStepTap(std::function<void(std::span<const std::int16_t>)> tap) { m_stepTap = std::move(tap); }
    /// SDL's id of the playback device (0 offline, or while the device is lost). For tests and logs.
    [[nodiscard]] std::uint32_t deviceId() const { return m_device ? m_device->deviceId() : 0; }
    /// How many times the device was lost and opened again.
    [[nodiscard]] std::uint32_t deviceReopens() const { return m_reopens; }
    /// One line on where the sound goes, for the log at start-up.
    [[nodiscard]] std::string startLine() const;
    /// One line on what was mixed (offline: frames, peak and hash), for the log at the end. Counts only.
    [[nodiscard]] std::string summary() const;

    /// How many volumes there are: the master volume (index 0), then one per bus.
    [[nodiscard]] std::size_t volumeCount() const { return audio::kBusCount + 1; }
    /// Volume `index`'s name.
    [[nodiscard]] std::string_view volumeName(std::size_t index) const;
    /// Volume `index`, as the mixer holds it.
    [[nodiscard]] float volume(std::size_t index) const;
    /// Sets volume `index` on the mixer; an index past the buses does nothing.
    void setVolume(std::size_t index, float volume);
    /// Whether the looping test tone plays.
    [[nodiscard]] bool testTone() const;
    /// Starts or stops the looping test tone.
    void setTestTone(bool on);
    /// The output's state now.
    [[nodiscard]] audio::OutputStatus status() const;

  private:
    AudioOutput();
    // Counts the wait after a failed reopen down; whether a reopen is due now.
    bool reopenDue();

    // The mixer first: the device that calls it must go before it does.
    std::unique_ptr<audio::Mixer> m_mixer;
    audio::SoundPlayer m_sounds;
    audio::GameSound m_game;
    std::optional<audio::OfflineDevice> m_offline;
    std::function<void(std::span<const std::int16_t>)> m_stepTap; // setStepTap()
    std::unique_ptr<SdlAudioDevice> m_device;
    std::uint32_t m_reopens = 0;    // devices opened again after a loss
    std::uint32_t m_reopenWait = 0; // frames until the next try after a failed reopen
    std::shared_ptr<const audio::PcmSound> m_tone;
    audio::VoiceHandle m_toneVoice;
};

} // namespace coney::platform
