// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the debug menu's Movies page (docs/guides/debug-menu.md#movies) plays a
// movie over a level and gives the level back exactly as it was. `level99` is started as `coney --play-level level99`
// starts it (support/disc_session.h), with the pad going through the debug menus' input gate as in `coney`: the intro
// scene skipped, a walk at 60 % of the stick, then the menu opened with L3 + R3, walked to Movies and PLOGO chosen,
// which plays to its end; then the menu reopened on the Movies page and L99_IN chosen with its captions, skipped with
// cross. Before and after each movie the mode stack, player 1's feet and heading, the camera, the level and
// checkpoint, the HUD and its checklist and the sound engine's pause must be the same. It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "audio/sound_engine.h"
#include "core/input_script.h"
#include "debug/debug_session.h"
#include "debug/tunables.h"
#include "fileio/wad.h"
#include "gamemodes/movie_player.h"
#include "platform/audio_output.h"
#include "platform/game_session.h"
#include "platform/game_session_debug.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/game_state.h"

namespace {

// The play frames: the intro scene skipped, a walk, the menu opened and Movies reached, PLOGO chosen; later the menu
// reopened and L99_IN chosen, then skipped after kSkipAfter frames of it.
constexpr std::uint64_t kSkipScene = 100;
constexpr std::uint64_t kWalk = 150;
constexpr std::uint64_t kStop = 190;
constexpr std::uint64_t kOpenMenu = 220;
constexpr std::uint64_t kReopenMenu = 700;
constexpr std::uint64_t kSkipAfter = 1500;

// What the game looks like from outside: everything the movie must leave as it was.
struct Snapshot {
    std::uint32_t topMode = 0;
    std::size_t modes = 0;
    std::array<float, 4> player{}; // feet x, y, z and heading
    std::array<float, 6> camera{}; // eye and target
    std::size_t level = 0;
    double checkpoint = 0.0;
    bool hudVisible = false;
    std::vector<std::string> checklist;
    bool soundPaused = false;

    bool operator==(const Snapshot&) const = default;
};

// Reads the snapshot of `run`'s level in play.
Snapshot snapshot(coney::test::DiscSession& run) {
    Snapshot seen;
    seen.topMode = run.stack().topId();
    seen.modes = run.stack().size();
    const coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    const coney::anim::Vec3 feet = play->playerFeet();
    seen.player = {feet.x, feet.y, feet.z, play->playerHeadingDegrees()};
    const coney::anim::Vec3 eye = play->cameraEye();
    const coney::anim::Vec3 target = play->cameraTarget();
    seen.camera = {eye.x, eye.y, eye.z, target.x, target.y, target.z};
    coney::StartUpFlow& flow = run.session().flow();
    seen.level = flow.state().currentLevel;
    seen.checkpoint = flow.state().checkPoint;
    seen.hudVisible = flow.hud().visible();
    for (const auto& line : flow.hud().checklist().slots) {
        seen.checklist.push_back(line ? line->text : std::string{});
    }
    const coney::audio::SoundEngine* engine = run.audio().sounds().engine();
    seen.soundPaused = engine != nullptr && engine->paused();
    return seen;
}

// Taps `button` `count` times from play frame `from`, one every second frame, as a thumb on the d-pad does; returns
// the lines and moves `from` past them.
std::string taps(std::string_view button, std::size_t count, std::uint64_t& from) {
    std::string lines;
    for (std::size_t i = 0; i < count; ++i) {
        lines += std::format("{} tap {}\n", from, button);
        from += 2;
    }
    return lines;
}

} // namespace

