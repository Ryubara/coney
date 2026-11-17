// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "core/pads.h"

using Catch::Approx;
using coney::FollowCameraFacts;
using coney::Pads;
using coney::PadSample;
using coney::PlayerCameraFacts;
using coney::PortSamples;
using coney::StickTurnCameras;

namespace {

// The quaternion (x, y, z, w) the original kept at 0x005ddc90 in mission 1's yard, run r-20261115-pads-stick-frame.
constexpr std::array<float, 4> kYardCamera{-0.048316F, 0.017879F, -0.346578F, 0.936605F};
// A camera yawed 90 degrees about the vertical: its x axis is the world's +y, its y axis the world's -x.
constexpr std::array<float, 4> kYawed90{0.0F, 0.0F, 0.70710678F, 0.70710678F};

// Port samples with port 1's left stick at raw bytes (x, y) (217 and 57 are 0.6 and 0.4 up) and port 2 at rest.
PortSamples leftStick(std::uint8_t x, std::uint8_t y) {
    PortSamples samples;
    for (PadSample& sample : samples) {
        sample.connected = true;
    }
    samples[0].sticks[2] = x;
    samples[0].sticks[3] = y;
    return samples;
}

// The game's side of the turn, set by each test: which player each record belongs to, each player's current camera
// and follow camera, and the follow cameras marked by a stick at rest.
class FakeCameras final : public StickTurnCameras {
  public:
    std::map<std::size_t, std::size_t> players;       // record -> player
    std::map<std::size_t, PlayerCameraFacts> cameras; // player -> his current camera
    std::map<std::size_t, FollowCameraFacts> follows; // player -> his follow camera
    bool viewsDiffer = false;                         // 0x0050b198 != 0x0050b19c
    std::vector<std::size_t> rested;                  // players whose follow camera a stick at rest marked

    [[nodiscard]] std::optional<std::size_t> playerOfRecord(std::size_t record) const override {
        const auto found = players.find(record);
        return found == players.end() ? std::nullopt : std::optional<std::size_t>(found->second);
    }
    [[nodiscard]] bool viewsDifferFromPlayers() const override { return viewsDiffer; }
    [[nodiscard]] std::optional<PlayerCameraFacts> playerCamera(std::size_t player) const override {
        const auto found = cameras.find(player);
        return found == cameras.end() ? std::nullopt : std::optional<PlayerCameraFacts>(found->second);
    }
    [[nodiscard]] std::optional<FollowCameraFacts> follow(std::size_t player) const override {
        const auto found = follows.find(player);
        return found == follows.end() ? std::nullopt : std::optional<FollowCameraFacts>(found->second);
    }
    void stickAtRest(std::size_t player) override { rested.push_back(player); }
};

// Puts player 0 on record 0, his follow camera current and his base camera, taking its orientation afresh, facing
// `orientation`.
void facePlayerZero(FakeCameras& cameras, const std::array<float, 4>& orientation) {
    cameras.players[0] = 0;
    cameras.cameras[0] = PlayerCameraFacts{.orientation = orientation, .slot = 0};
    cameras.follows[0] = FollowCameraFacts{
        .isBaseCamera = true, .takesFrameAfresh = true, .isPlayerCamera = true, .lookingBehind = false};
}

} // namespace

TEST_CASE("outside play a pad belongs to no player and its stick is turned by the identity", "[pad]") {
    // Expected: input.md#turned-stick, confirmed (code)
    // Pads_Update hands Pad_Update the identity (0x005117a0) for a pad whose +0x42 is -1.
    Pads pads;
    pads.update(leftStick(217, 57));
    CHECK(pads.port(0).turnedX() == Approx(0.6F));
    CHECK(pads.port(0).turnedY() == Approx(0.4F));
    CHECK_FALSE(pads.port(0).player().has_value());
}

