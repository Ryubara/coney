// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's own streaming ring (no @orig): the game's music and speech streams are not traced yet
// (docs/research/sound.md).
#include "audio/pcm_stream.h"

#include <algorithm>
#include <format>

#include "audio/pcm_sound.h"

namespace coney::audio {

PcmStream::PcmStream(int channels, int sampleRate, std::uint32_t capacityFrames)
    : m_channels(channels), m_sampleRate(sampleRate), m_slots(static_cast<std::size_t>(capacityFrames) + 1),
      m_ring(m_slots * static_cast<std::size_t>(channels)) {}

std::expected<std::shared_ptr<PcmStream>, Error> PcmStream::create(int channels, int sampleRate,
                                                                   std::uint32_t capacityFrames) {
    if (channels != 1 && channels != 2) {
        return fail(ErrorCode::InvalidArgument, std::format("a stream has 1 or 2 channels, not {}", channels));
    }
    if (sampleRate < 1 || sampleRate > kMaxSourceRate) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("a stream's rate is 1 to {} Hz, not {}", kMaxSourceRate, sampleRate));
    }
    if (capacityFrames == 0) {
        return fail(ErrorCode::InvalidArgument, "a stream needs room for at least one frame");
    }
    // Not make_shared: the constructor is private, so only create() can check the shape first.
    return std::shared_ptr<PcmStream>(new PcmStream(channels, sampleRate, capacityFrames));
}

std::size_t PcmStream::freeFrames() const {
    const std::size_t read = m_read.load(std::memory_order_acquire);
    const std::size_t write = m_write.load(std::memory_order_relaxed);
    const std::size_t used = (write + m_slots - read) % m_slots;
    return m_slots - 1 - used;
}

std::size_t PcmStream::write(std::span<const std::int16_t> samples) {
    const auto perFrame = static_cast<std::size_t>(m_channels);
    const std::size_t frames = std::min(samples.size() / perFrame, freeFrames());
    std::size_t write = m_write.load(std::memory_order_relaxed);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t c = 0; c < perFrame; ++c) {
            m_ring[(write * perFrame) + c] = samples[(frame * perFrame) + c];
        }
        write = (write + 1) % m_slots;
    }
    m_write.store(write, std::memory_order_release); // publishes the frames to the voice
    return frames;
}

std::optional<std::array<std::int16_t, 2>> PcmStream::readFrame() {
    const std::size_t read = m_read.load(std::memory_order_relaxed);
    if (read == m_write.load(std::memory_order_acquire)) {
        if (!m_finished.load(std::memory_order_acquire)) {
            m_underruns.fetch_add(1, std::memory_order_relaxed);
        }
        return std::nullopt;
    }
    const auto perFrame = static_cast<std::size_t>(m_channels);
    const std::int16_t left = m_ring[read * perFrame];
    const std::int16_t right = m_channels == 2 ? m_ring[(read * perFrame) + 1] : left;
    m_read.store((read + 1) % m_slots, std::memory_order_release); // hands the frame back to the producer
    return std::array<std::int16_t, 2>{left, right};
}

bool PcmStream::drained() const {
    // Finished first: a frame written before finish() is then sure to be seen by the emptiness check.
    return m_finished.load(std::memory_order_acquire) &&
           m_read.load(std::memory_order_relaxed) == m_write.load(std::memory_order_acquire);
}

} // namespace coney::audio
