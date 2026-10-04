// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/idle_mode.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "graphics/render_device.h"

using coney::GameModeStack;
using coney::GameTimer;
using coney::IdleMode;
using coney::graphics::Rgba;

namespace {

/// A render device that records the calls a mode makes, in order.
class RecordingDevice final : public coney::graphics::RenderDevice {
  public:
    void beginFrame(Rgba clear) override {
        calls.emplace_back("begin");
        lastClear = clear;
    }
    void present() override { calls.emplace_back("present"); }

    std::vector<std::string> calls;
    Rgba lastClear;
};

} // namespace

TEST_CASE("the idle mode clears and presents once per frame", "[idle_mode]") {
    RecordingDevice device;
    IdleMode idle(&device);
    GameModeStack stack;
    stack.push(idle);
    GameTimer timer;
    timer.setFixedStep(true);
    CHECK(stack.runUntilEmpty(timer, {}, std::uint64_t{2}) == 2);
    CHECK(device.calls == std::vector<std::string>{"begin", "present", "begin", "present"});
    CHECK(device.lastClear == IdleMode::kClearColour);
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
