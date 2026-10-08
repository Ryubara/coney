// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/input_script.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "core/error.h"
#include "core/pad.h"
#include "core/pads.h"

using coney::ErrorCode;
using coney::InputEvent;
using coney::parseInputScript;
using coney::ScriptedInput;
namespace pad = coney::pad;

namespace {

// Parses `text`, which the test expects to be valid.
std::vector<InputEvent> parsed(std::string_view text) {
    auto events = parseInputScript(text);
    REQUIRE(events.has_value());
    return *events;
}

// The error message for `text`, which the test expects to be refused as invalid.
std::string refusal(std::string_view text) {
    auto events = parseInputScript(text);
    REQUIRE_FALSE(events.has_value());
    CHECK(events.error().code == ErrorCode::Invalid);
    return events.error().message;
}

} // namespace

TEST_CASE("an input script parses frames, ports, actions and buttons, skipping comments and blank lines",
          "[input_script]") {
    const auto events = parsed("# a comment\n"
                               "\n"
                               "150 tap start   # trailing comment\r\n"
                               "160 p2 press cross circle\n"
                               "170\tp1 release cross\n"
                               "200 stick right -100 50\n"
                               "210 p2 disconnect\n"
                               "210 connect");
    REQUIRE(events.size() == 6);
    CHECK(events[0].frame == 150);
    CHECK(events[0].port == 0);
    CHECK(events[0].action == InputEvent::Action::Tap);
    CHECK(events[0].buttons == pad::kStart);
    CHECK(events[1].port == 1);
    CHECK(events[1].buttons == (pad::kCross | pad::kCircle));
    CHECK(events[2].action == InputEvent::Action::Release);
    CHECK(events[3].action == InputEvent::Action::Stick);
    CHECK(events[3].stick == 1);
    CHECK(events[3].x == -100);
    CHECK(events[3].y == 50);
    CHECK(events[4].action == InputEvent::Action::Disconnect);
    CHECK(events[5].action == InputEvent::Action::Connect);
}

TEST_CASE("an empty input script is valid and holds nothing", "[input_script]") {
    CHECK(parsed("").empty());
    CHECK(parsed("# only a comment\n\n").empty());
}

TEST_CASE("an input script refuses bad lines and names the line", "[input_script]") {
    using Catch::Matchers::ContainsSubstring;
    CHECK_THAT(refusal("10 tap start\nten tap start\n"), ContainsSubstring("line 2"));
    CHECK_THAT(refusal("-1 tap start"), ContainsSubstring("frame number"));
    CHECK_THAT(refusal("10"), ContainsSubstring("action"));
    CHECK_THAT(refusal("10 p3 tap start"), ContainsSubstring("unknown action"));
    CHECK_THAT(refusal("10 jump"), ContainsSubstring("unknown action"));
    CHECK_THAT(refusal("10 press"), ContainsSubstring("button"));
    CHECK_THAT(refusal("10 press x"), ContainsSubstring("unknown button \"x\""));
    CHECK_THAT(refusal("10 stick left 0"), ContainsSubstring("stick"));
    CHECK_THAT(refusal("10 stick middle 0 0"), ContainsSubstring("stick"));
    CHECK_THAT(refusal("10 stick left 101 0"), ContainsSubstring("-100 to 100"));
    CHECK_THAT(refusal("10 stick left 1.5 0"), ContainsSubstring("-100 to 100"));
    CHECK_THAT(refusal("10 connect now"), ContainsSubstring("no arguments"));
    CHECK_THAT(refusal("10 steer"), ContainsSubstring("at least one X,Y point"));
    CHECK_THAT(refusal("10 steer 40"), ContainsSubstring("at least one X,Y point"));
    CHECK_THAT(refusal("10 steer 0 1,2"), ContainsSubstring("1 to 100"));
    CHECK_THAT(refusal("10 steer 1,2 3"), ContainsSubstring("not an X,Y point"));
    CHECK_THAT(refusal("20 tap start\n10 tap start"), ContainsSubstring("frame 10 comes after frame 20"));
}

