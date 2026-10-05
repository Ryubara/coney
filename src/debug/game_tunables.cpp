// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/game_tunables.h"

#include "camera/follow_camera.h"
#include "human/locomotion.h"

namespace coney::debug {

void registerGameTunables(TunableRegistry& registry) {
    // The player's locomotion: read every update.
    human::LocomotionTuning& move = human::locomotionTuning();
    registry.add("Movement", "Stick dead zone", &move.stickDeadZone)
        .range(0, 0.9, 0.01)
        .describe("Stick length at or below which the player stands");
    registry.add("Movement", "Run threshold", &move.runThreshold)
        .range(0.1, 1, 0.01)
        .describe("Stick length above which the player runs instead of walking");
    registry.add("Movement", "Acceleration", &move.acceleration)
        .range(0, 120, 1)
        .units("m/s2")
        .describe("Speed gained each second while speeding up (0.8 m/s an update)");
    registry.add("Movement", "Turn walking", &move.walkTurnDegrees).range(0, 45, 0.5).units("deg/step");
    registry.add("Movement", "Turn jogging", &move.jogTurnDegrees).range(0, 45, 0.5).units("deg/step");
    registry.add("Movement", "Turn running", &move.runTurnDegrees).range(0, 45, 0.5).units("deg/step");
    registry.add("Movement", "Turn sprinting", &move.sprintTurnDegrees).range(0, 45, 0.5).units("deg/step");

    // The follow camera: the lag and collision are read every update; the band and pitch when a camera is placed.
    camera::FollowTuning& follow = camera::followTuning();
    registry.add("Follow camera", "Position lag", &follow.positionLag)
        .range(0.01, 1, 0.01)
        .describe("Share of the wanted move the camera covers each update");
    registry.add("Follow camera", "Collision margin", &follow.collisionMargin).range(0, 2, 0.05).units("m");
    registry.add("Follow camera", "Closest after collision", &follow.minCollisionDistance).range(0, 5, 0.1).units("m");
    camera::FollowSettings& placed = camera::followDefaults();
    registry.add("Follow camera", "Leash near", &placed.leashNear)
        .range(0.5, 20, 0.1)
        .units("m")
        .describe("Near edge of the distance band; applies when the camera is next placed");
    registry.add("Follow camera", "Leash far", &placed.leashFar)
        .range(0.5, 20, 0.1)
        .units("m")
        .describe("Far edge of the distance band; applies when the camera is next placed");
    registry.add("Follow camera", "Pitch", &placed.pitchDegrees)
        .range(-20, 60, 1)
        .units("deg")
        .describe("Target pitch; applies when the camera is next placed");
    registry.add("Follow camera", "Look-at height", &placed.lookAtHeight)
        .range(0, 3, 0.05)
        .units("m")
        .describe("Look-at point above the player's feet; applies when the camera is next placed");
}

} // namespace coney::debug
