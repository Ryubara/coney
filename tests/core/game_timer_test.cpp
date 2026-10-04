// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/game_timer.h"

#include <catch2/catch_test_macros.hpp>

using coney::GameTimer;

TEST_CASE("the fixed step is exactly 1/30 s and ignores real time", "[game_timer]") {
    STATIC_CHECK(GameTimer::kFixedStepTicks * 30 == GameTimer::kTicksPerSecond);
    GameTimer timer;
    CHECK(timer.fixedStep());
    for (int i = 0; i < 30; ++i) {
        CHECK(timer.update(123'456'789) == GameTimer::kFixedStepTicks);
    }
    CHECK(timer.ticks() == GameTimer::kTicksPerSecond);
    CHECK(timer.milliseconds() == 1000);
    CHECK(GameTimer::toSeconds(timer.ticks()) == 1.0);
}

TEST_CASE("real-time mode advances by the measured time, at most 40 ms", "[game_timer]") {
    GameTimer timer;
    timer.setFixedStep(false);
    CHECK(timer.update(1000) == 1000);
    CHECK(timer.update(GameTimer::kTicksPerSecond) == GameTimer::kMaxRealStepTicks);
    CHECK(GameTimer::kMaxRealStepTicks * 25 == GameTimer::kTicksPerSecond); // 40 ms
    CHECK(timer.ticks() == 1000 + GameTimer::kMaxRealStepTicks);
}

TEST_CASE("a paused timer does not advance", "[game_timer]") {
    GameTimer timer;
    timer.setPaused(true);
    CHECK(timer.update() == 0);
    CHECK(timer.ticks() == 0);
    timer.setPaused(false);
    CHECK(timer.update() == GameTimer::kFixedStepTicks);
}
