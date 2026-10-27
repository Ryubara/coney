// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "combat/anim_ids.h"
#include "combat/combat_tuning.h"
#include "human/fighter.h"
#include "human/fighter_clips.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/locomotion.h"
#include "human/target_human.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// The player's target and lock as the original's stance update keeps them (`Player_UpdateSprint` 0x0027ce90,
// `Human_FightStanceMove` 0x00241b90, docs/research/combat-targets.md#fight-stance), each case as the original ran it
// in PCSX2 (the traces of research/traces/scenarios/target_*): an attack's pick is only a target, a lock starts from a
// threat within 2 m while not running, the lock-on walk faces the target within 2.5 m, and the target goes beyond 3 m.
// Issue #82.

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::Gait;
using coney::test::Fight;
using coney::test::FightCharacter;
namespace id = coney::combat::anim_id;

namespace {

constexpr float kPi = 3.14159265F;
constexpr float kDegrees = kPi / 180.0F;

// The fight's clips with the armed run sweep 501 and the run attack 24 as long as on the disc, moving their human at a
// run (7.09 m/s), so a sweep at a run carries the player on past its target.
std::vector<coney::test::LocomotionClip> sweepClips() {
    std::vector<coney::test::LocomotionClip> clips = coney::test::fightClips();
    const auto timed = [](std::uint32_t clip, float updates, float velocity) {
        return coney::test::LocomotionClip{.id = clip,
                                           .speed = 0.0F,
                                           .duration = updates * 0.75F / 30.0F,
                                           .rootVelocity = velocity,
                                           .rangeFlags = 0,
                                           .reach = 0.0F,
                                           .knockdown = false};
    };
    for (const coney::test::LocomotionClip& clip : {timed(24, 21.33F, 7.09F), timed(501, 21.0F, 7.09F)}) {
        const auto same = std::ranges::find(clips, clip.id, &coney::test::LocomotionClip::id);
        if (same != clips.end()) {
            *same = clip;
        } else {
            clips.push_back(clip);
        }
    }
    return clips;
}

// The character, built once.
const FightCharacter& sweepCharacter() {
    static const FightCharacter character(sweepClips());
    return character;
}

// The distance in plan from the player to the fight's target.
float toTarget(Fight& fight) {
    const Vec3 to = coney::anim::subtract(fight.target().position(), fight.human().position());
    return std::hypot(to.x, to.y);
}

// How far the player's heading is from facing the fight's target, radians.
float offFacing(Fight& fight) {
    const Vec3 to = coney::anim::subtract(fight.target().position(), fight.human().position());
    return std::fabs(coney::human::wrapAngle(coney::human::headingOf(to) - fight.human().heading()));
}

// What one update left.
struct Sample {
    float distance = 0.0F;
    float heading = 0.0F;
    float offFacing = 0.0F;
    bool hasTarget = false;
    bool locked = false;
    bool stance = false;
    Gait gait = Gait::Standing;
    std::uint32_t clip = 0;
};

// Runs `script` for `frames` updates and keeps what each left.
std::vector<Sample> record(Fight& fight, const std::string& script, std::uint64_t frames) {
    std::vector<Sample> line;
    fight.run(script, frames, [&](std::uint64_t) {
        const coney::human::Fighter& fighter = fight.human().fighter();
        line.push_back(Sample{.distance = toTarget(fight),
                              .heading = fight.human().heading(),
                              .offFacing = offFacing(fight),
                              .hasTarget = fighter.target() == &fight.target(),
                              .locked = fighter.lockTarget() != nullptr,
                              .stance = fighter.inFightStance(),
                              .gait = fight.human().gait(),
                              .clip = fight.human().animator().animId()});
    });
    return line;
}

} // namespace

