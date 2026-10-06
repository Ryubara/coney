// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace coney::audio {

// The output's shape. **Coney's choices** until docs/research/sound.md says how the game sets up its output: 48 kHz
// stereo is the PS2 sound processor's own output rate (public hardware documentation), and 48 voices its hardware
// voice count (two cores of 24). The game's sound engine may use fewer; it will sit on top of this mixer.

/// Frames a second the mixer produces: every voice is resampled to this rate.
inline constexpr int kOutputRate = 48'000;
/// Output channels, interleaved left then right.
inline constexpr int kOutputChannels = 2;
/// Voices that can play at once; a play beyond them steals one (Mixer).
inline constexpr std::size_t kVoiceCount = 48;
/// Output frames in one fixed 1/30 s step: what an offline pull takes per step in test mode.
inline constexpr int kFramesPerStep = kOutputRate / 30;
static_assert(kFramesPerStep * 30 == kOutputRate, "a step must be a whole number of output frames");

/// The category a voice plays in. Each bus has its own volume, under the master volume. Coney's grouping (the game's
/// own is not traced yet): what a player's options and the debug menu set apart.
enum class Bus : std::uint8_t {
    Sfx,    ///< Sound effects: hits, footsteps, ambient emitters, the debug menu's test tone.
    Music,  ///< Streamed music.
    Speech, ///< Voice lines and speeches.
};

/// How many buses there are.
inline constexpr std::size_t kBusCount = 3;

/// The bus names, by Bus value, for logs and the debug menu.
inline constexpr std::array<std::string_view, kBusCount> kBusNames{"Effects", "Music", "Speech"};

/// The name of `bus` (kBusNames).
[[nodiscard]] constexpr std::string_view busName(Bus bus) { return kBusNames.at(static_cast<std::size_t>(bus)); }

} // namespace coney::audio
