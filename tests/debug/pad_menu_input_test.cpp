// SPDX-License-Identifier: GPL-3.0-or-later
// The pad menu's input: hold-to-repeat that speeds up, the value multiplier, partial stick deflections, the shoulder
// modifiers and buttons on press, all counted in steps.
#include "debug/pad_menu_input.h"

#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"

using coney::Pad;
using coney::PadSample;
using coney::debug::MenuAction;
using coney::debug::PadMenuInput;
using coney::debug::StepSize;
namespace pad = coney::pad;

namespace {

// A connected sample holding `buttons`, the left stick at (x, y) in percent of full deflection (y up).
PadSample sample(std::uint16_t buttons, int x = 0, int y = 0) {
    PadSample s;
    s.connected = true;
    s.buttons = buttons;
    // The input script's mapping: -100..100 onto the raw byte, y down positive.
    const auto byte = [](int percent) { return static_cast<std::uint8_t>(128 + percent * 127 / 100); };
    s.sticks[2] = byte(x);
    s.sticks[3] = byte(-y);
    return s;
}

// The steps (1-based) on which holding `held` for `steps` steps fires an action.
std::vector<int> firingSteps(const PadSample& held, int steps) {
    Pad record;
    PadMenuInput input;
    std::vector<int> fired;
    for (int step = 1; step <= steps; ++step) {
        record.update(held);
        if (input.read(record).action) {
            fired.push_back(step);
        }
    }
    return fired;
}

} // namespace

TEST_CASE("a held direction fires at once, then after the delay every 4 steps, then faster", "[debug]") {
    const std::vector<int> fired = firingSteps(sample(pad::kDown), 50);
    REQUIRE(fired.size() >= 4);
    CHECK(fired[0] == 1);
    CHECK(fired[1] == PadMenuInput::kRepeatDelay);
    CHECK(fired[2] == PadMenuInput::kRepeatDelay + PadMenuInput::kRepeatInterval);
    // After kFastAfter steps the repeats come every 2 steps.
    CHECK(fired.back() - fired[fired.size() - 2] == 2);
}

TEST_CASE("holding left or right grows the value multiplier", "[debug]") {
    Pad record;
    PadMenuInput input;
    int last = 0;
    for (int step = 1; step <= PadMenuInput::kTurboAfter; ++step) {
        record.update(sample(pad::kRight));
        const auto frame = input.read(record);
        if (frame.action) {
            CHECK(*frame.action == MenuAction::Right);
            last = frame.multiplier;
        }
    }
    CHECK(last == 10);
}

TEST_CASE("the left stick counts past half deflection, so a partial push navigates", "[debug]") {
    // The PS2 dead zone takes a third of the travel: 75 % reads as 0.65, 40 % as 0.18.
    CHECK(firingSteps(sample(0, 0, 75), 1) == std::vector<int>{1});
    CHECK(firingSteps(sample(0, -75, 0), 1) == std::vector<int>{1});
    CHECK(firingSteps(sample(0, 0, 40), 30).empty());
}

TEST_CASE("buttons fire once on press, and L2 and R2 pick fine and coarse steps", "[debug]") {
    Pad record;
    PadMenuInput input;
    record.update(sample(pad::kCross));
    CHECK(input.read(record).action == MenuAction::Accept);
    record.update(sample(pad::kCross));
    CHECK_FALSE(input.read(record).action);
    record.update(sample(pad::kSquare | pad::kL2));
    const auto fine = input.read(record);
    CHECK(fine.action == MenuAction::Pin);
    CHECK(fine.stepSize == StepSize::Fine);
    record.update(sample(pad::kR2 | pad::kTriangle));
    const auto coarse = input.read(record);
    CHECK(coarse.action == MenuAction::Reset);
    CHECK(coarse.stepSize == StepSize::Coarse);
}
