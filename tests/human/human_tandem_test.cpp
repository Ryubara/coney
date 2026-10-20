// SPDX-License-Identifier: GPL-3.0-or-later
// The tandem played by three humans (human::Fighter, fighter_tandem.cpp; docs/research/combat-moves.md#tandem): an ally
// (a human no player controls) grabs an enemy from behind, the player in front presses square, cross or circle, and the
// three play the set's clips together; the victim takes the attacker's intro and end damage when his intro ends, and
// the grabber is let go into the fight stance. Synthetic clips as long as the disc's; the player's buttons come from an
// input script, the ally's command from the test as its brain would write it.
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "combat/anim_ids.h"
#include "combat/commands.h"
#include "combat/player_combat.h"
#include "combat/power_class.h"
#include "combat/tandem.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::Human;
using coney::human::TargetState;
using coney::test::FightCharacter;
namespace combat = coney::combat;

namespace {

// The enemy at (40, 42) facing +y, the ally `behind` metres behind him facing the same way, and the player
// `ahead` metres in front of him, `sideways` to his right, facing him. Each update steps the player (fighting the
// enemy), then the ally (fighting the enemy, with the command `allyCommand` gives), then the enemy.
class TandemFight {
  public:
    explicit TandemFight(const FightCharacter& character, float ahead = 1.8F, float sideways = 0.0F,
                         float behind = 1.0F, int enemyHealth = 600)
        : m_mesh(coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F))),
          m_player(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                   &character.ranges),
          m_ally(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                 &character.ranges),
          m_enemy(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                  &character.ranges) {
        const coney::human::FighterProfile ai{
            .player = false, .powerClass = combat::kCivilianPowerClass, .health = enemyHealth};
        m_ally.setFighterProfile(
            coney::human::FighterProfile{.player = false, .powerClass = combat::kCivilianPowerClass, .health = 600});
        m_enemy.setFighterProfile(ai);
        m_enemy.spawn(m_mesh.get(), kEnemy, 0.0F);
        // Behind him he faces the same way; a negative `behind` puts him in front, facing the enemy.
        m_ally.spawn(m_mesh.get(), Vec3{kEnemy.x, kEnemy.y - behind, 0.0F}, behind >= 0.0F ? 0.0F : 180.0F);
        // The player faces the enemy from where he stands.
        const Vec3 at{kEnemy.x + sideways, kEnemy.y + ahead, 0.0F};
        const float towards = std::atan2(-(kEnemy.x - at.x), kEnemy.y - at.y) * 180.0F / std::numbers::pi_v<float>;
        m_player.spawn(m_mesh.get(), at, towards);
    }

    // Runs `frames` updates of the player's `script`, calling `each` after every update with its index.
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
                                                   .targets = m_playerTargets},
                          m_mesh.get());
            m_ally.step(coney::human::HumanInput{.stickX = 0.0F,
                                                 .stickY = 0.0F,
                                                 .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                                 .sprintHeld = false,
                                                 .actionPressed = false,
                                                 .command = allyCommand ? allyCommand(m_frame) : combat::command::kNone,
                                                 .buttons = 0,
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
            each(m_frame++);
        }
    }
    // Runs `frames` updates of `script`.
    void run(std::string_view script, std::uint64_t frames) {
        run(script, frames, [](std::uint64_t /*frame*/) {});
    }
    // The ally grabs the enemy from behind (circle tapped at update 2) and the hold settles.
    void allyHoldsFromBehind() {
        allyCommand = [](std::uint64_t frame) {
            return frame == 2 ? combat::command::kCircleTapped : combat::command::kNone;
        };
        run("", 40);
    }

    // Adds a bystander the player could fight too, at `at`, facing +y. Returns it.
    coney::human::TargetHuman& addBystander(const FightCharacter& character, Vec3 at) {
        m_bystanders.push_back(std::make_unique<coney::human::TargetHuman>(
            character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 600, at, 0.0F));
        m_playerTargets.push_back(m_bystanders.back().get());
        return *m_bystanders.back();
    }

    Human& player() { return m_player; }
    Human& ally() { return m_ally; }
    Human& enemy() { return m_enemy; }
    // The ally's command on each update (its brain's, in play): none when empty.
    std::function<combat::CommandId(std::uint64_t)> allyCommand;

    static constexpr Vec3 kEnemy{40.0F, 42.0F, 0.0F};

  private:
    std::unique_ptr<coney::raycast::CollisionMesh> m_mesh;
    Human m_player;
    Human m_ally;
    Human m_enemy;
    std::array<coney::human::Combatant*, 1> m_enemyOnly{&m_enemy};
    std::vector<std::unique_ptr<coney::human::TargetHuman>> m_bystanders;
    std::vector<coney::human::Combatant*> m_playerTargets{&m_enemy}; // the enemy and the bystanders
    combat::CommandTables m_tables = combat::CommandTables::street();
    combat::CommandMatcher m_matcher;
    std::uint64_t m_frame = 0;
};

