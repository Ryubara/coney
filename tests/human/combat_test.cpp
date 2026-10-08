// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "combat/anim_ids.h"
#include "combat/anim_ranges.h"
#include "combat/combat_script.h"
#include "combat/combat_tuning.h"
#include "combat/commands.h"
#include "combat/player_combat.h"
#include "combat/stick_games.h"
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

namespace {

// Turns the street's auto-lock off for one test, so a target taken does not turn the player to face it.
struct NoAutoLock {
    NoAutoLock() { coney::combat::combatTuning().autoLockAndCombat = false; }
    NoAutoLock(const NoAutoLock&) = delete;
    NoAutoLock& operator=(const NoAutoLock&) = delete;
    NoAutoLock(NoAutoLock&&) = delete;
    NoAutoLock& operator=(NoAutoLock&&) = delete;
    ~NoAutoLock() { coney::combat::combatTuning() = coney::combat::CombatTuning{}; }
};

} // namespace

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
    // Not locked on (the street's auto-lock off), so the stance does not face the target before the attack.
    const NoAutoLock unlocked;
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
    // The synthetic X1's first event (the window at frame 5, rate 0.75) comes 0.222 s in: the turn and the slide last
    // 0.322 s, 9 updates at one rate and two thirds of one more, then stop; no easing (combat-moves.md#reach).
    const float turnTime = (5.0F / 30.0F / 0.75F) + 0.1F;
    const float angle = std::atan2(-0.4F, 1.1F);
    const float perUpdate = angle * (1.0F / 30.0F) / turnTime;
    for (std::size_t i = k + 1; i <= k + 9; ++i) {
        INFO("update " << i - k);
        CHECK(samples[i].heading - samples[i - 1].heading == Approx(perUpdate).margin(1e-4));
    }
    CHECK(samples[k + 10].heading - samples[k + 9].heading == Approx(perUpdate * 2.0F / 3.0F).margin(1e-4));
    CHECK(samples[k + 11].heading == samples[k + 10].heading);
    CHECK(samples[k + 10].heading == Approx(angle).margin(1e-3));
    // The slide (the synthetic clips carry no root motion): a constant step to stand at the clip's 1 m reach.
    const float distance = std::hypot(0.4F, 1.1F);
    const float stepLength = (distance - 1.0F) * (1.0F / 30.0F) / turnTime;
    for (std::size_t i = k + 1; i <= k + 9; ++i) {
        const Vec3 a = samples[i - 1].position;
        const Vec3 b = samples[i].position;
        CHECK(std::hypot(b.x - a.x, b.y - a.y) == Approx(stepLength).margin(1e-4));
    }
    const Vec3 end = samples[k + 11].position;
    CHECK(std::hypot(40.4F - end.x, 41.1F - end.y) == Approx(1.0F).margin(1e-3));
}

namespace {

// The clips the player played, in order, and the heading after every update, for one script.
struct Played {
    std::vector<std::uint32_t> clips;
    std::vector<float> headings;
    std::vector<Vec3> positions;
};

// Runs `script` for `frames` updates of `fight`, noting each new clip the player plays.
Played play(Fight& fight, std::string_view script, std::uint64_t frames) {
    Played out;
    fight.run(script, frames, [&](std::uint64_t) {
        const std::uint32_t now = fight.human().animator().animId();
        if (out.clips.empty() || out.clips.back() != now) {
            out.clips.push_back(now);
        }
        out.headings.push_back(fight.human().heading());
        out.positions.push_back(fight.human().position());
    });
    return out;
}

// Whether `clips` holds `clip`.
bool playedClip(const Played& played, std::uint32_t clip) {
    return std::ranges::find(played.clips, clip) != played.clips.end();
}

// The stick fully to the right one update before square, as the research reached lesson 7's snaps
// (docs/research/combat.md#attacks), then at rest.
constexpr std::string_view kSnapRightScript = "10 stick left 100 0\n11 tap square\n12 stick left 0 0\n";

} // namespace