TEST_CASE("script stick percentages map onto raw bytes that read back as the same deflection", "[input_script]") {
    CHECK(coney::stickByteFromPercent(-100) == 0);
    CHECK(coney::stickByteFromPercent(0) == pad::kStickCentre);
    CHECK(coney::stickByteFromPercent(100) == 255);
    CHECK(coney::stickByteFromPercent(1) == pad::kStickDeadHigh + 1);
    CHECK(coney::stickByteFromPercent(-1) == pad::kStickDeadLow - 1);
    for (int percent = -100; percent <= 100; ++percent) {
        const float value = pad::stickValue(coney::stickByteFromPercent(percent));
        INFO("percent " << percent);
        CHECK(value * 100.0F - static_cast<float>(percent) < 1.0F);
        CHECK(static_cast<float>(percent) - value * 100.0F < 1.0F);
    }
}

TEST_CASE("a scripted input starts with port 1 connected, port 2 not, nothing held", "[input_script]") {
    ScriptedInput input({});
    const coney::PortSamples samples = input.sample(0);
    CHECK(samples[0].connected);
    CHECK_FALSE(samples[1].connected);
    CHECK(samples[0].buttons == 0);
    CHECK(samples[0].sticks[3] == pad::kStickCentre);
}

TEST_CASE("a scripted input holds presses until released and taps for one frame", "[input_script]") {
    ScriptedInput input(parsed("2 press down\n"
                               "3 tap cross\n"
                               "5 release down\n"
                               "6 tap start\n"
                               "7 tap start\n"));
    const std::uint16_t expected[] = {0,           0,           pad::kDown, pad::kDown | pad::kCross, pad::kDown, 0,
                                      pad::kStart, pad::kStart, 0};
    for (std::uint64_t frame = 0; frame < std::size(expected); ++frame) {
        INFO("frame " << frame);
        CHECK(input.sample(frame)[0].buttons == expected[frame]);
    }
}

TEST_CASE("a scripted input gives held buttons full pressure and moves sticks with y up", "[input_script]") {
    ScriptedInput input(parsed("0 press up cross start\n"
                               "0 stick left 100 100\n"
                               "1 p2 connect\n"
                               "1 p2 stick right -100 -100\n"));
    const coney::PortSamples first = input.sample(0);
    CHECK(first[0].pressure.at(static_cast<std::size_t>(pad::Pressure::Up)) == 255);
    CHECK(first[0].pressure.at(static_cast<std::size_t>(pad::Pressure::Cross)) == 255);
    CHECK(first[0].pressure.at(static_cast<std::size_t>(pad::Pressure::Down)) == 0);
    CHECK(first[0].sticks[2] == 255); // left x: full right
    CHECK(first[0].sticks[3] == 0);   // left y: full up is the byte's 0
    CHECK_FALSE(first[1].connected);

    const coney::PortSamples second = input.sample(1);
    CHECK(second[1].connected);
    CHECK(second[1].sticks[0] == 0);   // right x: full left
    CHECK(second[1].sticks[1] == 255); // right y: full down
}

TEST_CASE("a scripted input applies the lines of frames it was not asked for, in order", "[input_script]") {
    ScriptedInput input(parsed("1 press cross\n"
                               "2 release cross\n"
                               "3 press circle\n"));
    CHECK(input.sample(0)[0].buttons == 0);
    CHECK(input.sample(10)[0].buttons == pad::kCircle);
}

TEST_CASE("an input script that does not exist is reported as not found", "[input_script]") {
    auto events = coney::loadInputScript("no-such-dir/no-such-script.txt");
    REQUIRE_FALSE(events.has_value());
    CHECK(events.error().code == ErrorCode::NotFound);
}

