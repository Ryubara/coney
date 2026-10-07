// SPDX-License-Identifier: GPL-3.0-or-later

// Rumble matches played on the player's own disc, from boot through the menus, headless, with the game's own scripts
// (docs/research/rumble.md): a QUICK RUMBLE Brawl the player loses ends on the result screen with the other gang's
// win, the winner cheering and the player revived on the way; in a WAR PARTY the pad passes to a team-mate when the
// player goes down; in King of the hill the player held on the top wins for his gang; in Battle royal the side left in
// the ring wins; in Survival the spawned enemies beat the player and his time is the result; in Wheelchair the player
// rolls with L1 and R1 and the CPU racer wins. They run only when
// CONEY_DISC names the disc and skip otherwise; they print counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/options.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/rumble_result_mode.h"
#include "human/human_flags.h"
#include "platform/play_level_mode.h"
#include "scripting/lua_value.h"
#include "support/disc_play_fixtures.h"
#include "warriors/created_humans.h"
#include "world_objects/volume_boxes.h"

TEST_CASE("the disc's QUICK RUMBLE Brawl the player loses ends on the result screen with the Orphans' win",
          "[disc][rumble]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Through the menus and the intro, then the pad left alone: the Orphan confronts the Fury and beats him.
    coney::test::DiscGame game(*wad, coney::test::kQuickRumbleScript);
    game.run(1000);
    REQUIRE(game.stack().topId() == coney::GameplayMode::kId);
    CHECK_FALSE(game.flow().rumbleIntro().showing());

    // The Fury's side has nobody standing: the win sequence (3 s) and then the result screen, mode 0x14, naming the
    // Orphans. Within five minutes of play.
    constexpr std::uint64_t kLimit = 9000;
    const bool ended = game.runUntilTop(coney::RumbleResultMode::kId, kLimit);
    for (const std::string& line : game.log()) {
        UNSCOPED_INFO(line);
    }
    REQUIRE(ended);
    const std::uint64_t resultFrame = game.frames();
    const coney::RumbleResultMode& result = game.flow().rumbleResult();
    CHECK(result.winner().find("ORPHANS") != std::string::npos);
    CHECK_FALSE(result.reason().empty());
    // The winner cheers: his idle replaced by the cheer clip loaded from the disc (HuUseAnim).
    CHECK(std::ranges::any_of(game.log(), [](const std::string& line) {
        return line.starts_with("anim: ") && line.find("replaces a human's idle") != std::string::npos;
    }));
    // The player's 4 s revival (CheckPlayerDead), a second after the result screen opens.
    game.run(45);
    const coney::platform::PlayLevelMode* play = game.play();
    REQUIRE(play != nullptr);
    CHECK(play->player().human().alive());
    CHECK(game.flow().scripts().errors() == 0);
    std::printf("  rumble lost: result screen at frame %llu, %zu log lines\n",
                static_cast<unsigned long long>(resultFrame), game.log().size());
}

TEST_CASE("the disc's WAR PARTY hands the pad to a team-mate when the player goes down", "[disc][rumble]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // QUICK RUMBLE with WAR PARTY (the stick down most of the way on Game Mode), then after the player is downed the
    // stick at 40 % right and 70 % forward for two seconds.
    std::string script(coney::test::kQuickRumbleScript);
    script.insert(script.find("280 tap cross"), "275 stick left 0 -70\n277 stick left 0 0\n");
    script += "1100 stick left 40 70\n1160 stick left 0 0\n";
    coney::test::DiscGame game(*wad, script);
    game.run(1000);
    REQUIRE(game.stack().topId() == coney::GameplayMode::kId);
    CHECK(game.flow().state().rumble.values[coney::RumbleSetup::kGangSize] == 5);
    const coney::platform::PlayLevelMode* play = game.play();
    REQUIRE(play != nullptr);
    const auto& humans = game.flow().humans().all();
    const auto p11 = std::ranges::find(humans, std::string_view("P11"), &coney::HumanCreation::name);
    REQUIRE(p11 != humans.end());

    // P11 knocked out: CheckPlayerDead with four standing hands the pad on (HuSwitchPlayer).
    const std::array<coney::script::Value, 1> args{coney::script::Value(p11->handle)};
    REQUIRE(game.flow().scripts().call("HuKill", args));
    game.run(30);
    const auto passes = std::ranges::count_if(
        game.log(), [](const std::string& line) { return line.starts_with("player: the pad passes from human"); });
    CHECK(passes == 1);
    CHECK(&play->player().human() != &play->player().body());
    CHECK(play->player().human().alive());

    // The pad drives the team-mate now. The other gangs' brains are suspended (BrSuspend) so that none grabs him while
    // the stick is pushed: the AI grabs and tackles (docs/research/ai.md#coney), and this checks only the pad.
    for (const coney::HumanCreation& human : humans) {
        if (human.gang != p11->gang) {
            const std::array<coney::script::Value, 2> suspend{coney::script::Value(human.handle),
                                                              coney::script::Value(true)};
            CHECK(game.flow().scripts().call("BrSuspend", suspend));
        }
    }
    const coney::anim::Vec3 before = play->player().human().position();
    game.run(150);
    const coney::anim::Vec3 after = play->player().human().position();
    CHECK(std::hypot(after.x - before.x, after.y - before.y) > 1.0F);
    CHECK(game.flow().scripts().errors() == 0);
    std::printf("  war party: the pad passed %lld time(s); the new player moved %.2f m\n",
                static_cast<long long>(passes),
                static_cast<double>(std::hypot(after.x - before.x, after.y - before.y)));
}

