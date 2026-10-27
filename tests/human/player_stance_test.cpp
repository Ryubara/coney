// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "combat/anim_ids.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/target_human.h"
#include "support/fight_fixtures.h"

// When the player's fight stance is entered and the lock-on walk comes back: a grab, a tackle or a miss (and the lock
// through their intro), after a throw on another enemy (the stance update's step 3 gates on the lock-on movement, not
// on the lock), and a grab or attack from hiding ending the hiding (`Player_GrabOrTackle` 0x00284920,
// `Player_UpdateSprint` 0x0027ce90, `Player_EnterStance` 0x00280068, docs/research/combat-targets.md#fight-stance).
// The throw with L1 held, the special 653 and hidden L1 are in human_grab_test.cpp. Issue #82.

using coney::test::Fight;
using coney::test::FightCharacter;
using coney::test::throwAhead;
using coney::test::throwOver;
namespace id = coney::combat::anim_id;

namespace {

// The fight's character, built once.
const FightCharacter& character() {
    static const FightCharacter built;
    return built;
}

} // namespace

TEST_CASE("after a throw another enemy within 2 m becomes the target and the lock-on walk comes back at once",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (code). Player_UpdateSprint 0x0027ce90's step 3 tests that
    // the movement is not Human_FightStanceMove, not the lock bit: on the throw's free movement (Human_MoveGrabbing
    // 0x00245310) it runs with the thrown body still the target, sets the threat within the near distance (2 m) as the
    // target and locks it (Player_LockOn 0x00280158), so there is no update with no target in between.
    Fight fight(character(), 1.5F);
    const coney::human::TargetHuman& other = fight.addTarget(character(), 0.0F, -1.9F);
    const auto [line, thrown] = throwAhead(fight, false);
    REQUIRE(thrown > 0);
    REQUIRE(line[static_cast<std::size_t>(thrown) - 1].target == &fight.target());
    const auto over = throwOver(line, thrown);
    REQUIRE(over != line.end());
    bool tookOther = false;
    for (auto it = line.begin() + thrown; it != line.end(); ++it) {
        INFO("update " << (it - line.begin()));
        CHECK(it->target != nullptr);
        tookOther = tookOther || it->target == &other;
        if (tookOther) {
            CHECK(it->target == &other);
            CHECK(it->lockWalk);
        }
    }
    CHECK(tookOther);
}

namespace {

// What one update of a grab or tackle left: the player's clip, whether he is in the stance and whom he is locked on.
struct GrabSample {
    std::uint32_t clip = 0;
    bool stance = false;
    const coney::human::Combatant* locked = nullptr;
};

// Runs `script` for `updates` updates and samples each one.
std::vector<GrabSample> sampleGrab(Fight& fight, const std::string& script, std::uint64_t updates) {
    std::vector<GrabSample> line;
    fight.run(script, updates, [&](std::uint64_t) {
        const coney::human::Fighter& fighter = fight.human().fighter();
        line.push_back(GrabSample{.clip = fight.human().animator().animId(),
                                  .stance = fighter.inFightStance(),
                                  .locked = fighter.lockTarget()});
    });
    return line;
}

// The updates on which `clip` plays.
std::vector<GrabSample> playing(const std::vector<GrabSample>& line, int clip) {
    std::vector<GrabSample> found;
    std::ranges::copy_if(line, std::back_inserter(found),
                         [clip](const GrabSample& s) { return s.clip == static_cast<std::uint32_t>(clip); });
    return found;
}

} // namespace

TEST_CASE("a grab puts the player in the fight stance and locks the victim during its intro",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (code). Player_GrabOrTackle 0x00284920 calls
    // Player_EnterStance 0x00280068 before Grab_Start 0x0026c548, which sets only the record's 0x10; the grabbing state
    // (0xc0) comes at the connect, so through the intro Player_UpdateActions 0x0027c120 still runs the stance update,
    // whose step 3 (Player_UpdateSprint 0x0027ce90) locks the victim within 2 m (Player_FindNearestEnemy falls back on
    // the target held). The victim is calm, so nothing but the grab enters the stance.
    Fight fight(character(), 1.5F);
    fight.makeCalm(fight.target());
    const std::vector<GrabSample> line = sampleGrab(fight, "10 tap circle\n", 30);
    CHECK_FALSE(line.front().stance);
    const std::vector<GrabSample> intro = playing(line, id::kGrabPlayerIntro);
    REQUIRE(intro.size() > 2);
    for (const GrabSample& s : intro) {
        CHECK(s.stance);
    }
    CHECK(std::ranges::any_of(intro, [&](const GrabSample& s) { return s.locked == &fight.target(); }));
}

