// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "core/error.h"

namespace coney::audio {

/// The part of a sound a looping voice repeats: frames [start, end). A voice plays from frame 0 to `end`, then
/// returns to `start` for as long as it plays.
struct LoopPoints {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
};

/// The highest sample rate a sound may have.
inline constexpr int kMaxSourceRate = 192'000;

/// A sound held whole in memory: signed 16-bit PCM, mono or stereo (interleaved left then right), at any rate up to
/// kMaxSourceRate. The game's decoded sound effects and speech lines will be these; the mixer resamples them to its
/// output rate. Immutable once made, so the mixer's thread can read it while the game's holds it.
class PcmSound {
  public:
    /// A sound from `samples` (`channels` 1 or 2, interleaved), played at `sampleRate` frames a second, looping over
    /// `loop` when a voice plays it (none: it plays once). Fails with InvalidArgument on a bad shape: no frames, a
    /// channel count other than 1 or 2, samples not a whole number of frames, a rate outside 1 to kMaxSourceRate, or
    /// loop points outside the sound or empty.
    [[nodiscard]] static std::expected<PcmSound, Error> create(std::vector<std::int16_t> samples, int channels,
                                                               int sampleRate,
                                                               std::optional<LoopPoints> loop = std::nullopt);

    /// The samples, interleaved.
    [[nodiscard]] std::span<const std::int16_t> samples() const { return m_samples; }
    /// 1 (mono) or 2 (stereo).
    [[nodiscard]] int channels() const { return m_channels; }
    /// Frames a second at pitch 1.
    [[nodiscard]] int sampleRate() const { return m_sampleRate; }
    /// How many frames (a sample per channel) the sound has.
    [[nodiscard]] std::uint32_t frames() const { return m_frames; }
    /// Where a voice loops, or nothing for a sound played once.
    [[nodiscard]] const std::optional<LoopPoints>& loop() const { return m_loop; }

  private:
    PcmSound() = default;

    std::vector<std::int16_t> m_samples;
    int m_channels = 1;
    int m_sampleRate = 0;
    std::uint32_t m_frames = 0;
    std::optional<LoopPoints> m_loop;
};

} // namespace coney::audio