TEST_CASE("the disc's King of the hill scores a point a tick for the gang whose player holds the top",
          "[disc][rumble]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Three a side in arena 101; once the countdown is over the player is put on the top tier's flag.
    coney::test::DiscGame game(*wad, coney::test::kQuickRumbleScript);
    game.chooseRumble(2, 101, 3);
    game.run(1000 - game.frames());
    REQUIRE(game.stack().topId() == coney::GameplayMode::kId);
    const auto& humans = game.flow().humans().all();
    const auto p11 = std::ranges::find(humans, std::string_view("P11"), &coney::HumanCreation::name);
    REQUIRE(p11 != humans.end());
    const std::optional<double> top = game.flow().scripts().vm().global("fTopTier").number();
    REQUIRE(top.has_value());
    if (!top) {
        return;
    }
    const std::array<coney::script::Value, 2> args{coney::script::Value(p11->handle), coney::script::Value(*top)};
    REQUIRE(game.flow().scripts().call("TeleportToFlag", args));

    // X.Update every 1,850 ms: the Furies score while their player stands in vTopTier, and lead the scoreboard.
    game.run(900);
    const auto& rows = game.flow().hud().textProgress();
    REQUIRE(rows[0].active);
    CHECK(rows[0].label.find("FURIES") != std::string::npos);
    CHECK(rows[0].score >= 3);
    // The Orphans' AI climbs to the top too (TacticDomination): they score, but less.
    CHECK(rows[0].score > rows[1].score);
    std::printf("  king of the hill: %s %u, %s %u after 15 s on top\n", rows[0].label.c_str(), rows[0].score,
                rows[1].label.c_str(), rows[1].score);

    // Held to 100: the movement lock, X.GameOver 4 s later and the result screen naming the winner. Either gang may
    // win: the Orphans' AI grabs, tackles and throws the idle Furies player off the top (docs/research/ai.md#coney).
    const bool ended = game.runUntilTop(coney::RumbleResultMode::kId, 9000);
    REQUIRE(ended);
    const std::string& winner = game.flow().rumbleResult().winner();
    CHECK((winner.find("FURIES") != std::string::npos || winner.find("ORPHANS") != std::string::npos));
    // X.OffTopTier still runs for the fighters X.GameOver teleports off the top and those the win camera deletes;
    // each still has its gang (HuGetGang), so no script fails.
    CHECK(game.flow().scripts().errors() == 0);
    std::printf("  king of the hill: result screen at frame %llu\n", static_cast<unsigned long long>(game.frames()));
}

