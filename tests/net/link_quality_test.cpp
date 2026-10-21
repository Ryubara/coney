// SPDX-License-Identifier: GPL-3.0-or-later
// The round trip and loss from the sessions' own pings (docs/guides/multiplayer.md#link-quality).
#include "net/link_quality.h"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using coney::net::LinkQuality;

namespace {

constexpr std::uint64_t kMs = 1'000'000;

} // namespace

TEST_CASE("the first answer sets the round trip and later ones move it an eighth", "[net][link_quality]") {
    LinkQuality quality;
    CHECK(quality.roundTripNs() == 0);
    const auto first = quality.pingSent(0);
    quality.pongReceived(first, 80 * kMs);
    CHECK(quality.roundTripNs() == 80 * kMs);
    CHECK(quality.lastRoundTripNs() == 80 * kMs);
    const auto second = quality.pingSent(100 * kMs);
    quality.pongReceived(second, 260 * kMs); // 160 ms
    CHECK(quality.lastRoundTripNs() == 160 * kMs);
    CHECK(quality.roundTripNs() == 90 * kMs); // 80 - 10 + 20
}

TEST_CASE("duplicate, unknown and forgotten answers are ignored", "[net][link_quality]") {
    LinkQuality quality;
    const auto ping = quality.pingSent(0);
    quality.pongReceived(ping, 10 * kMs);
    quality.pongReceived(ping, 500 * kMs); // a duplicate
    quality.pongReceived(77, 20 * kMs);    // never sent
    CHECK(quality.pongsReceived() == 1);
    CHECK(quality.roundTripNs() == 10 * kMs);
    // Once the window has moved past a ping, its late answer counts for nothing.
    const auto old = quality.pingSent(0);
    for (std::uint32_t i = 0; i < LinkQuality::kWindow; ++i) {
        (void)quality.pingSent(0);
    }
    quality.pongReceived(old, 30 * kMs);
    CHECK(quality.pongsReceived() == 1);
}

TEST_CASE("loss counts only pings past their grace that were never answered", "[net][link_quality]") {
    LinkQuality quality;
    // Send kWindow pings and answer every other one.
    for (std::uint32_t i = 0; i < LinkQuality::kWindow; ++i) {
        const auto ping = quality.pingSent(i);
        if (i % 2 == 0) {
            quality.pongReceived(ping, i + 1);
        }
    }
    // The newest kGrace are not judged yet; of the rest, half were answered.
    CHECK(quality.lossFraction() == 0.5F);

    LinkQuality fresh;
    for (std::uint32_t i = 0; i < LinkQuality::kGrace; ++i) {
        (void)fresh.pingSent(i);
    }
    CHECK(fresh.lossFraction() == 0.0F); // all still within their grace
}
