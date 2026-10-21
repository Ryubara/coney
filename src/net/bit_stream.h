// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"

// The bit-level encoding every network message uses: values packed least significant bit first into bytes, so a
// boolean costs one bit and a quantized position as many bits as its precision needs. Coney's own; the original has
// no network play (docs/guides/multiplayer.md#bit-stream).

namespace coney::net {

/// Maps `value`, clamped to [min, max], onto the whole numbers 0 .. 2^bits - 1 with rounding to the nearest, so the
/// ends of the range are exact. `bits` is 1 to 32 and `max` is above `min` (CONEY_ASSERT).
[[nodiscard]] std::uint32_t quantize(float value, float min, float max, int bits);

/// The inverse of quantize(): the value a quantized number `q` stands for. The round trip is within half a step,
/// `(max - min) / (2^bits - 1) / 2`, of the clamped value.
[[nodiscard]] float dequantize(std::uint32_t q, float min, float max, int bits);

/// Writes values into a growing byte buffer, bit by bit. Writing never fails: a value out of the range its bit count
/// allows is a programmer error (CONEY_ASSERT), since the writer encodes our own state, never untrusted input.
class BitWriter {
  public:
    /// Writes the low `bits` bits of `value` (1 to 32; the other bits must be zero).
    void writeBits(std::uint32_t value, int bits);
    /// Writes one bit.
    void writeBool(bool value) { writeBits(value ? 1U : 0U, 1); }
    /// Writes a byte, a 16-bit or a 32-bit number in full.
    void writeU8(std::uint8_t value) { writeBits(value, 8); }
    void writeU16(std::uint16_t value) { writeBits(value, 16); }
    void writeU32(std::uint32_t value) { writeBits(value, 32); }
    /// Writes an unsigned number in groups of 7 bits, each followed by a bit saying whether another group follows:
    /// small numbers (sequence deltas, counts) take 8 bits, the largest 64-bit ones 80.
    void writeVarUint(std::uint64_t value);
    /// Writes a signed number zig-zag mapped (0, -1, 1, -2, ... to 0, 1, 2, 3, ...) through writeVarUint, so a small
    /// negative number is as cheap as a small positive one.
    void writeVarInt(std::int64_t value);
    /// Writes a float's 32 bits exactly (IEEE 754).
    void writeF32(float value);
    /// Writes quantize(value, min, max, bits).
    void writeQuantized(float value, float min, float max, int bits) {
        writeBits(quantize(value, min, max, bits), bits);
    }
    /// Writes a string as its byte count (writeVarUint) and its bytes. Longer than `maxBytes` is a programmer error.
    void writeString(std::string_view text, std::size_t maxBytes);
    /// Pads with zero bits to the next whole byte.
    void alignToByte();

    /// The bytes written so far; the last byte is padded with zero bits.
    [[nodiscard]] std::span<const std::byte> bytes() const { return m_bytes; }
    /// Moves the bytes out, leaving the writer empty.
    [[nodiscard]] std::vector<std::byte> take();
    /// Bits written so far.
    [[nodiscard]] std::size_t bitCount() const { return m_bitCount; }

  private:
    std::vector<std::byte> m_bytes;
    std::size_t m_bitCount = 0;
};

/// Reads what a BitWriter wrote from a buffer it does not own. Every read checks the bounds and returns
/// ErrorCode::Truncated past the end; a malformed value (a varint too long, a string over its limit) returns
/// ErrorCode::Invalid. The data comes from the network, so nothing here asserts on it.
class BitReader {
  public:
    explicit BitReader(std::span<const std::byte> data) : m_data(data) {}

    /// Reads `bits` bits (1 to 32) as an unsigned number.
    [[nodiscard]] std::expected<std::uint32_t, Error> readBits(int bits);
    [[nodiscard]] std::expected<bool, Error> readBool();
    [[nodiscard]] std::expected<std::uint8_t, Error> readU8();
    [[nodiscard]] std::expected<std::uint16_t, Error> readU16();
    [[nodiscard]] std::expected<std::uint32_t, Error> readU32();
    /// Reads a writeVarUint number. Fails with Invalid when it runs past 64 bits (a corrupt or hostile packet).
    [[nodiscard]] std::expected<std::uint64_t, Error> readVarUint();
    /// Reads a writeVarInt number.
    [[nodiscard]] std::expected<std::int64_t, Error> readVarInt();
    /// Reads a writeF32 float. NaN and infinity come through as they were sent; a caller that cannot take them checks.
    [[nodiscard]] std::expected<float, Error> readF32();
    /// Reads a writeQuantized value.
    [[nodiscard]] std::expected<float, Error> readQuantized(float min, float max, int bits);
    /// Reads a writeString string. Fails with Invalid when its count is over `maxBytes`, before reading any of it.
    [[nodiscard]] std::expected<std::string, Error> readString(std::size_t maxBytes);
    /// Skips to the next whole byte. Fails with Invalid when a skipped bit is set: a writer pads with zeros.
    [[nodiscard]] std::expected<void, Error> alignToByte();

    /// Checks that nothing but the writer's zero padding is left: fewer than 8 bits, all zero. A message decoder calls
    /// it last, so trailing bytes (a newer or forged message) are refused rather than ignored.
    [[nodiscard]] std::expected<void, Error> finish() const;

    /// Bits not read yet.
    [[nodiscard]] std::size_t remainingBits() const { return (m_data.size() * 8) - m_bitPosition; }

  private:
    std::span<const std::byte> m_data;
    std::size_t m_bitPosition = 0;
};

} // namespace coney::net
