// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the clubhouse gym's workouts (level95, docs/research/workouts.md) play
// through the pad as a player plays them: level95 starts at checkpoint 2 in the game session as `--play-level` starts
// it (support/disc_session.h), and a driver that reads the game walks player 1 to a station with the left stick at a
// partial deflection along the level's paths, taps triangle beside its prompt and then plays the station. It runs only
// when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/live_pad.h"
#include "support/stick_walker.h"
#include "world_objects/flags.h"

namespace {

using coney::anim::Vec3;

// The stations' prompt flags (fStartWorkout[k]) and workout flags (fWorkout[k]) as the research gives them, metres.
// Each has its unlock offset (the rank's records start there) and, for a cycle station, the span of updates between
// two repetitions pumped every 4 updates: the runtime's (37 at the chin-ups, 30-31 at the push-ups, 39-40 at the
// sit-ups, docs/research/workouts.md#repetitions) and 3 either side, for the blend's value near but not at 2. Coney's
// repetitions come about a tenth sooner (an open question on that page), so the chin-ups' span reaches down to 32.
struct Station {
    std::string_view name;
    Vec3 prompt;
    Vec3 workout;
    int unlockOffset = 0;
    std::uint64_t repGapMin = 0;
    std::uint64_t repGapMax = 0;
};
constexpr std::array<Station, 4> kStations{{
    {"heavy bag", {-174.8F, 106.0F, -197.5F}, {-175.5F, 106.0F, -197.4F}, 11, 0, 0},
    {"chin-ups", {-181.0F, 106.4F, -197.5F}, {-181.0F, 105.7F, -197.5F}, 31, 32, 40},
    {"push-ups", {-174.0F, 109.6F, -197.4F}, {-174.0F, 109.0F, -197.4F}, 41, 27, 34},
    {"sit-ups", {-177.2F, 109.5F, -197.4F}, {-177.2F, 108.7F, -197.4F}, 51, 36, 43},
}};

// Frames the walk onto fWorkout[k] may take: up to worksetup.TimeOut's 5 s (150 updates), since a crew mate the
// scripts put on the station (flags.AssignToClubhouseFlags, from the gameplay stream) is first sent off it; the
// engine's start after it (the workout file's load and StartTimeOut's 1.5 s), the prompt's return (DelayQuit's 1.5 s
// and more), and the updates from the loop's start to the end with no presses (63 at runtime,
// docs/research/workouts.md#repetitions).
constexpr int kArrivalFrames = 150;
constexpr int kStartFrames = 60;
constexpr int kPromptFrames = 150;
constexpr int kRunDownMin = 58;
constexpr int kRunDownMax = 68;

// How near the driver goes to the prompt before it stops: inside the context action's 1.8 m (the walk itself is
// support/stick_walker.h's).
constexpr float kPromptReach = 1.0F;
// The gym's stairs, from the main room (z -194.3) down to the gym's floor (z -197.5), as the level's path data has
// them; a player above kGymFloorAbove is in the main room.
constexpr Vec3 kStairsTop{-164.02F, 97.34F, -194.30F};
constexpr Vec3 kStairsFoot{-163.46F, 91.59F, -197.47F};
constexpr float kGymFloorAbove = -196.0F;

// How many of `lines` from `from` on contain `text`.
std::size_t countFrom(const std::vector<std::string>& lines, std::size_t from, std::string_view text) {
    std::size_t count = 0;
    for (std::size_t i = from; i < lines.size(); ++i) {
        count += lines[i].find(text) != std::string::npos ? 1 : 0;
    }
    return count;
}

// level95 at checkpoint 2 with a pad the driver steers frame by frame, the scripts' calls traced.
class Gym {
  public:
    explicit Gym(const coney::io::Wad& wad) : m_run(wad, m_pad), m_scripts(m_run.session().flow().scripts()) {
        m_run.session().startAtLevel("level95", 2, [this] {
            m_scripts.traceCalls([this](std::string_view line) { m_trace.emplace_back(line); });
        });
    }

    // Runs until the level plays; whether it does.
    bool start() { return m_run.runUntilPlay(); }

    // The play mode (null before the level plays) and player 1's human.
    coney::platform::PlayLevelMode* play() { return m_run.session().play(); }
    const coney::human::Human& player() { return play()->player().human(); }
    // Whether the pad drives player 1 now (the scripts hold him otherwise).
    bool padControlled() { return play()->player().padControlled(); }

    // The pad at rest, a tap or the stick for one frame, and the walks along the level's paths
    // (support/stick_walker.h).
    void frame(int x = 0, int y = 0, std::string_view taps = {}) { m_walker.frame(x, y, taps); }
    void idle(int frames) { m_walker.idle(frames); }
    bool idleUntil(const std::function<bool()>& done, int limit) { return m_walker.idleUntil(done, limit); }
    bool walkTo(Vec3 point, float reach, int limit) { return m_walker.walkTo(point, reach, limit); }

