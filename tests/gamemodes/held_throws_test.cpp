// SPDX-License-Identifier: GPL-3.0-or-later
// When a throw lets go of what a human holds (gamemodes/held_throws.h): the player's and an AI human's alike, from the
// release event of one of the throw clips with something in hand (docs/research/combat-throws.md#armed-special).
// Clip ids only; nothing here comes from the game.
#include "gamemodes/held_throws.h"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "combat/anim_ids.h"
#include "world_objects/object_services.h"

using coney::isHeldThrowClip;
using coney::releasesHeldObject;
namespace anim_id = coney::combat::anim_id;

// Expected: docs/research/combat-throws.md#armed-special, confirmed (code) (the throw clips of a held object)
TEST_CASE("the held-object throw clips are the one-handed, barrel and ghetto throws, the knife's and the swingable's",
          "[gamemodes][throws]") {
    for (const int clip : {anim_id::kOneHandedThrow, anim_id::kOneHandedThrowFromWalk, anim_id::kOneHandedThrowFromRun,
                           anim_id::kBarrelThrow, anim_id::kBarrelThrowFromWalk, anim_id::kBarrelThrowFromRun,
                           anim_id::kGhettoThrow, anim_id::kGhettoThrowFromWalk, anim_id::kGhettoThrowFromRun,
                           anim_id::kKnifeThrow, anim_id::kSwingableThrow}) {
        CAPTURE(clip);
        CHECK(isHeldThrowClip(static_cast<std::uint32_t>(clip)));
    }
    // An idle, an attack or any other clip throws nothing.
    CHECK_FALSE(isHeldThrowClip(0));
    CHECK_FALSE(isHeldThrowClip(11));
    CHECK_FALSE(isHeldThrowClip(static_cast<std::uint32_t>(anim_id::kSwingableThrow) + 100));
}

// Expected: docs/research/objects.md#held, inferred from Human_ReleaseThrow's one caller (`0x002586d8`, event 10)
TEST_CASE("a throw lets go of the held object only at a release event of a throw clip with something in hand",
          "[gamemodes][throws]") {
    const auto clip = static_cast<std::uint32_t>(anim_id::kBarrelThrow);
    CHECK(releasesHeldObject(true, clip, 12.0));
    // No release event passed yet.
    CHECK_FALSE(releasesHeldObject(false, clip, 12.0));
    // A release event in a clip that is no throw (a swing's).
    CHECK_FALSE(releasesHeldObject(true, 11, 12.0));
    // An empty hand.
    CHECK_FALSE(releasesHeldObject(true, clip, coney::world_objects::kNoObject));
}
