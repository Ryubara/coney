#include "core/frame_pacer.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using coney::core::FramePacer;
using coney::core::FrameSteps;
using namespace std::chrono_literals;

namespace {

struct Totals {
    std::int64_t ticks = 0;
    std::int64_t dropped = 0;
    int most_in_one_frame = 0;
    FrameSteps last;
};

// Advances the pacer `frames` times, `frame_time` apart, starting from `now`.
Totals run(FramePacer& pacer, std::chrono::nanoseconds& now, std::chrono::nanoseconds frame_time, int frames) {
    Totals totals;
    for (int i = 0; i < frames; ++i) {
        now += frame_time;
        totals.last = pacer.advance(now);
        totals.ticks += totals.last.ticks;
        totals.dropped += totals.last.dropped;
        totals.most_in_one_frame = std::max(totals.most_in_one_frame, totals.last.ticks);
    }
    return totals;
}

} // namespace

TEST_CASE("pacer ticks at the configured rate when frames come at the tick rate") {
    std::chrono::nanoseconds now{0};
    FramePacer pacer(30, 5, now);
    // 1/30 s is not a whole number of nanoseconds, so frames alternate between 33'333'333 and 33'333'334 ns.
    std::int64_t ticks = 0;
    for (int i = 1; i <= 300; ++i) {
        const auto t = std::chrono::nanoseconds{(std::int64_t{i} * 1'000'000'000 + 29) / 30};
        ticks += pacer.advance(t).ticks;
    }
    CHECK(ticks == 300);
}

TEST_CASE("pacer ticks at the configured rate when frames are faster than ticks") {
    std::chrono::nanoseconds now{0};
    FramePacer pacer(30, 5, now);
    const Totals totals = run(pacer, now, 8ms, 1250); // 125 frames a second for 10 s
    CHECK(totals.ticks == 300);
    CHECK(totals.most_in_one_frame == 1);
    CHECK(totals.dropped == 0);
}

TEST_CASE("pacer runs several ticks a frame when frames are slower than ticks") {
    std::chrono::nanoseconds now{0};
    FramePacer pacer(30, 5, now);
    const Totals totals = run(pacer, now, 100ms, 50); // 10 frames a second, 3 ticks each
    CHECK(totals.ticks == 150);
    CHECK(totals.most_in_one_frame == 3);
    CHECK(totals.dropped == 0);
}

TEST_CASE("pacer uses the configured rate") {
    std::chrono::nanoseconds now{0};
    FramePacer pacer(60, 5, now);
    CHECK(run(pacer, now, 10ms, 500).ticks == 300);
    CHECK(pacer.tick_length() == 16'666'666ns);
}

TEST_CASE("pacer reports the fraction of a tick elapsed since the last one") {
    std::chrono::nanoseconds now{0};
    FramePacer pacer(10, 5, now); // 100 ms ticks

    FrameSteps steps = pacer.advance(25ms);
    CHECK(steps.ticks == 0);
    CHECK(steps.alpha == Catch::Approx(0.25));

    steps = pacer.advance(100ms);
    CHECK(steps.ticks == 1);
    CHECK(steps.alpha == Catch::Approx(0.0));

    steps = pacer.advance(275ms);
    CHECK(steps.ticks == 1);
    CHECK(steps.alpha == Catch::Approx(0.75));

    steps = pacer.advance(299'999'999ns);
    CHECK(steps.ticks == 0);
    CHECK(steps.alpha < 1.0);
}

TEST_CASE("pacer caps catch-up ticks after a long stall and keeps the fraction") {
    std::chrono::nanoseconds now{0};
    FramePacer pacer(10, 4, now);

    const FrameSteps steps = pacer.advance(2'050ms); // 20.5 ticks due
    CHECK(steps.ticks == 4);
    CHECK(steps.dropped == 16);
    CHECK(steps.alpha == Catch::Approx(0.5));

    // Back to normal afterwards: no backlog is carried.
    now = 2'050ms;
    const Totals after = run(pacer, now, 50ms, 20);
    CHECK(after.ticks == 10);
    CHECK(after.most_in_one_frame == 1);
    CHECK(after.dropped == 0);
    CHECK(pacer.total_ticks() == 14);
    CHECK(pacer.total_dropped() == 16);
}

TEST_CASE("pacer survives a stall of hours without overflow") {
    FramePacer pacer(30, 5, 0ns);
    const FrameSteps steps = pacer.advance(std::chrono::hours{1000});
    CHECK(steps.ticks == 5);
    CHECK(steps.dropped > 0);
    CHECK(steps.alpha >= 0.0);
    CHECK(steps.alpha < 1.0);
}

TEST_CASE("pacer does not drift over many seconds of uneven frames") {
    std::chrono::nanoseconds now{0};
    FramePacer pacer(30, 5, now);
    // Frame times cycle through awkward lengths for a simulated hour.
    const std::chrono::nanoseconds pattern[] = {7'000'001ns, 16'666'667ns, 33'333'333ns, 41'000'000ns, 3'141'593ns};
    std::int64_t ticks = 0;
    std::size_t i = 0;
    while (now < std::chrono::hours{1}) {
        now += pattern[i++ % std::size(pattern)];
        ticks += pacer.advance(now).ticks;
    }
    const std::int64_t expected = now.count() * 30 / 1'000'000'000;
    CHECK(ticks == expected);
    CHECK(pacer.total_dropped() == 0);
}

TEST_CASE("pacer treats a clock going backwards as no time") {
    FramePacer pacer(10, 5, 1s);
    CHECK(pacer.advance(500ms).ticks == 0);
    CHECK(pacer.advance(1'100ms).ticks == 1);
}

TEST_CASE("pacer rejects a rate or cap that is not positive") {
    CHECK_THROWS(FramePacer(0, 5, 0ns));
    CHECK_THROWS(FramePacer(30, 0, 0ns));
}
