// SPDX-License-Identifier: GPL-3.0-or-later
// The stealth kill on the player's own disc (docs/research/combat-moves.md#stealth,
// docs/research/stealth.md#stealth-kill): Rembrandt, made from the disc's character data and Anim Range List, hides,
// holds L1 behind a human who faces away and presses square, as the runtime check on the research page did in the
// original (moves_stealth: the hidden state on the player, a civilian 1.0 m ahead facing away). It runs only when
// CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md). Issue #242.
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "characters/character_data.h"
#include "combat/combat_script.h"
#include "combat/commands.h"
#include "combat/power_class.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "human/player.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"

namespace combat = coney::combat;
using coney::anim::Vec3;

TEST_CASE("on the disc, Rembrandt's stealth kill from hiding knocks out a human facing away",
          "[disc][combat][stealth]") {
    // Expected: combat-moves.md#stealth, confirmed (runtime): square on the update after L1 started 637 with the
    // victim in 638, the victim's health fell at once (knocked out), power 400 -> 300, and 637 ran 52 updates.
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::characters::addCharacterDataHandlers(table);
    auto loaded = coney::human::PlayerCharacter::load(*wad, table, coney::human::kPlayerModel);
    REQUIRE(loaded.has_value());
    const coney::human::PlayerCharacter& character = **loaded;

    // The player at (40, 40) facing +y, the victim 1.0 m ahead facing the same way (away from him), on a flat floor.
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    coney::human::Human player(character.anims(), coney::human::AnimSlots::player(), coney::anim::referenceRotations(),
                               1.0F, &character.ranges());
    coney::human::Human victim(character.anims(), coney::human::AnimSlots::player(), coney::anim::referenceRotations(),
                               1.0F, &character.ranges());
    victim.setFighterProfile(
        coney::human::FighterProfile{.player = false, .powerClass = combat::kCivilianPowerClass, .health = 600});
    player.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    victim.spawn(mesh.get(), Vec3{40.0F, 41.0F, 0.0F}, 0.0F);
    player.enterHiding();
    REQUIRE(player.hidden());
    const int powerBefore = player.fighter().combat().power().value();

    // L1 held from the first update, square tapped on update 8; the victim does nothing.
    std::array<coney::human::Combatant*, 1> targets{&victim};
    combat::CommandTables tables = combat::CommandTables::street();
    combat::CommandMatcher matcher;
    int killUpdates = 0;
    bool victimReacted = false;
    std::optional<int> victimHealthAtKill;
    int powerAtKill = -1;
    for (const coney::test::PadFrame& frame : coney::test::playScript("0 press l1\n8 tap square\n", 120)) {
        const combat::CommandId command =
            matcher.update(frame.buttons, tables, combat::combatTuning().historyHoldSamples);
        player.step(coney::human::HumanInput{.cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                             .command = command,
                                             .buttons = frame.buttons,
                                             .targets = targets},
                    mesh.get());
        victim.step(coney::human::HumanInput{.cameraForward = Vec3{0.0F, 1.0F, 0.0F}}, mesh.get());
        if (player.animator().animId() == 637) {
            ++killUpdates;
            if (!victimHealthAtKill) {
                victimHealthAtKill = victim.fighter().health().value();
                powerAtKill = player.fighter().combat().power().value();
            }
        }
        victimReacted = victimReacted || victim.animator().animId() == 638;
    }
    std::printf("stealth kill: 637 for %d updates, power %d -> %d, victim health at the kill %d\n", killUpdates,
                powerBefore, powerAtKill, victimHealthAtKill.value_or(-1));
    CHECK(killUpdates == 52);
    CHECK(victimReacted);
    // Run out, the death chain leaves him 1 (combat-reactions.md#down-health, Human_PlayDeathReaction 0x00267a00).
    CHECK(victimHealthAtKill == 1);
    CHECK(victim.fighter().health().depleted());
    CHECK(powerBefore - powerAtKill == powerBefore / 4);
}
