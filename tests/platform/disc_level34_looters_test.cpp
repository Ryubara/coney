// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that `level34` (mission 4, the riot) checkpoint 1's intro looters wreck their
// object zone before they leave (issue #326; item 5 of docs/research/scripting.md#level34-fixes,
// docs/research/ai-code.md#t2-vandalize). When player 1 enters the box `vStartIntroLooters`, `P1.StartIntroLooters`
// (level34_riot.lua) wakes the gang and gives it `TacticVandalize(gang, 51, 1000, "P1.DoneLooting", -1)`;
// `P1.DoneLooting` sends the gang away on code 1, 5 or 6. In the original (PCSX2, run r-20261121-level34-intro-looters)
// the looters broke seven objects of zone 51 within 15 s and the tactic answered 1, its only own code, once the zone
// was empty (VandalizeTactic_Process 0x0031ea00). Most of zone 51 is carried objects (garbage bags, trash cans), which
// the Destroy goal fetches and throws (DestroyGoal_Process 0x002cdbc0), so the looters' throws must let go of what they
// hold (Human_ReleaseThrow 0x002586d8, for any human). The level plays in the game session as `--play-level level34
// --checkpoint 1` plays it (support/disc_session.h); player 1 is put in the box (the debug teleport stands for the walk
// up the stairs) and then stands still. It runs only when the environment variable CONEY_DISC names the disc and skips
// otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/goal.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "human/human.h"
#include "human/place.h"
#include "human/script_state.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/live_pad.h"
#include "world_objects/level_objects.h"
#include "world_objects/spawn_records.h"

namespace {

// The frame of play at which the opening scene is skipped, and the one player 1 is put in the box at.
constexpr std::uint64_t kSkipAt = 100;
constexpr std::uint64_t kEnterAt = 400;
// How long the looters are given after that: 60 s of 1/30 s frames, four times the original's 15 s.
constexpr std::uint64_t kLootFrames = 30 * 60;
// The intro looters' object zone (level34_riot.lua, P1.StartIntroLooters).
constexpr std::uint32_t kZone = 51;
// The middle of `vStartIntroLooters` (level34.lua: corner (-8.9, -72.8, -6.2), size 3 x 4 x 4).
constexpr float kBoxX = -7.4F;
constexpr float kBoxY = -70.8F;

// The records of zone kZone, and how many of them are broken (a hit counter run down) or gone.
struct Zone {
    std::size_t records = 0;
    std::size_t broken = 0;
};

// Counts zone kZone's records in `run`.
Zone countZone(coney::test::DiscSession& run) {
    Zone zone;
    const coney::world_objects::LevelObjects& objects = run.session().gameplay().objects();
    for (const coney::world_objects::SpawnRecord& record : run.session().flow().spawnRecords().all()) {
        if (record.zone != kZone) {
            continue;
        }
        ++zone.records;
        const auto counters = objects.props.counters(record.handle);
        if (record.removed || (counters && (counters->first == 0 || counters->second == 0))) {
            ++zone.broken;
        }
    }
    return zone;
}

// The first trace line that starts with `text`; nothing when none does.
std::optional<std::string> firstLine(const std::vector<std::string>& trace, std::string_view text) {
    for (const std::string& line : trace) {
        if (line.starts_with(text)) {
            return line;
        }
    }
    return std::nullopt;
}

// Checkpoint 1 played to the moment player 1 stands in the looters' box, with the script trace kept.
struct Looters {
    coney::test::LivePad pad;
    std::optional<coney::test::DiscSession> run;
    std::vector<std::string> trace;
    std::uint64_t entered = 0;

    // Starts checkpoint 1 over `wad`, skips the opening scene and puts player 1 in the box.
    explicit Looters(const coney::io::Wad& wad) {
        pad.play(std::format("{} tap cross\n", kSkipAt));
        run.emplace(wad, pad);
        coney::script::ScriptSystem& scripts = run->session().flow().scripts();
        run->session().startAtLevel("level34", 1, [&scripts, this] {
            scripts.traceCalls([this](std::string_view line) { trace.emplace_back(line); });
        });
        REQUIRE(run->runUntilPlay());
        while (run->playFrames() < kEnterAt) {
            run->run(1);
        }
        REQUIRE(countZone(*run).records > 0);
        coney::platform::PlayLevelMode* mode = run->session().play();
        REQUIRE(mode != nullptr);
        mode->teleport(coney::human::Place{.name = "looters", .feet = {kBoxX, kBoxY, mode->playerFeet().z}});
        entered = run->playFrames();
    }

    // The AI brains that run a Destroy goal (the looters at work).
    [[nodiscard]] std::vector<coney::ai::Brain*> wreckers() {
        std::vector<coney::ai::Brain*> found;
        const coney::ai::Brains* brains = run->session().gameplay().brains();
        if (brains == nullptr) {
            return found;
        }
        auto& all = const_cast<coney::ai::Brains&>(*brains);
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all.at(i).findGoal(coney::ai::GoalType::Destroy) != nullptr) {
                found.push_back(&all.at(i));
            }
        }
        return found;
    }
};

} // namespace

TEST_CASE("the disc's level34 checkpoint 1: the intro looters break zone 51's objects before they leave",
          "[disc][story]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    Looters looters(*wad);
    coney::test::DiscSession& run = *looters.run;
    const Zone before = countZone(run);
    std::optional<std::string> done;
    std::size_t brokenWhenDone = 0;
    while (run.playFrames() < looters.entered + kLootFrames && !done) {
        run.run(1);
        done = firstLine(looters.trace, "> P1.DoneLooting(");
        if (done) {
            brokenWhenDone = countZone(run).broken;
        }
    }
    const Zone after = countZone(run);
    std::printf("  level34 checkpoint 1, zone %u: %zu records, %zu broken before, %zu when the looters left, %zu "
                "after %llu frames; %s\n",
                kZone, before.records, before.broken, brokenWhenDone, after.broken,
                static_cast<unsigned long long>(run.playFrames() - looters.entered),
                done ? done->c_str() : "no DoneLooting call");
    REQUIRE(firstLine(looters.trace, "TacticVandalize(").has_value());
    // They broke most of the zone and called back, and not before they had broken something.
    CHECK(after.broken * 2 > after.records);
    REQUIRE(done.has_value());
    CHECK(brokenWhenDone > before.broken);
}

TEST_CASE("the disc's level34 checkpoint 1: a looter's throw lets go of the object he fetched", "[disc][story]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    Looters looters(*wad);
    coney::test::DiscSession& run = *looters.run;
    // Each looter's held object from the last frame; a throw empties the hand.
    std::map<coney::ai::Brain*, double> held;
    std::size_t fetched = 0;
    std::size_t thrown = 0;
    while (run.playFrames() < looters.entered + kLootFrames && thrown == 0) {
        run.run(1);
        for (coney::ai::Brain* looter : looters.wreckers()) {
            const double now = looter->human().script().heldObject;
            const double was = held.contains(looter) ? held[looter] : 0.0;
            fetched += was == 0.0 && now != 0.0 ? 1 : 0;
            thrown += was != 0.0 && now == 0.0 ? 1 : 0;
            held[looter] = now;
        }
    }
    std::printf("  level34 checkpoint 1: %zu objects fetched, %zu let go, by frame %llu\n", fetched, thrown,
                static_cast<unsigned long long>(run.playFrames() - looters.entered));
    REQUIRE(fetched > 0);
    CHECK(thrown > 0);
}