    // Walks player 1 to `station`'s prompt and stops there: from the main room down the gym's stairs first (the gym
    // lies under the main room, and the planner, which looks in plan, would send him at the rail above it).
    bool goToStation(const Station& station, int limit) {
        if (player().position().z > kGymFloorAbove) {
            if (!walkTo(kStairsTop, coney::test::StickWalker::kWaypointReach, limit) ||
                !walkTo(kStairsFoot, coney::test::StickWalker::kWaypointReach, limit)) {
                return false;
            }
        }
        const bool there = walkTo(station.prompt, kPromptReach, limit);
        idle(2);
        return there;
    }

    // The scripts' traced calls, and how many from `from` on contain `text`.
    const std::vector<std::string>& trace() const { return m_trace; }
    std::size_t traced(std::size_t from, std::string_view text) const { return countFrom(m_trace, from, text); }
    // The frames of play so far.
    std::uint64_t playFrames() const { return m_run.playFrames(); }
    coney::script::ScriptSystem& scripts() { return m_scripts; }

  private:
    coney::test::LivePad m_pad;
    coney::test::DiscSession m_run;
    coney::test::StickWalker m_walker{m_run, m_pad};
    coney::script::ScriptSystem& m_scripts;
    std::vector<std::string> m_trace;
};

// What one cycle station's playthrough saw.
struct CycleRun {
    std::vector<std::uint64_t> repFrames; // the frames of play each workout.DoRep came on
    bool started = false;
    bool ended = false;
    int loopAt = -1; // the frame (from the call's start) the loop first played, after the enter clip
    int endAt = -1;  // the frame the end callback came on
};

// Starts the station whose prompt the player stands at: triangle, then the walk onto fWorkout[k] (message 8) and
// the engine's start callback (workout.Started). Whether it started.
bool startCycle(Gym& gym, std::size_t& from) {
    from = gym.trace().size();
    gym.frame(0, 0, "triangle");
    return gym.idleUntil([&] { return gym.traced(from, "> worksetup.DoWorkout(") == 1; }, kArrivalFrames) &&
           gym.idleUntil([&] { return gym.traced(from, "> workout.Started(") == 1; }, kStartFrames);
}

// Plays the started cycle game for at most `limit` frames: cross every `every` frames (0: never), triangle at frame
// `quitAt` (none when negative); the frames of each repetition, until the engine's end callback (workout.CycleQuit).
CycleRun playCycle(Gym& gym, std::size_t from, int every, int limit, int quitAt = -1) {
    CycleRun run{.repFrames = {}, .started = true, .ended = false};
    for (int i = 0; i < limit && !run.ended; ++i) {
        const bool quit = i == quitAt;
        const bool pump = every > 0 && i % every == 0;
        gym.frame(0, 0, quit ? "triangle" : (pump ? "cross" : ""));
        if (run.loopAt < 0 && gym.player().workoutLoop() != nullptr) {
            run.loopAt = i;
        }
        while (gym.traced(from, "> workout.DoRep(") > run.repFrames.size()) {
            run.repFrames.push_back(gym.playFrames());
        }
        run.ended = gym.traced(from, "> workout.CycleQuit(") > 0;
        if (run.ended) {
            run.endAt = i;
        }
    }
    return run;
}

// Waits for the station's prompt to come back (worksetup.FinishWorkout's handler on fStartWorkout[k]) and the pad to be
// the player's again; whether it did.
bool promptBack(Gym& gym, std::size_t from) {
    return gym.idleUntil(
        [&] {
            return gym.traced(from, "\"worksetup.StartWorkout\"") > 0 && gym.padControlled() &&
                   !gym.player().script().movementLocked;
        },
        kPromptFrames);
}

} // namespace

