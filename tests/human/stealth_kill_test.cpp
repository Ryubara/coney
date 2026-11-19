// SPDX-License-Identifier: GPL-3.0-or-later
// The stealth kill (docs/research/stealth.md#stealth-kill, docs/research/combat-moves.md#stealth): a hidden player
// holding L1 behind a human who faces away presses square, cross or circle, and the pair plays, the killer's 637 (639
// with a knife, 641 with a baton or club) and the victim's side, the next id, from the killer's set; the victim takes
// 3000 and is knocked out. Synthetic clips; the player's buttons come from an input script through the street's command
// tables. Issue #242.
#include <array>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string_view>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "combat/commands.h"
#include "combat/power_class.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/human_flags.h"
#include "human/target_human.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::Human;
using coney::test::Fight;
using coney::test::FightCharacter;
namespace combat = coney::combat;

namespace {

// The fight's character, built once.
const FightCharacter& character() {
    static const FightCharacter built;
    return built;
}

// The clips of the kill, bare-handed (docs/research/combat-moves.md#stealth).
constexpr std::uint32_t kKill = 637;
constexpr std::uint32_t kKillReact = 638;
// The death chain's loop for a human who is not woundable (docs/research/combat-reactions.md#knockout-timing).
constexpr std::uint32_t kDeathLoop = 349;
// L1 held from the first update, the press on update 8.
constexpr std::string_view kSquareWithL1 = "0 press l1\n8 tap square\n";

// What a run of the kill left: the clips the killer and the victim played at the kill's start, and whether they did.
struct KillSeen {
    int powerAtKill = -1; // the killer's power on the kill's first update
    bool killerPlayed = false;
    bool victimPlayed = false;
    bool killerStruck = false; // the killer played anything of the fight's own moves (11-19: the square strikes)
};

// Runs `script` for `updates` on a Fight whose player hides first, and notes the kill's clips.
KillSeen runHidden(Fight& fight, std::string_view script, std::uint64_t updates, std::uint32_t kill = kKill) {
    fight.human().enterHiding();
    REQUIRE(fight.human().hidden());
    KillSeen seen;
    fight.run(script, updates, [&](std::uint64_t) {
        const std::uint32_t clip = fight.human().animator().animId();
        if (clip == kill && !seen.killerPlayed) {
            seen.powerAtKill = fight.human().fighter().combat().power().value();
        }
        seen.killerPlayed = seen.killerPlayed || clip == kill;
        seen.killerStruck = seen.killerStruck || (clip >= 11 && clip <= 19);
        seen.victimPlayed = seen.victimPlayed || fight.target().animator().animId() == kill + 1;
    });
    return seen;
}

// The player hidden at (40, 40) facing +y and an AI human 1 m ahead facing `enemyHeadingDegrees` (0: away from him),
// woundable, as level humans are made; each update steps the player, then the enemy.
class StealthFight {
  public:
    explicit StealthFight(float enemyHeadingDegrees = 0.0F)
        : m_mesh(coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F))),
          m_player(character().anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                   &character().ranges),
          m_enemy(character().anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                  &character().ranges) {
        m_enemy.setFighterProfile(
            coney::human::FighterProfile{.player = false, .powerClass = combat::kCivilianPowerClass, .health = 600});
        m_enemy.setFlag(coney::human::flag::kWoundable, true);
        m_player.spawn(m_mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
        m_enemy.spawn(m_mesh.get(), Vec3{40.0F, 41.0F, 0.0F}, enemyHeadingDegrees);
    }

    // Runs `frames` updates of the player's `script`, calling `each` after every update.
    template <typename Each> void run(std::string_view script, std::uint64_t frames, Each each) {
        for (const coney::test::PadFrame& frame : coney::test::playScript(script, frames)) {
            const combat::CommandId command =
                m_matcher.update(frame.buttons, m_tables, combat::combatTuning().historyHoldSamples);
            m_player.step(coney::human::HumanInput{.stickX = frame.leftX,
                                                   .stickY = frame.leftY,
                                                   .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                                   .sprintHeld = false,
                                                   .actionPressed = false,
                                                   .command = command,
                                                   .buttons = frame.buttons,
                                                   .targets = m_enemyOnly},
                          m_mesh.get());
            m_enemy.step(coney::human::HumanInput{.stickX = 0.0F,
                                                  .stickY = 0.0F,
                                                  .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                                  .sprintHeld = false,
                                                  .actionPressed = false,
                                                  .command = combat::command::kNone,
                                                  .buttons = 0,
                                                  .targets = {}},
                         m_mesh.get());
            each();
        }
    }

    Human& player() { return m_player; }
    Human& enemy() { return m_enemy; }

  private:
    std::unique_ptr<coney::raycast::CollisionMesh> m_mesh;
    Human m_player;
    Human m_enemy;
    std::array<coney::human::Combatant*, 1> m_enemyOnly{&m_enemy};
    combat::CommandTables m_tables = combat::CommandTables::street();
    combat::CommandMatcher m_matcher;
};

} // namespace

