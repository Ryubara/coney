// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's own container for decoded sound (no @orig): the formats the game's banks use are decoded into it elsewhere.
#include "audio/pcm_sound.h"

#include <format>
#include <limits>
#include <utility>

namespace coney::audio {

std::expected<PcmSound, Error> PcmSound::create(std::vector<std::int16_t> samples, int channels, int sampleRate,
                                                std::optional<LoopPoints> loop) {
    if (channels != 1 && channels != 2) {
        return fail(ErrorCode::InvalidArgument, std::format("a sound has 1 or 2 channels, not {}", channels));
    }
    if (sampleRate < 1 || sampleRate > kMaxSourceRate) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("a sound's rate is 1 to {} Hz, not {}", kMaxSourceRate, sampleRate));
    }
    const auto perFrame = static_cast<std::size_t>(channels);
    if (samples.empty() || samples.size() % perFrame != 0 ||
        samples.size() / perFrame > std::numeric_limits<std::uint32_t>::max()) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("a sound needs a whole, non-zero number of frames ({} samples, {} channels)",
                                samples.size(), channels));
    }
    const auto frames = static_cast<std::uint32_t>(samples.size() / perFrame);
    if (loop && (loop->start >= loop->end || loop->end > frames)) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("loop points {}..{} do not fit a sound of {} frames", loop->start, loop->end, frames));
    }
    PcmSound sound;
    sound.m_samples = std::move(samples);
    sound.m_channels = channels;
    sound.m_sampleRate = sampleRate;
    sound.m_frames = frames;
    sound.m_loop = loop;
    return sound;
}

} // namespace coney::audio
