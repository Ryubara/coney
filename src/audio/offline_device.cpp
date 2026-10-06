// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's own test device (no @orig).
#include "audio/offline_device.h"

#include <algorithm>
#include <cstdlib>
#include <format>

namespace coney::audio {

std::span<const std::int16_t> OfflineDevice::pull(std::size_t frames) {
    const std::size_t blockFrames = m_block.size() / kOutputChannels;
    std::size_t last = 0;
    // In blocks of at most a step, the block buffer's size; the last block is what the caller gets back.
    for (std::size_t done = 0; done < frames; done += last) {
        last = std::min(blockFrames, frames - done);
        const std::span<std::int16_t> block(m_block.data(), last * kOutputChannels);
        m_mixer.mix(block);
        for (const std::int16_t sample : block) {
            m_peak = std::max(m_peak, std::abs(static_cast<int>(sample)));
            const auto bits = static_cast<std::uint16_t>(sample);
            for (const auto byte : {static_cast<std::uint8_t>(bits & 0xffU), static_cast<std::uint8_t>(bits >> 8U)}) {
                m_hash = (m_hash ^ byte) * 0x100000001b3ULL; // FNV-1a's prime
            }
        }
    }
    m_frames += frames;
    return {m_block.data(), last * kOutputChannels};
}

std::string OfflineDevice::summary() const {
    return std::format("audio: offline, {} frames mixed ({:.2f} s at {} Hz), peak {}, hash {:#018x}\n", m_frames,
                       static_cast<double>(m_frames) / kOutputRate, kOutputRate, m_peak, m_hash);
}

} // namespace coney::audio
