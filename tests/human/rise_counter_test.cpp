// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <format>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_task.h"
#include "combat/attacks.h"
#include "combat/being_hit.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/human_flags.h"
#include "human/target_human.h"
#include "human/victim.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// The counter from the rise (docs/research/combat-reactions.md#ground-escape): a player knocked down by the crushing
// special 653 (stunned until after his rise) gets up with 199, and a square or cross whose command reaches him while
// the rise's own 0x25 events fire counters with the duck's counter 617-620 by the side his target stands on. The
// synthetic 199 is the disc's: 21 frames at the unflagged rate 0.75, with event 0x25 on frames 11-18 and the recovery
// 0x48 on frame 14. The tests hold Coney to the runtime's edges, k = 15-17 counted from 199's first update, not to the
// frames through a rate.

using coney::anim::Vec3;
using coney::human::IncomingHit;
using coney::test::Fight;
using coney::test::FightCharacter;
namespace combat = coney::combat;

namespace {

// The rise and the counters' clips.
constexpr std::uint32_t kRise = 199;
constexpr std::uint32_t kCounterFront = 617;
constexpr std::uint32_t kCounterLeft = 620;
constexpr std::uint32_t kStunEnd = 357;
// The puppet of the runtime stands 0.8 m to the player's left (the player at (40, 40) facing +y).
constexpr float kLeft = -0.8F;
// How long the knockdown, the 2750 ms on the ground and the rise may take, in updates.
constexpr int kRiseWithin = 120;

// The fight's clips with 199 as on the disc: 21 frames (0.7 s) at rate 0.75, events 0x25 on frames 11-18 and 0x48 on
// frame 14.
std::vector<coney::test::LocomotionClip> riseClips() {
    std::vector<coney::test::LocomotionClip> clips;
    for (coney::test::LocomotionClip clip : coney::test::fightClips()) {
        if (clip.id == kRise) {
            clip.duration = 21.0F / 30.0F;
            for (std::uint16_t frame = 11; frame <= 18; ++frame) {
                clip.markers.push_back({frame, combat::kDuckCounterEvent});
            }
            clip.markers.push_back({14, coney::anim::kEventRecovery});
        }
        clips.push_back(clip);
    }
    return clips;
}

// The character with that 199 (the fight's ranges give the counters 617-620 their 50 damage).
struct RiseCharacter {
    FightCharacter character{riseClips()};
};

// The crushing special 653 (code 0x39, stunning) from `attacker`, as the runtime's puppet knocked the player down.
IncomingHit crushingFrom(Vec3 attacker) {
    IncomingHit hit;
    hit.damage = 50;
    hit.attackAnim = 653;
    hit.code = 0x39;
    hit.flags = 0x400;
    hit.attacker = attacker;
    return hit;
}

// What one run left: the clip the player played on each update from 199's first (k = 0), and the attacker's health.
struct RiseRun {
    std::vector<std::uint32_t> clips;
    int attackerHealth = 0;
    int attackerKnockdowns = 0;
    int riseCounters = 0;
    bool stunnedAfter = false;
};

// Knocks the player down with 653 from the attacker 0.8 m to his left, waits for 199, then plays `script` (its frame 0
// is k = 1) for `updates` more updates. `player` false makes him a human no player controls.
RiseRun riseWith(const FightCharacter& character, const std::string& script, int updates = 60, bool player = true,
                 std::uint64_t flags = 0, float sideways = kLeft) {
    Fight fight(character, 0.0F, sideways, -std::numbers::pi_v<float> / 2.0F);
    if (!player) {
        fight.human().setFighterProfile(coney::human::FighterProfile{.player = false});
    }
    if (flags != 0) {
        fight.human().setFlag(flags, true);
    }
    fight.human().takeHit(crushingFrom(Vec3{40.0F + sideways, 40.0F, 0.0F}));
    RiseRun run;
    for (int update = 0; update < kRiseWithin && fight.human().animator().animId() != kRise; ++update) {
        fight.run("", 1);
    }
    REQUIRE(fight.human().animator().animId() == kRise);
    REQUIRE(fight.human().fighter().victim().stunned());
    run.clips.push_back(kRise);
    const int healthBefore = fight.target().health().value();
    fight.run(script, static_cast<std::uint64_t>(updates),
              [&](std::uint64_t) { run.clips.push_back(fight.human().animator().animId()); });
    run.attackerHealth = healthBefore - fight.target().health().value();
    run.attackerKnockdowns = fight.target().knockdowns();
    run.riseCounters = fight.human().fighter().riseCounters();
    run.stunnedAfter = fight.human().fighter().victim().stunned();
    return run;
}

// The first k at which `clip` plays, or -1.
int firstK(const RiseRun& run, std::uint32_t clip) {
    for (std::size_t k = 0; k < run.clips.size(); ++k) {
        if (run.clips[k] == clip) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

// A tap of `button` whose press reaches the player at k (the script's frame 0 is k = 1).
std::string tapAt(const char* button, int k) { return std::format("{} tap {}\n", k - 1, button); }

} // namespace

TEST_CASE("square in the rise's window, k = 15-17, counters with 620 that update at the attacker on his left",
          "[human][combat]") {
    const RiseCharacter rise;
    for (int k = 15; k <= 17; ++k) {
        INFO("square at k = " << k);
        const RiseRun run = riseWith(rise.character, tapAt("square", k));
        CHECK(firstK(run, kCounterLeft) == k);
        CHECK(run.riseCounters == 1);
        // The counter ends the stun: no 357 after it.
        CHECK_FALSE(run.stunnedAfter);
        CHECK(firstK(run, kStunEnd) == -1);
        // The counter's hit lands on the attacker for 50 and knocks him down, as at runtime.
        CHECK(run.attackerHealth == 50);
        CHECK(run.attackerKnockdowns == 1);
    }
}

TEST_CASE("square before or after the rise's window does nothing: he rises into the stun's end", "[human][combat]") {
    const RiseCharacter rise;
    for (const int k : {9, 12, 14, 18, 19, 20, 24}) {
        INFO("square at k = " << k);
        const RiseRun run = riseWith(rise.character, tapAt("square", k));
        for (std::uint32_t counter = kCounterFront; counter <= kCounterLeft; ++counter) {
            CHECK(firstK(run, counter) == -1);
        }
        CHECK(run.riseCounters == 0);
        CHECK(run.attackerHealth == 0);
        CHECK(firstK(run, kStunEnd) > 0);
    }
}

TEST_CASE("cross in the rise's window counters: its release's command counts as its press's does", "[human][combat]") {
    const RiseCharacter rise;
    // Cross pressed at k (0x12), released at k + 1 (the long hold 0x10): pressed at 14 counters at 15 on the release,
    // at 16 on the press; pressed at 18 is too late for both.
    struct Case {
        int pressed;
        int counter;
    };
    for (const Case c : {Case{14, 15}, Case{15, 15}, Case{16, 16}, Case{18, -1}}) {
        INFO("cross pressed at k = " << c.pressed);
        const RiseRun run = riseWith(rise.character, tapAt("cross", c.pressed));
        CHECK(firstK(run, kCounterLeft) == c.counter);
    }
}

TEST_CASE("the counter from the rise plays by the side its target stands on: 618 to his right", "[human][combat]") {
    const RiseCharacter rise;
    const RiseRun run = riseWith(rise.character, tapAt("square", 16), 60, true, 0, 0.8F);
    CHECK(firstK(run, kCounterFront + 1) == 16);
}

TEST_CASE("no counter from the rise without a target in reach, for an AI, or stunned with the manual stun",
          "[human][combat]") {
    const RiseCharacter rise;
    // The attacker 3 m away: beyond 1.25 x 617's reach.
    {
        const RiseRun run = riseWith(rise.character, tapAt("square", 16), 60, true, 0, -3.0F);
        CHECK(run.riseCounters == 0);
        CHECK(firstK(run, kStunEnd) > 0);
    }
    // A human no player controls: his Grounded goal says no through the whole rise.
    {
        const RiseRun run = riseWith(rise.character, tapAt("square", 16), 60, false);
        CHECK(run.riseCounters == 0);
    }
    // Stunned and flagged 0x8000000 (HuSetManualStun): the message is refused.
    {
        const RiseRun run = riseWith(rise.character, tapAt("square", 16), 60, true, coney::human::flag::kManualStun);
        CHECK(run.riseCounters == 0);
    }
}

TEST_CASE("a square pressed in the window with no target in reach lasts until one comes into reach, while the rise is "
          "free",
          "[human][combat]") {
    const RiseCharacter rise;
    // The attacker is 3 m away at the press (k = 16, in the window): no counter yet. He then steps to 0.8 m on the
    // player's left at update `arrives`: Coney's stand-in keeps the request (record +0x14) until a target is found or
    // the rise stops being free (the recovery holds 0x40000 from k = 19).
    struct Case {
        int arrives;
        int counter;
    };
    constexpr float kFacing = -std::numbers::pi_v<float> / 2.0F;
    for (const Case c : {Case{17, 17}, Case{18, 18}, Case{19, -1}, Case{22, -1}}) {
        INFO("the attacker arrives at k = " << c.arrives);
        Fight fight(rise.character, 0.0F, -3.0F, kFacing);
        fight.human().takeHit(crushingFrom(Vec3{37.0F, 40.0F, 0.0F}));
        for (int update = 0; update < kRiseWithin && fight.human().animator().animId() != kRise; ++update) {
            fight.run("", 1);
        }
        REQUIRE(fight.human().animator().animId() == kRise);
        RiseRun run;
        run.clips.push_back(kRise);
        const auto record = [&](std::uint64_t) { run.clips.push_back(fight.human().animator().animId()); };
        // k = 1 to arrives - 1, the square reaching the player at k = 16.
        fight.run(tapAt("square", 16), static_cast<std::uint64_t>(c.arrives - 1), record);
        REQUIRE(fight.human().fighter().riseCounters() == 0);
        fight.target().place(Vec3{40.0F + kLeft, 40.0F, 0.0F}, kFacing);
        fight.run("", 40, record);
        CHECK(firstK(run, kCounterLeft) == c.counter);
        CHECK(fight.human().fighter().riseCounters() == (c.counter > 0 ? 1 : 0));
    }
}

TEST_CASE("the rise holds 0x8000 until its recovery event, which holds 0x40000 until the rise hands over to the stun",
          "[human][combat]") {
    const RiseCharacter rise;
    Fight fight(rise.character, 0.0F, kLeft);
    fight.human().takeHit(crushingFrom(Vec3{40.0F + kLeft, 40.0F, 0.0F}));
    for (int update = 0; update < kRiseWithin && fight.human().animator().animId() != kRise; ++update) {
        fight.run("", 1);
    }
    REQUIRE(fight.human().animator().animId() == kRise);
    std::vector<std::uint32_t> held;
    fight.run("", 40, [&](std::uint64_t) { held.push_back(fight.human().animator().flags()); });
    // k = 1 to 18 hold the rise's bit; from k = 19 (frame 14 at 0.75) the recovery's.
    CHECK((held.at(0) & combat::kPhaseRise) != 0);
    CHECK((held.at(17) & combat::kPhaseRise) != 0);
    CHECK((held.at(18) & combat::kPhaseRecovery) != 0);
    CHECK((held.at(18) & combat::kPhaseRise) == 0);
    // 199 (28 updates at 0.75) ends in the stun's loop, which holds nothing: both bits are given back.
    CHECK(fight.human().animator().animId() != kRise);
    CHECK((held.back() & (combat::kPhaseRise | combat::kPhaseRecovery)) == 0);
}
