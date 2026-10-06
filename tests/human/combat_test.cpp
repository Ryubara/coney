// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <numbers>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "combat/anim_ids.h"
#include "combat/anim_ranges.h"
#include "combat/combat_script.h"
#include "combat/commands.h"
#include "combat/player_combat.h"
#include "core/pad.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/pair_placement.h"
#include "human/target_human.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// The player's fighting inside the human, driven by input scripts at partial stick deflections: the chain plays its
// clips and lands its hits on a passive target, which reacts as the research's victim does; the block holds the body;
// the grab, its strike, the throw, the spin and the let-go; the tackle; the turn into an attack.

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::AnimState;
using coney::human::Human;
using coney::human::TargetHuman;
using coney::human::TargetState;
namespace id = coney::combat::anim_id;

using coney::test::Fight;
using coney::test::FightCharacter;

TEST_CASE("square three times plays S1, SS2 and SSS3 on a target, which reacts and is stunned", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character);
    std::vector<std::uint32_t> played;
    std::vector<int> reactions;
    std::vector<std::uint32_t> struck; // the anim ids the tutorial callback is given, update by update
    // A press every 6 updates, the stick at rest (pushed, square would play the walk attack 23).
    fight.run("10 tap square\n16 tap square\n22 tap square\n", 80, [&](std::uint64_t) {
        for (const int strike : fight.human().fighter().strikes()) {
            struck.push_back(static_cast<std::uint32_t>(strike));
        }
        const std::uint32_t now = fight.human().animator().animId();
        if (played.empty() || played.back() != now) {
            played.push_back(now);
        }
        if (reactions.empty() || reactions.back() != fight.target().lastReaction()) {
            reactions.push_back(fight.target().lastReaction());
        }
    });
    // The chain's clips in order, then the fight idle.
    const std::vector<std::uint32_t> chain{id::kAttackS1, id::kAttackSS2, id::kAttackSSS3, 358};
    std::size_t next = 0;
    for (const std::uint32_t clip : played) {
        if (next < chain.size() && clip == chain[next]) {
            ++next;
        }
    }
    CHECK(next == chain.size());
    // 17 + 36 + 53 of the target's 600.
    CHECK(fight.target().damageTaken() == 106);
    CHECK(fight.target().hitsTaken() == 3);
    CHECK(fight.human().fighter().hitsLanded() == 3);
    CHECK(struck == std::vector<std::uint32_t>{id::kAttackS1, id::kAttackSS2, id::kAttackSSS3});
    // From in front: S1 272, SS2 273 (a combo id, strength 0), SSS3 276 (a strength lighter), and its 0x400 stuns.
    CHECK(reactions == std::vector<int>{-1, 272, 273, 276});
    CHECK(fight.target().stuns() == 1);
    // The player did not walk: the attacks hold the body.
    CHECK(fight.human().position().y == Approx(40.0F).margin(0.3F));
}

TEST_CASE("a stun runs 750 ms, then 357 and the idle", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character);
    // Square, then cross in S1's window: SX2, whose flag 0x400 stuns.
    std::uint64_t stunnedAt = 0;
    std::uint64_t endedAt = 0;
    fight.run("10 tap square\n16 tap cross\n", 80, [&](std::uint64_t frame) {
        if (stunnedAt == 0 && fight.target().stunned()) {
            stunnedAt = frame;
        }
        if (stunnedAt != 0 && endedAt == 0 && !fight.target().stunned()) {
            endedAt = frame;
        }
    });
    REQUIRE(stunnedAt != 0);
    CHECK(fight.target().damageTaken() == 17 + 44);
    // 750 ms is 22.5 updates.
    CHECK(endedAt - stunnedAt >= 22);
    CHECK(endedAt - stunnedAt <= 24);
}

TEST_CASE("R1 held blocks: the stick at 0.6 turns the player in place with the shuffle", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 6.0F);
    fight.run("0 press r1\n5 stick left 60 0\n40 release r1\n40 stick left 0 0\n", 40, [&](std::uint64_t frame) {
        if (frame >= 2) {
            CHECK(fight.human().fighter().blocking());
        }
    });
    CHECK(fight.human().position().x == Approx(40.0F).margin(0.01F));
    CHECK(fight.human().position().y == Approx(40.0F).margin(0.01F));
    CHECK(fight.human().animator().animId() == 607);
    // Turned towards the stick's right.
    CHECK(fight.human().heading() < -0.5F);
}

TEST_CASE("circle grabs, square strikes in the hold and circle with the stick at 0.7 throws", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 1.5F);
    TargetState stateAfterGrab = TargetState::Standing;
    int damageAfterStrike = 0;
    fight.run("5 tap circle\n30 tap square\n60 stick left 0 70\n61 tap circle\n63 stick left 0 0\n", 80,
              [&](std::uint64_t frame) {
                  if (frame == 20) {
                      stateAfterGrab = fight.target().state();
                  }
                  if (frame == 50) {
                      damageAfterStrike = fight.target().damageTaken();
                  }
              });
    CHECK(stateAfterGrab == TargetState::Held);
    CHECK(damageAfterStrike == 57);
    // The throw (147) puts it on the ground, 66 more.
    CHECK(fight.target().state() == TargetState::Grounded);
    CHECK(fight.target().damageTaken() == 57 + 66);
    CHECK(fight.human().fighter().combat().mode() == coney::combat::CombatMode::Free);
    // 400 less 40 and 100, less the hold's drain, refilled since.
    CHECK(fight.human().fighter().combat().power().value() < 400);
    // After its 2000 ms on the ground it gets up.
    fight.run("", 70);
    CHECK(fight.target().state() == TargetState::Standing);
}

