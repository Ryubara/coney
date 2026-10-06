// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/frame_rate_meter.h"

#include <cstdint>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::FrameRateMeter;

namespace {

constexpr std::uint64_t kSecond = FrameRateMeter::kSecond;
// A frame of 50 ms (20 frames a second): ten of them make exactly half a second.
constexpr std::uint64_t kFrame20 = kSecond / 20;

// The meter's reading, zeros before the first: a REQUIRE checks there is one first.
coney::FrameRateReading readingOf(const FrameRateMeter& meter) {
    return meter.reading().value_or(coney::FrameRateReading{});
}

} // namespace

TEST_CASE("the meter gives no reading until its frames span the window", "[frame_rate]") {
    FrameRateMeter meter(FrameRateMeter::kHalfSecond);
    // Nine frames, one or two steps each as 20 frames a second run 30 steps: under half a second.
    for (int i = 0; i < 9; ++i) {
        CHECK_FALSE(meter.add(kFrame20, static_cast<std::uint32_t>(1 + (i % 2))));
    }
    CHECK_FALSE(meter.reading().has_value());
    // The tenth completes it: 20 frames a second, 50 ms each, 15 steps in half a second.
    CHECK(meter.add(kFrame20, 2));
    REQUIRE(meter.reading().has_value());
    CHECK(readingOf(meter).framesPerSecond == Approx(20.0));
    CHECK(readingOf(meter).frameMilliseconds == Approx(50.0));
    CHECK(readingOf(meter).stepsPerSecond == Approx(30.0));
}

TEST_CASE("the meter averages each span on its own and keeps the last reading between spans", "[frame_rate]") {
    FrameRateMeter meter(FrameRateMeter::kHalfSecond);
    for (int i = 0; i < 10; ++i) {
        (void)meter.add(kFrame20, static_cast<std::uint32_t>(1 + (i % 2)));
    }
    REQUIRE(meter.reading().has_value());
    // Halfway into a slower span (10 frames a second, four steps a frame: the catch-up cap), the old reading stays.
    CHECK_FALSE(meter.add(kSecond / 10, 4));
    CHECK(readingOf(meter).framesPerSecond == Approx(20.0));
    for (int i = 0; i < 4; ++i) {
        (void)meter.add(kSecond / 10, 4);
    }
    CHECK(readingOf(meter).framesPerSecond == Approx(10.0));
    CHECK(readingOf(meter).frameMilliseconds == Approx(100.0));
    CHECK(readingOf(meter).stepsPerSecond == Approx(40.0));
}

TEST_CASE("a frame with no measured time is not counted", "[frame_rate]") {
    // The pacer's first frame has nothing before it to be measured from.
    FrameRateMeter meter(kSecond);
    CHECK_FALSE(meter.add(0, 1));
    CHECK(meter.add(kSecond, 30));
    REQUIRE(meter.reading().has_value());
    CHECK(readingOf(meter).framesPerSecond == Approx(1.0));
    CHECK(readingOf(meter).stepsPerSecond == Approx(30.0));
}