TEST_CASE("a snap turns the player so a human on the stick's side sits at the snap's side, and strikes it",
          "[human][combat]") {
    const FightCharacter character;
    // The target 0.6 m ahead and 1.0 m to the right: 1.17 m away (inside the snap's 1.25 m far range), 59° right of
    // the facing, so 31° off the stick pushed to the right.
    Fight fight(character, 0.6F, 1.0F);
    // The current target 0.9 m ahead (the stance takes the nearest within 2 m): a snap goes only for another human.
    static_cast<void>(fight.addTarget(character, 0.9F, 0.0F));
    const Played played = play(fight, kSnapRightScript, 40);
    CHECK(playedClip(played, id::kSnapRight));
    CHECK_FALSE(playedClip(played, id::kAttackS1));
    CHECK(fight.target().damageTaken() == 31);
    CHECK(fight.human().fighter().hitsLanded() == 1);
    // The steer, set on the snap's start (update 11) from the next update on: over 0.1 s (3 updates, at one rate) the
    // facing turns to put the target at the snap's direction on the disc, (0.999, -0.012), 90.7° to the right.
    const Vec3 to = coney::anim::subtract(fight.target().position(), played.positions[11]);
    const float wanted = coney::human::wrapAngle(coney::human::headingOf(to) - std::atan2(-0.999F, -0.012F));
    CHECK(played.headings[14] == Approx(wanted).margin(1e-3));
    const auto turned = [&](std::size_t frame) { return played.headings[frame] - played.headings[frame - 1]; };
    CHECK(turned(13) == Approx(turned(12)).margin(1e-4));
    CHECK(turned(14) == Approx(turned(12)).margin(1e-4));
    CHECK(turned(15) == Approx(0.0F).margin(1e-5));
    CHECK(std::fabs(turned(12)) > 0.05F);
}

TEST_CASE("a snap backwards strikes a human behind the player", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, -1.0F);
    static_cast<void>(fight.addTarget(character, 0.9F, 0.0F));
    const Played played = play(fight, "10 stick left 0 -100\n11 tap square\n12 stick left 0 0\n", 40);
    CHECK(playedClip(played, id::kSnapBack));
    CHECK(fight.target().damageTaken() == 31);
}

TEST_CASE("without a human within 2 m and 45 degrees of the stick square is an S1, not a snap", "[human][combat]") {
    const FightCharacter character;
    SECTION("only a human ahead, 90 degrees off the stick") {
        Fight fight(character, 1.0F);
        const Played played = play(fight, kSnapRightScript, 40);
        CHECK(playedClip(played, id::kAttackS1));
        CHECK_FALSE(playedClip(played, id::kSnapRight));
    }
    SECTION("a human to the right, 2.5 m away") {
        Fight fight(character, 0.0F, 2.5F);
        const Played played = play(fight, kSnapRightScript, 40);
        CHECK(playedClip(played, id::kAttackS1));
        CHECK_FALSE(playedClip(played, id::kSnapRight));
        CHECK(fight.target().damageTaken() == 0);
    }
    SECTION("a human 1.2 m away, 50 degrees off the stick") {
        // 40° right of the facing.
        Fight fight(character, 1.2F * std::cos(0.698F), 1.2F * std::sin(0.698F));
        const Played played = play(fight, kSnapRightScript, 40);
        CHECK_FALSE(playedClip(played, id::kSnapRight));
    }
}

