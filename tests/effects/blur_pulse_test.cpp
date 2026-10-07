// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/blur_pulse.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using coney::effects::BlurPulse;
using State = BlurPulse::State;

namespace {

// Steps `pulse` by `seconds` in 30 Hz updates.
void run(BlurPulse& pulse, float seconds) {
    for (float t = 0.0F; t < seconds - 1e-4F; t += 1.0F / 30.0F) {
        pulse.step(1.0F / 30.0F);
    }
}

} // namespace

TEST_CASE("the blur pulse is off and draws nothing until started", "[blur_pulse]") {
    BlurPulse pulse;
    pulse.step(1.0F / 30.0F);
    CHECK_FALSE(pulse.running());
    CHECK_FALSE(pulse.passes().has_value());
}

TEST_CASE("the death camera's pulse waits out its delay, rises over its time and holds", "[blur_pulse]") {
    BlurPulse pulse;
    pulse.start(6.5F, 1500); // the game-over shot
    CHECK(pulse.running());
    run(pulse, 1.4F);
    CHECK_FALSE(pulse.passes().has_value()); // still in the delay
    CHECK(pulse.level() == 0.0F);
    run(pulse, 0.2F);
    // Past the delay: started, then moving; the view is replaced even at 0 passes.
    REQUIRE(pulse.passes().has_value());
    CHECK(pulse.state() == State::Moving);
    run(pulse, 3.25F);
    CHECK_THAT(pulse.level(), WithinAbs(0.5, 0.02));
    CHECK(*pulse.passes() == static_cast<int>(pulse.level() * 28.0F));
    run(pulse, 4.0F);
    CHECK(pulse.state() == State::Held);
    CHECK(*pulse.passes() == 28);
    // No auto-end: it holds until ended.
    run(pulse, 10.0F);
    CHECK(pulse.state() == State::Held);
    // The death camera's fade has covered the screen: off at once.
    pulse.end(0.0F);
    CHECK_FALSE(pulse.running());
    pulse.step(1.0F / 30.0F);
    CHECK_FALSE(pulse.passes().has_value());
}

TEST_CASE("a queued pulse rises over look 5's in time, holds 360 ms and falls over its out time", "[blur_pulse]") {
    BlurPulse pulse;
    pulse.queueStart();
    run(pulse, 1.25F + 2.0F / 30.0F);
    CHECK(pulse.state() == State::Held);
    run(pulse, 0.3F);
    CHECK(pulse.state() == State::Held);
    run(pulse, 0.1F);
    // Ended itself: from full, falling.
    CHECK(pulse.state() != State::Held);
    CHECK(pulse.level() <= 1.0F);
    run(pulse, 4.2F);
    CHECK_FALSE(pulse.running());
    CHECK(pulse.level() == 0.0F);
}

TEST_CASE("a pulse started part-way keeps its level; an end with time jumps to full and falls", "[blur_pulse]") {
    BlurPulse pulse;
    pulse.start(1.0F, 0);
    run(pulse, 0.5F + 1.0F / 30.0F);
    const float half = pulse.level();
    REQUIRE(half > 0.0F);
    pulse.start(2.0F, 0);
    CHECK(pulse.level() == half);
    pulse.end(2.0F);
    CHECK(pulse.level() == 1.0F);
    pulse.step(1.0F / 30.0F); // started: drawn at full
    CHECK(pulse.passes() == 28);
    pulse.step(1.0F / 30.0F);
    CHECK(pulse.level() < 1.0F);
    pulse.queueEnd(0.0F);
    CHECK_FALSE(pulse.running());
}