TEST_CASE("in a grab R1 spins to the rear hold, and L2 lets go", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 1.0F);
    bool rear = false;
    fight.run("5 tap circle\n30 tap r1\n60 press l2\n62 release l2\n", 80, [&](std::uint64_t frame) {
        if (frame == 50) {
            rear = fight.human().fighter().fromRear();
        }
    });
    CHECK(rear);
    CHECK(fight.target().state() == TargetState::Standing);
    CHECK(fight.human().fighter().held() == nullptr);
    CHECK(fight.human().fighter().combat().mode() == coney::combat::CombatMode::Free);
}

TEST_CASE("circle held tackles: the target is mounted and square strikes it", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 2.0F);
    fight.run("5 press circle\n15 release circle\n50 tap square\n", 70);
    CHECK(fight.target().state() == TargetState::Mounted);
    CHECK(fight.target().damageTaken() == 61); // 219 or 221
}

TEST_CASE("an attack turns to a target off to the side within its range", "[human][combat]") {
    const FightCharacter character;
    // The target 0.9 m ahead and 0.6 m to the right: within the 1.25 m far range, 34° off.
    Fight fight(character, 0.9F, 0.6F);
    fight.run("5 tap square\n", 20);
    const float expected = std::atan2(-0.6F, 0.9F);
    CHECK(fight.human().heading() == Approx(expected).margin(0.05F));
    CHECK(fight.target().damageTaken() == 17);
}

TEST_CASE("an attack turns and slides onto its target at a constant rate up to its first event", "[human][combat]") {
    const FightCharacter character;
    // The target 1.1 m ahead and 0.4 m to the right (1.17 m, 20° off), within the 1.25 m far range.
    Fight fight(character, 1.1F, 0.4F);
    struct Sample {
        std::uint32_t clip;
        float heading;
        Vec3 position;
    };
    std::vector<Sample> samples;
    fight.run("5 tap cross\n", 30, [&](std::uint64_t) {
        samples.push_back(Sample{fight.human().animator().animId(), fight.human().heading(), fight.human().position()});
    });
    // X1 starts on the release; the body does not turn on that update (the steer is set after the state update).
    const auto start = std::ranges::find_if(samples, [](const Sample& s) { return s.clip == id::kAttackX1; });
    REQUIRE(start != samples.end());
    const auto k = static_cast<std::size_t>(start - samples.begin());
    REQUIRE(k >= 1);
    REQUIRE(samples.size() > k + 12);
    CHECK(samples[k].heading == samples[k - 1].heading);
    // The synthetic X1's first event (the window at frame 5, rate 0.75) comes 0.222 s in: the steer lasts 0.322 s, 9
    // updates at one rate and two thirds of one more, then stops; no easing.
    const float steerTime = (5.0F / 30.0F / 0.75F) + 0.1F;
    const float angle = std::atan2(-0.4F, 1.1F);
    const float perUpdate = angle * (1.0F / 30.0F) / steerTime;
    for (std::size_t i = k + 1; i <= k + 9; ++i) {
        INFO("update " << i - k);
        CHECK(samples[i].heading - samples[i - 1].heading == Approx(perUpdate).margin(1e-4));
    }
    CHECK(samples[k + 10].heading - samples[k + 9].heading == Approx(perUpdate * 2.0F / 3.0F).margin(1e-4));
    CHECK(samples[k + 11].heading == samples[k + 10].heading);
    CHECK(samples[k + 10].heading == Approx(angle).margin(1e-3));
    // The slide (the synthetic clips carry no root motion): a constant step to stand at the clip's 1 m reach.
    const float distance = std::hypot(0.4F, 1.1F);
    const float stepLength = (distance - 1.0F) * (1.0F / 30.0F) / steerTime;
    for (std::size_t i = k + 1; i <= k + 9; ++i) {
        const Vec3 a = samples[i - 1].position;
        const Vec3 b = samples[i].position;
        CHECK(std::hypot(b.x - a.x, b.y - a.y) == Approx(stepLength).margin(1e-4));
    }
    const Vec3 end = samples[k + 11].position;
    CHECK(std::hypot(40.4F - end.x, 41.1F - end.y) == Approx(1.0F).margin(1e-3));
}

