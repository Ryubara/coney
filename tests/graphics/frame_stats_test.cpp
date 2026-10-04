// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/frame_stats.h"

#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using coney::graphics::Rgba;
using coney::graphics::summarizeFrame;

TEST_CASE("summarizeFrame counts the pixels that are not the background", "[frame_stats]") {
    // Three pixels: the background, the background with another alpha (still background), and red.
    const std::array<std::uint8_t, 12> frame{10, 20, 30, 255, 10, 20, 30, 0, 255, 0, 0, 255};
    auto stats = summarizeFrame(frame, Rgba{10, 20, 30, 255});
    CHECK(stats.pixels == 3);
    CHECK(stats.notBackground == 1);
}

TEST_CASE("summarizeFrame hashes the bytes with 64-bit FNV-1a", "[frame_stats]") {
    // No bytes give the offset basis; the four bytes "abcd" give the value an independent FNV-1a implementation
    // computes.
    CHECK(summarizeFrame({}, Rgba{}).hash == 0xcbf29ce484222325ULL);
    const std::array<std::uint8_t, 4> abcd{'a', 'b', 'c', 'd'};
    CHECK(summarizeFrame(abcd, Rgba{}).hash == 0xfc179f83ee0724ddULL);
}