TEST_CASE("the disc's level99: a movie from the debug menu plays over the level and gives it back as it was",
          "[disc][story][debug][movies]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // The Movies page lists the captions switch, then the disc's movies in order.
    const std::vector<std::string> movies = coney::discMovies(wad->disc());
    const auto rowOf = [&movies](std::string_view name) {
        const auto found = std::ranges::find(movies, name);
        REQUIRE(found != movies.end());
        return static_cast<std::size_t>(std::distance(movies.begin(), found)) + 1;
    };
    const std::size_t plogo = rowOf("PLOGO");
    const std::size_t l99 = rowOf("L99_IN");
    REQUIRE(l99 > plogo);

    // The pad: the scene skipped, a walk ahead at 60 % of the stick, then the menu.
    std::string pad = std::format("{} tap cross\n{} stick left 0 60\n{} stick left 0 0\n{} tap l3 r3\n", kSkipScene,
                                  kWalk, kStop, kOpenMenu);
    std::uint64_t at = kOpenMenu + 3;
    // Root page: Favourites, then the pages; Movies follows Missions.
    coney::debug::TunableRegistry probeTunables;
    const std::vector<std::string> titles =
        coney::debug::DebugSession(probeTunables, coney::debug::DebugServices{}, nullptr).model().pageTitles();
    const auto moviesPage = std::ranges::find(titles, "Movies");
    REQUIRE(moviesPage != titles.end());
    pad += taps("down", static_cast<std::size_t>(std::distance(titles.begin(), moviesPage)) + 1, at);
    pad += taps("cross", 1, at);
    pad += taps("down", plogo, at);
    const std::uint64_t choosePlogo = at;
    pad += taps("cross", 1, at);
    // PLOGO (12 s) is over long before the menu reopens, on the Movies page with the cursor where it was.
    at = kReopenMenu;
    pad += taps("l3 r3", 1, at);
    at += 1;
    pad += taps("down", l99 - plogo, at);
    const std::uint64_t chooseL99 = at;
    pad += taps("cross", 1, at);
    pad += std::format("{} tap cross\n", chooseL99 + 1 + kSkipAfter);
    auto parsed = coney::parseInputScript(pad);
    REQUIRE(parsed.has_value());
    coney::ScriptedInput script(parsed ? std::move(*parsed) : std::vector<coney::InputEvent>{});

    // The debug menus as `coney` wires them: the gate over the pad, the game's services from the session.
    std::unique_ptr<coney::test::DiscSession> run;
    coney::debug::DebugServices services;
    coney::platform::connectDebugServices(services, [&run] { return &run->session(); });
    coney::debug::TunableRegistry tunables;
    coney::debug::DebugSession debug(tunables, services, &script);
    run = std::make_unique<coney::test::DiscSession>(*wad, debug.gate());
    run->session().startAtLevel("level99", 1);
    REQUIRE(run->runUntilPlay());
    REQUIRE(run->session().play() != nullptr);
    const coney::anim::Vec3 start = run->session().play()->playerFeet();
    const coney::movies::MovieMode& player = run->session().movies();

    // PLOGO, chosen on the Movies page, to its end.
    REQUIRE(run->runToPlayFrame(choosePlogo + 1));
    CHECK_FALSE(debug.navigator().isOpen());
    const Snapshot beforePlogo = snapshot(*run);
    REQUIRE(run->runUntil([&run, &player] { return run->stack().top() == &player; }, 10));
    const std::uint64_t plogoStart = run->frames();
    REQUIRE(run->runUntil([&run, &player] { return run->stack().top() != &player; }, 1000));
    const std::uint64_t plogoFrames = run->frames() - plogoStart;
    const Snapshot afterPlogo = snapshot(*run);
    CHECK(player.counts().movies == 1);
    CHECK(player.counts().skipped == 0);

    // L99_IN, from the reopened menu, with its captions, skipped by cross.
    REQUIRE(run->runToPlayFrame(chooseL99 + 1));
    CHECK_FALSE(debug.navigator().isOpen());
    const Snapshot beforeL99 = snapshot(*run);
    REQUIRE(run->runUntil([&run, &player] { return run->stack().top() == &player; }, 10));
    REQUIRE(run->runUntil([&run, &player] { return run->stack().top() != &player; }, kSkipAfter + 100));
    const Snapshot afterL99 = snapshot(*run);
    CHECK(player.counts().movies == 2);
    CHECK(player.counts().skipped == 1);
    const auto captions = player.counts().captionsShown;
    // The level plays on afterwards.
    REQUIRE(run->runToPlayFrame(chooseL99 + kSkipAfter + 60));
    CHECK(run->session().inPlay());

    std::printf("  level99 debug movies: %zu on the disc; PLOGO over %llu frames, L99_IN skipped, %llu frames "
                "decoded, %llu captions shown; game state equal after each: %d, %d\n",
                movies.size(), static_cast<unsigned long long>(plogoFrames),
                static_cast<unsigned long long>(player.counts().framesDecoded),
                static_cast<unsigned long long>(captions), beforePlogo == afterPlogo ? 1 : 0,
                beforeL99 == afterL99 ? 1 : 0);
    CHECK(beforePlogo == afterPlogo);
    CHECK(beforeL99 == afterL99);
    CHECK(captions > 0);
    // The player walked before the movies, so the place kept is one he reached, not the start.
    CHECK(std::hypot(beforePlogo.player[0] - start.x, beforePlogo.player[1] - start.y) > 0.5F);
}
