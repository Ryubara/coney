// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace coney::audio {

/// The sound output's state now: where it goes and what the mixer has done (platform::AudioOutput::status(), which
/// the debug menus' Audio page shows). Counts only.
struct OutputStatus {
    std::string device{};           ///< Where the sound goes: the device's name, or "offline (test mode)".
    std::size_t voicesPlaying = 0;  ///< Voices playing now.
    std::size_t voiceCount = 0;     ///< Voices there are.
    std::uint64_t framesMixed = 0;  ///< Output frames mixed so far.
    int sampleRate = 0;             ///< Output frames a second.
    std::uint64_t voicesStolen = 0; ///< Plays that took a busy voice.
    std::uint64_t playsDropped = 0; ///< Plays that found no voice to take.
};

} // namespace coney::audio
