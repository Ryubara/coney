// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/skeleton.h"
#include "combat/anim_ids.h"
#include "combat/commands.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/humans.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// A free attack's hit has no fixed update: it lands on the first update one of the attacker's strike shapes touches
// the victim's spine or head, and misses when none does (docs/research/combat-moves.md#timing). Synthetic clips and
// skeletons: S1's clip switches every shape on at frame 8 and off at 11; the attacker's hands stand `handReach`
// metres ahead of its feet.

using coney::human::Human;
using coney::human::Humans;
namespace id = coney::combat::anim_id;
namespace command = coney::combat::command;

namespace {

// The fight's clips, S1 (12) switching every strike shape on at frame 8 and off at frame 11.
std::vector<coney::test::LocomotionClip> strikingClips() {
    std::vector<coney::test::LocomotionClip> clips = coney::test::fightClips();
    for (coney::test::LocomotionClip& clip : clips) {
        if (clip.id == static_cast<std::uint32_t>(id::kAttackS1)) {
            clip.markers.push_back({8, 0x13});
            clip.markers.push_back({11, 0x14});
        }
    }
    return clips;
}

// A skeleton whose bones all stand at the feet but the hands (19 left, 25 right), `handReach` metres ahead.
coney::anim::Skeleton reachingSkeleton(float handReach) {
    coney::anim::Skeleton skeleton;
    skeleton.offsets.at(19) = coney::anim::Vec3{0.0F, handReach, 0.0F};
    skeleton.offsets.at(25) = coney::anim::Vec3{0.0F, handReach, 0.0F};
    return skeleton;
}

// An attacker (driven by the brains' hook) and a victim `apart` metres ahead of it, facing each other, both with
// strike shapes.
struct Strike {
    explicit Strike(float apart, float handReach) : skeleton(reachingSkeleton(handReach)) {
        attacker.spawn(mesh.get(), coney::anim::Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
        victim.spawn(mesh.get(), coney::anim::Vec3{40.0F, 40.0F + apart, 0.0F}, std::numbers::pi_v<float>);
        attacker.setSkeleton(&skeleton);
        victim.setSkeleton(&skeleton);
        humans.add(victim, true);
        humans.add(attacker, false);
    }

    // Steps until square has played out, the brain pressing it on step 2; returns the first update the attacker's
    // shapes were on and the update the victim was hit, each when it happened (from the press).
    void run() {
        std::uint64_t step = 0;
        humans.setBrains([&step](std::span<Human* const> all) {
            if (step == 2) {
                all[1]->record().command = command::kSquarePressed;
            }
            ++step;
        });
        for (int k = 0; k < 40; ++k) {
            humans.update(mesh.get());
            const int since = k - 2;
            if (!shapesOn && attacker.strikeShapes().anyOn()) {
                shapesOn = since;
            }
            if (!hit && attacker.fighter().hitsLanded() > 0) {
                hit = since;
            }
        }
    }

    coney::test::FightCharacter character{strikingClips()};
    std::unique_ptr<coney::raycast::CollisionMesh> mesh =
        coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    coney::anim::Skeleton skeleton;
    Human victim{character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                 &character.ranges};
    Human attacker{character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                   &character.ranges};
    Humans humans;
    std::optional<int> shapesOn;
    std::optional<int> hit;
};

} // namespace

TEST_CASE("a free attack hits on the update its strike shapes first touch the victim, not at a fixed update",
          "[human][combat][strike]") {
    Strike strike(1.0F, 0.85F);
    strike.run();
    REQUIRE(strike.shapesOn.has_value());
    REQUIRE(strike.hit.has_value());
    // The shapes come on at the clip's frame 8, well after the measured S1 contact of 2 updates the old schedule used.
    CHECK(*strike.shapesOn > 2);
    CHECK(*strike.hit == *strike.shapesOn);
    CHECK(strike.attacker.fighter().hitsLanded() == 1);
    CHECK(strike.attacker.fighter().damageDealt() == 17);
    CHECK(strike.victim.health().value() < strike.victim.health().maximum());
}

TEST_CASE("an attack whose shapes never reach the victim misses, though it stands within the attack's reach",
          "[human][combat][strike]") {
    // Hands only 0.2 m ahead: the victim 1 m away, inside S1's reach, is never touched.
    Strike strike(1.0F, 0.2F);
    strike.run();
    REQUIRE(strike.shapesOn.has_value());
    CHECK_FALSE(strike.hit.has_value());
    CHECK(strike.attacker.fighter().hitsLanded() == 0);
    CHECK(strike.victim.health().value() == strike.victim.health().maximum());
}
