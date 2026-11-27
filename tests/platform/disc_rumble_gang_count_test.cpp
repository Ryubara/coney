// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the Rumble HUD's gang counter counts a gang's standing members, not the
// members with health left (issue #456, docs/research/hud-functions-lua.md#gang-count-what): a WAR PARTY (Furies
// against Orphans, five a side, in the Fight Pen) started from QUICK RUMBLE with every fighter handed to the AI, as the
// original was watched in PCSX2. Each update, the shared indicator's count for the Orphans must equal the members
// for whom `Human_IsDownOrDead` is false, on the HUD the scripts set it on and the play mode steps. The updates where
// some member is down by state with health left (knocked out or dying at 1 health,
// docs/research/combat-reactions.md#down-health) are the ones a health count gets wrong: the original has them (watched
// in PCSX2, docs/research/hud-functions-lua.md#gang-count-what), so the match must have some (issue #592), and the
// count must still be the standing members on them. A second match checks the fighters knocked out lie there with 1
// health, down by state. It runs only when the environment variable CONEY_DISC names the
// disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "core/options.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "hud/hud.h"
#include "human/fighter.h"
#include "human/human.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"

namespace {

// The most updates of play looked at (five minutes of the match after the menus), and how many updates with an Orphan
// down by state with health left end the look early. The match runs until that many are seen, so a change to the AI
// that moves the first fall (under seed 3 it came at about game frame 3,260, and at 4,015 once the gang leader stayed
// player 1 and was picked as Gang_PickLeader picks, docs/research/gang-records.md#leader) does not take the updates
// the count is wrong on out of the window.
constexpr int kMaxUpdates = 9000;
constexpr int kDecisiveUpdates = 300;
// The AI's seed (`--ai-seed`, StartUpFlow::setAiSeed()). Which side wins is the AI's draw, and the counter's test needs
// an Orphan down by state with health left: under seed 0 the Orphans win with none down (since the fight stance follows
// each re-target, docs/research/ai-fights.md#fight-reposition), under seed 3 Orphans fall.
constexpr std::uint32_t kAiSeed = 3;

} // namespace

// Guards the counter across the gang leader fix (task 639): it passes before and after it.
TEST_CASE("the disc's WAR PARTY gang counter shows the standing Orphans, not those with health left",
          "[disc][rumble][hud]") {
    // Expected: hud-functions-lua.md#gang-count-what, confirmed (code) at NumIndicator_Update (0x001b67b8) and
    // Gang_CountLiving (0x00166158): the count is the members for whom Human_IsDownOrDead (0x00227e60) is false; and
    // combat-reactions.md#down-health, confirmed (runtime), run r-20261122-war-party: members lie down by state with 1
    // health, so a match has updates where the health count and the state count differ.
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscGame game(*wad, coney::test::kQuickRumbleScript,
                               coney::RumbleLaunch{.gameType = 14, .arena = 102, .gangSize = 5, .aiPlayer = true});
    game.flow().setAiSeed(kAiSeed); // before the first update
    game.run(1000);
    REQUIRE(game.stack().topId() == coney::GameplayMode::kId);
    const coney::platform::PlayLevelMode* play = game.play();
    REQUIRE(play != nullptr);
    // The game's HUD: the scripts' bindings set the indicator on it, and the play mode steps it (playLoader()).
    const coney::hud::Hud* hud = &game.flow().hud();
    REQUIRE(hud != nullptr);

    int shown = 0;                   // updates with the shared indicator on
    int mismatch = 0;                // updates whose count is not the standing members
    int decisive = 0;                // updates with a member down by state but with health left
    int healthWas = 0;               // of those, updates where the count is the members with health left
    std::uint64_t firstDecisive = 0; // the game frame of the first such update
    int looked = 0;                  // updates looked at
    for (; looked < kMaxUpdates && decisive < kDecisiveUpdates; ++looked) {
        game.run(1);
        const coney::hud::NumIndicator& indicator = hud->numIndicator(coney::hud::Hud::kSharedNumIndicator);
        const coney::ai::Brains* brains = game.flow().gameplay().brains();
        if (!indicator.on || indicator.gang < 0 || brains == nullptr) {
            continue;
        }
        ++shown;
        std::uint32_t standing = 0;
        std::uint32_t withHealth = 0;
        for (std::size_t b = 0; b < brains->size(); ++b) {
            const coney::ai::Brain& brain = brains->at(b);
            if (brain.gang() == nullptr || brain.gang()->id() != indicator.gang) {
                continue;
            }
            standing += coney::ai::humanIsDownOrDead(brain) ? 0U : 1U;
            withHealth += brain.human().fighter().health().value() > 0 ? 1U : 0U;
        }
        mismatch += indicator.count != standing ? 1 : 0;
        if (withHealth > standing) {
            ++decisive;
            firstDecisive = firstDecisive == 0 ? game.frames() : firstDecisive;
            healthWas += indicator.count == withHealth ? 1 : 0;
        }
    }
    std::printf("  war party gang counter: shown %d of %d updates, %d not the standing count; %d updates with a "
                "member down by state with health left (the first at game frame %llu), %d of them showing the health "
                "count\n",
                shown, looked, mismatch, decisive, static_cast<unsigned long long>(firstDecisive), healthWas);
    CHECK(shown > looked / 2);
    CHECK(mismatch == 0);
    CHECK(decisive > 0);
    CHECK(healthWas == 0);
}

TEST_CASE("the disc's WAR PARTY knocks fighters out with 1 health left, down by state", "[disc][rumble][combat]") {
    // Expected: combat-reactions.md#down-health, confirmed (runtime), run r-20261122-war-party: in the original's
    // WAR PARTY a fighter beaten to 0 health lies dying and then knocked out with 1 health (Human_PlayDeathReaction
    // 0x00267a00 writes it), and Human_IsDownOrDead (0x00227e60) counts him down by his state, not his health.
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscGame game(*wad, coney::test::kQuickRumbleScript,
                               coney::RumbleLaunch{.gameType = 14, .arena = 102, .gangSize = 5, .aiPlayer = true});
    game.run(1000);
    REQUIRE(game.stack().topId() == coney::GameplayMode::kId);
    constexpr int kFightUpdates = 4000;
    std::vector<const coney::human::Human*> knockedOut; // each fighter seen knocked out, once
    int downUpdates = 0;                                // fighter-updates down by state
    int downWithHealth = 0;                             // of those, with health left
    int downAtOne = 0;                                  // of those, with exactly 1
    for (int i = 0; i < kFightUpdates; ++i) {
        game.run(1);
        const coney::ai::Brains* brains = game.flow().gameplay().brains();
        if (brains == nullptr) {
            continue;
        }
        for (std::size_t b = 0; b < brains->size(); ++b) {
            const coney::human::Human& human = brains->at(b).human();
            if (!coney::ai::humanIsDownOrDead(brains->at(b))) {
                continue;
            }
            ++downUpdates;
            const int health = human.fighter().health().value();
            downWithHealth += health > 0 ? 1 : 0;
            downAtOne += health == 1 ? 1 : 0;
            if (human.knockedOut() && std::ranges::find(knockedOut, &human) == knockedOut.end()) {
                knockedOut.push_back(&human);
            }
        }
    }
    std::printf("  war party knock-outs: %zu fighters knocked out; %d fighter-updates down by state, %d with health "
                "left, %d at 1\n",
                knockedOut.size(), downUpdates, downWithHealth, downAtOne);
    CHECK_FALSE(knockedOut.empty());
    CHECK(downUpdates > 0);
    CHECK(downWithHealth == downUpdates);
    CHECK(downAtOne == downUpdates);
}
