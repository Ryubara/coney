// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace coney::debug {

/// What the Audio page shows of the sound output now.
struct AudioStatus {
    std::string device{};           ///< Where the sound goes: the device's name, or "offline (test mode)".
    std::size_t voicesPlaying = 0;  ///< Voices playing now.
    std::size_t voiceCount = 0;     ///< Voices there are.
    std::uint64_t framesMixed = 0;  ///< Output frames mixed so far.
    int sampleRate = 0;             ///< Output frames a second.
    std::uint64_t voicesStolen = 0; ///< Plays that took a busy voice.
    std::uint64_t playsDropped = 0; ///< Plays that found no voice to take.
};

/// What the debug menus can reach of the sound output: its volumes and a test tone. The platform's audio output
/// implements it; the Audio page (src/debug/pages_engine.cpp) is defined over it, so it is the same in every front end
/// and needs no platform code of its own. Called on the game thread, between steps.
class AudioControls {
  public:
    virtual ~AudioControls() = default;
    AudioControls() = default;
    AudioControls(const AudioControls&) = delete;
    AudioControls& operator=(const AudioControls&) = delete;
    AudioControls(AudioControls&&) = delete;
    AudioControls& operator=(AudioControls&&) = delete;

    /// How many volumes there are: the master volume (index 0), then one per bus.
    [[nodiscard]] virtual std::size_t volumeCount() const = 0;
    /// The name of volume `index` ("Master", "Effects", ...).
    [[nodiscard]] virtual std::string_view volumeName(std::size_t index) const = 0;
    /// Volume `index`, 0 to 1 (linear).
    [[nodiscard]] virtual float volume(std::size_t index) const = 0;
    /// Sets volume `index` (0 to 1).
    virtual void setVolume(std::size_t index, float volume) = 0;
    /// Whether the test tone (a looping synthesised sweep on the effects bus) plays.
    [[nodiscard]] virtual bool testTone() const = 0;
    /// Starts or stops the test tone.
    virtual void setTestTone(bool on) = 0;
    /// The output's state now.
    [[nodiscard]] virtual AudioStatus status() const = 0;
};

} // namespace coney::debug
