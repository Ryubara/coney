// SPDX-License-Identifier: GPL-3.0-or-later
// The bit stream network messages are written with (docs/guides/multiplayer.md#bit-stream): bits packed least
// significant first, varints, zig-zag, quantized floats, strings, and reads that refuse short or malformed data.
#include "net/bit_stream.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"

using coney::ErrorCode;
using coney::net::BitReader;
using coney::net::BitWriter;
using coney::net::dequantize;
using coney::net::quantize;

TEST_CASE("bits are packed least significant first and the last byte is padded with zeros", "[net][bit_stream]") {
    BitWriter out;
    out.writeBits(0b101, 3);
    out.writeBool(true);
    out.writeBits(0x3f, 6); // crosses into the second byte
    CHECK(out.bitCount() == 10);
    const std::vector<std::byte> bytes(out.bytes().begin(), out.bytes().end());
    REQUIRE(bytes.size() == 2);
    // 101, then 1, then the low four bits of 111111 fill the first byte; the top two start the second.
    CHECK(bytes[0] == std::byte{0b1111'1101});
    CHECK(bytes[1] == std::byte{0b0000'0011});

    BitReader in(bytes);
    CHECK(in.readBits(3).value() == 0b101);
    CHECK(in.readBool().value());
    CHECK(in.readBits(6).value() == 0x3f);
    CHECK(in.finish().has_value());
}

TEST_CASE("whole numbers of every width round-trip", "[net][bit_stream]") {
    BitWriter out;
    out.writeU8(0xab);
    out.writeU16(0xbeef);
    out.writeU32(0xdeadbeefU);
    out.writeBits(1, 1);
    out.writeU32(0x80000001U); // unaligned
    BitReader in(out.bytes());
    CHECK(in.readU8().value() == 0xab);
    CHECK(in.readU16().value() == 0xbeef);
    CHECK(in.readU32().value() == 0xdeadbeefU);
    CHECK(in.readBits(1).value() == 1);
    CHECK(in.readU32().value() == 0x80000001U);
    CHECK(in.finish().has_value());
}

TEST_CASE("varints take a byte for small numbers and round-trip the whole 64-bit range", "[net][bit_stream]") {
    for (const std::uint64_t value :
         {std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{127}, std::uint64_t{128}, std::uint64_t{300},
          std::uint64_t{0xffffffffU}, std::numeric_limits<std::uint64_t>::max()}) {
        BitWriter out;
        out.writeVarUint(value);
        BitReader in(out.bytes());
        CHECK(in.readVarUint().value() == value);
        CHECK(in.finish().has_value());
    }
    BitWriter small;
    small.writeVarUint(127);
    CHECK(small.bitCount() == 8);
    BitWriter largest;
    largest.writeVarUint(std::numeric_limits<std::uint64_t>::max());
    CHECK(largest.bitCount() == 80);
}

TEST_CASE("signed varints are zig-zag: small magnitudes are small either side of zero", "[net][bit_stream]") {
    for (const std::int64_t value :
         {std::int64_t{0}, std::int64_t{-1}, std::int64_t{1}, std::int64_t{-64}, std::int64_t{63},
          std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()}) {
        BitWriter out;
        out.writeVarInt(value);
        BitReader in(out.bytes());
        CHECK(in.readVarInt().value() == value);
    }
    BitWriter minus64;
    minus64.writeVarInt(-64); // zig-zag 127: still one group
    CHECK(minus64.bitCount() == 8);
}

TEST_CASE("a varint longer than 64 bits is refused", "[net][bit_stream]") {
    // Eleven groups that all say "another follows".
    BitWriter out;
    for (int i = 0; i < 11; ++i) {
        out.writeBits(0x7f, 7);
        out.writeBool(true);
    }
    BitReader in(out.bytes());
    const auto value = in.readVarUint();
    REQUIRE_FALSE(value.has_value());
    CHECK(value.error().code == ErrorCode::Invalid);

    // Ten groups whose last carries more than bit 63.
    BitWriter over;
    for (int i = 0; i < 9; ++i) {
        over.writeBits(0x7f, 7);
        over.writeBool(true);
    }
    over.writeBits(2, 7);
    over.writeBool(false);
    BitReader overIn(over.bytes());
    CHECK_FALSE(overIn.readVarUint().has_value());
}

TEST_CASE("quantizing keeps the ends exact and the round trip within half a step", "[net][bit_stream]") {
    constexpr float kMin = -1000.0F;
    constexpr float kMax = 1000.0F;
    constexpr int kBits = 18;
    const float halfStep = (kMax - kMin) / static_cast<float>((1U << kBits) - 1U) / 2.0F;
    CHECK(quantize(kMin, kMin, kMax, kBits) == 0);
    CHECK(quantize(kMax, kMin, kMax, kBits) == (1U << kBits) - 1U);
    CHECK(dequantize(0, kMin, kMax, kBits) == kMin);
    CHECK(dequantize((1U << kBits) - 1U, kMin, kMax, kBits) == kMax);
    for (const float value : {-999.99F, -0.004F, 0.0F, 12.345F, 777.7F}) {
        const float back = dequantize(quantize(value, kMin, kMax, kBits), kMin, kMax, kBits);
        CHECK(std::fabs(back - value) <= halfStep * 1.01F);
    }
    // Out of range clamps; NaN goes to the bottom rather than anywhere.
    CHECK(quantize(5000.0F, kMin, kMax, kBits) == (1U << kBits) - 1U);
    CHECK(quantize(-5000.0F, kMin, kMax, kBits) == 0);
    CHECK(quantize(std::numeric_limits<float>::quiet_NaN(), kMin, kMax, kBits) == 0);

    BitWriter out;
    out.writeQuantized(12.5F, 0.0F, 100.0F, 10);
    CHECK(out.bitCount() == 10);
    BitReader in(out.bytes());
    CHECK(std::fabs(in.readQuantized(0.0F, 100.0F, 10).value() - 12.5F) < 0.05F);
}

TEST_CASE("floats round-trip bit for bit", "[net][bit_stream]") {
    BitWriter out;
    out.writeBool(false); // unaligned on purpose
    out.writeF32(-0.0F);
    out.writeF32(3.14159F);
    BitReader in(out.bytes());
    CHECK_FALSE(in.readBool().value());
    const float negativeZero = in.readF32().value();
    CHECK(negativeZero == 0.0F);
    CHECK(std::signbit(negativeZero));
    CHECK(in.readF32().value() == 3.14159F);
}

TEST_CASE("strings round-trip, and a count over the limit is refused before reading", "[net][bit_stream]") {
    BitWriter out;
    out.writeString("Swan", 32);
    out.writeString("", 32);
    BitReader in(out.bytes());
    CHECK(in.readString(32).value() == "Swan");
    CHECK(in.readString(32).value().empty());

    // A count of a million with no bytes after it: Invalid against a limit of 32, not an allocation or Truncated.
    BitWriter forged;
    forged.writeVarUint(1'000'000);
    BitReader forgedIn(forged.bytes());
    const auto text = forgedIn.readString(32);
    REQUIRE_FALSE(text.has_value());
    CHECK(text.error().code == ErrorCode::Invalid);

    // A count within the limit but past the end of the data: Truncated.
    BitWriter shortString;
    shortString.writeVarUint(10);
    shortString.writeU8('a');
    BitReader shortIn(shortString.bytes());
    const auto cut = shortIn.readString(32);
    REQUIRE_FALSE(cut.has_value());
    CHECK(cut.error().code == ErrorCode::Truncated);
}

TEST_CASE("reading past the end is Truncated and leftovers fail finish()", "[net][bit_stream]") {
    BitWriter out;
    out.writeU16(7);
    BitReader in(out.bytes());
    CHECK(in.readU8().has_value());
    const auto past = in.readU16();
    REQUIRE_FALSE(past.has_value());
    CHECK(past.error().code == ErrorCode::Truncated);

    BitReader early(out.bytes());
    CHECK(early.readU8().has_value());
    const auto trailing = early.finish();
    REQUIRE_FALSE(trailing.has_value());
    CHECK(trailing.error().code == ErrorCode::Invalid);

    // Set bits in the padding of the last byte are not the writer's: refused.
    const std::vector<std::byte> dirty{std::byte{0b1000'0001}};
    BitReader dirtyIn(dirty);
    CHECK(dirtyIn.readBits(1).value() == 1);
    CHECK_FALSE(dirtyIn.finish().has_value());
    BitReader dirtyAlign(dirty);
    CHECK(dirtyAlign.readBits(1).has_value());
    CHECK_FALSE(dirtyAlign.alignToByte().has_value());
}

TEST_CASE("take() hands over the bytes and empties the writer", "[net][bit_stream]") {
    BitWriter out;
    out.writeU8(1);
    out.writeBool(true);
    out.alignToByte();
    out.writeU8(2);
    const std::vector<std::byte> bytes = out.take();
    CHECK(bytes.size() == 3);
    CHECK(out.bitCount() == 0);
    CHECK(out.bytes().empty());
}