TEST_CASE("square with the stick to the side in S1's wind-up snaps to a human within 25's far range, else plays SS2",
          "[human][combat]") {
    const FightCharacter character;
    // S1 at the human 0.9 m ahead, then the stick fully to the left and square in its wind-up: the chain buffers a
    // left snap and plays it when the window opens if a human on the left is within the far range of 25 (1.25 m in
    // the fixture), else the plain square step (docs/research/combat-moves.md#input).
    const std::string script = "11 tap square\n13 stick left -100 0\n14 tap square\n15 stick left 0 0\n";
    SECTION("a human 1.0 m to the left") {
        Fight fight(character, 0.9F);
        static_cast<void>(fight.addTarget(character, 0.0F, -1.0F));
        const Played played = play(fight, script, 60);
        CHECK(playedClip(played, id::kAttackS1));
        CHECK(playedClip(played, id::kSnapLeft));
        CHECK_FALSE(playedClip(played, id::kAttackSS2));
    }
    SECTION("a human 1.8 m to the left") {
        Fight fight(character, 0.9F);
        static_cast<void>(fight.addTarget(character, 0.0F, -1.8F));
        const Played played = play(fight, script, 60);
        CHECK(playedClip(played, id::kAttackSS2));
        CHECK_FALSE(playedClip(played, id::kSnapLeft));
    }
}

TEST_CASE("a snap makes its human the target, so square with the stick at it again plays S1", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 0.0F, 1.1F);
    // The current target ahead; the snap's human to the right.
    static_cast<void>(fight.addTarget(character, 0.9F, 0.0F));
    const Played played =
        play(fight, std::string(kSnapRightScript) + "50 stick left 100 0\n51 tap square\n52 stick left 0 0\n", 80);
    CHECK(fight.human().fighter().target() == &fight.target());
    CHECK(playedClip(played, id::kSnapRight));
    CHECK(playedClip(played, id::kAttackS1));
    CHECK(fight.target().damageTaken() == 31 + 17);
}

TEST_CASE("walking, square with the stick fully to the side is the walk attack, not a snap", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 0.0F, 1.1F);
    const Played played = play(fight, "5 stick left 0 35\n30 stick left 100 0\n30 tap square\n31 stick left 0 0\n", 60);
    CHECK_FALSE(playedClip(played, id::kSnapRight));
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

TEST_CASE("square with no human in front strikes a pane ahead with 661 or 662 by its height", "[human][combat]") {
    const FightCharacter character;
    // The target 30 m away: nobody to fight.
    Fight fight(character, 30.0F);
    // A pane 0.9 m ahead at 1.59 m (a cabinet's front), and one behind, nearer.
    const std::vector<coney::human::ObjectTarget> panes{{.handle = 92, .point = Vec3{40.0F, 40.9F, 1.59F}},
                                                        {.handle = 93, .point = Vec3{40.0F, 39.5F, 0.5F}}};
    fight.human().setObjectTargets(panes);
    std::vector<std::uint32_t> played;
    std::vector<double> hits;
    fight.run("10 tap square\n", 40, [&](std::uint64_t) {
        const std::uint32_t now = fight.human().animator().animId();
        if (played.empty() || played.back() != now) {
            played.push_back(now);
        }
        if (const std::optional<double> hit = fight.human().fighter().objectHit()) {
            hits.push_back(*hit);
        }
    });
    CHECK(std::ranges::find(played, static_cast<std::uint32_t>(id::kBreakObjectMid)) != played.end());
    CHECK(hits == std::vector<double>{92});
    CHECK(fight.human().fighter().hitsLanded() == 0);

    // Low, at 0.5 m: 661.
    Fight low(character, 30.0F);
    low.human().setObjectTargets({{.handle = 94, .point = Vec3{40.0F, 40.8F, 0.5F}}});
    played.clear();
    low.run("10 tap square\n", 40, [&](std::uint64_t) {
        const std::uint32_t now = low.human().animator().animId();
        if (played.empty() || played.back() != now) {
            played.push_back(now);
        }
    });
    CHECK(std::ranges::find(played, static_cast<std::uint32_t>(id::kBreakObjectLow)) != played.end());
}

TEST_CASE("square with no object in reach plays the chain's first attack", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    // Out of the object attack's range (the default reach in these ranges).
    fight.human().setObjectTargets({{.handle = 92, .point = Vec3{40.0F, 45.0F, 1.59F}}});
    std::vector<std::uint32_t> played;
    bool struck = false;
    fight.run("10 tap square\n", 40, [&](std::uint64_t) {
        played.push_back(fight.human().animator().animId());
        struck = struck || fight.human().fighter().objectHit().has_value();
    });
    CHECK(std::ranges::find(played, static_cast<std::uint32_t>(id::kAttackS1)) != played.end());
    CHECK_FALSE(struck);
}