TEST_CASE("a heavy reaction knocks the target down, and it rises after 2000 ms", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character);
    // Hurt (below 35 % of 600) the combo id keeps its strength: SSS3 (0x26) is heavy, 288 knocks down.
    fight.target().hit(coney::human::TargetHit{
        .damage = 400, .attackAnim = 0, .code = 0, .flags = 0, .attacker = Vec3{40.0F, 40.0F, 0.0F}, .react = false});
    fight.target().step();
    REQUIRE(fight.target().hurt());
    fight.run("10 tap square\n16 tap square\n22 tap square\n", 40);
    CHECK(fight.target().knockdowns() == 1);
    CHECK(fight.target().state() == TargetState::Grounded);
    fight.run("", 65);
    CHECK(fight.target().state() == TargetState::Standing);
}

TEST_CASE("after the connect the victim sits at the front hold's offset and follows the grabber", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 1.5F);
    coney::human::PairStage connectingSeen = coney::human::PairStage::None;
    std::uint64_t attachedAt = 0;
    fight.run("5 tap circle\n", 40, [&](std::uint64_t frame) {
        const coney::human::PairStage stage = fight.human().fighter().pairStage();
        if (stage == coney::human::PairStage::Moving) {
            connectingSeen = stage;
            // While the connecting clips play, the victim is not attached and a strike is refused.
            CHECK_FALSE(fight.target().attached());
        }
        if (attachedAt == 0 && stage == coney::human::PairStage::Attached) {
            attachedAt = frame;
        }
    });
    CHECK(connectingSeen == coney::human::PairStage::Moving);
    REQUIRE(attachedAt != 0);
    CHECK(fight.target().attached());
    // (0.380, 1.012) in the grabber's frame, facing it.
    const Vec3 local =
        coney::human::toFrame(fight.human().position(), fight.human().heading(), fight.target().position());
    CHECK(local.x == Approx(0.38F).margin(0.01F));
    CHECK(local.y == Approx(1.01F).margin(0.01F));
    CHECK(std::fabs(coney::human::wrapAngle(fight.target().heading() - fight.human().heading())) ==
          Approx(std::numbers::pi_v<float>).margin(1e-3));
    // The alignment slid the grabber from 1.5 m to the clip's reach before the connect.
    CHECK(fight.human().position().y == Approx(40.0F + 1.5F - 0.999F).margin(0.02F));
}

TEST_CASE("a grab from behind connects with 74 and holds the victim at the rear offset", "[human][combat]") {
    const FightCharacter character;
    // The target 1.2 m ahead, facing away from the player.
    Fight fight(character, 1.2F, 0.0F, 0.0F);
    std::vector<std::uint32_t> played;
    fight.run("5 tap circle\n", 40, [&](std::uint64_t) {
        const std::uint32_t now = fight.human().animator().animId();
        if (played.empty() || played.back() != now) {
            played.push_back(now);
        }
    });
    CHECK(fight.human().fighter().fromRear());
    // The intro, the rear connecting clip and the rear hold, in order; never the front ones.
    const std::vector<std::uint32_t> grab(std::ranges::find(played, 71U), played.end());
    CHECK(grab == std::vector<std::uint32_t>{71, 74, 84});
    CHECK(fight.target().animator().animId() == 85);
    const Vec3 local =
        coney::human::toFrame(fight.human().position(), fight.human().heading(), fight.target().position());
    CHECK(local.x == Approx(-0.097F).margin(0.01F));
    CHECK(local.y == Approx(0.222F).margin(0.01F));
    CHECK(coney::human::wrapAngle(fight.target().heading() - fight.human().heading()) == Approx(0.0F).margin(1e-3));
}

TEST_CASE("a paired clip and its rate come from the attacker's anim set", "[human][combat]") {
    // Two sets: the victim's own 73 lasts 0.3 s with no rate flag (0.75); the attacker's lasts 0.9 s at flag 0x1000.
    const FightCharacter victimCharacter;
    std::vector<coney::test::LocomotionClip> clips = coney::test::locomotionClips();
    clips.push_back({.id = 73,
                     .speed = 0.0F,
                     .duration = 0.9F,
                     .rootVelocity = 0.0F,
                     .rangeFlags = 0x1000,
                     .reach = 0.0F,
                     .knockdown = false});
    clips.push_back({.id = 83,
                     .speed = 0.0F,
                     .duration = 1.0F,
                     .rootVelocity = 0.0F,
                     .rangeFlags = 0,
                     .reach = 0.0F,
                     .knockdown = false});
    const coney::characters::CharacterData attackerData = coney::test::locomotionData(clips);
    const coney::characters::AnimSet attacker{attackerData, nullptr};
    coney::human::HumanAnimator animator(victimCharacter.anims, coney::human::AnimSlots::player());
    const std::array<std::uint32_t, 1> react{73};
    animator.playPaired(react, attacker, 83, AnimState::Hold);
    const coney::anim::AnimTask* top = animator.tasks().top();
    REQUIRE(top != nullptr);
    CHECK(top->animId() == 73);
    CHECK(top->duration() == Approx(0.9F));
    CHECK(top->rate() == Approx(1.0F));
    // Played from its own set it is the victim's clip.
    animator.playCombat(react, 83, AnimState::Hold);
    CHECK(animator.tasks().top()->duration() == Approx(0.3F));
    CHECK(animator.tasks().top()->rate() == Approx(0.75F));
}
