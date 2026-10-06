// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/adpcm.h"

#include <algorithm>
#include <array>

namespace coney::audio {

namespace {

// The five prediction filters, in 64ths: the sample is predicted from the previous two as (s1 * f0 + s2 * f1) / 64.
constexpr std::array<std::array<std::int32_t, 2>, 5> kFilters{{{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}}};
constexpr std::size_t kMaxPredictor = 4;
constexpr std::int32_t kMaxShift = 12;
constexpr std::int32_t kShiftForInvalid = 9;

} // namespace

std::uint8_t AdpcmDecoder::decodeFrame(std::span<const std::byte, kAdpcmFrameBytes> frame,
                                       std::span<std::int16_t, kAdpcmFrameSamples> out) {
    const auto header = static_cast<std::uint8_t>(frame[0]);
    const std::size_t predictor = std::min<std::size_t>(header >> 4U, kMaxPredictor);
    std::int32_t shift = header & 0x0F;
    if (shift > kMaxShift) {
        shift = kShiftForInvalid;
    }
    const auto [f0, f1] = kFilters.at(predictor);
    std::size_t at = 0;
    for (std::size_t i = 2; i < kAdpcmFrameBytes; ++i) {
        const auto byte = static_cast<std::uint8_t>(frame[i]);
        for (const std::uint32_t nibble :
             std::array<std::uint32_t, 2>{byte & 0x0FU, static_cast<std::uint32_t>(byte) >> 4U}) {
            // The nibble is the top four bits of a signed 16-bit value, scaled down by the shift.
            const auto value = static_cast<std::int32_t>(static_cast<std::int16_t>(nibble << 12U)) >> shift;
            const std::int32_t sample =
                std::clamp(value + ((m_s1 * f0 + m_s2 * f1 + 32) >> 6), std::int32_t{-32768}, std::int32_t{32767});
            out[at++] = static_cast<std::int16_t>(sample);
            m_s2 = m_s1;
            m_s1 = sample;
        }
    }
    return static_cast<std::uint8_t>(frame[1]);
}

std::size_t AdpcmDecoder::decode(std::span<const std::byte> data, std::vector<std::int16_t>& out, bool stopAtEnd) {
    std::size_t frames = 0;
    for (std::size_t at = 0; at + kAdpcmFrameBytes <= data.size(); at += kAdpcmFrameBytes) {
        const std::size_t first = out.size();
        out.resize(first + kAdpcmFrameSamples);
        const std::uint8_t flags = decodeFrame(data.subspan(at).first<kAdpcmFrameBytes>(),
                                               std::span(out).subspan(first).first<kAdpcmFrameSamples>());
        ++frames;
        if (stopAtEnd && (flags & kAdpcmEnd) != 0) {
            break;
        }
    }
    return frames;
}

std::vector<std::int16_t> decodeAdpcm(std::span<const std::byte> data, bool stopAtEnd) {
    std::vector<std::int16_t> out;
    out.reserve(data.size() / kAdpcmFrameBytes * kAdpcmFrameSamples);
    AdpcmDecoder decoder;
    decoder.decode(data, out, stopAtEnd);
    return out;
}

} // namespace coney::audio
