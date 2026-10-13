// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the crew takes the Warrior commands as the original does
// (docs/research/ai.md#warrior-commands, docs/research/ai.md#crew-gaps): `level80` played in the game session as
// `coney --play-level level80 --checkpoint N` plays it (support/disc_session.h), its opening scene skipped by
// tapping cross, then the orders given from the R2 menu with the right stick, as a player gives them. It runs only
// when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "ai/tactic.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/game_state.h"

namespace {

// How long anything here may take: 60 s of frames.
constexpr std::uint64_t kLimit = 1800;
// The follow tactic's type.
constexpr int kFollowTacticType = 0x12;

// A pad script that taps cross every second for the first 20 s of play: it skips the level's opening scene.
constexpr const char* kSkipScene = "30 tap cross\n60 tap cross\n90 tap cross\n120 tap cross\n150 tap cross\n"
                                   "180 tap cross\n210 tap cross\n240 tap cross\n270 tap cross\n300 tap cross\n"
                                   "330 tap cross\n360 tap cross\n390 tap cross\n420 tap cross\n450 tap cross\n"
                                   "480 tap cross\n510 tap cross\n540 tap cross\n570 tap cross\n600 tap cross\n";

// Player 1's brain and his crew (the Warriors of his gang), from the session's brains.
struct Crew {
    coney::ai::Brain* chief = nullptr;
    std::vector<coney::ai::Brain*> members;
};

// Finds player 1 and his crew in `run`'s level.
Crew crewOf(coney::test::DiscSession& run) {
    Crew crew;
    const coney::ai::Brains* found = run.session().gameplay().brains();
    if (found == nullptr) {
        return crew;
    }
    auto& brains = const_cast<coney::ai::Brains&>(*found);
    for (std::size_t i = 0; i < brains.size(); ++i) {
        if (brains.at(i).type() == coney::ai::BrainType::Player) {
            crew.chief = &brains.at(i);
        }
    }
    for (std::size_t i = 0; i < brains.size() && crew.chief != nullptr; ++i) {
        coney::ai::Brain& brain = brains.at(i);
        if (brain.type() == coney::ai::BrainType::Warrior && brain.gang() == crew.chief->gang()) {
            crew.members.push_back(&brain);
        }
    }
    return crew;
}

// The type of the tactic the chief's gang runs; nothing for none.
std::optional<int> crewTactic(const Crew& crew) {
    if (crew.chief == nullptr || crew.chief->gang() == nullptr || crew.chief->gang()->tactic() == nullptr) {
        return std::nullopt;
    }
    return crew.chief->gang()->tactic()->type();
}

} // namespace

// level80 checkpoint 2's script switches every command off and follow back on before the level makes the player: the
// switch reaches player 1, so the level start's follow is given and the menu shows follow enabled, by name.
TEST_CASE("the disc's level80 checkpoint 2: follow stays enabled and the crew follows the chief",
          "[disc][story][commands]") {
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

    const coney::GameState& state = run.session().flow().state();
    CHECK(state.characters.warriorCommands[0][0]);
    int enabled = 0;
    for (const bool on : state.characters.warriorCommands[0]) {
        enabled += on ? 1 : 0;
    }
    CHECK(enabled == 1);
    const Crew crew = crewOf(run);
    REQUIRE(crew.chief != nullptr);
    CHECK_FALSE(crew.members.empty());
    CHECK(crewTactic(crew) == std::optional<int>(kFollowTacticType));
    std::printf("  level80 checkpoint 2: %d of 7 commands enabled, %zu Warriors in the crew\n", enabled,
                crew.members.size());
}
