// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that an AI of a class whose power class draws a weapon draws it in a fight, and
// one whose class flashes when hurt uses a flash (#285, docs/research/characters.md#power-class-bytes): `level99`
// checkpoint 1 played in the game session as `coney --play-level level99 --checkpoint 1` plays it
// (support/disc_session.h). An AI of the yard is given the disc's power class of the Savage Huns' lieutenant (type 107,
// class 52, which draws) and his weapon, then of the Boppers' lieutenant (type 129, class 44, which flashes), and set
// to fight the player, as the runtime check in the original did with a yard human's class bytes. It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/class_draws.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "human/human.h"
#include "human/script_state.h"
#include "platform/game_session.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/live_pad.h"

namespace {

// How long anything here may take: 30 s of frames.
constexpr std::uint64_t kLimit = 900;
// The lieutenants whose classes carry the switches on normal (docs/references/power-classes.md).
constexpr int kHunLieutenant = 107;
constexpr int kHunClass = 52;
constexpr int kBopperLieutenant = 129;
constexpr int kBopperClass = 44;

// The yard in play, its brains, the player's and one AI the test takes over.
struct Yard {
    coney::ai::Brains* brains = nullptr;
    coney::ai::Brain* player = nullptr;
    coney::ai::Brain* fighter = nullptr;
};

// Plays level99 checkpoint 1 until the player is in play and finds the player's brain and an AI's that is alive and
// bound to the scripts.
Yard yardOf(coney::test::DiscSession& run) {
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    run.run(30);
    const coney::ai::Brains* found = run.session().gameplay().brains();
    REQUIRE(found != nullptr);
    Yard yard;
    // The test steers brains gameplay owns.
    yard.brains = const_cast<coney::ai::Brains*>(found);
    for (std::size_t i = 0; i < yard.brains->size(); ++i) {
        coney::ai::Brain& brain = yard.brains->at(i);
        if (brain.type() == coney::ai::BrainType::Player) {
            yard.player = &brain;
        } else if (yard.fighter == nullptr && !brain.dead() && brain.services() != nullptr && brain.human().alive() &&
                   !brain.human().outOfWorld()) {
            yard.fighter = &brain;
        }
    }
    REQUIRE(yard.player != nullptr);
    REQUIRE(yard.fighter != nullptr);
    return yard;
}

// Gives `brain` the disc's power class `powerClass` and type `type`'s weapon, empty-handed, and sets him on the player.
void armAs(const coney::script::RecordedCalls& recorded, Yard& yard, int type, int powerClass) {
    const coney::ai::AiConfig config = coney::ai::aiConfigFrom(recorded, type, powerClass);
    coney::human::Human& body = yard.fighter->human();
    coney::human::FighterProfile profile = body.fighterProfile();
    profile.powerClass = config.powerClass;
    body.setFighterProfile(profile);
    yard.fighter->setClassWeapon(config.fighter.weapon);
    body.script().heldObject = 0.0;
    body.script().heldObjectName.clear();
    yard.fighter->startFight(*yard.player);
}

} // namespace

TEST_CASE("the disc's Huns' lieutenant class draws his weapon in a fight with nothing in hand", "[disc][ai][combat]") {
    // Expected: characters.md#power-class-bytes, confirmed (code), confirmed (runtime)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::LivePad pad;
    coney::test::DiscSession run(*wad, pad);
    Yard yard = yardOf(run);
    const coney::script::RecordedCalls& recorded = run.session().flow().recorded();
    const coney::ai::AiConfig hun = coney::ai::aiConfigFrom(recorded, kHunLieutenant, kHunClass);
    REQUIRE(hun.powerClass.drawsWeapon);
    REQUIRE_FALSE(hun.powerClass.flashesWhenHurt);
    REQUIRE_FALSE(hun.fighter.weapon.empty());
    armAs(recorded, yard, kHunLieutenant, kHunClass);
    coney::human::Human& body = yard.fighter->human();
    bool drew = false;
    REQUIRE(run.runUntil(
        [&] {
            drew = drew || body.animator().animId() == static_cast<std::uint32_t>(coney::ai::kWeaponDrawAnim);
            return drew && body.script().heldObject != 0.0;
        },
        kLimit));
    CHECK(body.script().heldObjectName == hun.fighter.weapon);
}

TEST_CASE("the disc's Boppers' lieutenant class uses a flash in a fight under 30 % health", "[disc][ai][combat]") {
    // Expected: characters.md#power-class-bytes, confirmed (code), confirmed (runtime)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::LivePad pad;
    coney::test::DiscSession run(*wad, pad);
    Yard yard = yardOf(run);
    const coney::script::RecordedCalls& recorded = run.session().flow().recorded();
    const coney::ai::AiConfig bopper = coney::ai::aiConfigFrom(recorded, kBopperLieutenant, kBopperClass);
    REQUIRE(bopper.powerClass.flashesWhenHurt);
    armAs(recorded, yard, kBopperLieutenant, kBopperClass);
    coney::human::Human& body = yard.fighter->human();
    coney::combat::Health& health = body.fighter().health();
    // Under 30 %: 89 of 300, as in the original's check.
    health.set(health.maximum() * 89 / 300);
    const int hurt = health.value();
    bool flashed = false;
    REQUIRE(run.runUntil(
        [&] {
            flashed = flashed || body.animator().animId() == static_cast<std::uint32_t>(coney::ai::kFlashAnim);
            return flashed && health.value() == health.maximum();
        },
        kLimit));
    CHECK(hurt < health.maximum());
}