// What one tandem did, update by update.
struct TandemRecord {
    int start = -1; // the update the player's intro started
    int base = combat::anim_id::kNone;
    int allyStart = -1;         // the update the ally's intro started
    int enemyStart = -1;        // the update the enemy's intro started
    int damageAt = -1;          // the update the enemy's health dropped
    int healthAfter = 0;        // and to what
    int enemyEndAt = -1;        // the update the enemy's end clip started
    int allyFreeAt = -1;        // the update the ally first stood in the fight idle holding no one after the start
    float standOff = 0.0F;      // the player's distance to the enemy when the player's end clip started
    bool groundedAfter = false; // the enemy lay on the ground on the first update after his end clip
    int afterEndAt = -1;        // that update
};

// Runs `script` on `fight` for `frames` updates and records the tandem.
TandemRecord recordTandem(TandemFight& fight, std::string_view script, std::uint64_t frames) {
    TandemRecord out;
    const int startHealth = fight.enemy().fighter().health().value();
    fight.run(script, frames, [&](std::uint64_t index) {
        const auto frame = static_cast<int>(index);
        const auto player = static_cast<int>(fight.player().animator().animId());
        const auto ally = static_cast<int>(fight.ally().animator().animId());
        const auto enemy = static_cast<int>(fight.enemy().animator().animId());
        if (out.start < 0 && player >= combat::kTandem1 && player < combat::kTandem1 + 30 &&
            (player - combat::kTandem1) % 6 == 2) {
            out.start = frame;
            out.base = player - 2;
        }
        if (out.base == combat::anim_id::kNone) {
            return;
        }
        if (out.allyStart < 0 && ally == out.base) {
            out.allyStart = frame;
        }
        if (out.enemyStart < 0 && enemy == out.base + 1) {
            out.enemyStart = frame;
        }
        if (out.damageAt < 0 && fight.enemy().fighter().health().value() != startHealth) {
            out.damageAt = frame;
            out.healthAfter = fight.enemy().fighter().health().value();
        }
        if (out.enemyEndAt < 0 && enemy == out.base + 4) {
            out.enemyEndAt = frame;
        }
        if (out.allyFreeAt < 0 && out.allyStart >= 0 && ally == static_cast<int>(coney::human::kAnimFightIdle) &&
            fight.ally().fighter().held() == nullptr) {
            out.allyFreeAt = frame;
        }
        if (out.enemyEndAt >= 0 && out.afterEndAt < 0 && enemy != out.base + 4) {
            out.afterEndAt = frame;
            out.groundedAfter = fight.enemy().state() == TargetState::Grounded;
        }
        if (player == out.base + 5 && out.standOff == 0.0F) {
            const Vec3 to = coney::anim::subtract(fight.enemy().position(), fight.player().position());
            out.standOff = std::hypot(to.x, to.y);
        }
    });
    return out;
}

// The intro's length in updates of set `base` (docs/research/combat-moves.md#frame-data).
int introUpdates(int base) {
    switch (base) {
    case combat::kTandem1:
        return 36;
    case combat::kTandem2:
        return 47;
    case combat::kTandem3:
        return 11;
    case combat::kBatTandem:
        return 14;
    default:
        return 68;
    }
}

} // namespace

TEST_CASE("an ally's grab from behind holds the enemy in state 0x20, which another human sees", "[human][tandem]") {
    const FightCharacter character;
    TandemFight fight(character);
    fight.allyHoldsFromBehind();
    REQUIRE(fight.ally().fighter().held() == &fight.enemy());
    CHECK(fight.ally().fighter().fromRear());
    CHECK(fight.enemy().state() == TargetState::Held);
    CHECK(fight.enemy().heldFromRear());
}

