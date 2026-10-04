// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/idle_mode.h"

#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "support/recording_device.h"

using coney::GameModeStack;
using coney::GameTimer;
using coney::IdleMode;
using coney::test::RecordingDevice;

TEST_CASE("the idle mode clears and presents once per frame", "[idle_mode]") {
    RecordingDevice device;
    IdleMode idle(&device);
    GameModeStack stack;
    stack.push(idle);
    GameTimer timer;
    timer.setFixedStep(true);
    CHECK(stack.runUntilEmpty(timer, {}, std::uint64_t{2}) == 2);
    CHECK(device.calls == std::vector<std::string>{"begin", "present", "begin", "present"});
    REQUIRE_FALSE(device.clears.empty());
    CHECK(device.clears.back() == IdleMode::kClearColour);
}

TEST_CASE("the idle mode without a device draws nothing and stays", "[idle_mode]") {
    IdleMode idle;
    GameModeStack stack;
    stack.push(idle);
    GameTimer timer;
    timer.setFixedStep(true);
    CHECK(stack.runUntilEmpty(timer, {}, std::uint64_t{3}) == 3);
    CHECK(stack.topId() == IdleMode::kId);
}