TEST_CASE("in play a player's stick is turned by his camera's orientation, which is kept for him", "[pad]") {
    // Expected: input.md#turned-stick, confirmed (runtime)
    // Run r-20261115-pads-stick-frame: the stick (0.6, 0.4) under the kept quaternion read (0.7156380, -0.0886695).
    Pads pads;
    FakeCameras cameras;
    facePlayerZero(cameras, kYardCamera);
    pads.update(leftStick(217, 57));
    pads.turnSticks(cameras);
    CHECK(pads.port(0).turnedX() == Approx(0.7156380F).margin(1e-5));
    CHECK(pads.port(0).turnedY() == Approx(-0.0886695F).margin(1e-5));
    CHECK(pads.port(0).player() == std::optional<std::size_t>(0));
    CHECK(pads.keptOrientation(0) == kYardCamera);
    CHECK(cameras.rested.empty());
}

TEST_CASE("while the follow camera is the base camera and keeps its frame, the kept quaternion turns the stick",
          "[pad]") {
    // Expected: input.md#turned-stick, confirmed (runtime)
    // Run r-20261115-pads-stick-frame: with +0x473 written 0 the kept quaternion and the turned stick stayed the same
    // for four reads while the camera turned on.
    Pads pads;
    FakeCameras cameras;
    facePlayerZero(cameras, kYardCamera);
    pads.update(leftStick(217, 57));
    pads.turnSticks(cameras);
    cameras.follows[0].takesFrameAfresh = false;
    cameras.cameras[0].orientation = kYawed90;
    pads.update(leftStick(217, 57));
    pads.turnSticks(cameras);
    CHECK(pads.port(0).turnedX() == Approx(0.7156380F).margin(1e-5));
    CHECK(pads.port(0).turnedY() == Approx(-0.0886695F).margin(1e-5));
    CHECK(pads.keptOrientation(0) == kYardCamera);

    // Not his base camera (a locked camera returned to), the camera's orientation is taken afresh all the same.
    cameras.follows[0].isBaseCamera = false;
    pads.update(leftStick(217, 57));
    pads.turnSticks(cameras);
    // Stick right 0.6 and up 0.4 under a camera yawed 90 degrees: x along the world's +y, y along its -x.
    CHECK(pads.port(0).turnedX() == Approx(-0.4F).margin(1e-5));
    CHECK(pads.port(0).turnedY() == Approx(0.6F).margin(1e-5));
    CHECK(pads.keptOrientation(0) == kYawed90);
}

TEST_CASE("when the views shown differ from the player cameras every pad takes player 0's camera", "[pad]") {
    // Expected: input.md#turned-stick, confirmed (code)
    // 0x0050b198 != 0x0050b19c: the camera is Camera_GetPlayerCamera(0) for every pad, never the identity, and the
    // orientation goes into the slot that camera names (its vtable +0x27c).
    Pads pads;
    FakeCameras cameras;
    cameras.viewsDiffer = true;
    cameras.cameras[0] = PlayerCameraFacts{.orientation = kYawed90, .slot = 1};
    PortSamples samples = leftStick(128, 128);
    samples[1].sticks[2] = 217; // port 2's stick right 0.6; record 4 belongs to no player
    pads.update(samples);
    pads.turnSticks(cameras);
    CHECK(pads.port(1).turnedX() == Approx(0.0F).margin(1e-5));
    CHECK(pads.port(1).turnedY() == Approx(0.6F).margin(1e-5));
    CHECK(pads.keptOrientation(1) == kYawed90);
    CHECK(pads.keptOrientation(0) == std::array<float, 4>{});
}

TEST_CASE("a stick at rest marks its player's follow camera to take its orientation afresh", "[pad]") {
    // Expected: input.md#turned-stick, confirmed (runtime)
    // Run r-20261115-pads-stick-frame: +0x473 written 0 with the stick held read 1 again once it was let go.
    Pads pads;
    FakeCameras cameras;
    facePlayerZero(cameras, kYardCamera);
    pads.update(leftStick(128, 128));
    pads.turnSticks(cameras);
    CHECK(pads.port(0).turnedX() == 0.0F);
    CHECK(pads.port(0).turnedY() == 0.0F);
    CHECK(cameras.rested == std::vector<std::size_t>{0});

    // A player with no follow camera has nothing to mark.
    cameras.follows.clear();
    cameras.rested.clear();
    pads.update(leftStick(128, 128));
    pads.turnSticks(cameras);
    CHECK(cameras.rested.empty());
}
