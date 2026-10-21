// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the Rumble HUD's gang counter counts a gang's standing members, not the
// members with health left (issue #456, docs/research/hud-functions-lua.md#gang-count-what): a WAR PARTY (Furies
// against Orphans, five a side, in the Fight Pen) started from QUICK RUMBLE with every fighter handed to the AI, as the
// original was watched in PCSX2. Each update, the shared indicator's count for the Orphans must equal the members
// for whom `Human_IsDownOrDead` is false, on the HUD the scripts set it on and the play mode steps. The updates where
// some member is down by state with health left (knocked out, wounded, dying) are the ones the old health count got
// wrong; they are counted and printed, not required: in the original a member knocked out keeps some health
// (watched in PCSX2, docs/research/hud-functions-lua.md#gang-count-what), but Coney's fighters reach no health before they are down, so this match has none
// (the unit tests in tests/ai/gangs_test.cpp hold that case). It runs only when the environment variable CONEY_DISC
// names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "core/options.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "hud/hud.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"

namespace {

// Updates of play looked at: about a minute and a half of the match after the menus.
constexpr int kUpdates = 2700;

} // namespace

TEST_CASE("the disc's WAR PARTY gang counter shows the standing Orphans, not those with health left",
          "[disc][rumble][hud]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscGame game(*wad, coney::test::kQuickRumbleScript,
                               coney::RumbleLaunch{.gameType = 14, .arena = 102, .gangSize = 5, .aiPlayer = true});
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
    for (int i = 0; i < kUpdates; ++i) {
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
            withHealth += brain.human().fighter().health().depleted() ? 0U : 1U;
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
                shown, kUpdates, mismatch, decisive, static_cast<unsigned long long>(firstDecisive), healthWas);
    CHECK(shown > kUpdates / 2);
    CHECK(mismatch == 0);
    CHECK(healthWas == 0);
}
