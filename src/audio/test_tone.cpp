// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's own test signal (no @orig).
#include "audio/test_tone.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

#include "core/assert.h"

namespace coney::audio {

namespace {

// The sweep's frequency at frame `i` of `frames`: up from low to high in the first half and back down in the second,
// evenly in pitch (exponentially in hertz).
double frequencyAt(const ToneSweep& sweep, std::size_t i, std::size_t frames) {
    const double t = static_cast<double>(i) / static_cast<double>(frames); // 0 to 1
    const double triangle = t < 0.5 ? t * 2.0 : (1.0 - t) * 2.0;           // 0, up to 1, back to 0
    return static_cast<double>(sweep.lowHz) *
           std::pow(static_cast<double>(sweep.highHz) / static_cast<double>(sweep.lowHz), triangle);
}

} // namespace

PcmSound makeToneSweep(const ToneSweep& sweep) {
    CONEY_ASSERT(sweep.lowHz > 0.0F && sweep.highHz > 0.0F && sweep.seconds > 0.0F && sweep.sampleRate > 0);
    const auto frames = static_cast<std::size_t>(
        std::max(1.0, std::round(static_cast<double>(sweep.seconds) * static_cast<double>(sweep.sampleRate))));
    // Cycles over the whole sweep, rounded to a whole number by scaling every frequency a little, so the phase is
    // back where it started at the loop.
    double cycles = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        cycles += frequencyAt(sweep, i, frames) / sweep.sampleRate;
    }
    const double scale = std::max(1.0, std::round(cycles)) / cycles;
    // The sine itself.
    std::vector<std::int16_t> samples(frames);
    const double peak = static_cast<double>(std::clamp(sweep.amplitude, 0.0F, 1.0F)) * 32767.0;
    double phase = 0.0; // in cycles
    for (std::size_t i = 0; i < frames; ++i) {
        samples[i] = static_cast<std::int16_t>(std::lround(peak * std::sin(2.0 * std::numbers::pi * phase)));
        phase += frequencyAt(sweep, i, frames) * scale / sweep.sampleRate;
        phase -= std::floor(phase);
    }
    auto sound = PcmSound::create(std::move(samples), 1, sweep.sampleRate,
                                  LoopPoints{.start = 0, .end = static_cast<std::uint32_t>(frames)});
    CONEY_ASSERT(sound.has_value()); // the shape above is always a valid one
    return std::move(*sound);
}

} // namespace coney::audio
