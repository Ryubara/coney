// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the money count's sound ends with the count (user item 22,
// docs/research/sound-events.md#money-count-sound): player 1 walks into a `dyn_money` pick-up put 2.5 m ahead of him,
// with the left stick at 65 % toward it; the HUD counts the money with the looping cue 16 (`vags/misc/count_money2`),
// which must not be playing a few seconds later. `level99` checkpoint 1 plays in the game session as `--play-level`
// plays it (support/disc_session.h), its intro scene skipped. It runs only when the environment variable CONEY_DISC
// names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "audio/sound_engine.h"
#include "core/name_hash.h"
#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "gamemodes/start_up_flow.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/inventory.h"
#include "world_objects/spawn_records.h"

namespace {

// The frames of play: the intro scene skipped, the pick-up put down, the walk at it, the end of the run.
constexpr std::uint64_t kSkipAt = 100;
constexpr std::uint64_t kPlaceAt = 200;
constexpr std::uint64_t kWalkAt = 220;
constexpr std::uint64_t kWalkFrames = 90;
constexpr std::uint64_t kEnd = kWalkAt + kWalkFrames + 150; // 5 s after the stick is let go
constexpr std::uint32_t kDollars = 50;

} // namespace

TEST_CASE("the disc's level99: the money count's looping sound stops when a pick-up's count ends",
          "[disc][story][audio]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Cross skips the intro scene; then the left stick at 65 % up (north, the way he faces) for 3 s.
    coney::test::DiscSession run(*wad, std::format("{} tap cross\n{} stick left 0 65\n{} stick left 0 0\n", kSkipAt,
                                                   kWalkAt, kWalkAt + kWalkFrames));
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    coney::audio::SoundEngine* engine = run.audio().sounds().engine();
    REQUIRE(engine != nullptr);
    const std::uint32_t countSound = coney::crc32("vags/misc/count_money2");
    std::size_t asked = 0;
    engine->setPlayObserver([&asked, countSound](std::uint32_t hash) { asked += hash == countSound ? 1 : 0; });

    // Player 1 turned north where he stands, and $50 2.5 m north of his feet.
    REQUIRE(run.runToPlayFrame(kPlaceAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    const coney::anim::Vec3 start = play->playerFeet();
    play->teleport(coney::debug::Place{.name = "start", .feet = start, .headingDegrees = 0.0F});
    REQUIRE(run.runToPlayFrame(kPlaceAt + 10));
    const coney::anim::Vec3 feet = play->playerFeet();
    coney::world_objects::SpawnRecord record;
    record.handle = run.session().flow().scripts().nextObjectHandle();
    record.typeName = "dyn_money";
    record.position = {feet.x, feet.y + 2.5F, feet.z};
    record.money = kDollars;
    REQUIRE(run.session().flow().spawnRecords().add(record) != nullptr);
    const coney::Inventory& inventory = run.session().flow().state().player.inventory;
    const int before = inventory.count(0, coney::item::kMoney);

    // The walk, then the count, then a few seconds of nothing.
    const std::string taken = std::format("pickup: walked over object {:.0f}", record.handle);
    const auto took = [&run, &taken] {
        return std::ranges::any_of(run.log(), [&taken](const std::string& line) { return line.starts_with(taken); });
    };
    REQUIRE(run.runUntil(took, kWalkAt + kWalkFrames + 30));
    std::size_t liveWhileCounting = 0;
    for (int frame = 0; frame < 10; ++frame) {
        run.run(1);
        liveWhileCounting = std::max(liveWhileCounting, engine->liveCount(countSound));
    }
    REQUIRE(run.runToPlayFrame(kEnd));
    const int after = inventory.count(0, coney::item::kMoney);
    const std::size_t liveAfter = engine->liveCount(countSound);
    std::printf("  level99 money count: $%d -> $%d; count sound asked %zu, live while counting %zu, live after %zu\n",
                before, after, asked, liveWhileCounting, liveAfter);
    CHECK(after == before + static_cast<int>(kDollars));
    // The count asked for its sound once and kept that one voice, and nothing of it plays once the count is over.
    CHECK(asked == 1);
    CHECK(liveWhileCounting == 1);
    CHECK(liveAfter == 0);
}
