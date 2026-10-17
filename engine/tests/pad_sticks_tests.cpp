// Stick values from synthetic stick bytes (research/pages/pad-input/page.md, Analogue sticks; Right stick and
// camera).

#include "game/pad.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace coney::game;
using Catch::Approx;

namespace {
PlayerSticks left_only(std::uint8_t x, std::uint8_t y) { return convert_sticks({.left = {x, y}, .right = {}}); }
PlayerSticks right_only(std::uint8_t x, std::uint8_t y) { return convert_sticks({.left = {}, .right = {x, y}}); }
} // namespace

TEST_CASE("the stick table at its edges") {
    CHECK(stick_axis_value(0) == -1.0f);
    CHECK(stick_axis_value(1) == Approx(-94.0 / 95.0));
    CHECK(stick_axis_value(94) == Approx(-1.0 / 95.0));
    CHECK(stick_axis_value(95) == 0.0f);
    CHECK(stick_axis_value(128) == 0.0f);
    CHECK(stick_axis_value(160) == 0.0f);
    CHECK(stick_axis_value(161) == Approx(1.0 / 95.0));
    CHECK(stick_axis_value(254) == Approx(94.0 / 95.0));
    CHECK(stick_axis_value(255) == 1.0f);
}

TEST_CASE("the dead zone holds 66 values and is per axis") {
    int zero = 0;
    for (int byte = 0; byte < 256; ++byte) {
        zero += stick_axis_value(static_cast<std::uint8_t>(byte)) == 0.0f ? 1 : 0;
    }
    CHECK(zero == 66);

    // A square dead zone: one axis inside it does not hide the other.
    const auto sticks = left_only(161, 160);
    CHECK(sticks.left.x == Approx(1.0 / 95.0));
    CHECK(sticks.left.y == 0.0f);
}

TEST_CASE("the vertical axes are sign-flipped so up is positive") {
    CHECK(left_only(128, 0).left.y == 1.0f);
    CHECK(left_only(128, 255).left.y == -1.0f);
    CHECK(left_only(0, 128).left.x == -1.0f);
    CHECK(right_only(128, 0).right.y == 1.0f);
    CHECK(right_only(128, 255).right.y == -1.0f);
}

TEST_CASE("the length is capped at 1 and the direction is a unit vector") {
    const auto corner = left_only(255, 0);
    CHECK(corner.left.x == 1.0f);
    CHECK(corner.left.y == 1.0f);
    CHECK(corner.left.length == 1.0f);
    CHECK(corner.left.direction_x == Approx(0.70710678));
    CHECK(corner.left.direction_y == Approx(0.70710678));

    const auto part = left_only(160 + 38, 128); // 0.4 to the right
    CHECK(part.left.length == Approx(0.4));
    CHECK(part.left.direction_x == 1.0f);
    CHECK(part.left.direction_y == 0.0f);

    const auto centred = left_only(128, 128);
    CHECK(centred.left.length == 0.0f);
    CHECK(centred.left.direction_x == 0.0f);
    CHECK(centred.left.direction_y == 0.0f);
}

TEST_CASE("the right stick has its own, wider dead zone") {
    CHECK(right_only(64, 128).right.x == Approx(stick_axis_value(64)));
    CHECK(right_only(65, 128).right.x == 0.0f);
    CHECK(right_only(175, 128).right.x == 0.0f);
    CHECK(right_only(176, 128).right.x == Approx(stick_axis_value(176)));

    CHECK(right_only(128, 8).right.y == Approx(-stick_axis_value(8)));
    CHECK(right_only(128, 9).right.y == 0.0f);
    CHECK(right_only(128, 231).right.y == 0.0f);
    CHECK(right_only(128, 232).right.y == Approx(-stick_axis_value(232)));

    // The left stick keeps the narrow one.
    CHECK(left_only(65, 128).left.x == Approx(stick_axis_value(65)));
    CHECK(left_only(65, 128).left.x != 0.0f);
}
