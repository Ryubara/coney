// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace coney::audio {

/// Bytes of one PS2 SPU2 ADPCM frame.
inline constexpr std::size_t kAdpcmFrameBytes = 16;
/// Samples one frame decodes to.
inline constexpr std::size_t kAdpcmFrameSamples = 28;

/// The flag bits of a frame's second byte, as the SPU2 reads them.
enum AdpcmFlag : std::uint8_t {
    kAdpcmEnd = 0x01,       ///< The last frame of a sample or of a loop.
    kAdpcmRepeat = 0x02,    ///< With kAdpcmEnd: jump back to the loop start instead of stopping.
    kAdpcmLoopStart = 0x04, ///< The loop starts at this frame.
};

/// A decoder of PS2 SPU2 ADPCM for one channel, the codec of all the game's sound: 16-byte frames of 28 samples, a
/// header byte holding the shift (low nibble) and the predictor (high nibble), a flag byte, then 14 bytes of signed
/// 4-bit samples, low nibble first. The data carries no header. It keeps the two previous samples between frames, so
/// a stream decodes frame by frame as it arrives.
///
/// This is the public SPU2 algorithm, not code of the game (docs/research/formats/audio.md#ps2-adpcm-all-sound-data).
/// Out-of-range header values are read as the SPU2 is commonly documented to read them: a predictor above 4 as 4, a
/// shift above 12 as 9; the disc has neither (same page).
class AdpcmDecoder {
  public:
    /// Decodes the frame `frame` (kAdpcmFrameBytes) into `out` (kAdpcmFrameSamples) and returns its flag byte.
    std::uint8_t decodeFrame(std::span<const std::byte, kAdpcmFrameBytes> frame,
                             std::span<std::int16_t, kAdpcmFrameSamples> out);
    /// Decodes every whole frame of `data`, appending to `out`; a trailing part frame is ignored. With `stopAtEnd`,
    /// stops after the first frame flagged kAdpcmEnd (a bank sample's end). Returns the frames decoded.
    std::size_t decode(std::span<const std::byte> data, std::vector<std::int16_t>& out, bool stopAtEnd = false);
    /// Forgets the previous samples, as at the start of a sound.
    void reset() { m_s1 = m_s2 = 0; }

  private:
    std::int32_t m_s1 = 0; // the previous sample
    std::int32_t m_s2 = 0; // the one before it
};

/// Decodes a whole mono sound of ADPCM `data` (see AdpcmDecoder::decode()).
[[nodiscard]] std::vector<std::int16_t> decodeAdpcm(std::span<const std::byte> data, bool stopAtEnd = false);

} // namespace coney::audio
