// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the HUD's crew status list shows a cuffed Warrior as the original does
// (docs/research/hud-functions-parts.md#crew-status): `level80` checkpoint 2 played in the game session as
// `coney --play-level level80 --checkpoint 2` plays it (support/disc_session.h), its opening scene skipped by tapping
// cross, then one of player 1's Warriors cuffed through `HuSetArrested`'s host, as a script cuffs him. It runs only
// when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/scripted_brains.h"
#include "ai/scripted_humans.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "hud/crew_status.h"
#include "hud/hud.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// How long reaching play may take: 60 s of frames.
constexpr std::uint64_t kLimit = 1800;

// A pad script that taps cross every second for the first 20 s of play: it skips the level's opening scene.
constexpr const char* kSkipScene = "30 tap cross\n60 tap cross\n90 tap cross\n120 tap cross\n150 tap cross\n"
                                   "180 tap cross\n210 tap cross\n240 tap cross\n270 tap cross\n300 tap cross\n"
                                   "330 tap cross\n360 tap cross\n390 tap cross\n420 tap cross\n450 tap cross\n"
                                   "480 tap cross\n510 tap cross\n540 tap cross\n570 tap cross\n600 tap cross\n";

// The first Warrior of player 1's gang in `run`'s level, standing and not cuffed; null when there is none.
coney::ai::Brain* firstCrewWarrior(coney::test::DiscSession& run) {
    const coney::ai::Brains* found = run.session().gameplay().brains();
    if (found == nullptr) {
        return nullptr;
    }
    auto& brains = const_cast<coney::ai::Brains&>(*found);
    const coney::ai::Brain* chief = nullptr;
    for (std::size_t i = 0; i < brains.size(); ++i) {
        if (brains.at(i).type() == coney::ai::BrainType::Player) {
            chief = &brains.at(i);
        }
    }
    for (std::size_t i = 0; i < brains.size() && chief != nullptr; ++i) {
        coney::ai::Brain& brain = brains.at(i);
        if (brain.type() == coney::ai::BrainType::Warrior && brain.gang() == chief->gang() &&
            !brain.human().knockedOut() && !brain.human().script().arrested) {
            return &brain;
        }
    }
    return nullptr;
}

} // namespace

// Expected: CrewStatus_Add (0x001a0420) lists a member of player 1's gang in state 0x20000, the state the arrest sets
// (docs/research/crimes.md#arrest); CrewStatusEntry_Update (0x001a0d28) gives him the blue badge 0x19; the caption
// ends in GSTRING.HUD 226; a release hides him and the next update removes him (confirmed (runtime) in the original,
// hud-functions-parts.md#crew-status).
TEST_CASE("the disc's level80: a Warrior cuffed by HuSetArrested gets a crew status entry with the arrested caption",
          "[disc][story][crew]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kSkipScene);
    run.session().startAtLevel("level80", 2);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runUntil(
        [&run] { return run.session().play() != nullptr && run.session().play()->player().padControlled(); }, kLimit));
    run.run(30);

    const coney::hud::Hud& hud = run.session().flow().hud();
    const std::size_t before = hud.crewStatus().count();
    coney::ai::Brain* warrior = firstCrewWarrior(run);
    REQUIRE(warrior != nullptr);
    const double handle = warrior->handle();
    const coney::ai::ScriptedBrains* scripted = run.session().gameplay().scripted();
    REQUIRE(scripted != nullptr);
    coney::ai::ScriptedHumans& host = const_cast<coney::ai::ScriptedBrains&>(*scripted).humanHost();

    // Cuffed: one entry more, the blue badge, the caption with the arrested text.
    host.setArrested(handle, true);
    run.run(2);
    CHECK(hud.crewStatus().count() == before + 1);
    const coney::hud::CrewStatusList::Entry* entry = hud.crewStatus().find(handle);
    REQUIRE(entry != nullptr);
    CHECK(entry->shown);
    CHECK(entry->status == coney::hud::kCrewCuffedRect);
    const std::string arrested =
        hud.services().hudString ? hud.services().hudString(coney::hud::kCrewCuffedString) : std::string{};
    REQUIRE_FALSE(arrested.empty());
    CHECK(hud.crewStatus().caption().starts_with("<DISPLAYTIME 2500><SIZE 1.0><CENTER>"));
    CHECK(hud.crewStatus().caption().ends_with(" " + arrested));

    // Freed: hidden at once, gone at the next update.
    host.setArrested(handle, false);
    run.run(1);
    run.run(1);
    CHECK(hud.crewStatus().find(handle) == nullptr);
    CHECK(hud.crewStatus().count() == before);
    std::printf("  level80 crew status: %zu listed before, %zu while cuffed, %zu after the release\n", before,
                before + 1, hud.crewStatus().count());
}