TEST_CASE("hidden with L1 held, square behind a target facing away starts the stealth kill",
          "[human][combat][stealth]") {
    // Expected: stealth.md#stealth-kill and combat-moves.md#stealth, confirmed (code) and confirmed (runtime).
    // Player_UpdateActionsHidden 0x0027e040: hidden, L1's bit (state 0x8) set, square (command 0xf) with the player in
    // the target's rear quarter (side 2) and the target in his front one (side 0), grabbable and the way clear, calls
    // Player_StartStealthKill 0x00264738: 637 bare-handed, the victim's 638 from the killer's set through
    // Attack_StartPaired 0x00262ac8, which spends the power endurance (0.25: 400 -> 300 at runtime) and deals the
    // range damage, 3000, a defeat for a 600-health target.
    Fight fight(character(), 1.0F, 0.0F, 0.0F);
    const KillSeen seen = runHidden(fight, kSquareWithL1, 30);
    CHECK(seen.killerPlayed);
    CHECK(seen.victimPlayed);
    CHECK_FALSE(seen.killerStruck);
    CHECK(fight.target().health().depleted());
    CHECK(seen.powerAtKill == 300);
}

TEST_CASE("hidden with L1 held, cross or circle behind a target facing away starts the stealth kill too",
          "[human][combat][stealth]") {
    // Expected: stealth.md#stealth-kill, confirmed (code). Player_UpdateActionsHidden 0x0027e040 runs the kill test
    // for square pressed (0xf), cross (0x10), circle held (0xe) and circle tapped (0xd).
    for (const char* script : {"0 press l1\n8 tap cross\n", "0 press l1\n8 tap circle\n",
                               "0 press l1\n8 press circle\n20 release circle\n"}) {
        INFO(script);
        Fight fight(character(), 1.0F, 0.0F, 0.0F);
        const KillSeen seen = runHidden(fight, script, 30);
        CHECK(seen.killerPlayed);
        CHECK(fight.target().health().depleted());
    }
}

TEST_CASE("hidden with L1 held, a press at a target facing the player does nothing", "[human][combat][stealth]") {
    // Expected: stealth.md#stealth-kill, confirmed (code). With L1's bit set the hidden buttons run only the kill
    // test (Player_UpdateActionsHidden 0x0027e040): a target who faces the player (the player on his side 0) fails it,
    // and the press returns with nothing played; the player stays hidden.
    Fight fight(character(), 1.0F);
    const KillSeen seen = runHidden(fight, kSquareWithL1, 30);
    CHECK_FALSE(seen.killerPlayed);
    CHECK_FALSE(seen.killerStruck);
    CHECK(fight.target().health().value() == fight.target().health().maximum());
    CHECK(fight.human().hidden());
}

TEST_CASE("the stealth kill needs more than a quarter of the power meter", "[human][combat][stealth]") {
    // Expected: combat-moves.md#stealth, confirmed (code). Attack_StartPaired 0x00262ac8 returns before anything plays
    // when the move deals damage and the power fraction is at most the power endurance (0x00510278, 0.25). The meter
    // empty at the start refills only a little before the press.
    Fight fight(character(), 1.0F, 0.0F, 0.0F);
    fight.human().fighter().combat().power().set(0);
    const KillSeen seen = runHidden(fight, kSquareWithL1, 30);
    CHECK_FALSE(seen.killerPlayed);
    CHECK(fight.target().health().value() == fight.target().health().maximum());
}

