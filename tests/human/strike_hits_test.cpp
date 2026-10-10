// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/skeleton.h"
#include "combat/anim_ids.h"
#include "combat/commands.h"
#include "combat/meters.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/human_flags.h"
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

// The fight's clips, S1 (12) switching every strike shape on at frame 8 and off at frame 11, as do the specials 645
// (in rage, carrying the sweeps' flags 0x10004: a capsule strike) and 653 (here without them).
std::vector<coney::test::LocomotionClip> strikingClips() {
    std::vector<coney::test::LocomotionClip> clips = coney::test::fightClips();
    for (coney::test::LocomotionClip& clip : clips) {
        // The thrown victim's flight 148 switches every shape on from its first frame to frame 15.
        if (clip.id == 148U) {
            clip.markers.push_back({0, 0x13});
            clip.markers.push_back({15, 0x14});
        }
        if (clip.id == static_cast<std::uint32_t>(id::kAttackS1) || clip.id == 645U || clip.id == 653U) {
            clip.markers.push_back({8, 0x13});
            clip.markers.push_back({11, 0x14});
        }
        if (clip.id == 645U) {
            clip.flags = 0x10004;
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
        victim.spawn(mesh.get(), coney::anim::Vec3{40.0F, 40.0F + apart, 0.0F}, 180.0F);
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

TEST_CASE("the led steer aims at the head, posed into the world", "[human][strike]") {
    // The head (bone 6) 0.05 m right of the victim's feet, 0.2 m forward and 1.7 m up, as a fighter's leans in the
    // fight idle 358. The victim faces the attacker, so the point the led steer aims at stands 0.2 m nearer him than
    // the victim's position, not beyond it, and on the victim's right, not his left: the model frame is x right,
    // y forward, z up (docs/research/combat.md#led-steer).
    Strike strike(1.5F, 0.5F);
    strike.skeleton.offsets.at(6) = coney::anim::Vec3{0.05F, 0.2F, 1.7F};
    strike.humans.update(strike.mesh.get());
    const coney::anim::Vec3 head = strike.victim.ledPoint();
    const coney::anim::Vec3 feet = strike.victim.position();
    const float heading = strike.victim.heading();
    INFO("victim heading " << heading << ", head (" << head.x << ", " << head.y << ")");
    const coney::anim::Vec3 toAttacker = coney::anim::subtract(strike.attacker.position(), feet);
    const float along =
        ((head.x - feet.x) * toAttacker.x + (head.y - feet.y) * toAttacker.y) / std::hypot(toAttacker.x, toAttacker.y);
    CHECK(along == Catch::Approx(0.2F).margin(1e-3));
    // His right is his facing turned a quarter clockwise seen from above: (cos h, sin h) in Coney's headings.
    const float right = ((head.x - feet.x) * std::cos(heading)) + ((head.y - feet.y) * std::sin(heading));
    CHECK(right == Catch::Approx(0.05F).margin(1e-3));
    CHECK(head.z - feet.z == Catch::Approx(1.7F).margin(1e-3));
    // Without a skeleton, the position.
    strike.victim.setSkeleton(nullptr);
    CHECK(strike.victim.ledPoint().y == Catch::Approx(41.5F).margin(1e-3));
}

TEST_CASE("a free attack hits on the update its strike shapes first touch the victim, not at a fixed update",
          "[human][combat][strike]") {
    Strike strike(1.0F, 0.85F);
    strike.run();
    REQUIRE(strike.shapesOn.has_value());
    REQUIRE(strike.hit.has_value());
    if (!strike.shapesOn || !strike.hit) {
        return;
    }
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

TEST_CASE("a thrown human's flight strikes a bystander it meets: no health lost, a crushing high hit",
          "[human][strike]") {
    // The "attacker" here is the thrown body: it plays the throw's victim clip 148, whose events switch all its shapes
    // on, 0.4 m from a bystander. 148's Anim Range entry is -60 with hit code 0x3a (docs/research/combat.md#throws).
    Strike strike(0.4F, 0.0F);
    const std::array<std::uint32_t, 1> flight{148};
    strike.attacker.play(flight, 196, coney::human::AnimState::Hold, coney::human::TargetState::Grounded);
    for (int k = 0; k < 15; ++k) {
        strike.humans.update(strike.mesh.get());
    }
    const coney::combat::Health& health = strike.victim.fighter().health();
    CHECK(strike.victim.fighter().hitsTaken() == 1);
    CHECK(health.value() == health.maximum());
    CHECK(strike.victim.state() == coney::human::TargetState::Grounded);
}

TEST_CASE("square at a human down behind the attacker within the pick's wide pass plays the grounded strike",
          "[human][strike]") {
    // Player_PickTarget's wide pass (any angle, 2.0 m × 0.7) skips only the knocked out, so it finds a downed human
    // 0.9 m behind, and its state picks 193; 1.6 m behind he is out of every pass and square plays S1
    // (docs/research/combat-moves.md#targeting).
    for (const auto& [behind, wanted] : {std::pair{0.9F, 193U}, std::pair{1.6F, 12U}}) {
        Strike strike(-behind, 0.5F);
        const std::array<std::uint32_t, 1> down{196};
        strike.victim.play(down, 196, coney::human::AnimState::Hold, coney::human::TargetState::Grounded);
        std::uint64_t step = 0;
        strike.humans.setBrains([&step](std::span<Human* const> all) {
            if (step == 2) {
                all[1]->record().command = command::kSquarePressed;
            }
            ++step;
        });
        bool played = false;
        for (int k = 0; k < 6; ++k) {
            strike.humans.update(strike.mesh.get());
            played = played || strike.attacker.animator().animId() == wanted;
        }
        INFO(behind << " m behind");
        CHECK(played);
    }
}

TEST_CASE("the rage sweep 645 strikes a bum's capsule at shin height: 1.0 m away it hits, 1.6 m away it misses",
          "[human][combat][strike]") {
    // 645 carries clip flag 0x10000, so its strike shapes are tested against the target's upright capsule (radius
    // 0.35, from the feet to the head + 0.2), not its spine and head (docs/research/combat.md#capsule-strike). The
    // foot (30, a sphere of 0.15) stands 0.6 m ahead and 0.2 m up; the spine sits 1.0 m up and the head 1.6 m. The
    // victim is a bum: flagged never to be a target, so nothing steers onto it. 653 without the flag never reaches the
    // spine or the head from shin height.
    struct Case {
        float apart;
        bool rage;
        bool hits;
    };
    for (const Case c : {Case{1.0F, true, true}, Case{1.6F, true, false}, Case{1.0F, false, false}}) {
        Strike strike(c.apart, 0.0F);
        strike.skeleton.offsets.at(3) = coney::anim::Vec3{0.0F, 0.0F, 1.0F};
        strike.skeleton.offsets.at(6) = coney::anim::Vec3{0.0F, 0.0F, 0.6F};
        strike.skeleton.offsets.at(30) = coney::anim::Vec3{0.0F, 0.6F, 0.2F};
        strike.victim.setFlag(coney::human::flag::kNoTarget, true);
        coney::combat::RageMeter& rage = strike.attacker.fighter().combat().rage();
        rage.fill(0, 1'000'000);
        if (c.rage) {
            rage.force(0);
        }
        std::uint64_t step = 0;
        strike.humans.setBrains([&step](std::span<Human* const> all) {
            if (step == 2) {
                all[1]->record().command = command::kCrossSquare;
            }
            ++step;
        });
        bool played = false;
        bool shapesOn = false;
        const int wanted = c.rage ? id::kSpecialRage : id::kSpecial;
        for (int k = 0; k < 30; ++k) {
            strike.humans.update(strike.mesh.get());
            played = played || strike.attacker.animator().animId() == static_cast<std::uint32_t>(wanted);
            shapesOn = shapesOn || strike.attacker.strikeShapes().anyOn();
        }
        INFO(c.apart << " m apart, " << (c.rage ? "645" : "653"));
        REQUIRE(played);
        REQUIRE(shapesOn);
        CHECK((strike.attacker.fighter().hitsLanded() > 0) == c.hits);
    }
}
