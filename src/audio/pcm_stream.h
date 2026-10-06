// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "core/error.h"

namespace coney::audio {

/// Sound that arrives while it plays: a fixed ring of signed 16-bit frames (mono or stereo) that one producer thread
/// fills (a decoder of streamed music or a long speech) and a voice drains on the mixer's thread. Lock-free between
/// those two threads, like SpscQueue. A voice that finds the ring empty plays silence and counts an underrun; once the
/// producer calls finish() and the ring runs dry, the voice ends.
class PcmStream {
  public:
    /// A stream of `channels` (1 or 2) at `sampleRate` (1 to kMaxSourceRate) holding up to `capacityFrames` frames
    /// (at least 1) at once. Fails with InvalidArgument on a bad shape.
    [[nodiscard]] static std::expected<std::shared_ptr<PcmStream>, Error> create(int channels, int sampleRate,
                                                                                 std::uint32_t capacityFrames);

    /// Producer side: appends as many whole frames of `samples` (interleaved) as fit; returns how many frames went in.
    std::size_t write(std::span<const std::int16_t> samples);
    /// Producer side: no more frames will come; the voice ends once it has played what is queued.
    void finish() { m_finished.store(true, std::memory_order_release); }
    /// How many more frames write() would take now.
    [[nodiscard]] std::size_t freeFrames() const;

    /// Consumer side (the mixer): the next frame as left and right (a mono frame twice), or nothing when the ring is
    /// empty, which counts an underrun unless the stream is finished.
    std::optional<std::array<std::int16_t, 2>> readFrame();
    /// Whether finish() was called and every frame has been read.
    [[nodiscard]] bool drained() const;

    /// 1 (mono) or 2 (stereo).
    [[nodiscard]] int channels() const { return m_channels; }
    /// Frames a second at pitch 1.
    [[nodiscard]] int sampleRate() const { return m_sampleRate; }
    /// How many times a voice found the ring empty before the stream finished.
    [[nodiscard]] std::uint64_t underruns() const { return m_underruns.load(std::memory_order_relaxed); }

  private:
    PcmStream(int channels, int sampleRate, std::uint32_t capacityFrames);

    int m_channels;
    int m_sampleRate;
    std::size_t m_slots; // frames the ring has room for, one more than it holds
    std::vector<std::int16_t> m_ring;
    std::atomic<std::size_t> m_read{0};  // the consumer's next frame
    std::atomic<std::size_t> m_write{0}; // the producer's next frame
    std::atomic<bool> m_finished{false};
    std::atomic<std::uint64_t> m_underruns{0};
};

} // namespace coney::audio
