// SPDX-License-Identifier: GPL-3.0-or-later
// What the level scripts switch on a human (docs/references/bindings/character.md,
// docs/research/combat.md#human-flags): god and demi-god mode against synthetic hits, the stun and knockdown flags, the
// health percentage, the revive, the normal mode, the arrest, tireless stamina, the rage meter's script operations and
// the target search's no-target flag. Synthetic humans and hits; the stick at full deflection where the human sprints.
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "combat/combat_tuning.h"
#include "combat/commands.h"
#include "combat/meters.h"
#include "combat/power_class.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

using coney::anim::Vec3;
using coney::human::Human;
using coney::human::IncomingHit;
using coney::test::Fight;
using coney::test::FightCharacter;
namespace combat = coney::combat;
namespace flag = coney::human::flag;

namespace {

// Where an attacker in front of the player (at (40, 40) facing +y) stands.
constexpr Vec3 kInFront{40.0F, 41.0F, 0.0F};

// A hit of attack `animId` with `damage`, `code` and `flags`, from in front.
IncomingHit hitOf(int animId, int damage, int code, std::uint16_t flags = 0) {
    return IncomingHit{.damage = damage, .attackAnim = animId, .code = code, .flags = flags, .attacker = kInFront};
}

// Puts the combat tuning back after a test that changes it (the demi-god floor is one global).
class TuningScope {
  public:
    TuningScope() = default;
    TuningScope(const TuningScope&) = delete;
    TuningScope& operator=(const TuningScope&) = delete;
    TuningScope(TuningScope&&) = delete;
    TuningScope& operator=(TuningScope&&) = delete;
    ~TuningScope() { combat::combatTuning() = combat::CombatTuning{}; }
};

} // namespace

