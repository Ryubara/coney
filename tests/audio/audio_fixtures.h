// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic sound data for the audio tests: ADPCM frames, the sound tables' chunks and in-memory sound files. No game
// data: every value is made up here.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <span>
#include <string_view>
#include <vector>

#include "audio/adpcm.h"
#include "audio/mixer.h"
#include "audio/sound_data.h"
#include "audio/sound_stream.h"
#include "support/fixtures.h"

namespace coney::test {

/// One ADPCM frame of 28 nibbles (-8 to 7), with predictor 0, the given shift and flags: each sample decodes to
/// `nibble << 12 >> shift` (shift 12: the nibble itself).
inline std::vector<std::byte> adpcmFrame(std::span<const std::int8_t, 28> nibbles, std::uint8_t shift,
                                         std::uint8_t flags = 0, std::uint8_t predictor = 0) {
    Bytes frame;
    frame.u8(static_cast<std::uint8_t>((predictor << 4U) | shift)).u8(flags);
    for (std::size_t i = 0; i < 28; i += 2) {
        const auto low = static_cast<std::uint8_t>(nibbles[i] & 0x0F);
        const auto high = static_cast<std::uint8_t>(nibbles[i + 1] & 0x0F);
        frame.u8(static_cast<std::uint8_t>(low | (high << 4U)));
    }
    return frame.data();
}

/// `frames` ADPCM frames whose every sample decodes to `nibble << 12 >> shift`; the last frame carries `lastFlags`.
inline std::vector<std::byte> constantAdpcm(std::int8_t nibble, std::size_t frames, std::uint8_t shift = 12,
                                            std::uint8_t lastFlags = 0) {
    std::array<std::int8_t, 28> nibbles{};
    nibbles.fill(nibble);
    Bytes data;
    for (std::size_t i = 0; i < frames; ++i) {
        data.append(adpcmFrame(nibbles, shift, i + 1 == frames ? lastFlags : 0));
    }
    return data.data();
}

/// A sound list record.
struct TestSound {
    std::uint32_t hash = 0;
    std::uint32_t size = 0;
    std::uint32_t offset = 0;
    std::uint8_t soundClass = 0;
    std::uint8_t rateIndex = 71; // 48000 Hz
    std::uint8_t volume = 100;
    std::uint8_t pitchVariation = 0;
};

/// Chunk 0x29 of `sounds`, which must be sorted by hash.
inline std::vector<std::byte> soundListChunk(std::span<const TestSound> sounds) {
    Bytes chunk;
    chunk.u32(static_cast<std::uint32_t>(sounds.size()));
    for (const TestSound& s : sounds) {
        chunk.u32(s.size).u32(s.offset).u32(s.hash).u8(s.pitchVariation).u8(s.volume).u8(s.rateIndex).u8(s.soundClass);
    }
    return chunk.data();
}

/// A sound class: near, far (metres, a multiple of 5), flags, priority, channels.
inline audio::SoundClass testClass(std::uint8_t near, std::uint16_t far, std::uint8_t flags, std::uint8_t priority,
                                   std::uint8_t channels = 1) {
    return audio::SoundClass{.near = near, .far = far, .flags = flags, .priority = priority, .channels = channels};
}

/// In-memory BFW.SND and MUSIC.SND.
class MemorySoundFiles final : public audio::SoundFiles {
  public:
    std::vector<std::byte> sounds;
    std::vector<std::byte> music;
    std::size_t reads = 0;

    std::expected<void, Error> read(audio::SoundFile file, std::uint64_t offset, std::span<std::byte> out) override {
        ++reads;
        const std::vector<std::byte>& data = file == audio::SoundFile::Sounds ? sounds : music;
        if (offset > data.size() || out.size() > data.size() - offset) {
            return fail(ErrorCode::Truncated, "past the end of the test file");
        }
        std::memcpy(out.data(), data.data() + offset, out.size());
        return {};
    }
};

/// Mixes `frames` frames from `mixer` and returns them, interleaved.
inline std::vector<std::int16_t> pull(audio::Mixer& mixer, std::size_t frames) {
    std::vector<std::int16_t> out(frames * 2);
    mixer.mix(out);
    return out;
}

} // namespace coney::test