TEST_CASE("a tackle puts the player in the fight stance and locks the victim during its intro",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (code). Player_GrabOrTackle 0x00284920 calls
    // Player_EnterStance 0x00280068 before Tackle_Start 0x002707a8, which sets only the record's 0x10; the tackling
    // state (0x400) comes from Tackle_StartMount at the intro's end (Tackle_OnIntroEnd 0x00270560), so the stance
    // update's step 3 locks the victim within 2 m through the intro.
    Fight fight(character(), 1.5F);
    fight.makeCalm(fight.target());
    const std::vector<GrabSample> line = sampleGrab(fight, "5 press circle\n15 release circle\n", 30);
    CHECK_FALSE(line.front().stance);
    const std::vector<GrabSample> intro = playing(line, id::kTacklePlayerIntro);
    REQUIRE(intro.size() > 2);
    for (const GrabSample& s : intro) {
        CHECK(s.stance);
    }
    CHECK(std::ranges::any_of(intro, [&](const GrabSample& s) { return s.locked == &fight.target(); }));
}

TEST_CASE("a grab that misses puts the player in the fight stance", "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (code). Player_GrabOrTackle 0x00284920 calls
    // Player_EnterStance 0x00280068 before Grab_Start whether or not a target was found; nobody stands within 2 m, so
    // nothing else enters it.
    Fight fight(character(), 6.0F);
    const std::vector<GrabSample> line = sampleGrab(fight, "5 tap circle\n", 20);
    const std::vector<GrabSample> intro = playing(line, id::kGrabPlayerIntro);
    REQUIRE_FALSE(intro.empty());
    for (const GrabSample& s : intro) {
        CHECK(s.stance);
    }
}

TEST_CASE("a tackle that misses puts the player in the fight stance", "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (code). Player_GrabOrTackle 0x00284920 calls
    // Player_EnterStance 0x00280068 before Tackle_Start whether or not a target was found; nobody stands within 2 m,
    // so nothing else enters it.
    Fight fight(character(), 6.0F);
    const std::vector<GrabSample> line = sampleGrab(fight, "5 press circle\n15 release circle\n", 30);
    const std::vector<GrabSample> intro = playing(line, id::kTacklePlayerIntro);
    REQUIRE_FALSE(intro.empty());
    for (const GrabSample& s : intro) {
        CHECK(s.stance);
    }
}

TEST_CASE("a grab or an attack from hiding ends the hiding", "[human][combat][targeting][stealth]") {
    // Expected: combat-targets.md#fight-stance and stealth.md#hidden, confirmed (code). Player_EnterStance 0x00280068
    // calls Human_ClearHiddenState 0x00230140 first; hidden, the buttons go to Player_UpdateActionsHidden 0x0027e040,
    // which calls Player_GrabOrTackle 0x00284920 and Player_Square 0x00286cc8, both of which enter the stance. The
    // target is calm, so the stance's own update would not take him.
    for (const char* script : {"5 tap circle\n", "5 tap square\n"}) {
        INFO(script);
        Fight fight(character(), 1.5F);
        fight.makeCalm(fight.target());
        fight.human().enterHiding();
        REQUIRE(fight.human().hidden());
        bool started = false;
        fight.run(script, 20, [&](std::uint64_t) {
            const auto clip = static_cast<int>(fight.human().animator().animId());
            if (!started && (clip == id::kGrabPlayerIntro || fight.human().fighter().inFightStance())) {
                started = true;
                CHECK(fight.human().fighter().inFightStance());
                CHECK_FALSE(fight.human().hidden());
            }
        });
        CHECK(started);
    }
}

TEST_CASE("leaving the shadow with a target, the hidden state lasts until the 4 s grace's end is past",
          "[human][targeting][stealth]") {
    // Expected: stealth.md#hidden, confirmed (code). Human_UpdateActions 0x00254e78 ends the grace when its end is
    // below the game time, so the update whose time is the end itself is still hidden.
    Fight fight(character(), 1.5F);
    fight.run("", 5);
    REQUIRE(fight.human().fighter().target() != nullptr);
    fight.human().enterHiding();
    fight.human().leaveHiding();
    REQUIRE(fight.human().hidden());
    const std::uint64_t end = fight.human().nowMs() + coney::human::Human::kHideGraceMs;
    std::uint64_t lastHidden = 0;
    std::uint64_t firstOut = 0;
    fight.run("", 150, [&](std::uint64_t) {
        if (fight.human().hidden()) {
            lastHidden = fight.human().nowMs();
        } else if (firstOut == 0) {
            firstOut = fight.human().nowMs();
        }
    });
    CHECK(lastHidden == end);
    CHECK(firstOut > end);
}
