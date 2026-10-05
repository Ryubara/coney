// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/frame_clock.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using coney::FrameClock;
using coney::FramePacing;
using coney::FramePlan;

namespace {

constexpr std::uint64_t kSecond = FrameClock::kNanosecondsPerSecond;

/// What feeding a clock some frame times gave: frames, steps, and the alpha range seen.
struct Fed {
    std::uint64_t frames = 0;
    std::uint64_t steps = 0;
    float minAlpha = 1.0F;
    float maxAlpha = 0.0F;
};

/// Feeds `clock` one frame per entry of `times` (nanoseconds each), after the first frame at time 0.
Fed feed(FrameClock& clock, const std::vector<std::uint64_t>& times) {
    Fed fed;
    // Records one plan.
    const auto take = [&fed](const FramePlan& plan) {
        ++fed.frames;
        fed.steps += plan.steps;
        fed.minAlpha = std::min(fed.minAlpha, plan.alpha);
        fed.maxAlpha = std::max(fed.maxAlpha, plan.alpha);
    };
    take(clock.advance(0));
    for (const std::uint64_t t : times) {
        take(clock.advance(t));
    }
    return fed;
}

/// `seconds` of frames at `hz`, each frame's length rounded to whole nanoseconds the way a real clock reads them: the
/// k-th frame ends at floor(k × 10^9 / hz), so the lengths add up to exactly `seconds`.
std::vector<std::uint64_t> framesAt(std::uint64_t hz, std::uint64_t seconds) {
    std::vector<std::uint64_t> times;
    times.reserve(hz * seconds);
    std::uint64_t last = 0;
    for (std::uint64_t k = 1; k <= hz * seconds; ++k) {
        const std::uint64_t end = k * kSecond / hz;
        times.push_back(end - last);
        last = end;
    }
    return times;
}

/// `seconds` of irregular frames, 3 to 47 ms long in a fixed jittery pattern, the last one trimmed so they add up to
/// exactly `seconds`.
std::vector<std::uint64_t> jitteryFrames(std::uint64_t seconds) {
    std::vector<std::uint64_t> times;
    std::uint64_t total = 0;
    std::uint64_t state = 12345;
    while (total < seconds * kSecond) {
        // A small linear congruential generator: the same "random" pattern every run.
        state = (state * 6364136223846793005ULL) + 1442695040888963407ULL;
        const std::uint64_t length = std::min(3'000'000 + ((state >> 33U) % 44'000'000), (seconds * kSecond) - total);
        times.push_back(length);
        total += length;
    }
    return times;
}

} // namespace

TEST_CASE("lockstep runs one step per frame drawn at the newest state, whatever the time", "[frame_clock]") {
    FrameClock clock(FramePacing::Lockstep);
    for (const std::uint64_t elapsed : {std::uint64_t{0}, std::uint64_t{1}, kSecond, kSecond * 60}) {
        const FramePlan plan = clock.advance(elapsed);
        CHECK(plan.steps == 1);
        CHECK(plan.alpha == 1.0F);
    }
}

TEST_CASE("the interpolated clock runs one step on its first frame", "[frame_clock]") {
    FrameClock clock(FramePacing::Interpolated);
    const FramePlan first = clock.advance(0);
    CHECK(first.steps == 1);
    CHECK(first.alpha == 0.0F);
    CHECK(clock.advance(0).steps == 0);
}

TEST_CASE("every display rate gives exactly 30 steps a second, with alpha in [0, 1)", "[frame_clock]") {
    const std::uint64_t hz = GENERATE(30, 60, 75, 120, 144, 165, 240, 360, 1000);
    CAPTURE(hz);
    FrameClock clock(FramePacing::Interpolated);
    const Fed fed = feed(clock, framesAt(hz, 10));
    // The first frame's step, then 30 for each second.
    CHECK(fed.steps == 1 + (30 * 10));
    CHECK(fed.frames == 1 + (hz * 10));
    CHECK(fed.minAlpha >= 0.0F);
    CHECK(fed.maxAlpha < 1.0F);
    // Exactly 10 s have passed, a whole number of steps: nothing is left over.
    CHECK(clock.leftover() == 0);
}

TEST_CASE("irregular frame times give the same 30 steps a second", "[frame_clock]") {
    FrameClock clock(FramePacing::Interpolated);
    const Fed fed = feed(clock, jitteryFrames(20));
    CHECK(fed.steps == 1 + (30 * 20));
    CHECK(fed.maxAlpha < 1.0F);
    CHECK(clock.leftover() == 0);
}

TEST_CASE("an hour at 144 Hz does not drift by a single step", "[frame_clock]") {
    FrameClock clock(FramePacing::Interpolated);
    std::uint64_t steps = clock.advance(0).steps;
    // 144 frames a second for 3,600 s, as framesAt() would give them, without keeping half a million lengths.
    std::uint64_t last = 0;
    for (std::uint64_t k = 1; k <= 144ULL * 3600; ++k) {
        const std::uint64_t end = k * kSecond / 144;
        steps += clock.advance(end - last).steps;
        last = end;
        // Checked every simulated second: the count never strays by more than the step in progress.
        if (k % 144 == 0) {
            const std::uint64_t expected = 1 + (30 * (k / 144));
            REQUIRE(steps == expected);
        }
    }
    CHECK(steps == 1 + (30 * 3600));
}

TEST_CASE("a frame never runs more than kMaxStepsPerFrame steps, and a gap is not fast-forwarded", "[frame_clock]") {
    FrameClock clock(FramePacing::Interpolated);
    (void)clock.advance(0);
    // A ten-second stall (a dragged window, a breakpoint) runs four steps, not three hundred.
    CHECK(clock.advance(10 * kSecond).steps == FrameClock::kMaxStepsPerFrame);
    // And the next normal frame carries on from there, with nothing banked.
    const FramePlan next = clock.advance(kSecond / 60);
    CHECK(next.steps <= 1);
    CHECK(next.alpha < 1.0F);
    // The largest time a clock could report does not overflow.
    CHECK(clock.advance(std::numeric_limits<std::uint64_t>::max()).steps == FrameClock::kMaxStepsPerFrame);
}

TEST_CASE("frames slower than the cap slow the game down instead of piling up steps", "[frame_clock]") {
    // A machine managing 5 frames a second: each frame is credited at most four steps, so the game runs at 20 steps a
    // second, slowed down as the original slows down, and never more than four steps a frame.
    FrameClock clock(FramePacing::Interpolated);
    const Fed fed = feed(clock, framesAt(5, 10));
    CHECK(fed.steps == 1 + (4 * 5 * 10));
}