TEST_CASE("the disc's Battle royal kills the fighters rung out and gives the win to the side left in the ring",
          "[disc][rumble]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Three a side in arena 131; after the countdown the Orphans are put outside the ring box, beyond its far side.
    coney::test::DiscGame game(*wad, coney::test::kQuickRumbleScript);
    game.chooseRumble(3, 131, 3);
    game.run(1000 - game.frames());
    REQUIRE(game.stack().topId() == coney::GameplayMode::kId);
    const std::optional<double> ring = game.flow().scripts().vm().global("vRing").number();
    REQUIRE(ring.has_value());
    if (!ring) {
        return;
    }
    const coney::world_objects::VolumeBox* box = game.flow().context().boxes->find(*ring);
    REQUIRE(box != nullptr);
    int rungOut = 0;
    for (const coney::HumanCreation& human : game.flow().humans().all()) {
        if (!human.name.starts_with("P2")) {
            continue;
        }
        auto point = std::make_shared<coney::script::Table>();
        REQUIRE(point->set(coney::script::Value(1.0), coney::script::Value(box->high[0] + 4.0 + rungOut)).has_value());
        REQUIRE(point->set(coney::script::Value(2.0), coney::script::Value(box->high[1] + 4.0)).has_value());
        REQUIRE(
            point->set(coney::script::Value(3.0), coney::script::Value(static_cast<double>(box->low[2]))).has_value());
        const std::array<coney::script::Value, 2> args{coney::script::Value(human.handle),
                                                       coney::script::Value(std::move(point))};
        REQUIRE(game.flow().scripts().call("Teleport", args));
        ++rungOut;
    }
    REQUIRE(rungOut == 3);

    // OutOfRing kills each 3 s later; with nobody of theirs left the Furies win.
    const bool ended = game.runUntilTop(coney::RumbleResultMode::kId, 1500);
    for (const std::string& line : game.log()) {
        UNSCOPED_INFO(line);
    }
    REQUIRE(ended);
    CHECK(game.flow().rumbleResult().winner().find("FURIES") != std::string::npos);
    CHECK(game.flow().scripts().errors() == 0);
    std::printf("  battle royal: %d rung out, result screen at frame %llu\n", rungOut,
                static_cast<unsigned long long>(game.frames()));
}

TEST_CASE("the disc's Survival sends spawned enemies at the player until he falls, the time his result",
          "[disc][rumble]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // One player in arena 134, the pad left alone: the two spawners make the enemies out of his sight, beyond 15 m.
    coney::test::DiscGame game(*wad, coney::test::kQuickRumbleScript);
    game.chooseRumble(9, 134, 1);
    // Each enemy is measured from the spawner's origin as it spawns (the camera at the player's height): the enemies
    // grab, tackle and throw the player, so he and his camera do not stay where they started
    // (docs/research/ai.md#coney).
    std::map<double, std::array<float, 3>> playerAtSpawn;
    const auto spawnOrigin = [&game]() -> std::optional<std::array<float, 3>> {
        const coney::HumanCreation* player = game.flow().humans().player(1);
        if (player == nullptr) {
            return std::nullopt;
        }
        const std::array<coney::script::Value, 1> args{coney::script::Value(player->handle)};
        const std::optional<std::vector<coney::script::Value>> at =
            game.flow().scripts().callResults("HuGetPosition", args);
        if (!at || at->empty() || !(*at)[0].table()) {
            return std::nullopt;
        }
        const coney::platform::PlayLevelMode* play = game.play();
        if (play == nullptr) {
            return std::nullopt;
        }
        // The search's origin: the camera's position with the player's height (docs/research/ai.md#spawner-search).
        const coney::anim::Vec3 eye = play->cameraEye();
        const coney::script::Value& v = (*at)[0];
        return std::array<float, 3>{eye.x, eye.y, static_cast<float>(v.table()->field("z").number().value_or(0.0))};
    };
    while (game.frames() < 1000) {
        game.run(1);
        for (const coney::HumanCreation& human : game.flow().humans().all()) {
            if (human.name.starts_with("ENEMYspawner") && !playerAtSpawn.contains(human.handle)) {
                if (const std::optional<std::array<float, 3>> at = spawnOrigin()) {
                    playerAtSpawn.emplace(human.handle, *at);
                }
            }
        }
    }
    REQUIRE(game.stack().topId() == coney::GameplayMode::kId);
    const auto spawned = [&game] {
        return std::ranges::count_if(game.flow().humans().all(), [](const coney::HumanCreation& human) {
            return human.name.starts_with("ENEMYspawner");
        });
    };
    // Both spawners, ten and six alive at most.
    CHECK(spawned() >= 10);
    CHECK(spawned() <= 16);
    // Each stands on a route node out of the camera's sight, beyond the spawners' 15 m from the origin as it spawned
    // (few nodes, the search being the same each time).
    float nearest = 1.0e9F;
    std::set<std::array<float, 3>> spots;
    for (const coney::HumanCreation& human : game.flow().humans().all()) {
        const auto there = playerAtSpawn.find(human.handle);
        if (human.name.starts_with("ENEMYspawner") && human.position && there != playerAtSpawn.end()) {
            const std::array<float, 3>& at = *human.position;
            const std::array<float, 3>& from = there->second;
            nearest = std::min(nearest, std::hypot(at[0] - from[0], at[1] - from[1], at[2] - from[2]));
            spots.insert(at);
        }
    }
    CHECK(nearest > 15.0F);
    std::printf("  survival: nearest spawn %.1f m from the origin as it spawned, %zu spots\n", nearest, spots.size());

    // They beat him: SavePlayerStats ends the match with side 1's "win" and the time he lasted.
    const bool ended = game.runUntilTop(coney::RumbleResultMode::kId, 9000);
    for (const std::string& line : game.log()) {
        UNSCOPED_INFO(line);
    }
    REQUIRE(ended);
    CHECK(game.flow().rumbleResult().reason().find("Time:") != std::string::npos);
    CHECK(game.flow().scripts().errors() == 0);
    std::printf("  survival: %lld enemies spawned, result screen at frame %llu\n", static_cast<long long>(spawned()),
                static_cast<unsigned long long>(game.frames()));
}