TEST_CASE("square at an enemy an ally holds from behind plays the tandem: the three intros, 400 at the victim's intro "
          "end, the ally let go into the fight stance",
          "[human][tandem]") {
    const FightCharacter character;
    TandemFight fight(character);
    fight.allyHoldsFromBehind();
    const TandemRecord tandem = recordTandem(fight, "2 tap square\n", 200);
    REQUIRE(tandem.start >= 0);
    INFO("set " << tandem.base << ": start " << tandem.start << ", enemy " << tandem.enemyStart << ", enemy end "
                << tandem.enemyEndAt << ", damage " << tandem.damageAt << ", ally " << tandem.allyStart
                << ", ally free " << tandem.allyFreeAt);
    CHECK((tandem.base == combat::kTandem1 || tandem.base == combat::kTandem2 || tandem.base == combat::kTandem3));
    // The three start together: the enemy on the player's update, the ally on its own update after it.
    CHECK(tandem.enemyStart == tandem.start);
    CHECK(tandem.allyStart == tandem.start);
    // 50 + 350 as the enemy's intro hands over to his end clip.
    REQUIRE(tandem.damageAt >= 0);
    CHECK(tandem.healthAfter == 600 - 400);
    CHECK(tandem.damageAt == tandem.enemyEndAt);
    CHECK(std::abs(tandem.enemyEndAt - tandem.start - introUpdates(tandem.base)) <= 1);
    // The ally lets go after his end clip, into the fight idle (358), holding no one.
    REQUIRE(tandem.allyFreeAt > tandem.enemyEndAt);
    CHECK(fight.ally().fighter().held() == nullptr);
    CHECK(fight.ally().fighter().combat().mode() == combat::CombatMode::Free);
    // The player was put at his intro's reach from the enemy (1.72, 1.46 or 1.69 m).
    const float reach = tandem.base == combat::kTandem1 ? 1.72F : (tandem.base == combat::kTandem2 ? 1.46F : 1.69F);
    CHECK(tandem.standOff == Approx(reach).margin(0.1F));
    // The enemy lies on the ground after his end clip, his hold over; the player and the ally are free to fight.
    CHECK(tandem.groundedAfter);
    CHECK_FALSE(fight.enemy().fighter().inPair());
    CHECK_FALSE(fight.player().fighter().holdsMovement(fight.player().animator()));
}

TEST_CASE("the tandem's start warns its victim alone, not a bystander", "[human][tandem]") {
    // docs/research/combat.md#attack-warnings: the tandem start passes its victim to Human_WarnOfAttack; a bystander
    // the player could fight, 3 m to the side, hears nothing of it.
    const FightCharacter character;
    TandemFight fight(character);
    const coney::human::TargetHuman& bystander =
        fight.addBystander(character, Vec3{TandemFight::kEnemy.x + 3.0F, TandemFight::kEnemy.y + 1.8F, 0.0F});
    fight.allyHoldsFromBehind();
    static_cast<void>(fight.enemy().takeAttackAnnouncements());
    const TandemRecord tandem = recordTandem(fight, "2 tap square\n", 60);
    REQUIRE(tandem.start >= 0);
    CHECK(fight.enemy().takeAttackAnnouncements().size() == 1);
    CHECK(bystander.attacksAnnounced() == 0);
}

TEST_CASE("cross, circle tapped and circle held start the tandem as square does", "[human][tandem]") {
    const FightCharacter character;
    for (const std::string_view script : {"2 tap cross\n", "2 tap circle\n", "2 press circle\n10 release circle\n"}) {
        TandemFight fight(character);
        fight.allyHoldsFromBehind();
        const TandemRecord tandem = recordTandem(fight, script, 120);
        CHECK(tandem.start >= 0);
        CHECK(tandem.healthAfter == 200);
        // Circle does not grab the enemy away from the ally.
        CHECK(fight.player().fighter().held() == nullptr);
    }
}

TEST_CASE("no tandem from the victim's side, at a victim held from the front, or with nobody holding him",
          "[human][tandem]") {
    const FightCharacter character;
    SECTION("from his side") {
        // 1.0 m ahead and 1.5 m to his right: beyond the 45-degree front cone.
        TandemFight fight(character, 1.0F, 1.5F);
        fight.allyHoldsFromBehind();
        const TandemRecord tandem = recordTandem(fight, "2 tap square\n", 60);
        CHECK(tandem.start < 0);
    }
    SECTION("held from the front") {
        // The ally stands in front of the enemy, facing him: his grab is a front hold.
        TandemFight fight(character, 1.8F, 0.0F, -1.0F);
        fight.allyHoldsFromBehind();
        REQUIRE(fight.enemy().state() == TargetState::Held);
        CHECK_FALSE(fight.enemy().heldFromRear());
        const TandemRecord tandem = recordTandem(fight, "2 tap square\n", 60);
        CHECK(tandem.start < 0);
    }
    SECTION("standing free") {
        TandemFight fight(character);
        const TandemRecord tandem = recordTandem(fight, "2 tap square\n", 60);
        CHECK(tandem.start < 0);
    }
}