TEST_CASE("the stereo theft turns the player to the stereo, plays 683 then 684, and the stick's turns win it",
          "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 6.0F);
    fight.human().startStereoTheft(Vec3{42.0F, 40.0F, 1.0F}, coney::combat::stereoStageTurns(2));
    CHECK(fight.human().heading() == Approx(-std::numbers::pi_v<float> / 2.0F).margin(1e-4));
    // The stick anticlockwise, 30 degrees an update at full deflection.
    std::string script;
    for (int frame = 0; frame < 300; ++frame) {
        const float radians = static_cast<float>(frame) * 30.0F * std::numbers::pi_v<float> / 180.0F;
        script += std::format("{} stick left {} {}\n", frame, std::lround(100.0F * std::cos(radians)),
                              std::lround(100.0F * std::sin(radians)));
    }
    std::vector<std::uint32_t> played;
    bool won = false;
    fight.run(script, 300, [&](std::uint64_t) {
        const std::uint32_t now = fight.human().animator().animId();
        if (played.empty() || played.back() != now) {
            played.push_back(now);
        }
        won = won || fight.human().fighter().last().game == coney::combat::GameResult::Succeeded;
    });
    REQUIRE(played.size() >= 2);
    CHECK(played[0] == 683);
    CHECK(played[1] == 684);
    CHECK(won);
}

TEST_CASE("a hit during the stereo theft ends it with no outcome, as death or a knock-down would", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 6.0F);
    fight.human().startStereoTheft(Vec3{42.0F, 40.0F, 1.0F}, coney::combat::stereoStageTurns(2));
    // A few updates of the stick turning anticlockwise at 0.9: the theft runs.
    std::string script;
    for (int frame = 0; frame < 10; ++frame) {
        const float radians = static_cast<float>(frame) * 30.0F * std::numbers::pi_v<float> / 180.0F;
        script += std::format("{} stick left {} {}\n", frame, std::lround(90.0F * std::cos(radians)),
                              std::lround(90.0F * std::sin(radians)));
    }
    fight.run(script, 10);
    REQUIRE(fight.human().fighter().combat().theft().has_value());
    REQUIRE(fight.human().stereoTheftPlaying());
    // A hit from in front takes the body: the next update ends the theft, back to free, no result.
    fight.human().takeHit(coney::human::IncomingHit{
        .damage = 14, .attackAnim = 12, .code = 0x0a, .flags = 0x800, .attacker = Vec3{40.0F, 41.0F, 0.0F}});
    fight.run("", 2);
    CHECK_FALSE(fight.human().stereoTheftPlaying());
    CHECK_FALSE(fight.human().fighter().combat().theft().has_value());
    CHECK(fight.human().fighter().combat().mode() == coney::combat::CombatMode::Free);
    CHECK(fight.human().fighter().last().game == coney::combat::GameResult::Running);
}

TEST_CASE("dying during the stereo theft ends it", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 6.0F);
    fight.human().startStereoTheft(Vec3{42.0F, 40.0F, 1.0F}, coney::combat::stereoStageTurns(2));
    fight.run("", 5);
    REQUIRE(fight.human().fighter().combat().theft().has_value());
    // A hit with no reaction of its own (a move inside a hold) that takes the last of his health.
    fight.human().takeHit(coney::human::IncomingHit{.damage = 5000,
                                                    .attackAnim = 12,
                                                    .code = 0x0a,
                                                    .flags = 0x800,
                                                    .attacker = Vec3{40.0F, 41.0F, 0.0F},
                                                    .react = false});
    fight.run("", 2);
    CHECK(fight.human().fighter().health().depleted());
    CHECK_FALSE(fight.human().fighter().combat().theft().has_value());
    CHECK(fight.human().fighter().combat().mode() == coney::combat::CombatMode::Free);
}
