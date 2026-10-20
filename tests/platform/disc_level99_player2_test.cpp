// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that `level99` keeps Ash, player 2's character, out of a one-player game at
// checkpoint 2 (docs/research/scripting.md#level99-p2-vermin-fence, item 41; issue #103). Every checkpoint's Warriors
// make Ash with `HuCreate(..., 2, ...)` at z -8.5, below the street; `HuCreate`'s player argument makes a player only
// when it is 1, so `HuIsAPlayer(Ash)` is false until a second pad joins, and `P2.SetupLesson1` then takes him off the
// radar, suspends his brain and leaves him out of the scene `l99_c2`. Both ways into checkpoint 2 are played in the
// game session as `--play-level level99` plays them (support/disc_session.h): a direct start, whose set-up runs while
// the level's humans are still being made, and the hand-over from checkpoint 1 (the course's end called by the test,
// as level99's hand-over test does). It runs only when the environment variable CONEY_DISC names the disc and skips
// otherwise; it prints counts and positions only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "fileio/wad.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/created_humans.h"

namespace {

// The pad: cross at 100 (the intro scene at checkpoint 1, `l99_c2` at checkpoint 2) and at 405 (`l99_c2` after the
// hand-over), then nothing.
constexpr std::string_view kPad = "100 tap cross\n405 tap cross\n";
// The frame of play the course's end is called on (checkpoint 1), and the frame Ash is looked at, long after `l99_c2`.
constexpr std::uint64_t kCleanupAt = 300;
constexpr std::uint64_t kLookAt = 900;
// Ash is made at z -8.5, 8.8 m below the street (z 0.3): anything below this is out of sight.
constexpr float kBelowStreet = -5.0F;

// The handle of the last human the scripts made with `name`, or 0.
double lastNamed(const coney::CreatedHumans& humans, std::string_view name) {
    double handle = 0.0;
    for (const coney::HumanCreation& human : humans.all()) {
        if (human.name == name) {
            handle = human.handle;
        }
    }
    return handle;
}

// The brain the scripts name `handle`, or null.
coney::ai::Brain* brainNamed(coney::platform::PlayLevelMode& play, double handle) {
    coney::ai::Brains& brains = play.fighters().brains();
    for (std::size_t i = 0; i < brains.size(); ++i) {
        if (brains.at(i).handle() == handle) {
            return &brains.at(i);
        }
    }
    return nullptr;
}

// How many of `lines` start with `text`.
std::size_t countStarting(const std::vector<std::string>& lines, std::string_view text) {
    std::size_t count = 0;
    for (const std::string& line : lines) {
        count += line.starts_with(text) ? 1 : 0;
    }
    return count;
}

// Checkpoint 2 reached in `run` (directly, or from checkpoint 1 when `handOver`), played on to kLookAt: Ash (the
// checkpoint's last) must be no player, below the street with his brain suspended, and the scripts must have seen so.
void checkAshAway(coney::test::DiscSession& run, bool handOver) {
    coney::script::ScriptSystem& scripts = run.session().flow().scripts();
    std::vector<std::string> trace;
    run.session().startAtLevel("level99", handOver ? 1 : 2, [&scripts, &trace] {
        scripts.traceCalls([&trace](std::string_view line) { trace.emplace_back(line); });
    });
    REQUIRE(run.runUntilPlay());
    if (handOver) {
        REQUIRE(run.runToPlayFrame(kCleanupAt));
        CHECK(scripts.call("P1.Cleanup"));
    }
    REQUIRE(run.runToPlayFrame(kLookAt));
    CHECK(run.session().flow().state().checkPoint == 2);
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    const double ash = lastNamed(run.session().flow().humans(), "Ash");
    REQUIRE(ash != 0.0);
    const coney::ai::Brain* brain = brainNamed(*play, ash);
    REQUIRE(brain != nullptr);

    const coney::anim::Vec3 feet = brain->human().position();
    const std::size_t asked = countStarting(trace, std::format("HuIsAPlayer({:.0f})", ash));
    const std::size_t yes = countStarting(trace, std::format("HuIsAPlayer({:.0f}) -> 1", ash));
    const std::size_t suspends = countStarting(trace, std::format("BrSuspend({:.0f}", ash));
    std::printf("  level99 checkpoint 2 (%s): Ash %.0f at (%.1f, %.1f, %.1f), suspended %d; HuIsAPlayer asked %zu, "
                "true %zu; BrSuspend %zu\n",
                handOver ? "hand-over" : "direct", ash, feet.x, feet.y, feet.z, brain->suspended() ? 1 : 0, asked, yes,
                suspends);
    CHECK(brain->type() != coney::ai::BrainType::Player);
    CHECK(asked >= 1);
    CHECK(yes == 0);
    CHECK(suspends >= 1);
    CHECK(brain->suspended());
    CHECK(feet.z < kBelowStreet);
}

} // namespace

TEST_CASE("the disc's level99: a direct start at checkpoint 2 keeps Ash below the street, suspended",
          "[disc][story][player2]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kPad);
    checkAshAway(run, false);
}

TEST_CASE("the disc's level99: the hand-over to checkpoint 2 keeps Ash below the street, suspended",
          "[disc][story][player2]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kPad);
    checkAshAway(run, true);
}