TEST_CASE("the stealth kill's clip follows the weapon held", "[human][combat][stealth]") {
    // Expected: stealth.md#stealth-kill, confirmed (code). Player_StartStealthKill 0x00264738: 639 with a set-1
    // weapon (the knife), 641 with set 2 or 3; the victim plays the next id.
    struct Case {
        int set;
        std::uint32_t kill;
    };
    for (const Case c : {Case{1, 639}, Case{2, 641}, Case{3, 641}}) {
        INFO("set " << c.set);
        Fight fight(character(), 1.0F, 0.0F, 0.0F);
        fight.human().fighter().setAnimSet(c.set);
        const KillSeen seen = runHidden(fight, kSquareWithL1, 30, c.kill);
        CHECK(seen.killerPlayed);
        CHECK(seen.victimPlayed);
    }
}

TEST_CASE("without L1, square from hiding behind a target facing away strikes as usual", "[human][combat][stealth]") {
    // Expected: stealth.md#stealth-kill, confirmed (code). Without state 0x8 Player_UpdateActionsHidden 0x0027e040
    // passes square to Player_Square 0x00286cc8, the normal strike.
    Fight fight(character(), 1.0F, 0.0F, 0.0F);
    const KillSeen seen = runHidden(fight, "8 tap square\n", 30);
    CHECK_FALSE(seen.killerPlayed);
    CHECK(seen.killerStruck);
}

TEST_CASE("a human killed from hiding is knocked out through the killer's 638 into the loop 349, never wounded",
          "[human][combat][stealth]") {
    // Expected: stealth.md#stealth-kill and combat-reactions.md#knockout-timing, confirmed (code).
    // Player_StartStealthKill 0x00264738 clears the victim's human flag 0x8 (woundable) before Attack_StartPaired
    // 0x00262ac8, whose defeat plays Human_PlayDeathReaction 0x00267a00 with the paired clip 638 from the killer's set
    // as the dying clip; not woundable, the chain loops 349, not 205 then 200. At runtime the victim's health fell at
    // once (knocked out).
    StealthFight fight;
    fight.player().enterHiding();
    bool reacted = false;
    bool looped = false;
    bool woundedDown = false;
    fight.run(kSquareWithL1, 150, [&] {
        const std::uint32_t clip = fight.enemy().animator().animId();
        reacted = reacted || clip == kKillReact;
        looped = looped || clip == kDeathLoop;
        woundedDown = woundedDown || clip == 205 || clip == 200;
    });
    CHECK(reacted);
    CHECK(looped);
    CHECK_FALSE(woundedDown);
    CHECK(fight.enemy().fighter().health().depleted());
    CHECK_FALSE(fight.enemy().hasFlag(coney::human::flag::kWoundable));
}

TEST_CASE("the stealth kill's end clears the hiding and leaves the killer in the fight idle",
          "[human][combat][stealth]") {
    // Expected: stealth.md#stealth-kill and combat-moves.md#stealth, inferred and confirmed (runtime). The pair's end
    // (Human_BreakPair) clears the hidden state; at runtime 637 ran 52 updates, then the fight idle 358.
    Fight fight(character(), 1.0F, 0.0F, 0.0F);
    bool killed = false;
    bool hiddenDuringKill = true;
    std::uint32_t clipAfter = 0;
    bool hiddenAfter = true;
    fight.human().enterHiding();
    fight.run(kSquareWithL1, 80, [&](std::uint64_t) {
        const std::uint32_t clip = fight.human().animator().animId();
        if (clip == kKill) {
            killed = true;
            hiddenDuringKill = hiddenDuringKill && fight.human().hidden();
        } else if (killed && clipAfter == 0) {
            clipAfter = clip;
            hiddenAfter = fight.human().hidden();
        }
    });
    REQUIRE(killed);
    CHECK(hiddenDuringKill);
    CHECK(clipAfter == coney::human::kAnimFightIdle);
    CHECK_FALSE(hiddenAfter);
}