TEST_CASE("a steer line parses its reach and route points", "[input_script]") {
    const std::vector<InputEvent> events = parsed("5 steer 1.5,-2 3,4\n6 steer 40 -61.25,-80\n");
    REQUIRE(events.size() == 2);
    CHECK(events[0].action == InputEvent::Action::Steer);
    CHECK(events[0].x == 100);
    REQUIRE(events[0].route.size() == 2);
    CHECK(events[0].route[0][0] == 1.5F);
    CHECK(events[0].route[0][1] == -2.0F);
    CHECK(events[1].x == 40);
    REQUIRE(events[1].route.size() == 1);
    CHECK(events[1].route[0][0] == -61.25F);
}

TEST_CASE("the steer stick turns the target's bearing into the camera's frame", "[input_script]") {
    using Stick = std::array<int, 2>;
    // Camera heading 0 faces +y: a target ahead is stick up, one at +x is stick right.
    const coney::SteerView north{.x = 10.0F, .y = 10.0F, .cameraHeading = 0.0F};
    CHECK(coney::steerStick(north, 10.0F, 20.0F, 50) == Stick{0, 50});
    CHECK(coney::steerStick(north, 20.0F, 10.0F, 50) == Stick{50, 0});
    CHECK(coney::steerStick(north, 0.0F, 10.0F, 50) == Stick{-50, 0});
    CHECK(coney::steerStick(north, 10.0F, 0.0F, 50) == Stick{0, -50});
    // Camera heading 90 faces -x: a target at -x is straight ahead, one at +y is to the right.
    const coney::SteerView west{.x = 0.0F, .y = 0.0F, .cameraHeading = 90.0F};
    CHECK(coney::steerStick(west, -5.0F, 0.0F, 70) == Stick{0, 70});
    CHECK(coney::steerStick(west, 0.0F, 5.0F, 70) == Stick{70, 0});
    // Halfway between: both axes at cos 45 of the reach.
    CHECK(coney::steerStick(north, 15.0F, 15.0F, 100) == Stick{71, 71});
}

TEST_CASE("a steering port follows its route and centres the stick at the end", "[input_script]") {
    ScriptedInput input(parsed("0 steer 60 0,10 10,10\n"));
    coney::SteerView view{.x = 0.0F, .y = 0.0F, .cameraHeading = 0.0F};
    input.setSteerSource([&view](std::size_t port) -> std::optional<coney::SteerView> {
        if (port != 0) {
            return std::nullopt;
        }
        return view;
    });
    // Heading for the first point: stick up at 60 %.
    coney::PortSamples samples = input.sample(0);
    CHECK(samples[0].sticks[2] == coney::stickByteFromPercent(0));
    CHECK(samples[0].sticks[3] == coney::stickByteFromPercent(-60));
    // Within a metre of it: on to the second, to the right.
    view = {.x = 0.2F, .y = 9.5F, .cameraHeading = 0.0F};
    samples = input.sample(1);
    CHECK(samples[0].sticks[2] > coney::stickByteFromPercent(55));
    // At the last point the route ends and the stick centres.
    view = {.x = 9.6F, .y = 10.1F, .cameraHeading = 0.0F};
    samples = input.sample(2);
    CHECK(samples[0].sticks[2] == pad::kStickCentre);
    CHECK(samples[0].sticks[3] == pad::kStickCentre);
}

TEST_CASE("a steering port centres without a view and stops at a left stick line", "[input_script]") {
    ScriptedInput input(parsed("0 steer 0,10\n2 stick left 30 0\n"));
    CHECK(input.sample(0)[0].sticks[3] == pad::kStickCentre);
    input.setSteerSource([](std::size_t) -> std::optional<coney::SteerView> { return coney::SteerView{}; });
    CHECK(input.sample(1)[0].sticks[3] == coney::stickByteFromPercent(-100));
    const coney::PortSamples samples = input.sample(2);
    CHECK(samples[0].sticks[2] == coney::stickByteFromPercent(30));
    CHECK(samples[0].sticks[3] == pad::kStickCentre);
    CHECK(input.sample(3)[0].sticks[2] == coney::stickByteFromPercent(30));
}
