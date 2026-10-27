// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the crew's Wreck'em order at the level80 store does what the original's
// does (issue #241; docs/research/crew-wreck.md, docs/research/ai/index.md#get-item): level80 played
// in the game session at checkpoint 3 as `coney --play-level level80 --checkpoint 3` plays it
// (support/disc_session.h), player 1 and two of his crew put where the original's runs put them
// (research/traces/scenarios/wreck_store*.toml), the order given from the R2 menu with the right stick down-left as a
// player gives it, then 150 s of play, the original runs' length. In the original (2026-10-21, PCSX2, the level80
// "Let's Go" state, the order given there, player 1's follow camera made current so the store is within 50 m of a
// camera, as in play: docs/research/objects.md#pane-draw): inside the pharmacy the two Warriors swung at the display
// cases' glass (GetItem's glass branch), broke 8 panes from about 8 s on and took 20 objects from behind them (the
// wreck6 trace, 4500 updates); outside, by the store front's west windows (alarmed panes, which Glass_ClaimPane
// skips), no pane broke and nothing of the store's was taken. It runs only when the environment variable
// CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/goal.h"
#include "animation/anim_math.h"
#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/start_up_flow.h"
#include "human/human.h"
#include "human/script_state.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "warriors/game_state.h"
#include "world_objects/glass.h"
#include "world_objects/level_objects.h"
#include "world_objects/spawn_records.h"

#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// How long the level may take to give player 1 the pad: a minute of frames.
constexpr std::uint64_t kLimit = 1800;
// The original runs' length after the order: 150 s, 4500 updates.
constexpr std::uint64_t kWreckFrames = 4500;
// The store the crew robs: its object zone (storePharmacy01's group).
constexpr std::uint32_t kStoreZone = 24;
// The store front's centre, and how far from it a pane counts as the store's.
constexpr coney::anim::Vec3 kStoreFront{-242.0F, 226.9F, 1.2F};
constexpr float kStorePanes = 15.0F;
// The wreck / steal command's number in the menu.
constexpr std::size_t kWreckCommand = 5;

// Player 1's pad, set by the test: the buttons held and the right stick in [-1, 1], y up.
class DrivenPad final : public coney::InputSource {
  public:
    std::uint16_t buttons = 0;
    float rightX = 0.0F;
    float rightY = 0.0F;

    coney::PortSamples sample(std::uint64_t /*frame*/) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        pad.buttons = buttons;
        pad.sticks[0] = raw(rightX);
        pad.sticks[1] = raw(-rightY);
        pad.sticks[2] = raw(0.0F);
        pad.sticks[3] = raw(0.0F);
        return samples;
    }

  private:
    // The byte for `value` as an input script's `stick` line writes it.
    static std::uint8_t raw(float value) {
        return coney::stickByteFromPercent(static_cast<int>(std::lround(std::clamp(value, -1.0F, 1.0F) * 100.0F)));
    }
};

// Player 1's brain and his crew (the Warriors of his gang).
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

// Where the original's run put player 1 and two of his crew (x, y; z on the ground there).
struct Placing {
    coney::anim::Vec3 chief;
    coney::anim::Vec3 first;
    coney::anim::Vec3 second;
};

// What the 150 s showed.
struct Wreck {
    int panesBroken = 0;      // the store's panes broken
    std::ptrdiff_t taken = 0; // the store's stock gone from its shelves
    int heldFrames = 0;       // frames a crew member held something
    int getItemFrames = 0;    // frames a crew member's top goal was GetItem
    int smashGoalFrames = 0;  // frames a crew member's top goal was DestroyItem
};

// The store's panes that are broken.
int brokenStorePanes(coney::test::DiscSession& run) {
    int broken = 0;
    for (const coney::world_objects::GlassPane& pane : run.session().gameplay().objects().glass.panes()) {
        if (pane.broken && coney::anim::distance(pane.centre, kStoreFront) <= kStorePanes) {
            ++broken;
        }
    }
    return broken;
}

// The store's owned stock still on its shelves.
std::ptrdiff_t storeStock(coney::test::DiscSession& run) {
    return std::ranges::count_if(
        run.session().flow().spawnRecords().all(),
        [](const coney::world_objects::SpawnRecord& r) { return r.zone == kStoreZone && r.owned && !r.removed; });
}

