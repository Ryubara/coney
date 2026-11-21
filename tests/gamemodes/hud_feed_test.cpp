// SPDX-License-Identifier: GPL-3.0-or-later
// What a player slot's HUD state takes from his human (feedHumanToHud()): whether he is knocked out, which
// drops his Armies panel (docs/research/armies.md#the-join-messages), and his rage meter. A synthetic fighter beaten to
// 0 health, and a script's mark.
#include "gamemodes/hud_feed.h"

#include <catch2/catch_test_macros.hpp>

#include "hud/armies_hud.h"
#include "hud/hud.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/human_flags.h"
#include "support/fight_fixtures.h"

namespace {

// Beats the fight's player to 0 health with one hit and plays on until his dying reaction has ended.
void beatToZero(coney::test::Fight& fight) {
    coney::human::Human& human = fight.human();
    human.setFlag(coney::human::flag::kDemiGod, false);
    human.takeHit(coney::human::IncomingHit{
        .damage = 900, .attackAnim = 12, .code = 0x0a, .flags = 0x800, .attacker = {40.0F, 41.0F, 0.0F}});
    fight.run("", 1);
    for (int update = 0; update < 600 && human.animator().drivingClipPlaying(); ++update) {
        fight.run("", 1);
    }
}

} // namespace

TEST_CASE("the HUD frame reads a player beaten to 0 health as knocked out, with no script's mark", "[hud][armies]") {
    // Expected: armies.md#the-join-messages, confirmed (code): ANHud_Draw (0x00211938) asks Human_IsKnockedOut
    // (0x00227dd8), state 0x40000, which Human_OnHealthOut (0x002674c0) sets through Human_KnockOut (0x00230328) when
    // the dying reaction ends (combat-reactions.md#knockout-timing, confirmed (runtime)); no script is involved.
    const coney::test::FightCharacter character;
    coney::test::Fight fight(character, 30.0F);
    coney::hud::PlayerHudState state;
    coney::feedHumanToHud(state, fight.human());
    CHECK_FALSE(state.knockedOut);
    beatToZero(fight);
    REQUIRE(fight.human().fighter().health().depleted());
    REQUIRE_FALSE(fight.human().script().knockedOut);
    coney::feedHumanToHud(state, fight.human());
    CHECK(state.knockedOut);
    // The feed drops his panel: the frame after the one he is knocked out shows his join message.
    coney::hud::ArmiesHud armies;
    armies.levelSetUp();
    armies.panelAttached(0);
    armies.setJoinMessage(0, true);
    armies.update(0, 60, {true, false}, {false, false});
    REQUIRE_FALSE(armies.joinShowing(0));
    armies.update(10, 60, {true, false}, {state.knockedOut, false});
    armies.update(20, 60, {true, false}, {state.knockedOut, false});
    CHECK(armies.joinShowing(0));
}

TEST_CASE("the HUD frame reads a script's HuSetConscious(false) as knocked out, with health left", "[hud][armies]") {
    // Expected: combat-reactions.md#knocked-out-bit, confirmed (code): Human_SetConscious (0x00237778) knocks out
    // through Human_KnockOut, setting the same state 0x40000 that Human_IsKnockedOut reads.
    const coney::test::FightCharacter character;
    coney::test::Fight fight(character, 30.0F);
    fight.human().script().knockedOut = true;
    REQUIRE_FALSE(fight.human().fighter().health().depleted());
    coney::hud::PlayerHudState state;
    coney::feedHumanToHud(state, fight.human());
    CHECK(state.knockedOut);
}

TEST_CASE("the HUD frame takes the player's rage meter: its value, maximum and whether he rages", "[hud]") {
    // Expected: hud.md#the-rage-meter, confirmed (code): the panel's meter reads the rage (human +0x650) and its
    // maximum (the Warrior class's s16 +0x00) from the human; hud-radar.md#the-health-rings reads raging from human
    // +0xe0 bit 0x80000.
    const coney::test::FightCharacter character;
    coney::test::Fight fight(character, 30.0F);
    fight.human().fighter().combat().rage().set(5);
    coney::hud::PlayerHudState state;
    coney::feedHumanToHud(state, fight.human());
    const auto& rage = fight.human().fighter().combat().rage();
    CHECK(state.panel.rage == rage.value());
    CHECK(state.panel.rage == 5);
    CHECK(state.panel.rageMax == rage.maximum());
    CHECK(state.panel.raging == rage.raging());
}