TEST_CASE("a bat sweep at a run picks the enemy beside him but neither turns the player nor locks, and he runs on",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (runtime)
    // The original (run r-20261026-target-bat-sweep-run, slot 6): square at a run 2.73 m from PoizoCiv, 1.2 m to the
    // side: 501 took him as the target, the heading stayed at -29.1 degrees through the clip, the state word stayed 0
    // and the run went on at gait 4 past him (0.69 m away), the target kept but never locked.
    const auto run = [](int press) {
        Fight fight(sweepCharacter(), 14.0F, 1.2F);
        fight.human().fighter().setAnimSet(3);
        std::string script = "0 stick left 0 100\n";
        if (press >= 0) {
            script += std::format("{} tap square\n", press);
        }
        return record(fight, script, 110);
    };
    // The press goes in the update the player first comes within 2.6 m (inside the armed pick's 2.5 m x 1.1).
    const std::vector<Sample> probe = run(-1);
    const auto near = std::ranges::find_if(probe, [](const Sample& s) { return s.distance <= 2.6F; });
    REQUIRE(near != probe.end());
    const int press = static_cast<int>(near - probe.begin());
    REQUIRE(probe[static_cast<std::size_t>(press)].gait == Gait::Run);

    const std::vector<Sample> line = run(press);
    const auto at = [&](int update) { return line[static_cast<std::size_t>(update)]; };
    REQUIRE(at(press + 1).clip == static_cast<std::uint32_t>(id::kArmedAttackFromRun));
    // The pick is stored as the target.
    CHECK(at(press + 1).hasTarget);
    // No steer: the heading stays on the stick (straight ahead) for the sweep and the run after it, and nothing locks.
    for (int update = press; update < static_cast<int>(line.size()); ++update) {
        INFO("update " << update << ", " << at(update).distance << " m from him");
        CHECK(std::fabs(at(update).heading) < 1.0F * kDegrees);
        CHECK_FALSE(at(update).locked);
        CHECK_FALSE(at(update).stance);
    }
    // The run resumes after the clip and carries him past.
    const auto over = std::find_if(line.begin() + press + 1, line.end(), [](const Sample& s) {
        return s.clip != static_cast<std::uint32_t>(id::kArmedAttackFromRun);
    });
    REQUIRE(over != line.end());
    CHECK(over->gait == Gait::Run);
    CHECK(line.back().distance > 5.0F);
}

TEST_CASE("after S1 at a civilian the lock holds as he backs away, facing him within 2.5 m, and ends beyond 3 m",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (runtime)
    // The original (run r-20261026-target-attack-walk-away, slot 6): S1 at PoizoCiv (a neutral civilian, no threat) 1 m
    // ahead: the stance took him as the target and locked on the next update (state 0x5, Human_FightStanceMove);
    // standing, the lock held; backing off at 60 % the player walked backward facing him at 3.43 m/s, and the update
    // the distance passed 3 m (3.01) the target went and the walk was the free one again.
    Fight fight(sweepCharacter(), 1.0F);
    fight.makeCalm(fight.target());
    const std::vector<Sample> line = record(fight, "5 tap square\n50 stick left 0 -60\n", 120);
    // Locked while he stands after the attack, though the civilian is no threat: the stance falls back on the target.
    for (int update = 10; update < 50; ++update) {
        INFO("update " << update);
        CHECK(line[static_cast<std::size_t>(update)].locked);
    }
    bool sawKept = false;
    bool dropped = false;
    for (std::size_t update = 50; update < line.size(); ++update) {
        const Sample& s = line[update];
        INFO("update " << update << ", " << s.distance << " m");
        if (s.distance < 2.4F) {
            CHECK(s.locked);
            CHECK(s.offFacing < 2.0F * kDegrees);
        } else if (s.distance > 2.6F && s.distance < 2.95F) {
            // Beyond 2.5 m the target is kept, still within 3 m.
            CHECK(s.hasTarget);
            sawKept = true;
        } else if (s.distance > 3.0F + (3.429F / 30.0F)) {
            CHECK_FALSE(s.hasTarget);
            CHECK_FALSE(s.locked);
            dropped = true;
        }
    }
    CHECK(sawKept);
    CHECK(dropped);
}

TEST_CASE("walking past a human who is no threat, 1 m to the side, takes no target and never locks",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (runtime)
    // The original (runs r-20261026-target-walk-past-friends, slot 6, and r-20261026-target-level95-crew-beside,
    // Cochise held 1.23 m beside him): walking past
    // Vermin (his crew) at 0.69 m, or past a crew mate of another gang that is no enemy, left the target none and the
    // state word 0 on every update.
    Fight fight(sweepCharacter(), 3.0F, 1.0F);
    fight.makeCalm(fight.target());
    const std::vector<Sample> line = record(fight, "0 stick left 0 60\n70 stick left 0 0\n", 100);
    const auto closest = std::ranges::min_element(line, {}, &Sample::distance);
    REQUIRE(closest->distance < 1.2F);
    for (std::size_t update = 0; update < line.size(); ++update) {
        INFO("update " << update << ", " << line[update].distance << " m");
        CHECK_FALSE(line[update].hasTarget);
        CHECK_FALSE(line[update].locked);
        CHECK_FALSE(line[update].stance);
    }
}

