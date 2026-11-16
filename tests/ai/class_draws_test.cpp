// SPDX-License-Identifier: GPL-3.0-or-later
// What `Brain_Think` does with the power class's last two switches: an AI in a fight with nothing in hand draws a
// weapon (`+0x41`, clip 477, the type's weapon or a switchblade put in his hand by the clip's `random` event), and one
// under 30 % health uses a flash (`+0x42`, clip 665, a full heal at its action event); a cop draws his baton in or out
// of a fight. Synthetic humans on a floor: no disc.
// Research: docs/research/characters.md#power-class-bytes
#include "ai/class_draws.h"

#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/melee_goal.h"
#include "ai/play_anim_action.h"
#include "combat/power_class.h"
#include "human/human.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::BrainType;
using coney::test::AiScene;

namespace {

// A gang brain beside the player in `scene` with `powerClass`, holding nothing, its services the scene's.
Brain& fighterWith(AiScene& scene, const coney::combat::PowerClass& powerClass, BrainType type = BrainType::Gang) {
    Brain& brain = scene.add({44.0F, 40.0F, 0.0F}, 0.0F, type);
    brain.setServices(&scene.services);
    coney::human::FighterProfile profile = brain.human().fighterProfile();
    profile.powerClass = powerClass;
    brain.human().setFighterProfile(profile);
    return brain;
}

// The anim id the brain's front action plays, or -1 when its front action is no play-anim action.
int queuedAnim(Brain& brain) {
    const auto* action = dynamic_cast<const coney::ai::PlayAnimAction*>(brain.frontAction());
    return action != nullptr ? action->animId() : -1;
}

// A power class that draws a weapon (`+0x41` set) and one that flashes when hurt (`+0x42` set).
coney::combat::PowerClass drawing() {
    coney::combat::PowerClass power = coney::combat::kCivilianPowerClass;
    power.drawsWeapon = true;
    return power;
}
coney::combat::PowerClass flashing() {
    coney::combat::PowerClass power = coney::combat::kCivilianPowerClass;
    power.flashesWhenHurt = true;
    return power;
}

} // namespace

TEST_CASE("an AI of a class that draws, fighting with nothing in hand, plays 477", "[ai]") {
    // Expected: characters.md#power-class-bytes, confirmed (code)
    AiScene scene;
    Brain& brain = fighterWith(scene, drawing());
    REQUIRE(brain.pushGoal(std::make_unique<coney::ai::MeleeGoal>()));
    coney::ai::thinkClassActions(brain);
    CHECK(queuedAnim(brain) == coney::ai::kWeaponDrawAnim);
    // No draw out of a fight, with something in hand, or for a class without the switch.
    Brain& idle = fighterWith(scene, drawing());
    coney::ai::thinkClassActions(idle);
    CHECK(queuedAnim(idle) == -1);

    Brain& armed = fighterWith(scene, drawing());
    REQUIRE(armed.pushGoal(std::make_unique<coney::ai::MeleeGoal>()));
    armed.human().script().heldObject = 99.0;
    coney::ai::thinkClassActions(armed);
    CHECK(queuedAnim(armed) == -1);

    Brain& plain = fighterWith(scene, coney::combat::kCivilianPowerClass);
    REQUIRE(plain.pushGoal(std::make_unique<coney::ai::MeleeGoal>()));
    coney::ai::thinkClassActions(plain);
    CHECK(queuedAnim(plain) == -1);
}

TEST_CASE("a cop with a weapon draws it in or out of a fight; one whose weapon is none does not", "[ai]") {
    // Expected: characters.md#power-class-bytes, confirmed (code)
    AiScene scene;
    Brain& cop = fighterWith(scene, coney::combat::kCivilianPowerClass, BrainType::Cop);
    cop.setClassWeapon("dyn_baton");
    coney::ai::thinkClassActions(cop);
    CHECK(queuedAnim(cop) == coney::ai::kWeaponDrawAnim);

    Brain& unarmed = fighterWith(scene, drawing(), BrainType::Cop);
    unarmed.setClassWeapon("none");
    REQUIRE(unarmed.pushGoal(std::make_unique<coney::ai::MeleeGoal>()));
    coney::ai::thinkClassActions(unarmed);
    CHECK(queuedAnim(unarmed) == -1);
}

TEST_CASE("an AI of a class that flashes, fighting under 30 % health, plays 665 before any draw", "[ai]") {
    // Expected: characters.md#power-class-bytes, confirmed (code)
    AiScene scene;
    coney::combat::PowerClass both = flashing();
    both.drawsWeapon = true;
    Brain& brain = fighterWith(scene, both);
    REQUIRE(brain.pushGoal(std::make_unique<coney::ai::MeleeGoal>()));
    coney::combat::Health& health = brain.human().fighter().health();
    // 30 % exactly is not under it: the draw is taken.
    health.set(health.maximum() * 3 / 10);
    coney::ai::thinkClassActions(brain);
    CHECK(queuedAnim(brain) == coney::ai::kWeaponDrawAnim);
    REQUIRE(brain.clearActions());
    health.set(health.maximum() * 3 / 10 - 1);
    coney::ai::thinkClassActions(brain);
    CHECK(queuedAnim(brain) == coney::ai::kFlashAnim);
}

TEST_CASE("the flash's action event heals an AI to his maximum", "[ai]") {
    // Expected: characters.md#power-class-bytes, confirmed (code)
    AiScene scene;
    Brain& brain = fighterWith(scene, flashing());
    coney::combat::Health& health = brain.human().fighter().health();
    health.set(100);
    coney::ai::useFlash(brain);
    CHECK(health.value() == health.maximum());
}

TEST_CASE("the draw's random event puts the class's weapon in the hand, a switchblade when it is none", "[ai]") {
    // Expected: characters.md#power-class-bytes, confirmed (code)
    AiScene scene;
    Brain& hun = fighterWith(scene, drawing());
    REQUIRE(hun.pushGoal(std::make_unique<coney::ai::MeleeGoal>()));
    hun.setClassWeapon("dyn_hunter");
    coney::ai::giveHandObject(hun);
    REQUIRE(scene.services.handObjects.size() == 1);
    CHECK(scene.services.handObjects[0] == "dyn_hunter");

    Brain& bare = fighterWith(scene, drawing());
    REQUIRE(bare.pushGoal(std::make_unique<coney::ai::MeleeGoal>()));
    bare.setClassWeapon("none");
    coney::ai::giveHandObject(bare);
    REQUIRE(scene.services.handObjects.size() == 2);
    CHECK(scene.services.handObjects[1] == "dyn_swhbld");

    // With no goal on the stack nothing is given.
    Brain& goalless = fighterWith(scene, drawing());
    goalless.setClassWeapon("dyn_hunter");
    coney::ai::giveHandObject(goalless);
    CHECK(scene.services.handObjects.size() == 2);
}
