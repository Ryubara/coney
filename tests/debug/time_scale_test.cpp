// SPDX-License-Identifier: GPL-3.0-or-later
// The debug menus' time scale (docs/guides/debug-menu.md#pages, Time): below 1x the time controls hold a share of the
// due steps, above 1x the frame clock makes more steps due, and every step stays a whole 1/30 s. Checked as step
// counts over simulated seconds, and through the Time page as a front end drives it.
#include "debug/time_control.h"

#include <cstdint>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "core/frame_clock.h"
#include "debug/debug_session.h"
#include "debug/menu_model.h"

using coney::FrameClock;
using coney::FramePacing;
using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::TimeControl;
using coney::debug::TunableRegistry;

namespace {

constexpr std::uint64_t kSecond = FrameClock::kNanosecondsPerSecond;

/// What a run of frames gave: steps due (asked of the time controls) and steps run.
struct Counted {
    std::uint64_t due = 0;
    std::uint64_t ran = 0;
};

/// Runs `seconds` of frames at `hz` through `clock` and `time` as the main loop does: before each frame the clock is
/// told the speed the time controls want, then each due step asks the controls whether it runs. The clock's first
/// frame (one step at once) is run before counting.
Counted run(FrameClock& clock, TimeControl& time, std::uint64_t hz, std::uint64_t seconds) {
    (void)clock.advance(0);
    Counted counted;
    std::uint64_t last = 0;
    for (std::uint64_t k = 1; k <= hz * seconds; ++k) {
        const std::uint64_t end = k * kSecond / hz;
        clock.setSpeed(time.dueSpeedPercent());
        const std::uint32_t steps = clock.advance(end - last).steps;
        last = end;
        for (std::uint32_t s = 0; s < steps; ++s) {
            ++counted.due;
            if (time.shouldStep()) {
                ++counted.ran;
            }
        }
    }
    return counted;
}

} // namespace

TEST_CASE("the time scale runs the matching number of whole steps a second, from 0.1x to 4x", "[debug][time]") {
    struct Case {
        double scale;
        std::uint64_t due;
        std::uint64_t ran;
    };
    // One simulated second at 60 frames a second. Below 1x the steps stay due 30 times a second (the pads and menus
    // keep being read) and a share of them runs; the first due step after a change runs at once, so 0.25x runs the
    // 1st, 5th, ..., 29th: 8. Above 1x more steps are due and every one runs.
    const Case c = GENERATE(Case{0.1, 30, 3}, Case{0.25, 30, 8}, Case{0.5, 30, 15}, Case{1.0, 30, 30},
                            Case{2.0, 60, 60}, Case{4.0, 120, 120});
    CAPTURE(c.scale);
    TimeControl time;
    time.setScale(c.scale);
    FrameClock clock(FramePacing::Interpolated);
    const Counted counted = run(clock, time, 60, 1);
    CHECK(counted.due == c.due);
    CHECK(counted.ran == c.ran);
    CHECK(time.steps() == c.ran);
}

TEST_CASE("over a long run the scale gives exactly its share of the steps", "[debug][time]") {
    // Ten simulated seconds at 144 frames a second: 0.35x runs 105 of the 300 due steps, 1.5x runs 450.
    TimeControl time;
    FrameClock clock(FramePacing::Interpolated);
    time.setScale(0.35);
    CHECK(run(clock, time, 144, 10).ran == 105);
    time.setScale(1.5);
    const Counted fast = run(clock, time, 144, 10);
    CHECK(fast.ran == 450);
    CHECK(fast.due == 450);
}

TEST_CASE("below 1x the held steps fall evenly, and the first after a change runs at once", "[debug][time]") {
    TimeControl time;
    time.setScale(0.25);
    std::vector<bool> pattern;
    pattern.reserve(8);
    for (int i = 0; i < 8; ++i) {
        pattern.push_back(time.shouldStep());
    }
    CHECK(pattern == std::vector<bool>{true, false, false, false, true, false, false, false});
    // Pause wins over the scale, and a step asked for runs whatever the scale.
    time.setPaused(true);
    CHECK_FALSE(time.shouldStep());
    time.stepOnce();
    CHECK(time.shouldStep());
    time.setPaused(false);
    time.setScale(4.0);
    CHECK(time.shouldStep());
    CHECK(time.shouldStep());
}

TEST_CASE("the scale is kept in hundredths and clamped to its range", "[debug][time]") {
    TimeControl time;
    CHECK(time.scalePercent() == 100);
    CHECK(time.dueSpeedPercent() == 100);
    time.setScale(0.333);
    CHECK(time.scalePercent() == 33);
    CHECK(time.dueSpeedPercent() == 100); // below 1x the steps stay due at the real rate
    time.setScale(0.0);
    CHECK(time.scalePercent() == TimeControl::kMinScalePercent);
    time.setScale(100.0);
    CHECK(time.scalePercent() == TimeControl::kMaxScalePercent);
    CHECK(time.dueSpeedPercent() == static_cast<std::uint32_t>(TimeControl::kMaxScalePercent));
    CHECK(time.scale() == 4.0);
}

TEST_CASE("test mode stays one step a frame: a lockstep clock is never sped up", "[debug][time]") {
    // The main loop tells the clock the speed only when a real clock paces it; a lockstep test-mode run keeps one due
    // step a frame, so 4x runs one step a frame there and a scale below 1x still holds steps.
    TimeControl time;
    time.setScale(4.0);
    FrameClock clock(FramePacing::Lockstep);
    std::uint64_t ran = 0;
    for (int frame = 0; frame < 30; ++frame) {
        const std::uint32_t steps = clock.advance(0).steps;
        CHECK(steps == 1);
        ran += time.shouldStep() ? 1 : 0;
    }
    CHECK(ran == 30);
}

TEST_CASE("the Time page sets the scale, its presets and the reset to 1x", "[debug][time]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto page = session.model().openPage("Time");
    const coney::debug::MenuItem* scale = page->find("Time scale");
    REQUIRE(scale != nullptr);
    scale->setNumber(2.5);
    CHECK(session.time().scalePercent() == 250);
    CHECK(scale->getNumber() == 2.5);
    for (const auto& [label, percent] : std::vector<std::pair<const char*, int>>{
             {"Preset 0.1x", 10}, {"Preset 0.25x", 25}, {"Preset 0.5x", 50}, {"Preset 2x", 200}, {"Preset 4x", 400}}) {
        CAPTURE(label);
        const coney::debug::MenuItem* preset = page->find(label);
        REQUIRE(preset != nullptr);
        preset->run();
        CHECK(session.time().scalePercent() == percent);
    }
    page->find("Reset to 1x")->run();
    CHECK(session.time().scalePercent() == 100);
    // Triangle (a front end's reset) returns the number to 1x too.
    scale->setNumber(0.5);
    CHECK(coney::debug::resetItem(*scale));
    CHECK(session.time().scalePercent() == 100);
    // The frame stats corner line names a scale other than 1x.
    session.display().frameStats = true;
    page->find("Preset 0.25x")->run();
    REQUIRE(!session.cornerLines().empty());
    CHECK(session.cornerLines().front().ends_with("0.25x speed"));
}