TEST_CASE("walking past an enemy locks on within 2 m and faces him, keeps the heading beyond 2.5 m, ends beyond 3 m",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (runtime)
    // The original (run r-20261026-target-walk-past-enemy, slot 6): PoizoCiv an enemy 1 m to the side of the walk at 60
    // %: the stance took him and locked when he came within 2 m (1.62 m, once the run start's 0x10000000 had cleared),
    // the walk went on at 3.43 m/s facing him, and the target went the update the distance passed 3 m (3.00 m).
    Fight fight(sweepCharacter(), 3.0F, 1.0F);
    const std::vector<Sample> line = record(fight, "0 stick left 0 60\n90 stick left 0 0\n", 110);
    const auto first = std::ranges::find_if(line, &Sample::locked);
    REQUIRE(first != line.end());
    CHECK(first->distance <= 2.0F + 0.1F);
    bool sawFar = false;
    // The walk faces him from the update after the lock (the movement runs before the stance update), a few degrees
    // behind as he walks on (the original's bearing stayed within 9 degrees).
    for (auto it = first + 1; it + 1 != line.end(); ++it) {
        INFO("update " << (it - line.begin()) << ", " << it->distance << " m");
        if (it->distance < 2.4F && it->locked) {
            CHECK(it->offFacing < 10.0F * kDegrees);
        }
        // Between 2.5 and 3 m the walk keeps the heading (CfgStrafeRStick is off): no turn toward him.
        if ((it + 1)->distance > 2.55F && (it + 1)->distance < 2.95F && (it + 1)->locked) {
            CHECK(std::fabs(coney::human::wrapAngle((it + 1)->heading - it->heading)) < 0.01F);
            sawFar = true;
        }
        if (it->distance > 3.0F + 0.15F) {
            CHECK_FALSE(it->hasTarget);
            CHECK_FALSE(it->locked);
        }
    }
    CHECK(sawFar);
}

TEST_CASE("the stance keeps an enemy knocked over with health left as its target", "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (code). Player_UpdateSprint 0x0027ce90 drops a target only
    // when he is knocked out, dying or dead (or in a scene), never one knocked over who still has health, and
    // Player_FindNearestEnemy 0x0027b1a0 returns him while no standing threat is near.
    Fight fight(sweepCharacter(), 1.5F);
    std::vector<Sample> line = record(fight, "", 5);
    REQUIRE(line.back().locked);
    static constexpr std::array<std::uint32_t, 1> kDown{coney::human::clips::kIdle};
    fight.target().play(kDown, coney::human::clips::kIdle, coney::human::AnimState::Hold,
                        coney::human::TargetState::Grounded);
    REQUIRE(fight.target().state() == coney::human::TargetState::Grounded);
    REQUIRE_FALSE(fight.target().health().depleted());
    line = record(fight, "", 10);
    for (std::size_t update = 0; update < line.size(); ++update) {
        INFO("update " << update);
        CHECK(line[update].hasTarget);
    }
}

TEST_CASE("L1 held with a set-5 object in hand picks and locks nothing", "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (code). Player_UpdateActions 0x0027c120 does not call
    // Player_OnL1Held for a human holding a set-5 object.
    Fight fight(sweepCharacter(), 1.5F);
    fight.makeCalm(fight.target());
    fight.human().fighter().setAnimSet(5);
    const std::vector<Sample> line = record(fight, "0 press l1\n", 20);
    for (std::size_t update = 0; update < line.size(); ++update) {
        INFO("update " << update);
        CHECK_FALSE(line[update].hasTarget);
        CHECK_FALSE(line[update].locked);
    }
}

TEST_CASE("L1 tapped during an attack's recovery picks no target", "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (code). Player_UpdateActions 0x0027c120 returns on the busy
    // gate (record +0x08 & 0x5c7fee0, which holds the recovery 0x40000) before the stance update and the L1 handlers.
    const auto run = [](int press) {
        Fight fight(sweepCharacter(), 2.4F);
        fight.makeCalm(fight.target());
        std::string script = "2 tap square\n";
        if (press >= 0) {
            script += std::format("{} tap l1\n", press);
        }
        std::vector<std::uint32_t> flags;
        std::vector<bool> targeted;
        fight.run(script, 40, [&](std::uint64_t) {
            flags.push_back(fight.human().animator().flags());
            targeted.push_back(fight.human().fighter().target() != nullptr);
        });
        return std::pair{flags, targeted};
    };
    // The tap goes in on the first update of the recovery, so the press and its release both fall inside it.
    const auto probe = run(-1);
    const std::vector<std::uint32_t>& flags = probe.first;
    const auto recovery = std::ranges::find_if(flags, [](std::uint32_t f) { return (f & 0x40000U) != 0; });
    REQUIRE(recovery != flags.end());
    const auto start = static_cast<std::size_t>(recovery - flags.begin());
    REQUIRE(start + 2 < flags.size());
    REQUIRE((flags[start + 1] & 0x40000U) != 0);
    const std::vector<bool> targeted = run(static_cast<int>(start) + 1).second;
    for (std::size_t update = 0; update < targeted.size(); ++update) {
        INFO("update " << update);
        CHECK_FALSE(targeted[update]);
    }
}