TEST_CASE("the disc's level95: triangle at the heavy bag walks the player to it and starts the bag's game",
          "[disc][story][hub][workout]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    Gym gym(*wad);
    REQUIRE(gym.start());
    // The hub's opening holds the player a while; then he walks to the bag's prompt.
    const Station& bag = kStations[0];
    REQUIRE(gym.goToStation(bag, 3000));
    const std::size_t from = gym.trace().size();
    gym.frame(0, 0, "triangle");
    // Expected: workouts.md#start, confirmed (code), and workouts.md#heavy-bag, confirmed (runtime)
    // worksetup.StartWorkout; the walk and turn to fWorkout[1] arrive (message 8: worksetup.DoWorkout) inside
    // worksetup.TimeOut's 5 s, also when a crew mate held the bag; the bag's game starts with its 26 s clock.
    REQUIRE(gym.idleUntil([&] { return gym.traced(from, "> worksetup.DoWorkout(") == 1; }, kArrivalFrames));
    CHECK(gym.traced(from, "> worksetup.StartWorkout(") == 1);
    REQUIRE(gym.idleUntil([&] { return gym.traced(from, "W_SetStopWatch(26000, 0, \"workout.WorkoutTimeOut\")") > 0; },
                          60));
    CHECK(gym.traced(from, "> worksetup.TimeOut(") == 0);
    std::printf("  level95 heavy bag: started %zu, arrived %zu, clock set %zu, timed out %zu\n",
                gym.traced(from, "> worksetup.StartWorkout("), gym.traced(from, "> worksetup.DoWorkout("),
                gym.traced(from, "W_SetStopWatch(26000"), gym.traced(from, "> worksetup.TimeOut("));
}

TEST_CASE("the disc's level95: the cycle stations play through the pad: start, pump, run down, quit",
          "[disc][story][hub][workout]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    Gym gym(*wad);
    REQUIRE(gym.start());
    for (std::size_t k = 1; k < kStations.size(); ++k) {
        const Station& station = kStations[k];
        INFO(station.name);
        REQUIRE(gym.goToStation(station, 3000));

        // 1-2. Start and pump: cross every 4 updates gives a repetition every 30-40 updates; the fifth wins rank 1
        // (repTable[1] = 5, no clock): UM_Unlock(95, 1, offset), HuStopWorkout, the exit, the prompt back with the new
        // rank (docs/research/workouts.md#cycle-script).
        std::size_t from = 0;
        REQUIRE(startCycle(gym, from));
        CHECK(gym.traced(from, "HUDTurnOnActionCycleAnim(") == 1);
        const CycleRun pumped = playCycle(gym, from, 4, 600);
        CHECK(pumped.ended);
        CHECK(pumped.repFrames.size() == 5);
        for (std::size_t r = 1; r < pumped.repFrames.size(); ++r) {
            const std::uint64_t gap = pumped.repFrames[r] - pumped.repFrames[r - 1];
            INFO("repetition " << r << ": " << gap << " updates");
            CHECK(gap >= station.repGapMin);
            CHECK(gap <= station.repGapMax);
        }
        CHECK(gym.traced(from, std::format("UM_Unlock(95, 1, {})", station.unlockOffset)) == 1);
        CHECK(gym.traced(from, "HuStopWorkout(") == 1);
        CHECK(gym.traced(from, "HUDTurnOffActionCycleAnim(") == 1);
        REQUIRE(promptBack(gym, from));
        CHECK(gym.traced(from, "Rank: 1/10") > 0);
        CHECK_FALSE(gym.player().script().workingOut);
        std::string gaps;
        for (std::size_t r = 1; r < pumped.repFrames.size(); ++r) {
            gaps += std::format(" {}", pumped.repFrames[r] - pumped.repFrames[r - 1]);
        }
        std::printf("  level95 %s: %zu repetitions pumped, gaps%s, unlocked %zu\n", std::string(station.name).c_str(),
                    pumped.repFrames.size(), gaps.c_str(), gym.traced(from, "UM_Unlock(95, 1,"));
        if (k != 1) {
            continue;
        }

        // 3. Run down (chin-ups, now rank 2): no presses after the start, no repetition, the end about 63 updates
        // into the loop, the rank unchanged.
        REQUIRE(startCycle(gym, from));
        const CycleRun ranDown = playCycle(gym, from, 0, 300);
        CHECK(ranDown.ended);
        CHECK(ranDown.repFrames.empty());
        REQUIRE(ranDown.loopAt >= 0);
        CHECK(ranDown.endAt - ranDown.loopAt >= kRunDownMin);
        CHECK(ranDown.endAt - ranDown.loopAt <= kRunDownMax);
        CHECK(gym.traced(from, "UM_Unlock(") == 0);
        REQUIRE(promptBack(gym, from));

        // 4. Quit: triangle during the loop ends it on that update, the rank unchanged.
        REQUIRE(startCycle(gym, from));
        const CycleRun quit = playCycle(gym, from, 4, 300, 60);
        CHECK(quit.ended);
        CHECK(quit.loopAt >= 0);
        CHECK(quit.loopAt < 60);
        CHECK(quit.endAt == 60);
        CHECK(gym.traced(from, "UM_Unlock(") == 0);
        REQUIRE(promptBack(gym, from));
        std::printf("  level95 chin-ups: run down %zu repetitions in %d updates of loop, quit ended %d at %d\n",
                    ranDown.repFrames.size(), ranDown.endAt - ranDown.loopAt, quit.ended ? 1 : 0, quit.endAt);
    }
}