TEST_CASE("the disc's Wheelchair race is won by the CPU racer that drives the course while the player waits",
          "[disc][rumble]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // One player in arena 104: player 2 is the CPU, on the WheelChairRace path. The player pushes both wheels (L1 and
    // R1) for a second, then lets the pad go.
    std::string script(coney::test::kQuickRumbleScript);
    script += "1000 press l1\n1000 press r1\n1030 release l1\n1030 release r1\n";
    coney::test::DiscGame game(*wad, script);
    game.chooseRumble(24, 104, 1);
    game.run(1000 - game.frames());
    REQUIRE(game.stack().topId() == coney::GameplayMode::kId);
    const coney::platform::PlayLevelMode* play = game.play();
    REQUIRE(play != nullptr);
    CHECK(play->player().human().hasFlag(coney::human::flag::kWheelchair));
    const coney::anim::Vec3 before = play->player().human().position();
    game.run(30);
    const coney::anim::Vec3 after = play->player().human().position();
    // 0.5 m/s more each update, up to the sprint speed: about 9 m in the second.
    const float pushed = std::hypot(after.x - before.x, after.y - before.y);
    CHECK(pushed > 3.0F);
    std::printf("  wheelchair: the player rolled %.2f m in a second of L1 and R1\n", static_cast<double>(pushed));

    // Its glow moves on at each checkpoint it reaches (the trigger sphere on the teleported glow); a lap and the
    // finish line give it the race.
    const bool ended = game.runUntilTop(coney::RumbleResultMode::kId, 9000);
    for (const std::string& line : game.log()) {
        UNSCOPED_INFO(line);
    }
    REQUIRE(ended);
    CHECK(game.flow().rumbleResult().reason().find("WINS") != std::string::npos);
    CHECK(game.flow().scripts().errors() == 0);
    std::printf("  wheelchair: result screen at frame %llu\n", static_cast<unsigned long long>(game.frames()));
}

TEST_CASE("the disc's `--rumble kinghill` starts King of the hill in arena 101 from the Rumble menu's QUICK RUMBLE",
          "[disc][rumble]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // The command line's own parse, then the menu's QUICK RUMBLE with its default choice (1 ON 1, the Fight Pen): the
    // option overrides it when the arena is confirmed.
    const std::array<std::string_view, 4> args{"--disc", "x", "--rumble", "kinghill"};
    auto options = coney::parseOptions(args);
    REQUIRE(options.has_value());
    REQUIRE(options->rumble.has_value());
    coney::test::DiscGame game(*wad, coney::test::kQuickRumbleScript, options->rumble);
    game.run(1000);
    REQUIRE(game.stack().topId() == coney::GameplayMode::kId);
    const coney::RumbleSetup& rumble = game.flow().state().rumble;
    CHECK(rumble.values.at(coney::RumbleSetup::kGameType) == 2);
    CHECK(rumble.values.at(coney::RumbleSetup::kGangSize) == 3);
    CHECK(rumble.levelNumber == 101);
    // The arena's own script ran with that set-up: King of the hill's top tier flag exists and nothing failed.
    CHECK(game.flow().scripts().vm().global("fTopTier").number().has_value());
    CHECK(game.flow().scripts().errors() == 0);
    CHECK(std::ranges::any_of(game.log(), [](const std::string& line) {
        return line.find("rumble menu: start level101") != std::string::npos;
    }));
    std::printf("  --rumble kinghill: gameplay in arena %d, %zu log lines\n", rumble.levelNumber, game.log().size());
}
