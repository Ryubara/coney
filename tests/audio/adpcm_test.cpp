// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/adpcm.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "audio_fixtures.h"

using coney::audio::AdpcmDecoder;
using coney::audio::decodeAdpcm;

TEST_CASE("an ADPCM frame's nibbles are the top bits of a sample, scaled down by the shift", "[audio]") {
    std::array<std::int8_t, 28> nibbles{};
    for (std::size_t i = 0; i < nibbles.size(); ++i) {
        nibbles.at(i) = static_cast<std::int8_t>((static_cast<int>(i) % 16) - 8); // -8 to 7, low nibble first
    }
    const auto loud = decodeAdpcm(coney::test::adpcmFrame(nibbles, 0));
    REQUIRE(loud.size() == 28);
    CHECK(loud[0] == -8 * 4096);
    CHECK(loud[1] == -7 * 4096);
    CHECK(loud[15] == 7 * 4096);
    const auto quiet = decodeAdpcm(coney::test::adpcmFrame(nibbles, 12));
    CHECK(quiet[0] == -8);
    CHECK(quiet[15] == 7);
}

TEST_CASE("the prediction filters add the previous samples, kept from frame to frame", "[audio]") {
    // A first frame of 4096s, then a frame of zero nibbles with filter 1 (60/64 of the previous sample).
    std::vector<std::byte> data = coney::test::constantAdpcm(1, 1, 0);
    const std::array<std::int8_t, 28> zero{};
    const auto second = coney::test::adpcmFrame(zero, 12, 0, 1);
    data.insert(data.end(), second.begin(), second.end());
    const auto samples = decodeAdpcm(data);
    REQUIRE(samples.size() == 56);
    CHECK(samples[27] == 4096);
    CHECK(samples[28] == ((4096 * 60) + 32) >> 6);
    CHECK(samples[29] == ((samples[28] * 60) + 32) >> 6);

    // A predictor above 4 is read as 4: (122, -60).
    AdpcmDecoder decoder;
    std::vector<std::int16_t> out;
    decoder.decode(coney::test::constantAdpcm(1, 1, 0), out);
    const auto filter7 = coney::test::adpcmFrame(zero, 12, 0, 7);
    decoder.decode(filter7, out);
    CHECK(out[28] == ((4096 * 122) - (4096 * 60) + 32) >> 6);
}

TEST_CASE("decoded samples clamp at 16 bits, and decoding can stop at the end flag", "[audio]") {
    const auto loud = decodeAdpcm(coney::test::constantAdpcm(7, 3, 0));
    CHECK(loud.back() == 28672);
    std::array<std::int8_t, 28> nibbles{};
    nibbles.fill(7);
    // Filter 4 on 28672s overshoots: the sum is clamped.
    std::vector<std::byte> data = coney::test::constantAdpcm(7, 1, 0);
    const auto over = coney::test::adpcmFrame(nibbles, 0, 0, 4);
    data.insert(data.end(), over.begin(), over.end());
    const auto clamped = decodeAdpcm(data);
    CHECK(clamped[28] == 32767);

    const auto ended = coney::test::constantAdpcm(1, 3, 12, coney::audio::kAdpcmEnd);
    std::vector<std::byte> withTail = ended;
    const auto tail = coney::test::constantAdpcm(2, 2);
    withTail.insert(withTail.end(), tail.begin(), tail.end());
    CHECK(decodeAdpcm(withTail, true).size() == std::size_t{3} * 28);
    CHECK(decodeAdpcm(withTail).size() == std::size_t{5} * 28);
    // A part frame at the end is not decoded.
    withTail.resize(withTail.size() - 3);
    CHECK(decodeAdpcm(withTail).size() == std::size_t{4} * 28);
}