TEST_CASE("a player starts with Human_MakePlayer's flags and the combo rule, not demi-god", "[human][script]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    CHECK(fight.human().flags() == flag::kPlayerFlags);
    CHECK_FALSE(fight.human().hasFlag(flag::kDemiGod));
    Human other(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    other.setFighterProfile(
        coney::human::FighterProfile{.player = false, .powerClass = combat::kPlayerPowerClass, .health = 600});
    CHECK(other.flags() == 0);
    // The flags the scripts set stay when the human is placed again.
    fight.human().setFlag(flag::kTireless, true);
    fight.human().spawn(nullptr, Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    CHECK(fight.human().hasFlag(flag::kTireless));
}

TEST_CASE("god mode takes no damage; the reaction still plays", "[human][script]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    fight.human().setFlag(flag::kGod, true);
    fight.human().takeHit(hitOf(12, 14, 0x0a, 0x800));
    fight.run("", 1);
    CHECK(fight.human().fighter().health().value() == 900);
    CHECK(fight.human().animator().animId() == 272);
}

TEST_CASE("a demi-god's hit stops at the floor fraction, which makes it a god", "[human][script]") {
    const TuningScope scope;
    const FightCharacter character;
    Fight fight(character, 30.0F);
    fight.human().setFlag(flag::kDemiGod, true);
    fight.human().setHealthPercent(30.0F);
    REQUIRE(fight.human().fighter().health().value() == 270);
    // 100 would take 270 to 170: the floor (0.25 of 900) holds it at 225, and god mode follows.
    fight.human().takeHit(hitOf(12, 100, 0x0a, 0x800));
    fight.run("", 1);
    CHECK(fight.human().fighter().health().value() == 225);
    CHECK(fight.human().hasFlag(flag::kGod));
    fight.run("", 30);
    fight.human().takeHit(hitOf(12, 14, 0x0a, 0x800));
    fight.run("", 1);
    CHECK(fight.human().fighter().health().value() == 225);

    // Without the flag the hit takes it all; a different floor (HuSetDemiGodMode's fraction) holds elsewhere.
    Fight mortal(character, 30.0F);
    mortal.human().setHealthPercent(30.0F);
    mortal.human().takeHit(hitOf(12, 100, 0x0a, 0x800));
    mortal.run("", 1);
    CHECK(mortal.human().fighter().health().value() == 170);
    combat::combatTuning().healthFloor = 0.1F;
    Fight lower(character, 30.0F);
    lower.human().setFlag(flag::kDemiGod, true);
    lower.human().setHealthPercent(30.0F);
    lower.human().takeHit(hitOf(12, 200, 0x0a, 0x800));
    lower.run("", 1);
    CHECK(lower.human().fighter().health().value() == 90);
}

TEST_CASE("HuSetHealthPercent truncates; a percentage outside (0, 100] gives full health", "[human][script]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    fight.human().setHealthPercent(50.0F);
    CHECK(fight.human().fighter().health().value() == 450);
    CHECK(fight.human().healthPercent() == 50.0F);
    fight.human().setHealthPercent(33.3F);
    CHECK(fight.human().fighter().health().value() == 299);
    fight.human().setHealthPercent(0.0F);
    CHECK(fight.human().fighter().health().value() == 900);
    fight.human().setHealthPercent(20.0F);
    fight.human().setHealthPercent(150.0F);
    CHECK(fight.human().fighter().health().value() == 900);
}

TEST_CASE("an unstunnable human is not stunned and an ungroundable one is not knocked down", "[human][script]") {
    const FightCharacter character;
    SECTION("flag 0x100 against SX2's stun") {
        Fight fight(character, 30.0F);
        fight.human().setFlag(flag::kUnstunnable, true);
        fight.human().takeHit(hitOf(15, 37, 0x1a, 0xc00));
        fight.run("", 2);
        CHECK_FALSE(fight.human().fighter().victim().stunned());
        CHECK(fight.human().fighter().victim().stuns() == 0);
    }
    SECTION("flag 0x80 against XX2's knockdown") {
        Fight fight(character, 30.0F);
        fight.human().setFlag(flag::kUngroundable, true);
        fight.human().takeHit(hitOf(13, 46, 0x26));
        fight.run("", 40);
        CHECK(fight.human().fighter().victim().knockdowns() == 0);
        CHECK_FALSE(fight.human().fighter().victim().grounded());
    }
}

TEST_CASE("HuRevive brings a human out of health back up with full health; HuSetNormalMode ends a stun",
          "[human][script]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    fight.human().setFlag(flag::kDemiGod, false);
    fight.human().takeHit(hitOf(12, 900, 0x0a, 0x800));
    fight.run("", 5);
    REQUIRE(fight.human().fighter().health().depleted());
    CHECK_FALSE(fight.human().alive());
    fight.human().revive();
    CHECK(fight.human().fighter().health().value() == 900);
    CHECK(fight.human().alive());
    CHECK_FALSE(fight.human().fighter().victim().grounded());

    fight.human().takeHit(hitOf(15, 37, 0x1a, 0xc00));
    fight.run("", 2);
    REQUIRE(fight.human().fighter().victim().stunned());
    fight.human().setNormalMode(false);
    CHECK_FALSE(fight.human().fighter().victim().stunned());
}

TEST_CASE("an arrested human stays where it is and is not alive until released", "[human][script]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    fight.human().setArrested(true);
    CHECK_FALSE(fight.human().alive());
    CHECK(fight.human().script().arrested);
    // The stick at 80 % does not move it.
    fight.run("0 stick left 0 80\n", 30);
    CHECK(fight.human().position().x == 40.0F);
    CHECK(fight.human().position().y == 40.0F);
    fight.human().setArrested(false);
    CHECK(fight.human().alive());
    fight.run("0 stick left 0 80\n", 30);
    CHECK(std::fabs(fight.human().position().y - 40.0F) > 0.5F);
}

TEST_CASE("a tireless human's stamina stays full while it sprints", "[human][script]") {
    const FightCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh.get(), Vec3{40.0F, 5.0F, 0.0F}, 0.0F);
    human.setFlag(flag::kTireless, true);
    for (int i = 0; i < 45; ++i) {
        human.step(coney::human::HumanInput{.stickX = 0.0F,
                                            .stickY = 1.0F,
                                            .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                            .sprintHeld = true,
                                            .actionPressed = false,
                                            .command = combat::command::kNone,
                                            .buttons = 0,
                                            .targets = {}},
                   mesh.get());
    }
    CHECK(human.gait() == coney::human::Gait::Sprint);
    CHECK(human.stamina().value() == human.stamina().maximum());
}

TEST_CASE("the rage meter's script operations: full with a hold, a fraction, and the lock", "[human][script]") {
    const combat::CombatTuning tuning;
    combat::RageMeter rage(78, 144, 0);
    rage.fill(1000, 5000);
    CHECK(rage.full());
    // Held: no decay before 6000 ms.
    rage.update(5000, tuning);
    CHECK(rage.value() == 78);
    rage.setFraction(0.5F, 5000);
    CHECK(rage.value() == 39);
    rage.setLocked(true, 5000);
    rage.update(20000, tuning);
    CHECK(rage.value() == 39);
    rage.setLocked(false, 20000);
    rage.update(21000, tuning);
    CHECK(rage.value() < 39);

    combat::PowerMeter power(400, 60, 0);
    power.setUnlimited(true);
    CHECK(power.spend(0.25F) == 0);
    power.update(2000, true, 15.0F);
    CHECK(power.value() == 400);
    CHECK(power.fraction() == 1.0F);
}

TEST_CASE("a disabled command is never matched; the others are", "[human][script]") {
    const combat::CommandTables tables = combat::CommandTables::street();
    combat::CommandMatcher matcher;
    const std::uint64_t dpadOff = (std::uint64_t{1} << combat::command::kDpadDown);
    // D-pad down (0x4000) pressed: command 0x25 unless it is disabled.
    CHECK(matcher.update(0x4000, tables, 7) == combat::command::kDpadDown);
    static_cast<void>(matcher.update(0, tables, 7));
    CHECK(matcher.update(0x4000, tables, 7, dpadOff) == combat::command::kNone);
    static_cast<void>(matcher.update(0, tables, 7));
    CHECK(matcher.update(0x0080, tables, 7, dpadOff) == combat::command::kSquarePressed);
}

TEST_CASE("the target search skips a human with the no-target flag", "[human][script]") {
    const FightCharacter character;
    Human near(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    near.spawn(nullptr, Vec3{40.0F, 41.0F, 0.0F}, 180.0F);
    std::vector<coney::human::Combatant*> targets{&near};
    coney::human::FighterInput input;
    input.position = Vec3{40.0F, 40.0F, 0.0F};
    input.targets = targets;
    CHECK(coney::human::Fighter::pickTarget(input, 2.0F) == &near);
    near.setFlag(flag::kNoTarget, true);
    CHECK(coney::human::Fighter::pickTarget(input, 2.0F) == nullptr);
    near.setFlag(flag::kNoTarget, false);
    near.script().targetable = false;
    CHECK(coney::human::Fighter::pickTarget(input, 2.0F) == nullptr);
}