TEST_CASE("a bat in hand plays the bat's set and deals 50 + 500", "[human][tandem]") {
    const FightCharacter character;
    TandemFight fight(character);
    fight.player().fighter().setAnimSet(3);
    fight.allyHoldsFromBehind();
    const TandemRecord tandem = recordTandem(fight, "2 tap square\n", 200);
    REQUIRE(tandem.start >= 0);
    CHECK(tandem.base == combat::kBatTandem);
    CHECK(tandem.healthAfter == 50);
    CHECK(std::abs(tandem.damageAt - tandem.start - 14) <= 1);
}

TEST_CASE("a lethal tandem lets the victim finish his end clip before he dies", "[human][tandem]") {
    const FightCharacter character;
    // 300 health: 400 defeats him (Human_IsDamageDefeating).
    TandemFight fight(character, 1.8F, 0.0F, 1.0F, 300);
    fight.allyHoldsFromBehind();
    bool clipKeptAfterDeath = false;
    int deadAt = -1;
    int base = combat::anim_id::kNone;
    fight.run("2 tap square\n", 200, [&](std::uint64_t frame) {
        const auto player = static_cast<int>(fight.player().animator().animId());
        if (base == combat::anim_id::kNone && player >= combat::kTandem1 && player < combat::kTandem1 + 30) {
            base = player - 2;
        }
        if (deadAt < 0 && fight.enemy().fighter().health().depleted()) {
            deadAt = static_cast<int>(frame);
        }
        if (deadAt >= 0 && static_cast<int>(frame) == deadAt + 3) {
            clipKeptAfterDeath = static_cast<int>(fight.enemy().animator().animId()) == base + 4;
        }
    });
    REQUIRE(deadAt >= 0);
    CHECK(fight.enemy().fighter().health().value() == 0);
    // Out of health, the victim's end clip still plays (his held 0x400000 holds the death back), then he lies dead.
    CHECK(clipKeptAfterDeath);
    CHECK(fight.enemy().state() == TargetState::Grounded);
    CHECK(fight.ally().fighter().held() == nullptr);
}

TEST_CASE("a hit from a fourth human on the tandem's victim takes health but plays no reaction", "[human][tandem]") {
    // The victim holds 0x400000 and the other two 0x800000 (docs/research/combat-reactions.md#protected-moves): a
    // fourth human's hit is taken with no reaction, so the tandem plays on.
    const FightCharacter character;
    TandemFight fight(character);
    fight.allyHoldsFromBehind();
    bool hit = false;
    std::uint32_t clipAfterHit = 0;
    int base = combat::anim_id::kNone;
    std::uint64_t startedAt = 0;
    fight.run("2 tap square\n", 30, [&](std::uint64_t frame) {
        const auto player = static_cast<int>(fight.player().animator().animId());
        if (base == combat::anim_id::kNone && player >= combat::kTandem1 && player < combat::kTandem1 + 30) {
            base = player - 2;
            startedAt = frame;
        }
        // Four updates into the tandem a fourth human's hit lands on the victim (the fight's updates count on from the
        // hold's set-up).
        if (base != combat::anim_id::kNone && frame == startedAt + 4) {
            CHECK((fight.enemy().heldFlags() & combat::kTandemVictimHeld) != 0);
            CHECK((fight.player().heldFlags() & combat::kTandemAttackerHeld) != 0);
            CHECK((fight.ally().heldFlags() & combat::kTandemAttackerHeld) != 0);
            fight.enemy().takeHit(coney::human::IncomingHit{
                .damage = 10, .attackAnim = 12, .code = 0x0a, .attacker = Vec3{39.0F, 42.0F, 0.0F}});
            hit = true;
        }
        if (base != combat::anim_id::kNone && frame == startedAt + 6) {
            clipAfterHit = fight.enemy().animator().animId();
        }
    });
    REQUIRE(hit);
    CHECK(fight.enemy().fighter().health().value() == 590);
    CHECK(static_cast<int>(clipAfterHit) == base + 1);
}
