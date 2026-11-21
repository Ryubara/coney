// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "combat/anim_ids.h"
#include "world_objects/object_services.h"

// Which clips throw what a human holds, and when a throw lets go of it: the player's and an AI human's alike
// (PlayLevelSimulation::stepPickups(), stepAiThrows()). Research: docs/research/objects.md#held,
// docs/research/combat-throws.md#armed-special

namespace coney {

/// Whether `animId` is one of the held-object throws, whose release event lets go of what is held: the set 4-6
/// objects and the melee weapons' (491, 502; docs/research/combat-throws.md#armed-special).
/// @coney-only Coney's list of the throw clips, standing for the release handler's held-flag test (`0x1000010`)
[[nodiscard]] inline bool isHeldThrowClip(std::uint32_t animId) {
    constexpr std::array<int, 11> kThrows{
        combat::anim_id::kOneHandedThrow,        combat::anim_id::kOneHandedThrowFromWalk,
        combat::anim_id::kOneHandedThrowFromRun, combat::anim_id::kBarrelThrow,
        combat::anim_id::kBarrelThrowFromWalk,   combat::anim_id::kBarrelThrowFromRun,
        combat::anim_id::kGhettoThrow,           combat::anim_id::kGhettoThrowFromWalk,
        combat::anim_id::kGhettoThrowFromRun,    combat::anim_id::kKnifeThrow,
        combat::anim_id::kSwingableThrow};
    return std::ranges::find(kThrows, static_cast<int>(animId)) != kThrows.end();
}

/// Whether a human lets go of `held` this step: a release event passed (`releaseEvent`, Human::takeThrowRelease()) in
/// the clip `animId` that throws, and something is in his hand (Human_ReleaseThrow on event 10).
/// @orig 0x00245920 Human_HandleMessage (Human.cpp), the event-10 test that calls Human_ReleaseThrow; the clip list is
/// a stand-in (the original tests the human's own state), and the test is kept apart so a test can ask it
[[nodiscard]] inline bool releasesHeldObject(bool releaseEvent, std::uint32_t animId, double held) {
    return releaseEvent && isHeldThrowClip(animId) && held != world_objects::kNoObject;
}

} // namespace coney