// level80 checkpoint 3 until player 1 has the pad, the crew put as `placing` says, the Wreck'em order given with R2
// and the right stick down-left, then 150 s of play, counted.
Wreck wreckAt(const coney::io::Wad& wad, const Placing& placing) {
    DrivenPad pad;
    coney::test::DiscSession run(wad, pad);
    run.session().startAtLevel("level80", 3);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runUntil(
        [&run] { return run.session().play() != nullptr && run.session().play()->player().padControlled(); }, kLimit));
    // The chapter's Setup places the crew first.
    run.run(60);
    Crew crew = crewOf(run);
    REQUIRE(crew.chief != nullptr);
    REQUIRE(crew.members.size() >= 2);
    // The order enabled for player 1, as the original's run did (its game state's +0x411, +0x42e and +0x41e + 5).
    coney::GameState& state = run.session().flow().state();
    state.characters.warriorCommandsLocked = false;
    state.story.menuLocked[0] = false;
    state.characters.warriorCommands[0][kWreckCommand] = true;
    // Placed, facing the store (+y is heading 0; the store lies south, -y).
    constexpr float kFacingStore = 3.14159265F;
    run.session().play()->teleportPlayer(coney::world_objects::Placement{
        .position = {placing.chief.x, placing.chief.y, placing.chief.z}, .headingDegrees = 180.0F});
    crew.members[0]->human().place(placing.first, kFacingStore);
    crew.members[1]->human().place(placing.second, kFacingStore);
    run.run(5);
    const int brokenBefore = brokenStorePanes(run);
    const std::ptrdiff_t stockBefore = storeStock(run);

    // R2 held, the right stick down-left, R2 let go.
    pad.buttons = coney::pad::kR2;
    run.run(2);
    pad.rightX = -0.67F;
    pad.rightY = -0.67F;
    run.run(6);
    pad.buttons = 0;
    run.run(1);
    pad.rightX = 0.0F;
    pad.rightY = 0.0F;
    run.run(5);
    CHECK(state.characters.lastWarriorCommand.at(0) == static_cast<int>(kWreckCommand));

    Wreck wreck;
    for (std::uint64_t frame = 0; frame < kWreckFrames; ++frame) {
        run.run(1);
        for (coney::ai::Brain* member : crew.members) {
            if (member->human().script().heldObject != 0.0) {
                ++wreck.heldFrames;
            }
            if (const coney::ai::Goal* top = member->topGoal(); top != nullptr) {
                wreck.getItemFrames += top->type() == coney::ai::GoalType::GetItem ? 1 : 0;
                wreck.smashGoalFrames += top->type() == coney::ai::GoalType::DestroyItem ? 1 : 0;
            }
        }
    }
    wreck.panesBroken = brokenStorePanes(run) - brokenBefore;
    wreck.taken = stockBefore - storeStock(run);
    return wreck;
}

// Inside the pharmacy, between the display cases (research/traces/scenarios/wreck_store_inside.toml).
void checkInside(const coney::io::Wad& wad) {
    const Wreck wreck = wreckAt(wad, Placing{.chief = {-243.5F, 222.5F, 1.22F},
                                             .first = {-244.5F, 223.5F, 1.22F},
                                             .second = {-242.5F, 223.5F, 1.22F}});
    std::printf("  level80 store, inside: %d panes broken, %td taken, %d held frames, %d GetItem frames, %d smash "
                "goal frames\n",
                wreck.panesBroken, wreck.taken, wreck.heldFrames, wreck.getItemFrames, wreck.smashGoalFrames);
    // The original: the crew at GetItem swinging at the cases' glass, 8 panes broken, 20 objects taken.
    CHECK(wreck.getItemFrames > 0);
    CHECK(wreck.panesBroken > 0);
    CHECK(wreck.taken > 0);
    // The original's crew also held 2 objects that TryFetch's own search found once the stock was gone; Coney has no
    // such search (docs/research/crew-wreck.md#q-after-stock), so this is reported, not required, until that is
    // answered.
    CHECK_NOFAIL(wreck.heldFrames > 0);
}

// By the store front's west windows (research/traces/scenarios/wreck_store.toml).
void checkOutside(const coney::io::Wad& wad) {
    // On the pavement by the west windows, the parked car beyond 15 m.
    const Wreck wreck = wreckAt(wad, Placing{.chief = {-246.0F, 230.0F, 1.22F},
                                             .first = {-247.5F, 231.0F, 1.22F},
                                             .second = {-244.5F, 231.0F, 1.22F}});
    std::printf("  level80 store, outside: %d panes broken, %td taken, %d held frames, %d GetItem frames, %d smash "
                "goal frames\n",
                wreck.panesBroken, wreck.taken, wreck.heldFrames, wreck.getItemFrames, wreck.smashGoalFrames);
    // The original: no pane broken (the front's are alarmed), nothing of the store's taken.
    CHECK(wreck.panesBroken == 0);
    CHECK(wreck.taken == 0);
}

} // namespace

TEST_CASE("the disc's level80 store: Wreck'em breaks the cases' glass inside and none of the front's alarmed "
          "windows",
          "[disc][story][wreck]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    SECTION("inside the pharmacy") { checkInside(*wad); }
    SECTION("by the store front") { checkOutside(*wad); }
}
