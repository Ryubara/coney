// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "audio/audio_format.h"
#include "audio/mixer.h"

namespace coney::audio {

/// The audio "device" of test mode (`--headless`, `--frames`, scripted input) and of tests: nothing is heard and no
/// thread or clock is involved. The main loop pulls the mixer by hand, a fixed kFramesPerStep frames for each 1/30 s
/// frame, so a run mixes the same samples every time. It keeps a hash and the peak of everything pulled, which a test
/// can compare, and the last block pulled.
class OfflineDevice {
  public:
    /// A device pulling from `mixer`, which must outlive it.
    explicit OfflineDevice(Mixer& mixer) : m_mixer(mixer) {}

    /// Mixes the next `frames` output frames; returns the last up to kFramesPerStep of them (interleaved stereo),
    /// valid until the next pull.
    std::span<const std::int16_t> pull(std::size_t frames);
    /// Mixes one fixed step's worth of frames (kFramesPerStep).
    std::span<const std::int16_t> pullStep() { return pull(kFramesPerStep); }

    /// Frames pulled so far.
    [[nodiscard]] std::uint64_t frames() const { return m_frames; }
    /// The largest absolute sample pulled so far (0 to 32768).
    [[nodiscard]] int peak() const { return m_peak; }
    /// A 64-bit FNV-1a hash of every sample pulled so far, in order, as little-endian bytes.
    [[nodiscard]] std::uint64_t hash() const { return m_hash; }
    /// One line for the log: frames, seconds, peak and hash. Counts only.
    [[nodiscard]] std::string summary() const;

  private:
    Mixer& m_mixer;
    std::array<std::int16_t, static_cast<std::size_t>(kFramesPerStep) * kOutputChannels> m_block{};
    std::uint64_t m_frames = 0;
    int m_peak = 0;
    std::uint64_t m_hash = 0xcbf29ce484222325ULL; // FNV-1a's offset basis
};

} // namespace coney::audio
