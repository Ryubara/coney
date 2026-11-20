// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the objects a level script puts in humans' hands (`HuPlaceItemInHand`) are
// made as world objects and drawn in their hands (issue #739, docs/research/objects.md#held, Scripted placement):
// `level54` checkpoint 3 played in the game session as `coney --play-level level54 --checkpoint 3` plays it
// (support/disc_session.h), its opening scene skipped with the pad. When the scene ends the level's script makes the
// eight Furies of `StreetPursuit` and puts a `dyn_bat_fury` in each one's hand. In the original
// `Human_PlaceItemInHand` (`0x0024c280`) makes the object by name at the human, records it, and attaches it at the
// bone and offset of its low pick-up clip's first take event (`0x0024c6d8`). At runtime all eight held one, attached
// (object `+0x54` bit `0x10`) to its Fury on bone 25, the right hand (confirmed (runtime), run
// r-20261120-level54-scripted-bats). So each bat must be a spawn record of its type,
// held (in no pick-up search), and among the objects drawn in a hand, on bone 25, where his hand can hold it. It runs
// only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_pickups.h"
#include "human/human.h"
#include "human/player.h"
#include "human/script_state.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/live_pad.h"
#include "world_objects/spawn_records.h"

namespace {

// How long the start and the scene may take, and how long the bats are watched after, in frames.
constexpr std::uint64_t kLimit = 3000;
constexpr std::uint64_t kWatch = 60;
// The object the script puts in each pursuing Fury's hand, and how many Furies it arms (eight in the original).
constexpr std::string_view kBat = "dyn_bat_fury";
constexpr std::size_t kArmed = 8;
// The right hand, the bone of the bat's pick-up clip's take event (docs/research/combat-throws.md#bat).
constexpr std::size_t kRightHand = 25;
// How far from his feet a held bat may hang, metres: in plan within his arm's reach, in height above the ground and
// below his head (as disc_given_weapon_test.cpp holds a given bat).
constexpr float kReach = 1.2F;
constexpr float kLowest = 0.2F;
constexpr float kHighest = 1.9F;

// A pad script that taps cross every second for the first 20 s of play: it skips the level's opening scene.
constexpr const char* kSkipScene = "30 tap cross\n60 tap cross\n90 tap cross\n120 tap cross\n150 tap cross\n"
                                   "180 tap cross\n210 tap cross\n240 tap cross\n270 tap cross\n300 tap cross\n"
                                   "330 tap cross\n360 tap cross\n390 tap cross\n420 tap cross\n450 tap cross\n"
                                   "480 tap cross\n510 tap cross\n540 tap cross\n570 tap cross\n600 tap cross\n";

// The AI humans whose hand a script put a Fury's bat in.
std::vector<const coney::ai::Brain*> armedFuries(const coney::platform::PlayLevelMode& play) {
    std::vector<const coney::ai::Brain*> armed;
    const coney::ai::Brains& brains = play.fighters().brains();
    for (std::size_t i = 0; i < brains.size(); ++i) {
        const coney::ai::Brain& brain = brains.at(i);
        const coney::human::ScriptState& script = brain.human().script();
        if (brain.type() != coney::ai::BrainType::Player && script.heldObject != 0.0 && script.heldObjectName == kBat) {
            armed.push_back(&brain);
        }
    }
    return armed;
}

// The draw of the object in `holder`'s hand, if any.
std::optional<coney::platform::PlayLevelMode::HeldDraw> drawnIn(const coney::platform::PlayLevelMode& play,
                                                                const coney::human::Human& holder) {
    for (const coney::platform::PlayLevelMode::HeldDraw& draw : play.heldDraws()) {
        if (draw.holder == &holder) {
            return draw;
        }
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("the disc's level54 checkpoint 3: the bats the script puts in the Furies' hands are objects drawn there",
          "[disc][story][pickup]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::LivePad pad;
    pad.play(kSkipScene);
    coney::test::DiscSession run(*wad, pad);
    run.session().startAtLevel("level54", 3);
    REQUIRE(run.runUntilPlay());
    const coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    if (play == nullptr) {
        return;
    }

    // The scene ends and the script arms the pursuers as it makes them; a few steps more for the bats to be made.
    REQUIRE(run.runUntil([play] { return armedFuries(*play).size() >= kArmed; }, kLimit));
    run.run(2);
    const std::vector<const coney::ai::Brain*> furies = armedFuries(*play);
    CHECK(furies.size() == kArmed);

    // Each bat is a world object of its type, held: a spawn record, in a hand for the pick-ups.
    REQUIRE(run.session().gameplay().pickups() != nullptr);
    std::size_t drawn = 0;
    for (const coney::ai::Brain* fury : furies) {
        const double bat = fury->human().script().heldObject;
        const coney::world_objects::SpawnRecord* record = run.session().flow().spawnRecords().find(bat);
        REQUIRE(record != nullptr);
        if (record != nullptr) {
            CHECK(record->typeName == kBat);
            CHECK_FALSE(record->removed);
        }
        CHECK(run.session().gameplay().pickups()->inHand(bat));
    }

    // Each hangs from its Fury's right hand and is drawn there, and stays there as they come on (asked again each
    // frame: one whose bat a hit knocks away has nothing left to draw).
    for (std::uint64_t frame = 0; frame < kWatch; ++frame) {
        for (const coney::ai::Brain* fury : armedFuries(*play)) {
            const coney::human::Human& holder = fury->human();
            const std::optional<coney::platform::PlayLevelMode::HeldDraw> held = drawnIn(*play, holder);
            REQUIRE(held.has_value());
            // The guard an `if`: clang-tidy does not follow REQUIRE.
            if (!held.has_value()) {
                return;
            }
            CHECK(held->handle == holder.script().heldObject);
            CHECK(held->bone == kRightHand);
            const coney::anim::Vec3 feet = holder.position();
            const float plan = std::hypot(held->pose.position.x - feet.x, held->pose.position.y - feet.y);
            const float up = held->pose.position.z - feet.z;
            CHECK(plan <= kReach);
            CHECK(up >= kLowest);
            CHECK(up <= kHighest);
            drawn += frame == 0 ? 1 : 0;
        }
        run.run(1);
    }
    std::printf("  level54 checkpoint 3: %zu Furies armed by the script, %zu bats drawn in a right hand\n",
                furies.size(), drawn);
}
