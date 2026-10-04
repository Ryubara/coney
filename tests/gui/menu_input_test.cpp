// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/menu_input.h"

#include <cstdint>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"

using coney::Pad;
using coney::PadSample;
using coney::gui::MenuCommand;
using coney::gui::MenuInput;

namespace {

// A connected sample holding `buttons`, with the left stick at raw bytes `x`, `y` (0 left or up, 255 right or down).
PadSample sample(std::uint16_t buttons, std::uint8_t x = coney::pad::kStickCentre,
                 std::uint8_t y = coney::pad::kStickCentre) {
    PadSample s;
    s.connected = true;
    s.buttons = buttons;
    s.sticks = {coney::pad::kStickCentre, coney::pad::kStickCentre, x, y};
    s.pressure.fill(255);
    return s;
}

// One frame every 1/30 s, as the fixed step: frame `n` is at about n × 33 ms.
constexpr std::uint64_t msOf(int frame) { return static_cast<std::uint64_t>(frame) * 100 / 3; }

} // namespace

TEST_CASE("menu input: d-pad presses are commands, in the order up, down, left, right", "[menu_input]") {
    Pad pad;
    MenuInput input;
    pad.update(sample(coney::pad::kDown));
    CHECK(input.dispatch(pad, msOf(10)) == MenuCommand::Down);
    pad.update(sample(0));
    CHECK_FALSE(input.dispatch(pad, msOf(11)).has_value());
    pad.update(sample(coney::pad::kLeft | coney::pad::kRight));
    // The diagonal rule keeps one direction (right first in pressure order at equal pressure).
    CHECK(input.dispatch(pad, msOf(20)) == MenuCommand::Right);
}

TEST_CASE("menu input: a command needs more than 110 ms since the last one", "[menu_input]") {
    Pad pad;
    MenuInput input;
    pad.update(sample(coney::pad::kUp));
    REQUIRE(input.dispatch(pad, 1000) == MenuCommand::Up);
    pad.update(sample(0));
    pad.update(sample(coney::pad::kUp));
    CHECK_FALSE(input.dispatch(pad, 1110).has_value()); // exactly 110 ms: not more
    pad.update(sample(0));
    pad.update(sample(coney::pad::kUp));
    CHECK(input.dispatch(pad, 1111) == MenuCommand::Up);
}

TEST_CASE("menu input: a held d-pad direction repeats with the pad's auto-repeat", "[menu_input]") {
    Pad pad;
    MenuInput input;
    int commands = 0;
    for (int frame = 0; frame < 30; ++frame) {
        pad.update(sample(coney::pad::kDown));
        if (input.dispatch(pad, msOf(frame)) == MenuCommand::Down) {
            ++commands;
        }
    }
    // The first sample, then the 15th held sample and every 4th after it (frames 14, 18, 22, 26).
    CHECK(commands == 5);
}

TEST_CASE("menu input: accept and back fire when the button is let go", "[menu_input]") {
    Pad pad;
    MenuInput input;
    pad.update(sample(coney::pad::kCross));
    CHECK_FALSE(input.dispatch(pad, 1000).has_value());
    pad.update(sample(0));
    CHECK(input.dispatch(pad, 1033) == MenuCommand::Accept);
    pad.update(sample(coney::pad::kCircle));
    CHECK_FALSE(input.dispatch(pad, 2000).has_value());
    pad.update(sample(0));
    CHECK(input.dispatch(pad, 2033) == MenuCommand::Back);
    pad.update(sample(coney::pad::kTriangle));
    pad.update(sample(0));
    CHECK(input.dispatch(pad, 3000) == MenuCommand::Back);
}

TEST_CASE("menu input: a release too soon after a command is lost", "[menu_input]") {
    Pad pad;
    MenuInput input;
    pad.update(sample(coney::pad::kDown | coney::pad::kCross));
    REQUIRE(input.dispatch(pad, 1000) == MenuCommand::Down);
    pad.update(sample(0));
    CHECK_FALSE(input.dispatch(pad, 1033).has_value());
    pad.update(sample(0));
    CHECK_FALSE(input.dispatch(pad, 1200).has_value());
}

TEST_CASE("menu input: a stick held one way repeats every 400 ms until it returns to neutral", "[menu_input]") {
    Pad pad;
    MenuInput input;
    pad.update(sample(0, coney::pad::kStickCentre, 0)); // pushed fully up
    REQUIRE(input.dispatch(pad, 1000) == MenuCommand::Up);
    CHECK_FALSE(input.dispatch(pad, 1300).has_value());
    CHECK_FALSE(input.dispatch(pad, 1400).has_value()); // exactly 400 ms
    CHECK(input.dispatch(pad, 1401) == MenuCommand::Up);
    // Back to neutral, then pushed again: the short gap applies.
    pad.update(sample(0));
    CHECK_FALSE(input.dispatch(pad, 1450).has_value());
    pad.update(sample(0, coney::pad::kStickCentre, 0));
    CHECK(input.dispatch(pad, 1520) == MenuCommand::Up);
    // Another direction while still pushed: the short gap too.
    pad.update(sample(0, 255, coney::pad::kStickCentre));
    CHECK(input.dispatch(pad, 1640) == MenuCommand::Right);
}

TEST_CASE("menu input: the stick counts past half way only", "[menu_input]") {
    Pad pad;
    MenuInput input;
    pad.update(sample(0, coney::pad::kStickCentre, 60)); // y = 0.37 up: not past half way
    CHECK_FALSE(input.dispatch(pad, 1000).has_value());
    pad.update(sample(0, 20, coney::pad::kStickCentre)); // x = (20 - 95) / 95 = -0.79
    CHECK(input.dispatch(pad, 1000) == MenuCommand::Left);
}

TEST_CASE("menu input: taking focus forgets the last command and blocks the d-pad for 20 ms", "[menu_input]") {
    Pad pad;
    MenuInput input;
    pad.update(sample(coney::pad::kUp));
    REQUIRE(input.dispatch(pad, 1000) == MenuCommand::Up);
    input.focus(1010);
    pad.update(sample(0));
    pad.update(sample(coney::pad::kDown));
    CHECK_FALSE(input.dispatch(pad, 1029).has_value()); // d-pad blocked
    pad.update(sample(0));
    pad.update(sample(coney::pad::kDown));
    // 1030 is only 30 ms after the last command, but focus forgot it.
    CHECK(input.dispatch(pad, 1030) == MenuCommand::Down);
}
