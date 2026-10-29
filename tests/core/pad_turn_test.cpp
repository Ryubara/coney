// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdint>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"

using Catch::Approx;
using coney::Pad;
using coney::PadSample;
using coney::StickTurn;
namespace pad = coney::pad;

namespace {

// A connected sample with the left stick at raw bytes (x, y); 0 is left or up, 255 right or down.
PadSample leftStick(std::uint8_t x, std::uint8_t y) {
    PadSample sample;
    sample.connected = true;
    sample.sticks[2] = x;
    sample.sticks[3] = y;
    return sample;
}

// A turn of the camera's x and y axes by `degrees` about the vertical, the player's follow camera his current one.
StickTurn yawed(float degrees) {
    const float radians = degrees * 3.14159265F / 180.0F;
    StickTurn turn;
    turn.xAxis = {std::cos(radians), std::sin(radians)};
    turn.yAxis = {-std::sin(radians), std::cos(radians)};
    turn.hasFollow = true;
    turn.followIsPlayerCamera = true;
    return turn;
}

} // namespace

TEST_CASE("the left stick is turned by the camera's axes as the original turned it in the street", "[pad]") {
    // Expected: input.md#turned-stick, confirmed (runtime)
    // Run r-20261029-pad-turned-stick: the stick (0.6, 0.4) under rows 0 and 1 of the kept quaternion read
    // (0.6439977, -0.3244487) from the record's +0x00, +0x04. Bytes 217 and 57 are 0.6 and 0.4 through the table.
    Pad record;
    StickTurn turn;
    turn.xAxis = {0.5046696725F, -0.8633125015F};
    turn.yAxis = {0.8394021334F, 0.4906923054F};
    turn.hasFollow = true;
    turn.followIsPlayerCamera = true;
    record.update(leftStick(217, 57), turn);
    CHECK(record.leftX() == Approx(0.6F));
    CHECK(record.leftY() == Approx(0.4F));
    CHECK(record.turnedX() == Approx(0.6439977F).margin(1e-5));
    CHECK(record.turnedY() == Approx(-0.3244487F).margin(1e-5));
    CHECK_FALSE(record.followStickRest());
}

TEST_CASE("a stick pushed right under a camera yawed 90 degrees turns to the camera's x axis", "[pad]") {
    // Expected: input.md#turned-stick, confirmed (code)
    // t = x * row 0 + y * row 1 with (x, y) = (1, 0) and row 0 = (0, 1).
    Pad record;
    record.update(leftStick(255, pad::kStickCentre), yawed(90.0F));
    CHECK(record.turnedX() == Approx(0.0F).margin(1e-6));
    CHECK(record.turnedY() == Approx(1.0F));
}

TEST_CASE("the turned stick keeps the stick's length when the camera's axes are tilted", "[pad]") {
    // Expected: input.md#turned-stick, confirmed (code)
    // Axes pitched so that their x, y parts are half long: t = (0.5, 0) for the stick (1, 0), scaled back to 1.
    Pad record;
    StickTurn turn;
    turn.xAxis = {0.5F, 0.0F};
    turn.yAxis = {0.0F, 0.5F};
    record.update(leftStick(255, pad::kStickCentre), turn);
    CHECK(record.turnedX() == Approx(1.0F));
    CHECK(record.turnedY() == Approx(0.0F).margin(1e-6));
}

TEST_CASE("a turn whose axes have no x or y part writes the unscaled zero", "[pad]") {
    // Expected: input.md#turned-stick, confirmed (code)
    // t has length 0, so it is written as it is, not scaled (and replaces the last turned stick).
    Pad record;
    record.update(leftStick(255, 0), yawed(0.0F));
    CHECK(record.turnedX() == Approx(1.0F));
    StickTurn turn;
    turn.xAxis = {0.0F, 0.0F};
    turn.yAxis = {0.0F, 0.0F};
    record.update(leftStick(255, 0), turn);
    CHECK(record.turnedX() == 0.0F);
    CHECK(record.turnedY() == 0.0F);
}

TEST_CASE("the stick is reversed while the player's own follow camera looks behind", "[pad]") {
    // Expected: input.md#turned-stick, confirmed (code)
    Pad record;
    StickTurn turn = yawed(0.0F);
    turn.lookingBehind = true;
    record.update(leftStick(pad::kStickCentre, 0), turn); // stick up
    CHECK(record.turnedX() == Approx(0.0F).margin(1e-6));
    CHECK(record.turnedY() == Approx(-1.0F));
    // Looking behind on a follow camera that is not the player's current camera does not reverse it.
    turn.followIsPlayerCamera = false;
    record.update(leftStick(pad::kStickCentre, 0), turn);
    CHECK(record.turnedY() == Approx(1.0F));
}

TEST_CASE("a stick at rest is written unturned and marks the player's follow camera", "[pad]") {
    // Expected: input.md#turned-stick, confirmed (code)
    Pad record;
    record.update(leftStick(pad::kStickCentre, pad::kStickCentre), yawed(30.0F));
    CHECK(record.turnedX() == 0.0F);
    CHECK(record.turnedY() == 0.0F);
    CHECK(record.followStickRest());
    // Moving again: no mark this update.
    record.update(leftStick(255, pad::kStickCentre), yawed(30.0F));
    CHECK_FALSE(record.followStickRest());
    // At rest with no follow camera (no player): nothing to mark.
    StickTurn noFollow = yawed(30.0F);
    noFollow.hasFollow = false;
    noFollow.followIsPlayerCamera = false;
    record.update(leftStick(pad::kStickCentre, pad::kStickCentre), noFollow);
    CHECK_FALSE(record.followStickRest());
}

TEST_CASE("an update with no turn keeps the last turned stick", "[pad]") {
    // Expected: input.md#turned-stick, confirmed (code)
    Pad record;
    record.update(leftStick(255, pad::kStickCentre), yawed(0.0F));
    CHECK(record.turnedX() == Approx(1.0F));
    record.update(leftStick(0, pad::kStickCentre)); // stick left, no matrix
    CHECK(record.leftX() == -1.0F);
    CHECK(record.turnedX() == Approx(1.0F));
    CHECK_FALSE(record.followStickRest());
}
